#include "core_connection.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
namespace {
std::atomic<bool> delayObservation{false};
std::atomic<int> signals{0}, observedReaps{0}, signalBeforeRestoration{0};
std::atomic<bool> restoredForSignal{true};
using WaitFunction = pid_t (*)(pid_t, int *, int);
using KillFunction = int (*)(pid_t, int);
} // namespace
extern "C" pid_t waitpid(pid_t pid, int *status, int options) {
    static auto native = reinterpret_cast<WaitFunction>(dlsym(RTLD_NEXT, "waitpid"));
    if (delayObservation.load()) {
        if (pid <= 0 || options != WNOHANG)
            std::abort();
        return 0;
    }
    auto result = native(pid, status, options);
    if (result > 0)
        ++observedReaps;
    return result;
}
extern "C" int kill(pid_t pid, int sig) {
    static auto native = reinterpret_cast<KillFunction>(dlsym(RTLD_NEXT, "kill"));
    if (pid <= 0 || (sig != SIGTERM && sig != SIGKILL))
        std::abort();
    ++signals;
    if (!restoredForSignal.load())
        ++signalBeforeRestoration;
    return native(pid, sig);
}
using namespace agentvision;
using Clock = std::chrono::steady_clock;
static void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static ConnectionEvent event(const std::shared_ptr<CoreConnection> &c, ConnectionEvent::Kind kind) {
    auto end = Clock::now() + std::chrono::seconds(4);
    ConnectionEvent e;
    while (Clock::now() < end) {
        if (c->pollEvent(e)) {
            if (e.kind == kind)
                return e;
            check(e.kind != ConnectionEvent::Kind::Lost, "unexpected loss");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("event deadline");
}
static std::shared_ptr<CoreConnection> start(const char *mode) {
    setenv("AV_CONNECTION_CASE", mode, 1);
    auto r = CoreConnection::start(FAKE_CORE);
    check(bool(r.connection), "CoreConnection start owns launched core");
    event(r.connection, ConnectionEvent::Kind::Ready);
    return r.connection;
}
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "output-fragments") {
            auto c = start("output-fragments");
            c->createSession(24, 80);
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto borrowed = a->readChunk();
            c->createSession(24, 80);
            auto b = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto initial = b->readChunk();
            b->consumed(initial.bytes.size());
            check(c->submitInput(a->metadata().id, {'f', 'r', 'a', 'g', 'm', 'e', 'n', 't'},
                                 InputOrigin::User) == EnqueueResult::Queued,
                  "fragment command");
            auto barrier = b->readChunk();
            check(barrier.bytes == std::vector<char>({'b', 'a', 'r', 'r', 'i', 'e', 'r'}),
                  "valid tiny-frame burst must not lose contact below byte window");
            b->consumed(barrier.bytes.size());
            check(borrowed.bytes == std::vector<char>({'A', '\0', char(255)}) &&
                      borrowed.sequence == 1,
                  "borrowed chunk unchanged while queued tail coalesces");
            check(a->metadata().state == SessionState::Running,
                  "fragmented final status remains hidden while borrowed");
            a->consumed(1);
            check(a->metadata().state == SessionState::Running,
                  "partial consumption cannot publish exact exit");
            a->flushed(1025);
            check(a->metadata().state == SessionState::Running,
                  "early flush with borrowed tail rejected");
            a->consumed(borrowed.bytes.size() - 1);
            std::vector<char> bytes;
            uint64_t sequence = 1;
            for (;;) {
                auto part = a->readChunk();
                if (part.kind == TransportChunk::Kind::End) {
                    check(part.sequence == 1025 && sequence == 1025, "merged final sequence");
                    a->flushed(part.sequence);
                    break;
                }
                check(part.kind == TransportChunk::Kind::Data && part.bytes.size() <= 32768 &&
                          part.sequence > sequence,
                      "bounded ordered owned chunks");
                sequence = part.sequence;
                bytes.insert(bytes.end(), part.bytes.begin(), part.bytes.end());
                a->consumed(part.bytes.size());
            }
            check(bytes.size() == 1024, "all fragmented bytes retained");
            for (size_t i = 0; i < bytes.size(); ++i)
                check((unsigned char)bytes[i] == i % 251, "fragmented bytes remain ordered");
            check(a->metadata().state == SessionState::Exited && a->metadata().status.value == 7,
                  "exact final status after merged flush");
            check(c->submitInput(b->metadata().id, {'c', 'r', 'e', 'd', 'i', 't'},
                                 InputOrigin::User) == EnqueueResult::Queued,
                  "credit audit on B");
            auto credited = b->readChunk();
            check(credited.bytes == std::vector<char>({'c', 'r', 'e', 'd', 'i', 't', 'e', 'd'}),
                  "peer confirms exact consumed-only partial/full Credit");
            b->consumed(credited.bytes.size());
            check(a->requestClose() != 0, "fragmented Exited Close");
            event(c, ConnectionEvent::Kind::Closed);
            check(b->requestClose() != 0, "B still admits Close");
            event(c, ConnectionEvent::Kind::Closed);
            c->shutdown();
            check(c->join().graceful, "fragmented graceful cleanup");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "flush-close") {
            auto c = start("flush-close");
            for (int i = 0; i < 32; ++i) {
                c->createSession(24, 80);
                auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
                auto data = a->readChunk();
                a->consumed(data.bytes.size());
                auto end = a->readChunk();
                check(end.kind == TransportChunk::Kind::End, "concurrent fixture sealed output");
                std::promise<void> go;
                auto ready = go.get_future().share();
                auto flusher = std::async(std::launch::async, [&] {
                    ready.wait();
                    a->flushed(end.sequence);
                });
                auto closer = std::async(std::launch::async, [&] {
                    ready.wait();
                    return c->closeSession(a->metadata().id);
                });
                go.set_value();
                flusher.get();
                check(closer.get() != 0, "concurrent Close admitted");
                bool closed = false;
                auto deadline = Clock::now() + std::chrono::seconds(2);
                ConnectionEvent e;
                while (!closed && Clock::now() < deadline) {
                    if (!c->pollEvent(e)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    check(e.kind != ConnectionEvent::Kind::Lost,
                          "concurrent flush/Close keeps contact");
                    if (e.kind == ConnectionEvent::Kind::Exited)
                        check(e.metadata.state == SessionState::Exited &&
                                  e.metadata.status.value == 7,
                              "Exited event carries its committed snapshot");
                    if (e.kind == ConnectionEvent::Kind::Closed)
                        closed = true;
                }
                check(closed && a->metadata().state == SessionState::Closed,
                      "concurrent Closed eventually visible");
                while (c->pollEvent(e))
                    check(e.kind != ConnectionEvent::Kind::Exited,
                          "Exited notification cannot follow Closed");
            }
            c->shutdown();
            check(c->join().graceful, "concurrent notification ordering cleanup");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "stale") {
            auto c = start("stale");
            c->createSession(24, 80);
            auto old = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto first = old->readChunk();
            old->consumed(first.bytes.size());
            auto end = old->readChunk();
            old->flushed(end.sequence);
            event(c, ConnectionEvent::Kind::Exited);
            c->closeSession(old->metadata().id);
            event(c, ConnectionEvent::Kind::Closed);
            c->createSession(24, 80);
            auto current = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            check(current.get() != old.get() && current->metadata().id == old->metadata().id,
                  "synthetic retired-ID reuse creates a separate endpoint binding");
            auto data = current->readChunk();
            current->consumed(data.bytes.size());
            auto final = current->readChunk();
            old->consumed(3);
            old->flushed(final.sequence);
            check(current->metadata().state == SessionState::Running,
                  "old cancelled handle cannot commit a new endpoint's final status");
            check(old->metadata().state == SessionState::Closed &&
                      old->readChunk().kind == TransportChunk::Kind::Lost,
                  "old binding remains Closed/cancelled");
            current->flushed(final.sequence);
            event(c, ConnectionEvent::Kind::Exited);
            c->shutdown();
            check(c->join().graceful, "stale binding cleanup");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "correlations") {
            auto c = start("early-shutdown");
            c->createSession(24, 80);
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto d = a->readChunk();
            a->consumed(d.bytes.size());
            c->submitResize(a->metadata().id, 30, 100);
            auto held = a->readChunk();
            check(held.bytes == std::vector<char>({'h', 'e', 'l', 'd'}),
                  "peer holds accepted Resize response");
            c->shutdown();
            auto outcome = c->join();
            check(!outcome.graceful && outcome.contactError == ContactError::Protocol,
                  "Shutdown Ack cannot abandon an accepted wire correlation");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "controls") {
            auto c = start("control-lanes");
            std::vector<SessionId> ids;
            for (int i = 0; i < 16; ++i) {
                c->createSession(24, 80);
                ids.push_back(event(c, ConnectionEvent::Kind::Created).session);
            }
            for (int i = 0; i < 47; ++i)
                check(c->submitResize(ids[0], 30, 100) == EnqueueResult::Queued,
                      "saturated user correlations admitted");
            auto a = c->endpoint(ids[0]);
            auto data = a->readChunk();
            a->consumed(data.bytes.size());
            auto full = a->readChunk();
            check(full.bytes == std::vector<char>({'f', 'u', 'l', 'l'}),
                  "peer holds all47 actual ordinary requests");
            for (size_t i = 1; i < ids.size(); ++i) {
                auto ep = c->endpoint(ids[i]);
                auto d = ep->readChunk();
                ep->consumed(d.bytes.size());
            }
            for (auto id : ids)
                check(c->closeSession(id) != 0,
                      "all16 Close lanes admitted despite ordinary saturation");
            c->shutdown();
            auto outcome = c->join();
            check(outcome.graceful,
                  "16 Credit +16 Close +Shutdown progress beside full ordinary lane");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "policy") {
            connection_detail::Deadline frame, writer, stage;
            Clock::time_point start{};
            check(!frame.expired(start + std::chrono::hours(1)), "idle framing never times out");
            frame.arm(start);
            check(!frame.expired(start + std::chrono::milliseconds(1999)) &&
                      frame.expired(start + std::chrono::milliseconds(2000)),
                  "first-byte deadline expires at boundary, unaffected by drip");
            frame.disarm();
            check(!frame.expired(start + std::chrono::hours(1)),
                  "complete framing disarms idle deadline");
            writer.arm(start);
            writer.arm(start + std::chrono::milliseconds(1500));
            check(!writer.expired(start + std::chrono::milliseconds(2000)) &&
                      writer.expired(start + std::chrono::milliseconds(3500)),
                  "actual write progress resets no-progress deadline");
            stage.arm(start);
            check(stage.expired(start + std::chrono::milliseconds(2000)),
                  "Ack alone cannot reset stage deadline");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "zero") {
            auto c = start("zero-consumed");
            c->createSession(24, 80);
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            a->consumed(0);
            auto data = a->readChunk();
            a->consumed(data.bytes.size());
            a->consumed(0);
            check(c->submitResize(a->metadata().id, 30, 100) == EnqueueResult::Queued,
                  "zero-consumed fixture resize");
            auto next = a->readChunk();
            check(next.bytes == std::vector<char>({'n', 'e', 'x', 't'}),
                  "zero consumption cannot corrupt chunk accounting");
            a->consumed(next.bytes.size());
            c->shutdown();
            check(c->join().graceful, "zero no-op cleanup");
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "native") {
            setenv("SHELL", "/bin/sh", 1);
            auto r = CoreConnection::start(argv[2]);
            check(bool(r.connection), "real core launch");
            auto c = r.connection;
            event(c, ConnectionEvent::Kind::Ready);
            check(c->createSession(24, 80) != 0, "real core create");
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            const std::string command = "printf '\\001fixture\\377\\n'; exit 9\n";
            check(c->submitInput(a->metadata().id,
                                 std::vector<uint8_t>(command.begin(), command.end()),
                                 InputOrigin::User) == EnqueueResult::Queued,
                  "real synthetic shell input");
            std::vector<char> all;
            uint64_t sequence = 0;
            for (;;) {
                auto chunk = a->readChunk();
                check(chunk.kind != TransportChunk::Kind::Lost, "real core contact remains usable");
                sequence = chunk.sequence;
                if (chunk.kind == TransportChunk::Kind::End)
                    break;
                all.insert(all.end(), chunk.bytes.begin(), chunk.bytes.end());
                a->consumed(chunk.bytes.size());
            }
            const std::vector<char> expected = {1, 'f', 'i', 'x', 't', 'u', 'r', 'e', char(255)};
            check(std::search(all.begin(), all.end(), expected.begin(), expected.end()) !=
                      all.end(),
                  "real shell raw synthetic bytes");
            a->flushed(sequence);
            check(a->metadata().state == SessionState::Exited && a->metadata().status.value == 9,
                  "real core exact status after flush");
            event(c, ConnectionEvent::Kind::Exited);
            c->shutdown();
            check(c->join().graceful, "real retained exited session closes gracefully");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "cycles") {
            auto c = start("cycles");
            std::vector<std::shared_ptr<SessionEndpoint>> retained;
            for (int i = 0; i < 48; ++i) {
                check(c->createSession(24, 80) != 0,
                      "retired endpoint bookkeeping releases session slots");
                auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
                auto data = a->readChunk();
                a->consumed(data.bytes.size());
                for (int j = 0; j < 8; ++j)
                    check(c->submitResize(a->metadata().id, 30, 100) == EnqueueResult::Queued,
                          "pre-close staged emissions");
                check(c->closeSession(a->metadata().id) != 0, "cycle close");
                event(c, ConnectionEvent::Kind::Closed);
                retained.push_back(a);
            }
            for (auto &a : retained) {
                a->consumed(3);
                a->flushed(1);
            }
            for (auto &a : retained)
                check(a->readChunk().kind == TransportChunk::Kind::Lost &&
                          a->metadata().state == SessionState::Closed,
                      "retained endpoint owns Closed snapshot without live queue");
            c->shutdown();
            check(c->join().graceful, "bounded retired endpoint cycle cleanup");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "failures") {
            size_t baseline = 0;
            for (int fd = 0; fd < 1024; ++fd)
                if (fcntl(fd, F_GETFD) >= 0)
                    ++baseline;
            struct Case {
                const char *name;
                ContactError error;
            };
            for (auto test : {Case{"hello-stall", ContactError::HandshakeTimeout},
                              Case{"slow-drip", ContactError::FrameTimeout},
                              Case{"oversize", ContactError::Protocol},
                              Case{"wrong-ack", ContactError::Protocol},
                              Case{"malformed-status", ContactError::Protocol},
                              Case{"unexpected-credit-error", ContactError::Protocol},
                              Case{"overflow-output", ContactError::Protocol}}) {
                setenv("AV_CONNECTION_CASE", test.name, 1);
                auto then = Clock::now();
                auto r = CoreConnection::start(FAKE_CORE);
                check(bool(r.connection), "failure fixture launch");
                if (std::string(test.name) == "malformed-status" ||
                    std::string(test.name) == "unexpected-credit-error" ||
                    std::string(test.name) == "overflow-output") {
                    event(r.connection, ConnectionEvent::Kind::Ready);
                    r.connection->createSession(24, 80);
                    if (std::string(test.name) == "unexpected-credit-error") {
                        auto a = r.connection->endpoint(
                            event(r.connection, ConnectionEvent::Kind::Created).session);
                        auto data = a->readChunk();
                        a->consumed(data.bytes.size());
                    }
                }
                auto loss = event(r.connection, ConnectionEvent::Kind::Lost);
                check(loss.contactError == test.error, "typed framing/handshake/correlation loss");
                auto outcome = r.connection->join();
                check(!outcome.graceful && outcome.contactError == test.error,
                      "failure never graceful");
                check(Clock::now() - then < std::chrono::seconds(4),
                      "bounded native failure completion");
            }
            auto idle = start("idle");
            idle->createSession(24, 80);
            auto a = idle->endpoint(event(idle, ConnectionEvent::Kind::Created).session);
            auto data = a->readChunk();
            a->consumed(data.bytes.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(2200));
            check(idle->submitResize(a->metadata().id, 30, 100) == EnqueueResult::Queued,
                  "idle connection has no generic two-second timeout");
            idle->shutdown();
            check(idle->join().graceful, "idle remains usable");
            idle.reset();
            size_t finalFDs = 0;
            for (int fd = 0; fd < 1024; ++fd)
                if (fcntl(fd, F_GETFD) >= 0)
                    ++finalFDs;
            check(finalFDs == baseline, "failure and idle fd baseline");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "credit") {
            auto c = start("credit-detach");
            c->createSession(24, 80);
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto id = a->metadata().id;
            check(c->returnCredit(id, 1) == 0, "credit cannot return unread data");
            auto first = a->readChunk();
            a->consumed(3);
            check(c->returnCredit(id, 1) == 0, "one pending Credit per session");
            if(first.bytes.size()==3){
                auto second = a->readChunk();
                check(second.bytes==std::vector<char>({'t','a','i','l'}),"remaining arbitrary chunk bytes");
                a->consumed(second.bytes.size());
            }else{
                check(first.bytes==std::vector<char>({'A','\0',char(255),'t','a','i','l'}),"coalesced arbitrary chunk bytes");
                a->consumed(4);
            }
            a->cancelLocal();
            check(c->closeSession(id) != 0, "authoritative Close distinct from cancellation");
            event(c, ConnectionEvent::Kind::Closed);
            check(a->metadata().state == SessionState::Closed, "Closed metadata retained");
            c->createSession(24, 80);
            auto b = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto data = b->readChunk();
            b->consumed(data.bytes.size());
            check(c->submitResize(b->metadata().id, 30, 100) == EnqueueResult::Queued,
                  "detached credit does not corrupt B correlation");
            auto alive = b->readChunk();
            check(alive.bytes == std::vector<char>({'a', 'l', 'i', 'v', 'e'}),
                  "B continues after A late credit replies");
            b->consumed(alive.bytes.size());
            c->shutdown();
            check(c->join().graceful, "credit after Closed resolved exactly once");
            return 0;
        }
        if (argc == 2 && (std::string(argv[1]) == "blocked" || std::string(argv[1]) == "partial")) {
            bool blocked = std::string(argv[1]) == "blocked";
            auto c = start(blocked ? "blocked-writer" : "partial-write");
            std::vector<SessionId> ids;
            for (int i = 0; i < 16; ++i) {
                check(c->createSession(24, 80) != 0, "16 bounded sessions");
                ids.push_back(event(c, ConnectionEvent::Kind::Created).session);
            }
            check(c->createSession(24, 80) == 0, "17th session rejected");
            for (auto id : ids)
                for (int j = 0; j < 2; ++j)
                    check(c->submitInput(id, std::vector<uint8_t>(30720, uint8_t(id)),
                                         InputOrigin::User) == EnqueueResult::Queued,
                          "large owned input staged");
            if (!blocked) {
                auto ep = c->endpoint(ids.back());
                auto done = ep->readChunk();
                check(done.bytes == std::vector<char>({'c', 'o', 'm', 'p', 'l', 'e', 't', 'e'}),
                      "all32 short-write inputs checked independently by peer");
                ep->consumed(done.bytes.size());
            }
            auto admission = Clock::now();
            check(c->closeSession(ids[0]) != 0, "Close reserve independent of user tickets");
            c->shutdown();
            check(Clock::now() - admission < std::chrono::milliseconds(200),
                  "admission never waits for writer readiness");
            auto outcome = c->join();
            if (blocked)
                check(!outcome.graceful && (outcome.contactError == ContactError::WriteTimeout ||
                                            outcome.contactError == ContactError::ShutdownTimeout),
                      "blocked writer deadline");
            else
                check(outcome.graceful, "partial writes exact/noninterleaved expected streams");
            return 0;
        }
        if (argc == 2 &&
            (std::string(argv[1]) == "stopped" || std::string(argv[1]) == "uncertain")) {
            bool uncertain = std::string(argv[1]) == "uncertain";
            delayObservation = uncertain;
            signals = 0;
            signalBeforeRestoration = 0;
            restoredForSignal = false;
            auto reaps = observedReaps.load();
            setenv("AV_CONNECTION_CASE", "stopped-core", 1);
            auto r = CoreConnection::start(FAKE_CORE, [] { restoredForSignal = true; });
            check(bool(r.connection), "stopped core launch");
            event(r.connection, ConnectionEvent::Kind::Ready);
            auto then = Clock::now();
            r.connection->shutdown();
            auto outcome = r.connection->join();
            check(outcome.contactError == ContactError::ShutdownTimeout && !outcome.graceful,
                  "stopped core forced loss");
            check(outcome.core.termSent && outcome.core.killSent && signals == 2 &&
                      signalBeforeRestoration == 0,
                  "local restoration precedes owned TERM/KILL exactly once");
            check(outcome.core.kind == (uncertain ? CoreCompletion::Kind::CleanupUncertain
                                                  : CoreCompletion::Kind::Signaled),
                  "native reap versus cleanup uncertainty distinct");
            check(Clock::now() - then < std::chrono::seconds(5),
                  "local completion independent of kernel observation");
            delayObservation = false;
            auto deadline = Clock::now() + std::chrono::seconds(1);
            while (observedReaps == reaps && Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(observedReaps > reaps, "retained owner eventually reaps");
            r.connection->cancelLocal();
            check(signals == 2, "no post completion signal");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "watchdog") {
            std::atomic<int> restored{0};
            auto then = Clock::now();
            std::atomic<int64_t> restorationMs{0};
            setenv("AV_CONNECTION_CASE", "ack-close-stall", 1);
            auto r = CoreConnection::start(FAKE_CORE, [&] {
                restorationMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - then)
                        .count();
                ++restored;
            });
            check(bool(r.connection), "watchdog launched");
            event(r.connection, ConnectionEvent::Kind::Ready);
            then = Clock::now();
            r.connection->shutdown();
            auto outcome = r.connection->join();
            auto elapsed = Clock::now() - then;
            check(elapsed >= std::chrono::seconds(2) && elapsed < std::chrono::seconds(4),
                  "Ack without core exit keeps same bounded Shutdown deadline");
            check(!outcome.graceful && outcome.contactError == ContactError::ShutdownTimeout,
                  "watchdog is forced contact loss");
            check(outcome.core.kind == CoreCompletion::Kind::Signaled && outcome.core.termSent &&
                      outcome.core.killSent,
                  "ignored TERM receives owned KILL/reap");
            check(restored == 1 && restorationMs >= 0 && restorationMs < 2400,
                  "local restoration before escalation completion");
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "reserve") {
            auto c = start("reserve");
            c->createSession(24, 80);
            auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto data = a->readChunk();
            a->consumed(data.bytes.size());
            c->createSession(24, 80);
            auto b = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
            auto bd = b->readChunk();
            b->consumed(bd.bytes.size());
            auto id = a->metadata().id;
            check(c->submitInput(id, std::vector<uint8_t>(30720, 'u'), InputOrigin::User) ==
                      EnqueueResult::Queued,
                  "user first half");
            check(c->submitInput(id, std::vector<uint8_t>(30720, 'u'), InputOrigin::User) ==
                      EnqueueResult::Queued,
                  "user60KiB staging");
            check(c->submitInput(id, {'x'}, InputOrigin::User) == EnqueueResult::Overflow,
                  "user bytes cannot consume reply reserve");
            for (int i = 0; i < 45; ++i)
                check(c->submitResize(id, 30, 100) == EnqueueResult::Queued, "47 user tickets");
            check(c->submitResize(id, 30, 100) == EnqueueResult::Overflow, "48th user rejected");
            check(c->submitInput(id, std::vector<uint8_t>(2048, 'r'), InputOrigin::EmulatorReply) ==
                      EnqueueResult::Queued,
                  "slot48 admits reply");
            check(c->submitInput(id, std::vector<uint8_t>(2048, 's'), InputOrigin::EmulatorReply) ==
                      EnqueueResult::Queued,
                  "second reply segment stages while slot awaits Ack");
            check(c->submitInput(id, {'t'}, InputOrigin::EmulatorReply) == EnqueueResult::Overflow,
                  "reply reserve exhausted explicitly");
            auto ready = b->readChunk();
            check(ready.bytes == std::vector<char>({'r', 'e', 'a', 'd', 'y'}),
                  "peer reached reply1 while holding its Ack");
            b->consumed(ready.bytes.size());
            // Wait for the prior user tickets to complete, then stage behind reply2.
            auto deadline = Clock::now() + std::chrono::seconds(2);
            EnqueueResult queued;
            do {
                queued = c->submitInput(id, {'z'}, InputOrigin::User);
                if (queued == EnqueueResult::Queued)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } while (Clock::now() < deadline);
            check(queued == EnqueueResult::Queued, "user tickets reclaimed");
            check(c->submitResize(id, 40, 120) == EnqueueResult::Queued,
                  "resize ordered behind replies and user");
            check(c->submitResize(b->metadata().id, 30, 100) == EnqueueResult::Queued,
                  "B progresses while A reply awaits correlation slot");
            auto progress = b->readChunk();
            check(progress.bytes == std::vector<char>({'p', 'r', 'o', 'g', 'r', 'e', 's', 's'}),
                  "unrelated endpoint and reserved Credit progress");
            b->consumed(progress.bytes.size());
            auto ordered = a->readChunk();
            check(ordered.bytes == std::vector<char>({'o', 'r', 'd', 'e', 'r', 'e', 'd'}),
                  "accepted terminal emission order survives slot pressure");
            a->consumed(ordered.bytes.size());
            c->shutdown();
            check(c->join().graceful, "reserve cleanup");
            return 0;
        }
        // Missing launch/handshake/demux cannot pass: expectations are literal bytes,
        // and fake_core.py constructs framing independently of the C++ codec.
        auto c = start("streams");
        check(c->createSession(24, 80) != 0, "create accepted");
        auto a = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
        check(bool(a), "Created registers endpoint");
        check(c->createSession(24, 80) != 0, "second create accepted");
        auto b = c->endpoint(event(c, ConnectionEvent::Kind::Created).session);
        auto ar = std::async(std::launch::async, [a] { return a->readChunk(); });
        auto br = std::async(std::launch::async, [b] { return b->readChunk(); });
        auto ac = ar.get(), bc = br.get();
        check(ac.kind == TransportChunk::Kind::Data &&
                  ac.bytes == std::vector<char>({'A', 0, char(255)}) && ac.sequence == 1,
              "A owns raw00ff output");
        check(bc.bytes == std::vector<char>({'B', 'x', 'y'}) && bc.sequence == 1,
              "B reader cannot steal A");
        check(a->metadata().state == SessionState::Running,
              "wire Exited staged before consumption");
        a->flushed(1);
        check(a->metadata().state == SessionState::Running,
              "flush cannot publish before actual byte consumption");
        a->consumed(ac.bytes.size());
        b->consumed(bc.bytes.size());
        auto end = a->readChunk();
        check(end.kind == TransportChunk::Kind::End && end.sequence == 1,
              "End follows data with lastSequence");
        a->flushed(0);
        check(a->metadata().state == SessionState::Running, "wrong flush cannot expose status");
        a->flushed(1);
        check(a->metadata().state == SessionState::Exited && a->metadata().status.value == 7,
              "actual final flush publishes exact status");
        auto exitedEvent = event(c, ConnectionEvent::Kind::Exited);
        check(exitedEvent.metadata.state == SessionState::Exited &&
                  exitedEvent.metadata.status.value == 7,
              "Exited notification owns the committed final snapshot");
        a->cancelLocal();
        check(a->readChunk().kind == TransportChunk::Kind::Lost, "detached read wakes locally");
        check(c->submitResize(b->metadata().id, 30, 100) == EnqueueResult::Queued,
              "A cancellation preserves B admissions");
        auto waiting = std::async(std::launch::async, [b] { return b->readChunk(); });
        c->shutdown();
        check(c->join().graceful, "Shutdown Ack plus core exit graceful");
        bool woke = waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready;
        if (!woke)
            c->cancelLocal();
        auto cancelledChunk = waiting.get();
        check(cancelledChunk.kind == TransportChunk::Kind::Lost,
              "shutdown reader sees local cancellation");
        check(woke, "graceful local shutdown wakes endpoint presentation reader");
        std::cout << "bounded endpoint ownership and staged exit passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
