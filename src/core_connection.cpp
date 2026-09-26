#include "core_connection.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fcntl.h>
#include <limits>
#include <map>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
namespace agentvision {
#ifdef AGENTVISION_CREDIT_RACE_TEST
// Linked only by the separate instrumented regression executable.
namespace credit_race_test {
void beforeAdmission(SessionEndpoint *);
void afterAdmission(SessionEndpoint *, RequestId);
bool holdQuietCredit() noexcept;
} // namespace credit_race_test
#endif
namespace {
using Clock = std::chrono::steady_clock;
constexpr size_t Window = 262144, MaxChunks = 128, MaxSessions = 16;
constexpr size_t UserBytes = 61440, ReplyBytes = 4096, MaxEvents = 128;
constexpr auto StageTimeout = std::chrono::seconds(2);
struct RequestExhausted {};
uint64_t be(const uint8_t *p, size_t n) {
    uint64_t v = 0;
    while (n--)
        v = (v << 8) | *p++;
    return v;
}
void put(std::vector<uint8_t> &b, uint64_t v, size_t n) {
    for (size_t i = n; i; --i)
        b.push_back(uint8_t(v >> ((i - 1) * 8)));
}
std::vector<uint8_t> dimensions(uint16_t rows, uint16_t cols) {
    std::vector<uint8_t> b;
    put(b, rows, 2);
    put(b, cols, 2);
    return b;
}
bool validSize(uint16_t r, uint16_t c) {
    return r && c && r <= 4096 && c <= 4096;
}
} // namespace
void connection_detail::Deadline::arm(Clock::time_point now) noexcept {
    active_ = true;
    since_ = now;
}
void connection_detail::Deadline::disarm() noexcept {
    active_ = false;
}
bool connection_detail::Deadline::expired(Clock::time_point now) const noexcept {
    return active_ && now >= expiresAt();
}
connection_detail::Deadline::Clock::time_point
connection_detail::Deadline::expiresAt() const noexcept {
    return since_ + StageTimeout;
}
struct SessionEndpoint::State {
    mutable std::mutex mutex;
    std::condition_variable changed;
    SessionMetadata visible, final;
    std::deque<TransportChunk> chunks;
    size_t rawOutstanding = 0, chunkCount = 0, borrowed = 0, consumedPending = 0;
    uint64_t seen = 0, borrowedSequence = 0, consumedSequence = 0;
    bool cancelled = false, lost = false, sealed = false, endDelivered = false,
         finalPending = false;
};
struct CoreConnection::State {
    struct Pending {
        Frame frame;
        InputOrigin origin = InputOrigin::User;
        size_t bytes = 0;
        bool sent = false;
        Clock::time_point sentAt{};
    };
    struct Emission {
        Frame frame;
        InputOrigin origin = InputOrigin::User;
    };
    struct Session {
        std::deque<Emission> staged;
        std::shared_ptr<SessionEndpoint> endpoint;
        RequestId credit = 0, close = 0;
        size_t userBytes = 0, replyBytes = 0;
        bool closing = false, closed = false;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::weak_ptr<CoreConnection> self;
    std::unique_ptr<CoreProcess> process;
    std::function<void()> restored;
    std::thread readWorker, writeWorker, reapWorker, owner;
    std::mutex joinMutex;
    std::map<RequestId, Pending> pending;
    std::deque<RequestId> outgoing;
    std::map<SessionId, Session> sessions;
    // Metadata notifications use fixed storage, including loss reporting during
    // allocation failure. Output/input payloads have separately bounded budgets.
    std::array<ConnectionEvent, MaxEvents> events;
    size_t eventHead = 0, eventCount = 0;
    RequestId next = 0, hello = 0, shutdown = 0;
    size_t ordinary = 0, userTickets = 0, starting = 0, replyEntries = 0;
    bool replyPending = false, idsExhausted = false;
    bool ready = false, stopped = false, readDone = false, shutdownAck = false, done = false,
         reapDone = false;
    CoreCompletion reaped;
    ContactError error = ContactError::None;
    connection_detail::Deadline stageDeadline;
    ConnectionCompletion completion;
    State(std::unique_ptr<CoreProcess> p, std::function<void()> f)
        : process(std::move(p)), restored(std::move(f)) {
        stageDeadline.arm(Clock::now());
    }
    RequestId admit(Frame f, InputOrigin origin = InputOrigin::User) {
        if (next == std::numeric_limits<RequestId>::max()) {
            idsExhausted = true;
            changed.notify_all();
            return 0;
        }
        f.request = ++next;
        Pending p;
        p.bytes = f.payload.size();
        p.origin = origin;
        p.frame = std::move(f);
        auto id = p.frame.request;
        pending.emplace(id, std::move(p));
        outgoing.push_back(id);
        changed.notify_all();
        return id;
    }
    bool event(ConnectionEvent e) {
        if (eventCount == MaxEvents)
            return false;
        events[(eventHead + eventCount) % MaxEvents] = std::move(e);
        ++eventCount;
        return true;
    }
    void dropStaged(Session &session) {
        for (auto &e : session.staged) {
            if (e.origin == InputOrigin::User)
                --userTickets;
            else
                --replyEntries;
            if (e.frame.type == MessageType::InputBytes) {
                auto &bytes =
                    e.origin == InputOrigin::User ? session.userBytes : session.replyBytes;
                bytes -= e.frame.payload.size();
            }
        }
        session.staged.clear();
    }
    void retire(SessionId id) {
        auto it = sessions.find(id);
        if (it == sessions.end())
            return;
        bool pendingSession = false;
        for (auto &p : pending)
            if (p.second.frame.session == id)
                pendingSession = true;
        if (it->second.closed && !it->second.credit && !it->second.close && !pendingSession &&
            it->second.staged.empty()) {
            // Endpoint owns its retained state after removal; unconsumed output is
            // bounded by its original window. No IPC bookkeeping is lost here.
            sessions.erase(it);
        }
    }
};
SessionEndpoint::SessionEndpoint(std::shared_ptr<State> s, std::weak_ptr<CoreConnection> c)
    : state_(std::move(s)), connection_(std::move(c)) {}
TransportChunk SessionEndpoint::readChunk() noexcept {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->changed.wait(lock, [this] {
        return state_->cancelled || state_->lost ||
               (!state_->borrowed &&
                (!state_->chunks.empty() || (state_->sealed && !state_->endDelivered)));
    });
    if (state_->cancelled || state_->lost)
        return {};
    if (!state_->chunks.empty()) {
        auto c = std::move(state_->chunks.front());
        state_->chunks.pop_front();
        state_->borrowed = c.bytes.size();
        state_->borrowedSequence = c.sequence;
        return c;
    }
    state_->endDelivered = true;
    TransportChunk end;
    end.kind = TransportChunk::Kind::End;
    end.sequence = state_->seen;
    return end;
}
void SessionEndpoint::consumed(size_t bytes) noexcept {
    SessionId id;
    size_t amount;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!bytes || state_->cancelled || state_->lost || bytes > state_->borrowed)
            return;
        state_->borrowed -= bytes;
        state_->consumedPending += bytes;
        if (!state_->borrowed) {
            state_->consumedSequence = state_->borrowedSequence;
            --state_->chunkCount;
            state_->changed.notify_all();
        }
        id = state_->visible.id;
        amount = state_->consumedPending;
    }
#ifdef AGENTVISION_CREDIT_RACE_TEST
    credit_race_test::beforeAdmission(this);
#endif
    RequestId admitted = 0;
    if (auto c = connection_.lock())
        admitted = c->admitCredit(id, uint32_t(amount), this);
#ifdef AGENTVISION_CREDIT_RACE_TEST
    credit_race_test::afterAdmission(this, admitted);
#else
    (void)admitted;
#endif
}
SessionMetadata SessionEndpoint::metadata() const noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->visible;
}
void SessionEndpoint::flushed(uint64_t sequence) noexcept {
    if (auto connection = connection_.lock())
        connection->endpointFlushed(*this, sequence);
}
void SessionEndpoint::cancelLocal() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->cancelled = true;
    state_->chunks.clear();
    state_->borrowed = 0;
    state_->chunkCount = 0;
    state_->changed.notify_all();
}
CoreConnection::CoreConnection(std::unique_ptr<CoreProcess> p, std::function<void()> f)
    : state_(new State(std::move(p), std::move(f))) {}
