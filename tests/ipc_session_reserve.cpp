// Real libvterm DSR/CPR -> tagged Writer -> adapter -> literal Python wire peer.
#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tvterm/termemu.h>
#define private public
#include <tvterm/termctrl.h>
#undef private
#include "ipc_session.h"
#include <tvterm/vtermemu.h>
#define Uses_THardwareInfo
#include <tvision/tv.h>
using namespace agentvision;
static void check(bool v, const char *s) {
    if (!v)
        throw std::runtime_error(s);
}
static int descriptors() {
    int count = 0;
    for (int fd = 0; fd < 4096; ++fd)
        if (fcntl(fd, F_GETFD) != -1)
            ++count;
    return count;
}
struct Admission {
    std::string bytes;
    tvterm::EnqueueResult result;
};
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready = false, release = false, filled = false, early = false, timedOut = false, cancelled = false;
    std::string bytes, generated;
    size_t consumed = 0;
    int reads = 0, losses = 0;
    std::vector<Admission> replies;
};
// Observation only: all admission, read, consumption, cancellation and wire
// semantics remain those of the real bound IpcSessionTransport.
class RecordedTransport final : public tvterm::SessionTransport {
    IpcSessionTransport inner;
    Gate &gate;

  public:
    RecordedTransport(std::shared_ptr<CoreConnection> c, std::shared_ptr<SessionEndpoint> ep,
                      Gate &g) : inner(c, ep), gate(g) {}
    tvterm::EnqueueResult enqueueInput(TSpan<const char> b, tvterm::InputOrigin origin) noexcept override {
        auto result = inner.enqueueInput(b, origin);
        if (origin == tvterm::InputOrigin::EmulatorReply) {
            std::lock_guard<std::mutex> l(gate.mutex);
            gate.replies.push_back({std::string(b.data(), b.size()), result});
        }
        return result;
    }
    tvterm::TransportChunk readChunk() noexcept override {
        auto chunk = inner.readChunk();
        if (chunk.kind == tvterm::TransportChunk::Kind::Lost) {
            std::lock_guard<std::mutex> l(gate.mutex);
            if (!gate.cancelled) ++gate.losses;
        }
        return chunk;
    }
    void consumed(size_t n) noexcept override {
        {
            std::lock_guard<std::mutex> l(gate.mutex);
            gate.consumed += n;
        }
        inner.consumed(n); // Never acknowledge borrowed bytes or fabricate Credit.
    }
    void flushed(uint64_t n) noexcept override { inner.flushed(n); }
    void resize(TPoint p) noexcept override { inner.resize(p); }
    void cancelLocal() noexcept override {
        {
            std::lock_guard<std::mutex> l(gate.mutex);
            gate.cancelled = true;
        }
        inner.cancelLocal();
    }
};
class ObservedWriter final : public tvterm::Writer {
    tvterm::Writer &inner;
    Gate &gate;
  public:
    ObservedWriter(tvterm::Writer &w, Gate &g) : inner(w), gate(g) {}
    void write(TSpan<const char> b) noexcept override {
        {
            std::lock_guard<std::mutex> l(gate.mutex);
            gate.generated.append(b.data(), b.size());
        }
        inner.write(b); // Preserve actual tagged Writer behavior and order.
    }
};
class Observed final : public tvterm::TerminalEmulator {
    tvterm::TerminalEmulator &inner;
    Gate &gate;
    ObservedWriter &writer;

