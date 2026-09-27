// Opt-in native qualification: actual Go core, transport, libvterm and views.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <libproc.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <tvterm/termemu.h>
#include <unistd.h>
#include <vector>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#define private public
#include "core_connection.h"
#include <tvterm/termctrl.h>
#include <tvterm/termview.h>
#include <tvterm/termwnd.h>
#undef private
#pragma clang diagnostic pop
// Compile the exact production connection, exposing only read-only test snapshots.
// The static client archive does not extract its duplicate connection object.
#include AGENTVISION_CONNECTION_SOURCE
#include "ipc_session.h"
#include <tvterm/consts.h>
#include <tvterm/vtermemu.h>
#define Uses_TFrame
#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TEventQueue
#include <tvision/tv.h>
using namespace agentvision;
using Clock = std::chrono::steady_clock;
static const tvterm::TVTermConstants constants{2000, 2001, 2002, 2003, 2004,
                                               2005, 2006, 2010, 2011};
static void check(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
struct Observation {
    std::mutex mutex;
    std::string tail;
    size_t xs = 0, consumed = 0, emulated = 0, published = 0;
    size_t heavyBytes = 0;
    bool heavyValid = true;
    int focus = 0, scroll = 0, copies = 0, replies = 0;
    uint64_t flushed = 0;
    std::atomic<bool> flushEntered{false}, permitFlush{true};
};
class Emulator final : public tvterm::TerminalEmulator {
    tvterm::TerminalEmulator &inner;
    Observation &o;

  public:
    Emulator(tvterm::TerminalEmulator &e, Observation &obs) : inner(e), o(obs) {}
    ~Emulator() { delete &inner; }
    void handleEvent(const tvterm::TerminalEvent &e) noexcept override {
        {
            std::lock_guard<std::mutex> lock(o.mutex);
            if (e.type == tvterm::TerminalEventType::ClientDataRead) {
                o.emulated += e.clientDataRead.size;
                for (size_t i = 0; i < e.clientDataRead.size; ++i) {
                    char c = e.clientDataRead.data[i];
                    o.xs += c == 'X';
                    if (o.heavyBytes < 1027 * 1024 && (o.heavyBytes || c == 'X')) {
                        size_t offset = o.heavyBytes % 1027;
                        char expected = offset < 1024 ? 'X' : offset < 1026 ? '\r' : '\n';
                        o.heavyValid &= c == expected;
                        ++o.heavyBytes;
                    }
                }
                o.tail.append(e.clientDataRead.data, e.clientDataRead.size);
                if (o.tail.size() > 8192)
                    o.tail.erase(0, o.tail.size() - 8192);
            }
            o.focus += e.type == tvterm::TerminalEventType::FocusChange;
            o.scroll += e.type == tvterm::TerminalEventType::ScrollBackOffsetChange;
            o.copies += e.type == tvterm::TerminalEventType::CopySelection;
        }
        inner.handleEvent(e);
    }
    void updateState(tvterm::TerminalState &s) noexcept override {
        inner.updateState(s);
        std::lock_guard<std::mutex> lock(o.mutex);
        o.published = o.emulated;
    }
};
class Factory final : public tvterm::TerminalEmulatorFactory {
    tvterm::VTermEmulatorFactory inner;
    Observation &o;

  public:
    Factory(Observation &obs) : o(obs) {}
    tvterm::TerminalEmulator &create(TPoint s, tvterm::Writer &w) noexcept override {
        return *new Emulator(inner.create(s, w), o);
    }
    TSpan<const tvterm::EnvironmentVar> getCustomEnvironment() noexcept override {
        return inner.getCustomEnvironment();
    }
};
class Transport final : public tvterm::SessionTransport {
  public:
    std::shared_ptr<SessionEndpoint> bound;
    IpcSessionTransport ipc;
    Observation &o;
    Transport(std::shared_ptr<CoreConnection> c, std::shared_ptr<SessionEndpoint> e,
              Observation &obs)
        : bound(e), ipc(c, e), o(obs) {}
    tvterm::EnqueueResult enqueueInput(TSpan<const char> b,
                                       tvterm::InputOrigin origin) noexcept override {
        auto result = ipc.enqueueInput(b, origin);
        if (origin == tvterm::InputOrigin::EmulatorReply &&
            result == tvterm::EnqueueResult::Queued) {
            std::lock_guard<std::mutex> l(o.mutex);
            ++o.replies;
        }
        return result;
    }
    tvterm::TransportChunk readChunk() noexcept override { return ipc.readChunk(); }
    void consumed(size_t n) noexcept override {
        ipc.consumed(n);
        std::lock_guard<std::mutex> l(o.mutex);
        o.consumed += n;
    }
    void flushed(uint64_t n) noexcept override {
        o.flushEntered = true;
        while (!o.permitFlush)
            std::this_thread::yield(); // Test barrier outside product locks.
        ipc.flushed(n);
        {
            std::lock_guard<std::mutex> l(o.mutex);
            o.flushed = n;
        }
    }
    void resize(TPoint s) noexcept override { ipc.resize(s); }
    void cancelLocal() noexcept override {
        o.permitFlush = true;
        ipc.cancelLocal();
    }
};
class PresentationWindow final : public tvterm::BasicTerminalWindow {
  public:
    tvterm::TerminalController &controller;
    Transport &transport;
    std::string caption;
    bool lost = false;
    PresentationWindow(TRect bounds, tvterm::TerminalController &c, Transport &t)
        : TWindowInit(&initFrame), BasicTerminalWindow(bounds, c, constants), controller(c),
          transport(t) {}
    tvterm::TerminalView *terminalView() {
        auto *v = first();
        if (v)
            do {
                if (auto *t = dynamic_cast<tvterm::TerminalView *>(v))
                    return t;
                v = v->nextView();
            } while (v && v != first());
        throw std::runtime_error("real TerminalView child missing");
    }
    const char *getTitle(short) override {
        auto m = transport.ipc.metadata();
        caption = lost ? "IPC [authority/contact lost]"
                  : m.state == SessionState::Exited
                      ? "IPC [exited " + std::to_string(m.status.value) + "]"
                      : "IPC [running]";
        return caption.c_str();
    }
};
static int children(pid_t p) {
    pid_t list[64];
    int n = proc_listchildpids(p, list, sizeof list);
    check(n >= 0, "child inventory");
    return n;
}
static pid_t onlyChild() {
    pid_t list[64];
    int n = proc_listchildpids(getpid(), list, sizeof list);
    check(n == 1, "frontend owns only direct core child");
    return list[0];
}
static void resources(std::ofstream &log, const char *stage, int cycle, pid_t core) {
    log << "RESOURCE " << stage << " cycle=" << cycle;
    for (auto pid : {getpid(), core}) {
        proc_taskinfo info{};
        check(proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &info, sizeof info) == sizeof info,
              "task inventory");
        int n = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
        check(n > 0, "FD inventory");
        std::vector<char> fd(n + 256);
        n = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fd.data(), int(fd.size()));
        check(n > 0, "FD snapshot");
        log << (pid == core ? " core" : " frontend") << "_threads=" << info.pti_threadnum
            << " fds=" << n / sizeof(proc_fdinfo) << " children=" << children(pid);
    }
    log << '\n';
    log.flush();
}
struct Restore {
    std::mutex mutex;
    std::condition_variable changed;
    bool requested = false, ack = false, timeout = false;
    Clock::time_point requestedAt;
    void callback() {
        std::unique_lock<std::mutex> l(mutex);
        requestedAt = Clock::now();
        requested = true;
        changed.notify_all();
        if (!changed.wait_for(l, std::chrono::seconds(2), [&] { return ack; }))
            timeout = true;
    }
};
class App final : public TApplication {
  public:
    std::shared_ptr<CoreConnection> core;
    Restore &restore;
    std::ofstream &log;
    Observation a, b;
    PresentationWindow *wa = nullptr, *wb = nullptr;
    pid_t corePID = 0;
    int step = 0, cycle = 0;
    bool loss, prematureLoss, suppressCaption, failed = false;
    bool workloadCompleted = false, intentionalLoss = false, shutdownIssued = false;
    int creditBarriers = 0, aCompletions = 0;
    std::string failure;
    Clock::time_point deadline = Clock::now() + std::chrono::seconds(15), quiet;
    App(Restore &r, std::ofstream &l, const std::string &mode)
        : TProgInit(nullptr, nullptr, &initDeskTop), restore(r), log(l), loss(mode != "normal"),
          prematureLoss(mode == "premature-loss"), suppressCaption(mode == "no-caption") {
        auto result = CoreConnection::start(REAL_CORE, [&] { restore.callback(); });
        core = result.connection;
        check(bool(core), "real core start");
        corePID = onlyChild();
        resources(log, "pre-load", 0, corePID);
    }
    bool has(Observation &o, const char *text) {
        std::lock_guard<std::mutex> l(o.mutex);
        return o.tail.find(text) != std::string::npos;
    }
    void advance() {
        ++step;
        deadline = Clock::now() + std::chrono::seconds(15);
        log << "STAGE " << step << " cycle=" << cycle << '\n';
        log.flush();
    }
    PresentationWindow *make(SessionId id, Observation &o, bool second) {
        TRect bounds(second ? 5 : 0, second ? 3 : 0, second ? 67 : 62, second ? 25 : 22);
        auto size = PresentationWindow::viewSize(bounds);
        Factory factory(o);
        auto transport = std::unique_ptr<Transport>(new Transport(core, core->endpoint(id), o));
        auto &t = *transport;
        auto *c =
            tvterm::TerminalController::createWithTransport(size, factory, std::move(transport));
        check(c != nullptr, "production controller");
        auto *w = new PresentationWindow(bounds, *c, t);
        insertWindow(w);
        return w;
    }
    void type(PresentationWindow *w, const std::string &s) {
        w->select();
        for (char c : s) {
            TEvent ev{};
            ev.what = evKeyDown;
            ev.keyDown.keyCode = c == '\n' ? kbEnter : ushort((unsigned char)c);
            if (c != '\n') {
                ev.keyDown.text[0] = c;
                ev.keyDown.textLength = 1;
            }
            w->terminalView()->handleEvent(ev);
        }
    }
    void local(PresentationWindow *w, Observation &o) {
        int scroll, copies;
        {
            std::lock_guard<std::mutex> l(o.mutex);
            scroll = o.scroll;
            copies = o.copies;
        }
        tvterm::TerminalEvent e{};
        e.type = tvterm::TerminalEventType::ScrollBackOffsetChange;
        e.scrollBackOffsetChange.offset = 0;
        w->controller.sendEvent(e);
        w->controller.stateHasBeenUpdated();
        auto *view = w->terminalView();
        TEvent select{};
        select.what = evCommand;
        select.message.command = constants.cmStartSelection;
        view->handleEvent(select);
        TEvent extend{};
        extend.what = evMouseMove;
        extend.mouse.buttons = mbLeftButton;
        extend.mouse.where = view->makeGlobal({5, 0});
        view->handleEvent(extend);
        TEvent copy{};
        copy.what = evCommand;
        copy.message.command = constants.cmCopySelection;
        view->handleEvent(copy);
        w->controller.stateHasBeenUpdated();
        {
            std::lock_guard<std::mutex> l(o.mutex);
            check(o.scroll > scroll && o.copies > copies,
                  "retained local scroll/selection emulator dispatch");
        }
        w->terminalView()->draw();
    }
    void finish(PresentationWindow *w) {
        w->controller.finishPresentation();
        check(!w->controller.readerThread.joinable() && !w->controller.writerThread.joinable(),
              "both owned presentation workers joined");
        log << "WORKERS joined=2 cycle=" << cycle << '\n';
        log.flush();
    }
    bool creditsComplete(PresentationWindow *w) {
        auto binding = w->transport.bound;
        auto id = binding->metadata().id;
        auto &state = *core->state_;
        std::lock_guard<std::mutex> connection(state.mutex);
        auto it = state.sessions.find(id);
        if (!state.ready || state.stopped || state.readDone || state.shutdown ||
            state.error != ContactError::None || it == state.sessions.end() ||
            it->second.endpoint.get() != binding.get() || it->second.closed || it->second.closing)
            return false;
        auto &endpoint = *binding->state_;
        std::lock_guard<std::mutex> queue(endpoint.mutex);
        return !endpoint.cancelled && !endpoint.lost &&
               endpoint.visible.state == SessionState::Running && endpoint.rawOutstanding == 0 &&
               endpoint.consumedPending == 0 && it->second.credit == 0 && endpoint.borrowed == 0 &&
               endpoint.chunks.empty();
    }
    void idle() override {
        TApplication::idle();
        {
            std::lock_guard<std::mutex> l(restore.mutex);
            if (restore.requested) {
                if (!workloadCompleted || (loss ? !intentionalLoss : !shutdownIssued)) {
                    failed = true;
                    failure = "premature restoration/contact loss before workload completion";
                    log << "FAIL " << failure << " cycle=" << cycle << '\n';
                }
                endModal(cmQuit);
                return;
            }
        }
        if (failed)
            return;
        try {
            message(this, evBroadcast, constants.cmCheckTerminalUpdates, nullptr);
            check(Clock::now() < deadline, "native stage deadline");
            ConnectionEvent e;
            while (core->pollEvent(e)) {
                check(e.kind != ConnectionEvent::Kind::RequestError, "real core request error");
                if (e.kind == ConnectionEvent::Kind::Ready) {
                    check(step == 0, "Ready stage");
                    core->createSession(20, 60);
                    advance();
                } else if (e.kind == ConnectionEvent::Kind::Created) {
                    if (!wa) {
                        wa = make(e.session, a, false);
                        if (!wb && cycle == 0)
                            core->createSession(20, 60);
                    } else {
                        check(!wb, "only two sessions");
                        wb = make(e.session, b, true);
                    }
                } else if (e.kind == ConnectionEvent::Kind::Lost) {
                    check(workloadCompleted && intentionalLoss,
                          "premature contact loss before intentional trigger");
                    if (wa)
                        wa->lost = true;
                    if (wb)
                        wb->lost = true;
                }
            }
            switch (step) {
            case 1:
                if (wa && wb && has(a, "IPC_READY") && (cycle > 0 || has(b, "IPC_READY"))) {
                    resources(log, "started", cycle, corePID);
                    if (prematureLoss) {
                        check(kill(corePID, SIGKILL) == 0, "probe kills only direct core");
                        advance();
                        return;
                    }
                    type(wa, "dsr\n");
                    advance();
                }
                break;
            case 2: {
                bool replyAdmitted;
                {
                    std::lock_guard<std::mutex> l(a.mutex);
                    replyAdmitted = a.replies > 0;
                }
                if (replyAdmitted) {
                    type(wa, "Z");
                    advance();
                }
            } break;
            case 3:
                if (has(a, "CPR_1b5b313b31525a")) {
                    wa->changeBounds(TRect(0, 0, 72, 26));
                    type(wa, "size\n");
                    advance();
                }
                break;
            case 4:
                if (has(a, "GEOMETRY_24_70")) {
                    type(wb, "ping\n");
                    advance();
                }
                break;
            case 5:
                if (has(b, "PONG")) {
                    {
                        std::lock_guard<std::mutex> l(a.mutex);
                        check(a.focus > 0, "A real view focus routing");
                    }
                    {
                        std::lock_guard<std::mutex> l(b.mutex);
                        check(b.focus > 0, "B real view focus routing");
                    }
                    type(wa, "heavy\n");
                    advance();
                }
                break;
            case 6:
                if (has(a, "HEAVY_BARRIER")) {
                    bool completed;
                    {
                        std::lock_guard<std::mutex> l(a.mutex);
                        check(a.xs == 1048576, "exact one MiB frontend emulator consumption");
                        check(a.heavyBytes == 1027 * 1024 && a.heavyValid,
                              "exact synthetic MiB pattern including PTY CR/LF translation");
                        completed = a.consumed == a.emulated && a.published == a.emulated;
                    }
                    if (!completed || !creditsComplete(wa))
                        break;
                    log << "CREDIT_COMPLETE cycle=" << cycle
                        << " identity=current raw=0 pending=0 ticket=0\n";
                    ++creditBarriers;
                    resources(log, "consumed", cycle, corePID);
                    check(wa->transport.ipc.metadata().state == SessionState::Running,
                          "producer remains alive at consumption barrier");
                    a.permitFlush = false;
                    type(wa, "exit\n");
                    advance();
                }
                break;
            case 7:
                if (a.flushEntered) {
                    check(wa->transport.ipc.metadata().state != SessionState::Exited,
                          "exact status hidden until final publication/flush");
                    check(has(a, "FINAL_RETAINED"), "last output consumed before flush");
                    {
                        std::lock_guard<std::mutex> l(a.mutex);
                        check(a.published == a.emulated && a.consumed == a.emulated,
                              "final emulator updateState publication and complete consumption "
                              "before flush");
                    }
                    a.permitFlush = true;
                    advance();
                }
                break;
            case 8:
                if (wa->controller.clientIsDisconnected()) {
                    auto m = wa->transport.ipc.metadata();
                    check(m.state == SessionState::Exited && m.status.kind == ExitKind::Exit &&
                              m.status.value == 7 && m.lastSequence == a.flushed,
                          "authoritative exact exit retained after lastSequence flush");
                    finish(wa);
                    // A retained viewport need not follow the live terminal.
                    // Seek the bottom after joins and verify actual emulator cells.
                    auto bottom =
                        wa->controller.lockState([](auto &state) { return state.scrollbackLimit; });
                    tvterm::TerminalEvent scroll{};
                    scroll.type = tvterm::TerminalEventType::ScrollBackOffsetChange;
                    scroll.scrollBackOffsetChange.offset = bottom;
                    wa->controller.sendEvent(scroll);
                    wa->controller.stateHasBeenUpdated();
                    bool finalCells = wa->controller.lockState([](auto &state) {
                        for (int y = 0; y < state.surface.size.y; ++y) {
                            std::string row;
                            for (int x = 0; x < state.surface.size.x; ++x) {
                                auto text = state.surface.at(y, x).character.getText();
                                row.append(text.data(), text.size());
                            }
                            if (row.find("FINAL_RETAINED") != std::string::npos)
                                return true;
                        }
                        return false;
                    });
                    check(finalCells, "actual retained final marker accessible in emulator state");
                    ++aCompletions;
                    local(wa, a);
                    check(std::string(wa->getTitle(0)).find("exited 7") != std::string::npos,
                          "typed exit display");
                    check(wa->transport.ipc.requestClose() != 0,
                          "Exited Close after presentation finish");
                    advance();
                }
                break;
            case 9:
                if (!core->endpoint(wa->transport.ipc.metadata().id)) {
                    destroy(wa);
                    wa = nullptr;
                    {
                        std::lock_guard<std::mutex> l(b.mutex);
                        b.tail.clear();
                    }
                    type(wb, "ping\n");
                    resources(log, "after-cycle", cycle, corePID);
                    quiet = Clock::now() + std::chrono::milliseconds(250);
                    advance();
                }
                break;
            case 10:
                if (Clock::now() >= quiet && has(b, "PONG")) {
                    resources(log, "quiet", cycle, corePID);
                    check(children(corePID) == 1, "A closed while B continues");
                    if (++cycle < 16) {
                        std::lock_guard<std::mutex> l(a.mutex);
                        a.tail.clear();
                        a.xs = a.consumed = a.emulated = a.published = 0;
                        a.heavyBytes = 0;
                        a.heavyValid = true;
                        a.flushed = 0;
                        a.replies = a.focus = 0;
                        a.flushEntered = false;
                        core->createSession(20, 60);
                        step = 1;
                        deadline = Clock::now() + std::chrono::seconds(15);
                    } else if (loss) {
                        workloadCompleted = true;
                        check(kill(corePID, SIGKILL) == 0, "test kills only owned core");
                        intentionalLoss = true;
                        advance();
                    } else {
                        workloadCompleted = true;
                        shutdownIssued = true;
                        core->shutdown();
                        advance();
                    }
                }
                break;
            default:
                break;
            }
        } catch (const std::exception &e) {
            failed = true;
            failure = e.what();
            log << "FAIL stage=" << step << " cycle=" << cycle << " " << failure << '\n';
            {
                std::lock_guard<std::mutex> l(a.mutex);
                log << "SYNTHETIC_TAIL_HEX ";
                static const char hex[] = "0123456789abcdef";
                for (unsigned char c : a.tail) {
                    log << hex[c >> 4] << hex[c & 15];
                }
                log << " replies=" << a.replies << '\n';
            }
            log.flush();
            a.permitFlush = true;
            core->cancelLocal();
        }
    }
    void stopPresentation() {
        for (auto *w : {wa, wb}) {
            if (!w)
                continue;
            finish(w);
            local(w, w == wa ? a : b);
            w->lost = w->transport.ipc.metadata().state == SessionState::Lost;
        }
        if (loss && workloadCompleted && wb) {
            check(wb->transport.ipc.metadata().state == SessionState::Lost,
                  "loss is contact/authority, never fabricated exit");
            if (!suppressCaption)
                wb->frame->drawView();
            // Actual pinned event waits flush buffered display through FPS gating.
            // The outer decoder acknowledges only a caption in real terminal cells.
            auto until = Clock::now() + std::chrono::seconds(1);
            bool displayed = false;
            while (Clock::now() < until && !displayed) {
                TEventQueue::waitForEvents(20);
                TEvent event{};
                event.getKeyEvent();
                displayed = event.what == evKeyDown && event.keyDown.charScan.charCode == '~';
            }
            if (!displayed) {
                failed = true;
                failure = "loss caption publication acknowledgment missing";
            }
            log << "DISPLAY acknowledged=" << displayed << '\n';
        }
    }
};
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    setenv("SHELL", SESSION_FIXTURE, 1);
    std::ofstream log(argv[2]);
    Restore restore;
    try {
        App app(restore, log, argv[1]);
        app.run();
        try {
            app.stopPresentation();
        } catch (const std::exception &e) {
            app.failed = true;
            app.failure = e.what();
        }
        app.shutDown();
        app.suspend();
        // Real Turbo Vision teardown restores the outer terminal before ACK.
        {
            std::lock_guard<std::mutex> l(restore.mutex);
            log << "RESTORE_ACK milliseconds="
                << std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                         restore.requestedAt)
                       .count()
                << '\n';
            restore.ack = true;
            restore.changed.notify_all();
        }
        auto completion = app.core->join();
        log << "COMPLETION contact=" << int(completion.contactError)
            << " core=" << int(completion.core.kind) << " value=" << completion.core.value
            << " graceful=" << completion.graceful << '\n';
        check(!restore.timeout, "bounded UI restoration acknowledgement");
        log << "WORKLOAD completed=" << app.workloadCompleted
            << " intentional_loss=" << app.intentionalLoss << " cycles=" << app.cycle
            << " credits=" << app.creditBarriers << " A_completed=" << app.aCompletions << '\n';
        check(!app.failed, app.failure.c_str());
        check(app.workloadCompleted && app.cycle == 16 && app.creditBarriers == 16 &&
                  app.aCompletions == 16,
              "all sixteen workload/credit/completion gates");
        if (app.loss)
            check(app.intentionalLoss && !completion.graceful &&
                      completion.contactError == ContactError::EOFReached &&
                      completion.core.kind == CoreCompletion::Kind::Signaled &&
                      completion.core.value == SIGKILL,
                  "intentional direct-core SIGKILL and EOF authority loss");
        else
            check(app.shutdownIssued && completion.graceful &&
                      completion.contactError == ContactError::None &&
                      completion.core.kind == CoreCompletion::Kind::Exited &&
                      completion.core.value == 0,
                  "normal Shutdown authoritative direct-core exit0");
        log << "PASS actual presentation workers, UI restoration acknowledged, core joined\n";
        return 0;
    } catch (const std::exception &e) {
        log << "FAIL " << e.what() << '\n';
        return 1;
    }
}
