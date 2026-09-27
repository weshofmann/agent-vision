// Real libvterm DSR/CPR -> tagged Writer -> adapter -> literal Python wire peer.
#include <atomic>
#include <chrono>
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
    check(!controller->readerThread.joinable() && !controller->writerThread.joinable(),
          "both owned presentation workers joined");
    check(controller->lockState([](auto &s) { return s.surface.size == TPoint{80, 24}; }),
          "render state accessible after reply overflow/loss");
    controller->shutDown();
    owner.controller = nullptr;
    c->shutdown();
    auto completion = c->join();
    ep.reset();
    c.reset();
    check(descriptors() == baseDescriptors, "descriptor baseline restored");
    check(completion.core.kind == CoreCompletion::Kind::Exited && completion.core.value == 0 &&
              !completion.core.termSent && !completion.core.killSent,
          "fake peer assertions and direct-child cleanup exit0 without escalation");
    std::printf("PHASE: schedule=%s callbacks=%d consumed=%zu lost=%d replies=%zu filled=%d "
                "early=%d contact=%d graceful=%d; workers joined; descriptors baseline; peer exit0\n",
                schedule.c_str(), gate.reads, gate.consumed, gate.losses, gate.replies.size(),
                gate.filled, early, int(completion.contactError), completion.graceful);
    const bool overflow = !early && !gate.timedOut && gate.filled &&
        gate.bytes == "\033[5n\033[6nready\033[5n" && gate.consumed == 17 &&
        gate.replies.size() == 3 && gate.replies[1].bytes == std::string(4086, 'r') &&
        gate.replies[1].result == tvterm::EnqueueResult::Queued &&
        gate.replies[2].bytes == "\033[0n" &&
        gate.replies[2].result == tvterm::EnqueueResult::Overflow &&
        gate.generated == "\033[0n\033[1;1R\033[0n" && gate.losses == 0 &&
        completion.contactError == ContactError::None && completion.graceful;
    if (schedule == "early") {
        check(early && gate.generated == "\033[0n\033[1;1R\033[0n" &&
                  !overflow && completion.graceful &&
                  completion.contactError == ContactError::None,
              "early query negative must reject phase ordering with graceful cleanup");
        std::puts("PASS negative: real second DSR generated before filling cannot satisfy overflow oracle");
    } else if (schedule == "loss") {
        check(!overflow && gate.bytes == "\033[5n\033[6nready" && gate.losses == 1 &&
                  gate.replies.size() == 2 && !completion.graceful &&
                  completion.contactError == ContactError::EOFReached,
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
              schedule == "early" || schedule == "loss" || schedule == "mechanism"),
              "known fixture schedule");
        if (schedule == "mechanism") mechanismRegression();
        else run(schedule);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