  public:
    Observed(tvterm::TerminalEmulator &i, Gate &g, ObservedWriter &w) : inner(i), gate(g), writer(w) {}
    ~Observed() { delete &inner; delete &writer; }
    void handleEvent(const tvterm::TerminalEvent &e) noexcept override {
        inner.handleEvent(e);
        if (e.type == tvterm::TerminalEventType::ClientDataRead) {
            std::unique_lock<std::mutex> l(gate.mutex);
            ++gate.reads;
            gate.bytes.append(e.clientDataRead.data, e.clientDataRead.size);
            // Exact ordered stream prefix, independent of callback/chunk boundaries.
            // The peer cannot send the second query until Credit proves return from
            // this callback through all 13 first-query/readiness bytes.
            if (!gate.ready && gate.bytes.size() >= 13) {
                gate.early = gate.bytes.size() != 13;
                gate.ready = true;
                gate.cv.notify_all();
                if (!gate.cv.wait_for(l, std::chrono::seconds(3), [&] { return gate.release; }))
                    gate.timedOut = true;
            }
        }
    }
    void updateState(tvterm::TerminalState &s) noexcept override { inner.updateState(s); }
};
class Factory final : public tvterm::TerminalEmulatorFactory {
    tvterm::VTermEmulatorFactory inner;
    Gate &gate;

