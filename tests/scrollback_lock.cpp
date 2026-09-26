// R1: real state-held scrollbar callback must enqueue while publication owns
// the emulator mutex. Barriers force the order; no timing/stress-loop premise.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unistd.h>
#include <tvterm/termemu.h>
// Test-only access to the production implementation for deterministic scheduling.
// The product target has neither this visibility change nor scheduling hooks.
#define private public
#include <tvterm/termctrl.h>
#undef private
#include AGENTVISION_TERMCTRL_SOURCE
#include <tvterm/vtermemu.h>
#include <tvterm/termview.h>
#include <tvterm/consts.h>
#define Uses_TScrollBar
#define Uses_TGroup
#define Uses_THardwareInfo
#include <tvision/tv.h>

using namespace std::chrono;
struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool start {false}, acquired {false}, done {false};
};
class CoordinatedScrollBar final : public TScrollBar {
    Gate &gate;
public:
    bool armed {false};
    CoordinatedScrollBar(TRect bounds, Gate &g) : TScrollBar(bounds), gate(g) {}
    void scrollDraw() override {
        if (armed) {
            std::unique_lock<std::mutex> lock(gate.mutex);
            gate.start = true;
            gate.changed.notify_all();
            if (!gate.changed.wait_for(lock, seconds(2), [&] { return gate.acquired; })) {
                std::fputs("FAIL: worker did not acquire emulator mutex\n", stderr);
                std::abort();
            }
        }
        TScrollBar::scrollDraw(); // Actual broadcast → TerminalView → sendEvent.
    }
};
class ObservedEmulator final : public tvterm::TerminalEmulator {
    tvterm::TerminalEmulator &inner;
    std::atomic<int> &scrollEvents;
public:
    ObservedEmulator(tvterm::TerminalEmulator &e, std::atomic<int> &n) : inner(e), scrollEvents(n) {}
    ~ObservedEmulator() { delete &inner; }
    void handleEvent(const tvterm::TerminalEvent &event) noexcept override {
        if (event.type == tvterm::TerminalEventType::ScrollBackOffsetChange) ++scrollEvents;
        inner.handleEvent(event);
    }
    void updateState(tvterm::TerminalState &state) noexcept override { inner.updateState(state); }
};
class ObservedFactory final : public tvterm::TerminalEmulatorFactory {
    tvterm::VTermEmulatorFactory inner;
public:
    std::atomic<int> scrollEvents {0};
    tvterm::TerminalEmulator &create(TPoint size, tvterm::Writer &writer) noexcept override {
        return *new ObservedEmulator(inner.create(size, writer), scrollEvents);
    }
    TSpan<const tvterm::EnvironmentVar> getCustomEnvironment() noexcept override {
        return inner.getCustomEnvironment();
    }
};
int main()
{
    THardwareInfo hardware;
    TEventQueue::wakeUp();
    ObservedFactory factory;
    // The lock-order fixture needs no child I/O: an inert descriptor lets us
    // schedule production publication without reader/writer competition. Real
    // shell/resource cleanup is independently checked in the desktop PTY test.
    tvterm::PtyDescriptor descriptor {-1, -1};
    auto *controller = new tvterm::TerminalController({40, 10}, factory, std::unique_ptr<tvterm::SessionTransport>(new tvterm::LocalSessionTransport(descriptor)));
    Gate gate;
    auto *group = new TGroup(TRect(0, 0, 41, 10));
    const tvterm::TVTermConstants constants {2000,2001,2002,2003,100,101,102,1000,1001};
    auto *bar = new CoordinatedScrollBar(TRect(40, 0, 41, 10), gate);
    auto *view = new tvterm::TerminalView(TRect(0, 0, 40, 10), *controller, constants, bar);
    group->insert(bar); group->insert(view);
    controller->lockState([](auto &state) {
        state.surface.resize({40, 10});
        state.scrollbackEnabled = true; state.scrollbackChanged = true;
        state.scrollbackLimit = 4; state.scrollbackOffset = 1;
    });
    factory.scrollEvents = 0;
    bar->armed = true;
    std::thread worker([&] {
        {
            std::unique_lock<std::mutex> lock(gate.mutex);
            gate.changed.wait(lock, [&] { return gate.start; });
        }
        std::lock_guard<std::mutex> serial(controller->eventLoop.mutex);
        {
            std::lock_guard<std::mutex> lock(gate.mutex);
            gate.acquired = true; gate.changed.notify_all();
        }
        bool updated = false;
        controller->eventLoop.currentTimeout = {};
        controller->eventLoop.updateState(updated); // Waits for draw's state lock.
        controller->eventLoop.processEvents(); // Prove callback was not suppressed.
    });
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(gate.mutex);
        if (!gate.changed.wait_for(lock, seconds(3), [&] { return gate.done; })) {
            // Failure cannot unwind deadlocked threads. There is no child or
            // owned PTY in this isolated lock fixture; process exit is bounded.
            std::fputs("FAIL: state-held scrollbar callback blocked worker publication\n", stderr);
            _exit(1);
        }
    });
    view->draw(); // Real draw holds terminal state through scrollbar callback.
    worker.join();
    {
        std::lock_guard<std::mutex> lock(gate.mutex);
        gate.done = true; gate.changed.notify_all();
    }
    watchdog.join();
    if (factory.scrollEvents != 1) {
        std::fputs("FAIL: scrollbar event did not reach real emulator\n", stderr);
        TObject::destroy(group); return 1;
    }
    TObject::destroy(group); // TerminalView owns controller.
    std::puts("PASS: state-held draw callback, worker publication and event propagation complete");
}