StartResult CoreConnection::start(const std::string &path, std::function<void()> restored) {
    auto launch = CoreProcess::launch(path);
    if (!launch.process)
        return {nullptr, launch.error, launch.systemError};
    auto c = std::shared_ptr<CoreConnection>(
        new CoreConnection(std::move(launch.process), std::move(restored)));
    c->state_->self = c;
    int fd = c->state_->process->ipcFd();
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int error = errno;
        if (c->state_->restored)
            try {
                c->state_->restored();
            } catch (...) {
            }
        c->state_->process->requestStop();
        c->state_->process->join();
        return {nullptr, LaunchError::Socket, error};
    }
    Frame hello;
    hello.version = 0;
    hello.type = MessageType::Hello;
    hello.payload = {0, 1, 0, 1};
    c->state_->hello = c->state_->admit(std::move(hello));
    c->state_->ordinary = c->state_->userTickets = 1;
    try {
        c->state_->reapWorker = std::thread([ptr = c.get()] {
            auto completion = ptr->state_->process->join();
            std::lock_guard<std::mutex> lock(ptr->state_->mutex);
            ptr->state_->reaped = completion;
            ptr->state_->reapDone = true;
            ptr->state_->changed.notify_all();
        });
        c->state_->readWorker = std::thread([ptr = c.get()] { ptr->reader(); });
        c->state_->writeWorker = std::thread([ptr = c.get()] { ptr->writer(); });
        c->state_->owner = std::thread([ptr = c.get()] { ptr->lifecycle(); });
    } catch (...) {
        c->lose(ContactError::IO);
        if (c->state_->readWorker.joinable())
            c->state_->readWorker.join();
        if (c->state_->writeWorker.joinable())
            c->state_->writeWorker.join();
        if (c->state_->restored)
            try {
                c->state_->restored();
            } catch (...) {
            }
        c->state_->process->requestStop();
        if (c->state_->reapWorker.joinable())
            c->state_->reapWorker.join();
        else
            c->state_->process->join();
        return {nullptr, LaunchError::Owner, 0};
    }
    return {std::move(c), LaunchError::None, 0};
}
CoreConnection::~CoreConnection() {
    cancelLocal();
    join();
}
void CoreConnection::lose(ContactError error) noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stopped)
            return;
        state_->stopped = true;
        state_->error = error;
        for (auto &entry : state_->sessions) {
            auto &e = *entry.second.endpoint->state_;
            std::lock_guard<std::mutex> q(e.mutex);
            e.lost = true;
            if (e.visible.state != SessionState::Exited && e.visible.state != SessionState::Closed)
                e.visible.state = SessionState::Lost;
            e.changed.notify_all();
        }
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::Lost;
        ev.contactError = error;
        // Retain one explicit loss notice even if a stalled UI exhausted its budget.
        if (state_->eventCount == MaxEvents)
            --state_->eventCount;
        state_->event(ev);
        state_->changed.notify_all();
    }
    // Borrowed descriptor: shutdown wakes users, only CoreProcess closes it later.
    ::shutdown(state_->process->ipcFd(), SHUT_RDWR);
}
void CoreConnection::cancelLocal() noexcept {
    lose(ContactError::Cancelled);
}
ConnectionCompletion CoreConnection::join() noexcept {
    std::lock_guard<std::mutex> serial(state_->joinMutex);
    if (state_->owner.joinable())
        state_->owner.join();
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->completion;
}
bool CoreConnection::pollEvent(ConnectionEvent &e) noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->eventCount)
        return false;
    e = state_->events[state_->eventHead];
    state_->eventHead = (state_->eventHead + 1) % MaxEvents;
    --state_->eventCount;
    return true;
}
std::shared_ptr<SessionEndpoint> CoreConnection::endpoint(SessionId id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->sessions.find(id);
    return it == state_->sessions.end() ? nullptr : it->second.endpoint;
}
RequestId CoreConnection::createSession(uint16_t rows, uint16_t cols) {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->ready || state_->stopped || state_->shutdown || !validSize(rows, cols) ||
            state_->userTickets >= 47 || state_->sessions.size() + state_->starting >= MaxSessions)
            return 0;
        Frame f;
        f.type = MessageType::CreateSession;
        f.payload = dimensions(rows, cols);
        put(f.payload, Window, 4);
        auto id = state_->admit(std::move(f));
        if (id) {
            ++state_->ordinary;
            ++state_->userTickets;
            ++state_->starting;
        }
        return id;

    } catch (...) {
        lose(ContactError::ResourceLimit);
        return 0;
    }
}
EnqueueResult CoreConnection::submitInput(SessionId id, const std::vector<uint8_t> &bytes,
                                          InputOrigin origin) {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->sessions.find(id);
        if (state_->stopped || state_->shutdown || it == state_->sessions.end() ||
            it->second.closing || it->second.closed)
            return EnqueueResult::Closed;
        auto &s = it->second;
        auto limit = origin == InputOrigin::User ? UserBytes : ReplyBytes;
        auto &staged = origin == InputOrigin::User ? s.userBytes : s.replyBytes;
        if (bytes.empty() || bytes.size() > 32768 || bytes.size() > limit - staged ||
            (origin == InputOrigin::User ? state_->userTickets >= 47 : state_->replyEntries >= 128))
            return EnqueueResult::Overflow;
        Frame f;
        f.type = MessageType::InputBytes;
        f.session = id;
        f.payload = bytes;
        State::Emission emission;
        emission.frame = std::move(f);
        emission.origin = origin;
        s.staged.push_back(std::move(emission));
        if (origin == InputOrigin::User)
            ++state_->userTickets;
        else
            ++state_->replyEntries;
        staged += bytes.size();
        state_->changed.notify_all();
        return EnqueueResult::Queued;

    } catch (...) {
        lose(ContactError::ResourceLimit);
        return EnqueueResult::Overflow;
    }
}
EnqueueResult CoreConnection::submitResize(SessionId id, uint16_t rows, uint16_t cols) {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->sessions.find(id);
        if (state_->stopped || state_->shutdown || it == state_->sessions.end() ||
            it->second.closing || it->second.closed)
            return EnqueueResult::Closed;
        if (!validSize(rows, cols) || state_->userTickets >= 47)
            return EnqueueResult::Overflow;
        Frame f;
        f.type = MessageType::ResizeSession;
        f.session = id;
        f.payload = dimensions(rows, cols);
        State::Emission emission;
        emission.frame = std::move(f);
        it->second.staged.push_back(std::move(emission));
        ++state_->userTickets;
        state_->changed.notify_all();
        return EnqueueResult::Queued;

    } catch (...) {
        lose(ContactError::ResourceLimit);
        return EnqueueResult::Overflow;
    }
}
RequestId CoreConnection::closeSession(SessionId id) {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->sessions.find(id);
        if (state_->stopped || state_->shutdown || it == state_->sessions.end() ||
            it->second.closing || it->second.closed)
            return 0;
        Frame f;
        f.type = MessageType::CloseSession;
        f.session = id;
        auto r = state_->admit(std::move(f));
        if (r) {
            it->second.closing = true;
            it->second.close = r;
        }
        return r;

    } catch (...) {
        lose(ContactError::ResourceLimit);
        return 0;
    }
}
RequestId CoreConnection::returnCredit(SessionId id, uint32_t amount) {
    return admitCredit(id, amount, nullptr);
}
RequestId CoreConnection::admitCredit(SessionId id, uint32_t amount,
                                      const SessionEndpoint *source) {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto it = state_->sessions.find(id);
        if (state_->stopped || state_->shutdown || it == state_->sessions.end() ||
            it->second.credit || !amount || (source && it->second.endpoint.get() != source))
            return 0;
        auto &e = *it->second.endpoint->state_;
        std::lock_guard<std::mutex> q(e.mutex);
        // Endpoint callbacks carry binding authority, not merely an opaque ID.
        // Validate it atomically with credit reservation in connection->endpoint order.
        if ((source && (e.cancelled || e.lost || it->second.closed)) ||
            amount > e.consumedPending || amount > e.rawOutstanding)
            return 0;
        Frame f;
        f.type = MessageType::OutputCredit;
        f.session = id;
        put(f.payload, amount, 4);
        auto r = state_->admit(std::move(f));
        if (r) {
            it->second.credit = r;
            e.consumedPending -= amount;
            e.rawOutstanding -= amount;
        }
        return r;

    } catch (...) {
        lose(ContactError::ResourceLimit);
        return 0;
    }
}
void CoreConnection::shutdown() noexcept {
    try {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stopped || !state_->ready || state_->shutdown)
            return;
        Frame f;
        f.type = MessageType::Shutdown;
        state_->shutdown = state_->admit(std::move(f));
        state_->stageDeadline.arm(Clock::now());
    } catch (...) {
        lose(ContactError::ResourceLimit);
    }
}
void CoreConnection::endpointFlushed(SessionEndpoint &endpoint, uint64_t sequence) noexcept {
    bool ok = true;
    {
        // Status and its notification share the demux lock. A concurrent Closed
        // either follows this Exited event or cancels publication before it starts.
        std::lock_guard<std::mutex> lock(state_->mutex);
        auto &e = *endpoint.state_;
        std::lock_guard<std::mutex> queue(e.mutex);
        auto it = state_->sessions.find(e.visible.id);
        if (it == state_->sessions.end() || it->second.endpoint.get() != &endpoint ||
            state_->stopped || e.cancelled || e.lost || !e.finalPending || !e.endDelivered ||
            e.borrowed || !e.chunks.empty() || sequence != e.final.lastSequence ||
            e.consumedSequence != sequence)
            return;
        e.visible = e.final;
        e.finalPending = false;
        ConnectionEvent event;
        event.kind = ConnectionEvent::Kind::Exited;
        event.session = e.visible.id;
        event.metadata = e.visible;
        ok = state_->event(event);
    }
    if (!ok)
        lose(ContactError::ResourceLimit);
}
bool CoreConnection::dispatch(const Frame &f) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopped)
        return true;
    if (validateDirection(f, Direction::BackendToFrontend) != DecodeError::None)
        return false;
    auto p = state_->pending.find(f.request);
    if (!state_->ready) {
        if (f.type != MessageType::HelloAck || f.request != state_->hello ||
            p == state_->pending.end() || !p->second.sent)
            return false;
        state_->pending.erase(p);
        --state_->ordinary;
        --state_->userTickets;
        state_->ready = true;
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::Ready;
        return state_->event(ev);
    }
    if (f.version != 1 || f.type == MessageType::HelloAck)
        return false;
    auto it = state_->sessions.find(f.session);
    if (f.type == MessageType::OutputBytes || f.type == MessageType::SessionExited ||
        (f.type == MessageType::SessionClosed && !f.request)) {
        if (it == state_->sessions.end())
            return false;
        auto keepAlive = it->second.endpoint;
        auto &e = *keepAlive->state_;
        std::lock_guard<std::mutex> q(e.mutex);
        if (it->second.closed || (e.sealed && f.type != MessageType::SessionClosed))
            return false;
        uint64_t sequence =
            be(f.payload.data() + (f.type == MessageType::SessionExited ? 6 : 0), 8);
        if (f.type == MessageType::OutputBytes) {
            size_t bytes = f.payload.size() - 8;
            if (e.seen == std::numeric_limits<uint64_t>::max() || sequence != e.seen + 1 ||
                bytes > Window - e.rawOutstanding || e.chunkCount >= MaxChunks)
                return false;
            e.seen = sequence;
            e.rawOutstanding += bytes;
            if (!e.cancelled) {
                TransportChunk c;
                c.kind = TransportChunk::Kind::Data;
                c.sequence = sequence;
                c.bytes.assign(f.payload.begin() + 8, f.payload.end());
                e.chunks.push_back(std::move(c));
                ++e.chunkCount;
                e.changed.notify_all();
            }
            return true;
        }
        if (sequence != e.seen)
            return false;
        e.sealed = true;
        if (f.type == MessageType::SessionExited) {
            e.final = e.visible;
            e.final.state = SessionState::Exited;
            e.final.status.kind = ExitKind(f.payload[0]);
            e.final.status.value = uint32_t(be(f.payload.data() + 1, 4));
            e.final.status.coreDump = f.payload[5] != 0;
            e.final.lastSequence = sequence;
            e.final.drainReason = DrainReason(f.payload[14]);
            e.finalPending = true;
            e.changed.notify_all();
            return true;
        }
        it->second.closed = true;
        state_->dropStaged(it->second);
        e.visible.state = SessionState::Closed;
        e.visible.lastSequence = sequence;
        e.cancelled = true;
        e.chunks.clear();
        e.changed.notify_all();
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::Closed;
        ev.session = f.session;
        ev.metadata = e.visible;
        bool ok = state_->event(ev);
        state_->retire(f.session);
        return ok;
    }
    if (f.type == MessageType::Error && !f.request) {
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::RequestError;
        ev.session = f.session;
        ev.errorCode = uint16_t(be(f.payload.data(), 2));
        ev.partialInputBytes = uint32_t(be(f.payload.data() + 2, 4));
        return state_->event(ev);
    }
    if (p == state_->pending.end() || !p->second.sent)
        return false;
    auto &type = p->second.frame.type;
    if (f.type == MessageType::SessionCreated) {
        if (type != MessageType::CreateSession || state_->sessions.count(f.session) ||
            f.payload != p->second.frame.payload)
            return false;
        auto data = std::make_shared<SessionEndpoint::State>();
        data->visible.id = f.session;
        data->visible.state = SessionState::Running;
        State::Session session;
        session.endpoint =
            std::shared_ptr<SessionEndpoint>(new SessionEndpoint(data, state_->self));
        state_->sessions.emplace(f.session, std::move(session));
        --state_->starting;
        --state_->ordinary;
        --state_->userTickets;
        state_->pending.erase(p);
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::Created;
        ev.request = f.request;
        ev.session = f.session;
        ev.metadata = data->visible;
        return state_->event(ev);
    }
    if (f.session != p->second.frame.session)
        return false;
    if (f.type == MessageType::SessionClosed) {
        if (type != MessageType::CloseSession || it == state_->sessions.end())
            return false;
        auto keepAlive = it->second.endpoint;
        auto &e = *keepAlive->state_;
        std::lock_guard<std::mutex> q(e.mutex);
        auto sequence = be(f.payload.data(), 8);
        if (it->second.closed || sequence != e.seen)
            return false;
        e.sealed = true;
        e.cancelled = true;
        e.chunks.clear();
        e.borrowed = 0;
        e.chunkCount = 0;
        e.visible.state = SessionState::Closed;
        e.visible.lastSequence = sequence;
        e.changed.notify_all();
        it->second.closed = true;
        it->second.close = 0;
        state_->dropStaged(it->second);
        state_->pending.erase(p);
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::Closed;
        ev.request = f.request;
        ev.session = f.session;
        ev.metadata = e.visible;
        bool ok = state_->event(ev);
        state_->retire(f.session);
        return ok;
    }
    bool error = f.type == MessageType::Error;
    if (!error && (f.type != MessageType::Ack || be(f.payload.data(), 2) != uint16_t(type)))
        return false;
    if (error) {
        auto partial = be(f.payload.data() + 2, 4);
        if (type == MessageType::InputBytes ? partial > p->second.bytes : partial != 0)
            return false;
        ConnectionEvent ev;
        ev.kind = ConnectionEvent::Kind::RequestError;
        ev.request = f.request;
        ev.session = f.session;
        ev.errorCode = uint16_t(be(f.payload.data(), 2));
        ev.partialInputBytes = uint32_t(partial);
        if (!state_->event(ev))
            return false;
    }
    if (type == MessageType::OutputCredit) {
        if (it == state_->sessions.end() || it->second.credit != f.request)
            return false;
        if (error && (be(f.payload.data(), 2) != 3 || !it->second.closed))
            return false;
        it->second.credit = 0;
    } else if (type == MessageType::CloseSession) {
        if (!error || it == state_->sessions.end())
            return false;
        it->second.close = 0;
        it->second.closing = false;
    } else if (type == MessageType::Shutdown) {
        if (error || state_->pending.size() != 1 || !state_->outgoing.empty())
            return false;
        state_->shutdownAck = true;
    } else {
        --state_->ordinary;
        if (p->second.origin == InputOrigin::EmulatorReply) {
            state_->replyPending = false;
            --state_->replyEntries;
        } else
            --state_->userTickets;
        if (type == MessageType::CreateSession)
            --state_->starting;
        if (type == MessageType::InputBytes) {
            if (it == state_->sessions.end())
                return false;
            auto &bytes = p->second.origin == InputOrigin::User ? it->second.userBytes
                                                                : it->second.replyBytes;
            bytes -= p->second.bytes;
        }
    }
    state_->pending.erase(p);
    state_->retire(f.session);
    state_->changed.notify_all();
    return true;
}
void CoreConnection::reader() noexcept {
    try {
        int fd = state_->process->ipcFd();
        std::vector<uint8_t> bytes(32);
        size_t offset = 0, total = 32;
        bool started = false;
        connection_detail::Deadline first;
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                if (state_->stopped)
                    break;
            }
            pollfd p{fd, POLLIN, 0};
            int ready = poll(&p, 1, 5);
            if (ready < 0) {
                if (errno == EINTR)
                    continue;
                lose(ContactError::IO);
                break;
            }
            if (first.expired(Clock::now())) {
                lose(ContactError::FrameTimeout);
                break;
            }
            if (!ready)
                continue;
            auto n = recv(fd, bytes.data() + offset, total - offset, 0);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                lose(ContactError::IO);
                break;
            }
            if (n == 0) {
                bool normal;
                {
                    std::lock_guard<std::mutex> lock(state_->mutex);
                    normal = state_->shutdownAck && !offset;
                    state_->readDone = true;
                    state_->changed.notify_all();
                }
                if (!normal)
                    lose(offset ? ContactError::Truncated : ContactError::EOFReached);
                break;
            }
            if (!started) {
                first.arm(Clock::now());
                started = true;
            }
            offset += size_t(n);
            if (offset == 32 && total == 32) {
                // Validate framing before body allocation. Exact schema follows
                // only after all bounded bytes arrive; no partial resynchronizing.
                auto *p = bytes.data();
                auto t = be(p + 6, 2), v = be(p + 4, 2), length = be(p + 12, 4);
                if (p[0] != 'A' || p[1] != 'V' || p[2] != 'C' || p[3] != 'P' || t < 1 || t > 14 ||
                    be(p + 8, 4) || length > 65536 ||
                    (t == 1 || t == 2 ? v != 0
                     : t == 12        ? v > 1
                                      : v != 1)) {
                    lose(ContactError::Protocol);
                    break;
                }
                total = 32 + size_t(length);
                bytes.resize(total);
            }
            if (offset == total) {
                auto result = decodeFrame(bytes);
                if (!result.success || !dispatch(result.frame)) {
                    lose(ContactError::Protocol);
                    break;
                }
                bytes.resize(32);
                offset = 0;
                total = 32;
                started = false;
                first.disarm();
            }
        }
    } catch (...) {
        lose(ContactError::ResourceLimit);
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->readDone = true;
        state_->changed.notify_all();
    }
}
void CoreConnection::writer() noexcept {
    try {
        int fd = state_->process->ipcFd();
        for (;;) {
            std::vector<uint8_t> bytes;
            RequestId id = 0;
            {
                std::unique_lock<std::mutex> lock(state_->mutex);
                state_->changed.wait_for(lock, std::chrono::milliseconds(5), [this] {
                    return state_->stopped || !state_->outgoing.empty() || state_->readDone;
                });
                if (state_->stopped || state_->readDone)
                    break;
                for (auto &entry : state_->sessions) {
                    auto &s = entry.second;
                    if (state_->shutdown || s.credit)
                        continue;
#ifdef AGENTVISION_CREDIT_RACE_TEST
                    if (credit_race_test::holdQuietCredit())
                        continue;
#endif
                    auto &e = *s.endpoint->state_;
                    std::lock_guard<std::mutex> q(e.mutex);
                    if (!e.consumedPending)
                        continue;
                    Frame f;
                    f.type = MessageType::OutputCredit;
                    f.session = entry.first;
                    auto amount = e.consumedPending;
                    put(f.payload, amount, 4);
                    auto request = state_->admit(std::move(f));
                    if (!request)
                        throw RequestExhausted{};
                    s.credit = request;
                    e.consumedPending = 0;
                    e.rawOutstanding -= amount;
                }
                // Dispatch-time IDs keep controls moving without skipping wire IDs.
                // Each session's terminal emissions remain FIFO, including origins.
                for (auto &entry : state_->sessions) {
                    auto &s = entry.second;
                    if (state_->shutdown || s.staged.empty() || s.closed)
                        continue;
                    auto &e = s.staged.front();
                    if (e.origin == InputOrigin::EmulatorReply && state_->replyPending)
                        continue;
                    auto origin = e.origin;
                    auto request = state_->admit(std::move(e.frame), origin);
                    if (!request)
                        throw RequestExhausted{};
                    s.staged.pop_front();
                    ++state_->ordinary;
                    if (origin == InputOrigin::EmulatorReply)
                        state_->replyPending = true;
                }
                if (state_->outgoing.empty())
                    continue;
                id = state_->outgoing.front();
                state_->outgoing.pop_front();
                auto &p = state_->pending.at(id);
                bytes = encodeFrame(p.frame);
                p.sent = true;
                p.sentAt = Clock::now();
            }
            if (bytes.empty()) {
                lose(ContactError::Protocol);
                break;
            }
            size_t offset = 0;
            connection_detail::Deadline progress;
            progress.arm(Clock::now());
            while (offset < bytes.size()) {
                {
                    std::lock_guard<std::mutex> lock(state_->mutex);
                    if (state_->stopped)
                        return;
                }
                pollfd p{fd, POLLOUT, 0};
                int ready = poll(&p, 1, 5);
                if (ready < 0) {
                    if (errno == EINTR)
                        continue;
                    lose(ContactError::IO);
                    return;
                }
                if (progress.expired(Clock::now())) {
                    lose(ContactError::WriteTimeout);
                    return;
                }
                if (!ready)
                    continue;
                auto n = send(fd, bytes.data() + offset, bytes.size() - offset, 0);
                if (n < 0) {
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    lose(ContactError::IO);
                    return;
                }
                if (!n) {
                    lose(ContactError::IO);
                    return;
                }
                offset += size_t(n);
                progress.arm(Clock::now());
            }
        }
    } catch (const RequestExhausted &) {
        lose(ContactError::Protocol);
    } catch (...) {
        lose(ContactError::ResourceLimit);
    }
}
void CoreConnection::lifecycle() noexcept {
    for (;;) {
        ContactError deadline = ContactError::None;
        bool stop = false;
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->changed.wait_for(lock, std::chrono::milliseconds(5));
            stop = state_->stopped || state_->readDone;
            auto now = Clock::now();
            if (state_->idsExhausted)
                deadline = ContactError::Protocol;
            else if (!state_->ready && state_->stageDeadline.expired(now))
                deadline = ContactError::HandshakeTimeout;
            else if (state_->shutdown && state_->stageDeadline.expired(now))
                deadline = ContactError::ShutdownTimeout;
            for (auto &p : state_->pending)
                if (p.second.frame.type == MessageType::OutputCredit && p.second.sent &&
                    now - p.second.sentAt >= StageTimeout)
                    deadline = ContactError::CreditTimeout;
        }
        if (deadline != ContactError::None) {
            lose(deadline);
            break;
        }
        if (stop)
            break;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->changed.notify_all();
    }
    if (state_->readWorker.joinable())
        state_->readWorker.join();
    if (state_->writeWorker.joinable())
        state_->writeWorker.join();
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (auto &entry : state_->sessions) {
            state_->dropStaged(entry.second);
            entry.second.endpoint->cancelLocal();
            auto &e = *entry.second.endpoint->state_;
            std::lock_guard<std::mutex> q(e.mutex);
            if (e.visible.state == SessionState::Running)
                e.visible.state = SessionState::Lost;
        }
    }
    if (state_->restored)
        try {
            state_->restored();
        } catch (...) {
            lose(ContactError::IO);
        }
    bool normal;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        normal = state_->shutdownAck && state_->error == ContactError::None;
    }
    if (normal) {
        bool timeout = false;
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->changed.wait_until(lock, state_->stageDeadline.expiresAt(),
                                       [this] { return state_->reapDone || state_->stopped; });
            timeout = !state_->reapDone;
        }
        if (timeout) {
            lose(ContactError::ShutdownTimeout);
            normal = false;
        }
    }
    if (!normal)
        state_->process->requestStop();
    if (state_->reapWorker.joinable())
        state_->reapWorker.join();
    auto completion = state_->reaped;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->completion.core = completion;
        state_->completion.contactError = state_->error;
        state_->completion.graceful =
            normal && completion.kind == CoreCompletion::Kind::Exited && completion.value == 0;
        state_->done = true;
        state_->changed.notify_all();
    }
}
} // namespace agentvision