  public:
    Factory(Gate &g) : gate(g) {}
    tvterm::TerminalEmulator &create(TPoint p, tvterm::Writer &w) noexcept override {
        auto &observedWriter = *new ObservedWriter(w, gate);
        return *new Observed(inner.create(p, observedWriter), gate, observedWriter);
    }
    TSpan<const tvterm::EnvironmentVar> getCustomEnvironment() noexcept override {
        return inner.getCustomEnvironment();
    }
};
struct ControllerOwner {
    tvterm::TerminalController *controller;
    Gate &gate;
    ~ControllerOwner() {
        {
            std::lock_guard<std::mutex> l(gate.mutex);
            gate.release = true;
            gate.cv.notify_all();
        }
        if (controller)
            controller->shutDown();
    }
};
static ConnectionEvent event(std::shared_ptr<CoreConnection> c, ConnectionEvent::Kind kind) {
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    ConnectionEvent e;
    while (std::chrono::steady_clock::now() < end) {
        if (c->pollEvent(e)) {
            check(e.kind != ConnectionEvent::Kind::Lost, "loss before saturation");
            if (e.kind == kind)
                return e;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("event deadline");
}
class CapturedWriter final : public tvterm::Writer {
  public:
    std::string bytes;
    void write(TSpan<const char> b) noexcept override { bytes.append(b.data(), b.size()); }
};
static void mechanismRegression() {
    CapturedWriter writer;
    tvterm::VTermEmulatorFactory factory;
    std::unique_ptr<tvterm::TerminalEmulator> emulator(&factory.create({80, 24}, writer));
    const std::string merged = "ready\033[5n";
    tvterm::TerminalEvent e;
    e.type = tvterm::TerminalEventType::ClientDataRead;
    e.clientDataRead = {merged.data(), merged.size()};
    emulator->handleEvent(e);
    const bool oldReady = e.clientDataRead.size == 5 &&
                          std::string(e.clientDataRead.data, 5) == "ready";
    check(writer.bytes == "\033[0n" && !oldReady,
          "merged event emits actual DSR while old exact-chunk readiness is false");
    std::puts("PASS: merged ready+DSR emits real ESC[0n; old exact-five-byte gate false");
}
// The same predicates are used by the live fixture and the constructed matrix.
static bool positiveCleanup(const ConnectionCompletion &c) {
    return c.contactError == ContactError::None && c.graceful &&
           c.core.kind == CoreCompletion::Kind::Exited && c.core.value == 0 &&
           c.core.systemError == 0 && !c.core.termSent && !c.core.killSent;
}
static bool eofCleanup(const ConnectionCompletion &c) {
    return c.contactError == ContactError::EOFReached && !c.graceful &&
           c.core.systemError == 0 && !c.core.killSent &&
           ((c.core.kind == CoreCompletion::Kind::Exited && c.core.value == 0) ||
            (c.core.kind == CoreCompletion::Kind::Signaled && c.core.value == SIGTERM &&
             c.core.termSent));
}
static void completionMatrix() {
    using Kind = CoreCompletion::Kind;
    using Contact = ContactError;
    struct Case {
        const char *name;
        Kind kind;
        int value, error;
        bool term, kill;
        Contact contact;
        bool graceful, positive, eof;
    };
    const Case cases[] = {
        {"positive exit0", Kind::Exited, 0, 0, false, false, Contact::None, true, true, false},
        {"EOF exit0", Kind::Exited, 0, 0, false, false, Contact::EOFReached, false, false, true},
        {"EOF exit0 TERM race", Kind::Exited, 0, 0, true, false, Contact::EOFReached, false, false, true},
        {"EOF owned TERM", Kind::Signaled, SIGTERM, 0, true, false, Contact::EOFReached, false, false, true},
        {"EOF assertion exit1", Kind::Exited, 1, 0, false, false, Contact::EOFReached, false, false, false},
        {"EOF negative exit", Kind::Exited, -1, 0, true, false, Contact::EOFReached, false, false, false},
        {"EOF unowned TERM", Kind::Signaled, SIGTERM, 0, false, false, Contact::EOFReached, false, false, false},
        {"EOF unexpected signal", Kind::Signaled, SIGINT, 0, true, false, Contact::EOFReached, false, false, false},
        {"EOF SIGKILL", Kind::Signaled, SIGKILL, 0, true, true, Contact::EOFReached, false, false, false},
        {"EOF exit0 KILL recorded", Kind::Exited, 0, 0, false, true, Contact::EOFReached, false, false, false},
        {"EOF TERM KILL recorded", Kind::Signaled, SIGTERM, 0, true, true, Contact::EOFReached, false, false, false},
        {"EOF ownership uncertain", Kind::OwnershipUncertain, 0, 0, false, false, Contact::EOFReached, false, false, false},
        {"EOF cleanup uncertain", Kind::CleanupUncertain, 0, 0, false, false, Contact::EOFReached, false, false, false},
        {"EOF exit0 system error", Kind::Exited, 0, 5, false, false, Contact::EOFReached, false, false, false},
        {"EOF TERM system error", Kind::Signaled, SIGTERM, 5, true, false, Contact::EOFReached, false, false, false},
        {"EOF graceful invalid", Kind::Exited, 0, 0, false, false, Contact::EOFReached, true, false, false},
        {"loss without contact", Kind::Exited, 0, 0, false, false, Contact::None, false, false, false},
        {"wrong contact Protocol", Kind::Exited, 0, 0, false, false, Contact::Protocol, false, false, false},
        {"wrong contact IO", Kind::Signaled, SIGTERM, 0, true, false, Contact::IO, false, false, false},
        {"wrong contact Cancelled", Kind::Exited, 0, 0, true, false, Contact::Cancelled, false, false, false},
        {"positive TERM attempted", Kind::Exited, 0, 0, true, false, Contact::None, true, false, false},
        {"positive KILL attempted", Kind::Exited, 0, 0, false, true, Contact::None, true, false, false},
        {"positive nonzero exit", Kind::Exited, 1, 0, false, false, Contact::None, true, false, false},
        {"positive system error", Kind::Exited, 0, 5, false, false, Contact::None, true, false, false},
        {"positive signaled TERM", Kind::Signaled, SIGTERM, 0, true, false, Contact::None, true, false, false},
        {"positive ownership uncertain", Kind::OwnershipUncertain, 0, 0, false, false, Contact::None, true, false, false},
        {"positive cleanup uncertain", Kind::CleanupUncertain, 0, 0, false, false, Contact::None, true, false, false},
    };
    for (const auto &row : cases) {
        ConnectionCompletion c;
        c.core.kind = row.kind; c.core.value = row.value; c.core.systemError = row.error;
        c.core.termSent = row.term; c.core.killSent = row.kill;
        c.contactError = row.contact; c.graceful = row.graceful;
        const bool positive = positiveCleanup(c), eof = eofCleanup(c);
        std::printf("MATRIX: %s positive=%d EOF=%d expected=%d/%d\n", row.name,
                    positive, eof, row.positive, row.eof);
        check(positive == row.positive && eof == row.eof, row.name);
    }
    std::printf("PASS: %zu completion rows use live strict-positive/EOF predicates\n",
                sizeof(cases) / sizeof(cases[0]));
}
static void printBytes(const char *name, const std::string &bytes) {
    std::printf("BYTES: %s size=%zu hex=", name, bytes.size());
    for (unsigned char b : bytes) std::printf("%02x", b);
    std::puts("");
}
static void run(const std::string &schedule) {
    const int baseDescriptors = descriptors();
    setenv("AV_CONNECTION_CASE", "dsr-reserve", 1);
    setenv("AV_RESERVE_SCHEDULE", schedule.c_str(), 1);
    auto c = CoreConnection::start(FAKE_CORE).connection;
    check(bool(c), "launch");
    event(c, ConnectionEvent::Kind::Ready);
    c->createSession(24, 80);
    auto ep = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
    Gate gate;
    auto owned = std::unique_ptr<RecordedTransport>(new RecordedTransport(c, ep, gate));
    auto *transport = owned.get();
    std::vector<char> full(30720, 'u');
    for (int i = 0; i < 2; ++i)
        check(transport->enqueueInput({full.data(), full.size()}, tvterm::InputOrigin::User) ==
                  tvterm::EnqueueResult::Queued, "two 30KiB users admitted");
    for (int i = 0; i < 45; ++i)
        transport->resize({100, 30});
    check(transport->enqueueInput({"x", 1}, tvterm::InputOrigin::User) ==
              tvterm::EnqueueResult::Overflow, "60KiB user-byte limit reached");
    Factory factory(gate);
    auto *controller = tvterm::TerminalController::createWithTransport({80, 24}, factory,
                                                                      std::move(owned));
    ControllerOwner owner{controller, gate};
    bool early;
    {
        std::unique_lock<std::mutex> l(gate.mutex);
        check(gate.cv.wait_for(l, std::chrono::seconds(3), [&] { return gate.ready; }),
              "readiness deadline");
        check(!gate.timedOut && gate.bytes.compare(0, 13, "\033[5n\033[6nready") == 0,
              "ordered query/readiness prefix");
        check(gate.replies.size() == 1 && gate.replies[0].bytes == "\033[0n\033[1;1R" &&
                  gate.replies[0].result == tvterm::EnqueueResult::Queued,
              "actual 10-byte DSR/CPR admitted as unacknowledged slot48");
        check(gate.consumed < 13, "readiness consumption withheld at filler barrier");
        early = gate.early;
    }
    // No gate lock across forwarding admission: RecordedTransport observes it.
    if (!early) {
        std::vector<char> reserve(4086, 'r');
        check(transport->enqueueInput({reserve.data(), reserve.size()},
                                      tvterm::InputOrigin::EmulatorReply) ==
                  tvterm::EnqueueResult::Queued, "10-byte pending reply leaves 4086-byte reserve");
    }
    {
        std::lock_guard<std::mutex> l(gate.mutex);
        gate.filled = !early;
        gate.release = true;
        gate.cv.notify_all();
    }
    if (!early) {
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!controller->clientIsDisconnected() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(controller->clientIsDisconnected(), "disconnect deadline");
    }
    controller->finishPresentation();
    const bool readerJoined = !controller->readerThread.joinable();
    const bool writerJoined = !controller->writerThread.joinable();
    const bool stateAccessible = controller->lockState(
        [](auto &s) { return s.surface.size == TPoint{80, 24}; });
    controller->shutDown();
    owner.controller = nullptr;
    c->shutdown();
    auto completion = c->join();
    ep.reset();
    c.reset();
    const int finalDescriptors = descriptors();
    // Emit measured completion and semantic observations BEFORE asserting on them.
    std::printf("COMPLETION: schedule=%s kind=%d value=%d systemError=%d termSent=%d "
                "killSent=%d contactError=%d graceful=%d FD=%d/%d readerJoined=%d "
                "writerJoined=%d stateAccessible=%d\n", schedule.c_str(),
                int(completion.core.kind), completion.core.value, completion.core.systemError,
                completion.core.termSent, completion.core.killSent, int(completion.contactError),
                completion.graceful, finalDescriptors, baseDescriptors, readerJoined,
                writerJoined, stateAccessible);
    std::printf("PHASE: callbacks=%d consumed=%zu lost=%d replies=%zu ready=%d "
                "release=%d filled=%d early=%d timedOut=%d\n", gate.reads, gate.consumed,
                gate.losses, gate.replies.size(), gate.ready, gate.release, gate.filled,
                early, gate.timedOut);
    printBytes("query/readiness", gate.bytes);
    printBytes("emulator-generated", gate.generated);
    for (size_t i = 0; i < gate.replies.size(); ++i) {
        const auto &reply = gate.replies[i];
        std::printf("ADMISSION: index=%zu bytes=%zu result=%d allFillerR=%d\n", i,
                    reply.bytes.size(), int(reply.result),
                    reply.bytes == std::string(4086, 'r'));
        if (reply.bytes.size() <= 10) printBytes("reply", reply.bytes);
    }
    std::fflush(stdout);
    check(readerJoined && writerJoined, "both owned presentation workers joined");
    check(stateAccessible, "render state accessible after reply overflow/loss");
    check(finalDescriptors == baseDescriptors, "descriptor baseline restored");
    check(schedule == "loss" ? eofCleanup(completion) : positiveCleanup(completion),
          "schedule-specific verified child/contact cleanup");
    const bool overflow = !early && !gate.timedOut && gate.filled &&
        gate.bytes == "\033[5n\033[6nready\033[5n" && gate.consumed == 17 &&
        gate.replies.size() == 3 && gate.replies[1].bytes == std::string(4086, 'r') &&
        gate.replies[1].result == tvterm::EnqueueResult::Queued &&
        gate.replies[2].bytes == "\033[0n" &&
        gate.replies[2].result == tvterm::EnqueueResult::Overflow &&
        gate.generated == "\033[0n\033[1;1R\033[0n" && gate.losses == 0 &&
        positiveCleanup(completion);
    if (schedule == "early") {
        check(early && gate.generated == "\033[0n\033[1;1R\033[0n" &&
                  !overflow && completion.graceful &&
                  completion.contactError == ContactError::None,
              "early query negative must reject phase ordering with graceful cleanup");
        std::puts("PASS negative: real second DSR generated before filling cannot satisfy overflow oracle");
    } else if (schedule == "loss") {
        check(!overflow && !early && !gate.timedOut && gate.ready && gate.release &&
                  gate.filled && gate.bytes == "\033[5n\033[6nready" &&
                  gate.consumed == 13 && gate.losses == 1 &&
                  gate.generated == "\033[0n\033[1;1R" && gate.replies.size() == 2 &&
                  gate.replies[0].bytes == "\033[0n\033[1;1R" &&
                  gate.replies[0].result == tvterm::EnqueueResult::Queued &&
                  gate.replies[1].bytes == std::string(4086, 'r') &&
                  gate.replies[1].result == tvterm::EnqueueResult::Queued && eofCleanup(completion),
              "unrelated EOF must not satisfy reserve-overflow oracle");
        std::puts("PASS negative: unrelated EOF rejected, no second real reply/overflow");
    } else {
        check(overflow, "actual second DSR must fail specifically at full reply reserve");
        std::puts("PASS: wire47=[Input30720,Input30720,Resize30x100*45]; "
                  "slot48=realDSR/CPR10 Queued pending; filler4086 Queued; "
                  "realDSR4 Overflow; Credit barrier13; contactNone/graceful exit0");
    }
}
int main(int argc, char **argv) {
    THardwareInfo hardware;
    try {
        std::string schedule = argc == 2 ? argv[1] : "normal";
        check(argc <= 2 && (schedule == "normal" || schedule == "fragmented" ||
              schedule == "early" || schedule == "loss" || schedule == "mechanism" ||
              schedule == "matrix"),
              "known fixture schedule");
        if (schedule == "matrix") completionMatrix();
        else if (schedule == "mechanism") mechanismRegression();
        else run(schedule);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
