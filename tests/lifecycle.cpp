// Regression: natural direct-child reaping must not depend on closing a window.
#include <tvterm/pty.h>
#include <tvterm/termctrl.h>
#include <tvterm/vtermemu.h>
#define Uses_TKeys
#define Uses_TEventQueue
#define Uses_THardwareInfo
#define Uses_TPoint
#include <tvision/tv.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <fstream>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>
#include <atomic>
static std::atomic<int> createdWorkers{0}, joinedWorkers{0};
static std::atomic<uintptr_t> workerIdentities[128];
static std::atomic<int> workerJoinResults[128];
static uintptr_t identity(pthread_t thread) {
#if defined(__APPLE__)
    return reinterpret_cast<uintptr_t>(thread);
#else
    return static_cast<uintptr_t>(thread);
#endif
}
static void recordJoin(pthread_t thread, int status) {
    for (int ordinal = 0; ordinal < createdWorkers.load(); ++ordinal)
        if (workerIdentities[ordinal] == identity(thread) && workerJoinResults[ordinal] == -999) {
            workerJoinResults[ordinal] = status;
            if (!status) ++joinedWorkers;
            return;
        }
    std::abort(); // An unmatched observation is an instrumentation failure.
}
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                              void *(*start)(void *), void *argument)
{
    using Function = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    static auto native = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "pthread_create"));
    int result = native(thread, attributes, start, argument);
    if (!result) {
        int ordinal = createdWorkers.fetch_add(1);
        if (ordinal >= 128) std::abort();
        workerJoinResults[ordinal] = -999;
        workerIdentities[ordinal] = identity(*thread);
    }
    return result;
}
#if defined(__APPLE__)
// Test-only symbol wrapper. Actual libc++ join normal return is observed;
// underlying pthread rc=0 is a contract inference, never an intercepted value.
void std::thread::join() {
    using Function = void (*)(std::thread *);
    static auto native = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "_ZNSt3__16thread4joinEv"));
    static bool identified = [] {
        Dl_info info {};
        if (!native || !dladdr(reinterpret_cast<void *>(native), &info)) std::abort();
        std::fprintf(stderr, "JOIN_OBSERVER original_symbol=%s image=%s\n", info.dli_sname, info.dli_fname);
        return true;
    }();
    (void)identified;
    auto ownedIdentity = native_handle();
    native(this); // Original exception behavior propagates unchanged.
    if (joinable() || identity(native_handle()) != 0) std::abort();
    recordJoin(ownedIdentity, 0); // 0 denotes normal std::thread return here.
}
#endif
#if defined(__APPLE__)
#include <libproc.h>
#else
#include <dirent.h>
#endif

static void onError(const char *text) { std::fprintf(stderr, "%s\n", text); }
static int threads()
{
#if defined(__APPLE__)
    proc_taskinfo info {};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &info, sizeof(info)) != sizeof(info))
        std::abort();
    return info.pti_threadnum;
#else
    DIR *directory = opendir("/proc/self/task");
    int count = 0;
    if (!directory) std::abort();
    while (auto *entry = readdir(directory)) if (entry->d_name[0] != '.') ++count;
    closedir(directory);
    return count;
#endif
}
static int descriptors()
{
    int count = 0;
    for (int fd = 0; fd < 4096; ++fd) if (fcntl(fd, F_GETFD) != -1) ++count;
    return count;
}
static void requireResources(int, int, const char *);
static int controllerShutdown()
{
    setenv("SHELL", "/bin/sh", 1);
    setenv("ENV", "/dev/null", 1);
    THardwareInfo hardware; // Required upstream event-backend lifetime.
    TEventQueue::wakeUp(); // Warm the platform's event-wakeup resource.
    const int beforeThreads = threads(), beforeDescriptors = descriptors();
    tvterm::VTermEmulatorFactory factory;
    auto *controller = tvterm::TerminalController::create({40, 12}, factory, onError);
    if (!controller) return 1;
    controller->shutDown();
    requireResources(beforeThreads, beforeDescriptors,
                     "shutdown returned before worker/FD completion");
    std::puts("PASS: shutdown completes owned workers and FDs");
    return 0;
}

static void require(bool value, const char *reason)
{
    if (!value) throw std::runtime_error(reason);
}
// Apple userspace joins and native task disappearance have distinct boundaries.
// Require owned join/FD completion immediately, then one fixed native sample.
// This is test qualification only: no shipping wait, poll or retry.
static int nativeSamples = 0;
static void requireResources(int baseThreads, int baseDescriptors, const char *reason)
{
    int observedThreads = threads(), observedDescriptors = descriptors();
#if defined(__APPLE__)
    require(createdWorkers == joinedWorkers, "owned worker join pending before native sample");
    for (int ordinal = 0; ordinal < createdWorkers.load(); ++ordinal)
        require(workerJoinResults[ordinal] == 0, "owned worker lacks normal join and cleared handle");
    require(observedDescriptors == baseDescriptors, "FD baseline not restored before native sample");
    std::fprintf(stderr, "RESOURCE_OBSERVATION immediate_threads=%d baseline_threads=%d immediate_fds=%d completed_owned_joins=%d\n",
                 observedThreads, baseThreads, observedDescriptors, joinedWorkers.load());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ++nativeSamples;
    require(threads() == baseThreads && descriptors() == baseDescriptors, reason);
#else
    require(observedThreads == baseThreads && observedDescriptors == baseDescriptors, reason);
#endif
}
#if defined(__APPLE__)
static int resourceQualificationProbes()
{
    auto rejects = [](int baselineThreads, int baselineFDs, const char *expected, int samples) {
        int before = nativeSamples;
        bool rejected = false;
        try { requireResources(baselineThreads, baselineFDs, "fixed native sample remains nonbaseline"); }
        catch (const std::runtime_error &error) {
            rejected = std::string(error.what()) == expected;
        }
        require(rejected && nativeSamples - before == samples, "qualification failure probe missed exact stage");
    };
    int baselineThreads = threads(), baselineFDs = descriptors();
    std::atomic<bool> release{false};
    std::thread pending([&] { while (!release) std::this_thread::yield(); });
    try {
        rejects(baselineThreads, baselineFDs, "owned worker join pending before native sample", 0);
    } catch (...) { release = true; pending.join(); throw; }
    release = true; pending.join();
    // Allow the diagnosed kernel measurement boundary before another fixture.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int leaked = open("/dev/null", O_RDONLY);
    require(leaked >= 0, "FD probe fixture open");
    try { rejects(baselineThreads, baselineFDs, "FD baseline not restored before native sample", 0); }
    catch (...) { close(leaked); throw; }
    close(leaked);
    // Deliberately unowned thread: call original C symbol without owned observer.
    // A still-live native worker must fail the single final sample.
    struct NativeGate { std::atomic<bool> ready{false}, release{false}; } gate;
    using Create = int (*)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    using Join = int (*)(pthread_t, void **);
    auto originalCreate = reinterpret_cast<Create>(dlsym(RTLD_NEXT, "pthread_create"));
    auto originalJoin = reinterpret_cast<Join>(dlsym(RTLD_NEXT, "pthread_join"));
    require(originalCreate && originalJoin, "unowned probe native symbols");
    pthread_t unowned;
    require(originalCreate(&unowned, nullptr, [](void *p) -> void * {
        auto &g = *static_cast<NativeGate *>(p); g.ready = true;
        while (!g.release) std::this_thread::yield();
        return nullptr;
    }, &gate) == 0, "unowned native probe create");
    while (!gate.ready) std::this_thread::yield();
    try { rejects(baselineThreads, baselineFDs, "fixed native sample remains nonbaseline", 1); }
    catch (...) { gate.release = true; originalJoin(unowned, nullptr); throw; }
    gate.release = true;
    require(originalJoin(unowned, nullptr) == 0, "unowned native probe cleanup");
    std::puts("PASS: skipped join and FD leak reject before sample; unowned native thread fails final sample");
    return 0;
}
#endif
struct ControllerOwner
{
    tvterm::TerminalController *ptr;
    ~ControllerOwner() { if (ptr) ptr->shutDown(); }
};
static void send(tvterm::TerminalController &controller, const std::string &text)
{
    for (char c : text) {
        tvterm::TerminalEvent event {};
        event.type = tvterm::TerminalEventType::KeyDown;
        event.keyDown.keyCode = c == '\n' ? kbEnter : static_cast<unsigned char>(c);
        if (c != '\n') { event.keyDown.text[0] = c; event.keyDown.textLength = 1; }
        controller.sendEvent(event);
    }
}
static std::string surface(tvterm::TerminalController &controller)
{
    return controller.lockState([](auto &state) {
        std::string text;
        for (int y = 0; y < state.surface.size.y; ++y) {
            for (int x = 0; x < state.surface.size.x; ++x) {
                auto ch = state.surface.at(y, x).character.getText();
                text.append(ch.data(), ch.size());
            }
            text += '\n';
        }
        return text;
    });
}
static void awaitExit(tvterm::TerminalController &controller)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!controller.clientIsDisconnected() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    require(controller.clientIsDisconnected(), "natural controller exit timed out");
}
static void assertReaped(pid_t pid)
{
    errno = 0;
    require(waitpid(pid, nullptr, WNOHANG) == -1 && errno == ECHILD,
            "owned direct child still waitable after completion");
}
static int controllerCases(const std::string &which)
{
    THardwareInfo hardware;
    TEventQueue::wakeUp();
    setenv("SHELL", "/bin/sh", 1);
    setenv("ENV", "/dev/null", 1);
    setenv("PS1", "", 1);
    tvterm::VTermEmulatorFactory factory;
    int baseThreads = threads(), baseDescriptors = descriptors();
    if (which == "cycles") {
        for (int n = 0; n < 6; ++n) {
            ControllerOwner a {tvterm::TerminalController::create({40, 12}, factory, onError)};
            ControllerOwner b {tvterm::TerminalController::create({40, 12}, factory, onError)};
            require(a.ptr && b.ptr && a.ptr->childPid() != b.ptr->childPid(), "two distinct children missing");
            // Input is queued immediately, including before writer wait begins.
            send(*a.ptr, "printf 'FINAL_%s\\n' OUTPUT; exit 7\n");
            send(*b.ptr, "exit 0\n");
            awaitExit(*a.ptr); awaitExit(*b.ptr);
            int status = a.ptr->finish();
            require(WIFEXITED(status) && WEXITSTATUS(status) == 7, "exact exit 7 missing");
            require(a.ptr->finish() == status, "repeat finish changed captured status");
            b.ptr->finish();
            assertReaped(a.ptr->childPid()); assertReaped(b.ptr->childPid());
            require(surface(*a.ptr).find("FINAL_OUTPUT") != std::string::npos, "final emulator output lost");
            requireResources(baseThreads, baseDescriptors,
                    "owned workers or FDs remain after natural completion");
        }
    } else if (which == "held-slave") {
        ControllerOwner terminal {tvterm::TerminalController::create({40, 12}, factory, onError)};
        require(terminal.ptr, "create failed");
        // This bounded fixture deliberately ignores HUP, holding its inherited
        // slave for one second. It is outside the direct-child guarantee.
        auto start = std::chrono::steady_clock::now();
        send(*terminal.ptr, "(trap '' HUP; sleep 1) & exit 7\n");
        awaitExit(*terminal.ptr);
        int status = terminal.ptr->finish();
        require(WIFEXITED(status) && WEXITSTATUS(status) == 7, "held-slave child status lost");
        require(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(700),
                "direct-child completion depended on descendant EOF");
        assertReaped(terminal.ptr->childPid());
        // Let the bounded fixture end by itself; no descendant enumeration.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    } else if (which == "signal") {
        ControllerOwner terminal {tvterm::TerminalController::create({40, 12}, factory, onError)};
        require(terminal.ptr, "create failed");
        kill(terminal.ptr->childPid(), SIGKILL);
        awaitExit(*terminal.ptr);
        int status = terminal.ptr->finish();
        require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "exact signal status lost");
        assertReaped(terminal.ptr->childPid());
    } else if (which == "resize") {
        ControllerOwner terminal {tvterm::TerminalController::create({40, 12}, factory, onError)};
        require(terminal.ptr, "create failed");
        tvterm::TerminalEvent event {};
        event.type = tvterm::TerminalEventType::ViewportResize;
        event.viewportResize = {31, 9};
        terminal.ptr->sendEvent(event);
        send(*terminal.ptr, "printf 'SIZE_'; stty size; exit 0\n");
        awaitExit(*terminal.ptr); terminal.ptr->finish();
        require(surface(*terminal.ptr).find("SIZE_9 31") != std::string::npos, "queued viewport resize lost");
    } else if (which == "foreground") {
        char token[] = "/tmp/agentvision-hup-XXXXXX";
        int fd = mkstemp(token); require(fd >= 0, "fixture token failed");
        close(fd); unlink(token);
        ControllerOwner terminal {tvterm::TerminalController::create({100, 12}, factory, onError)};
        require(terminal.ptr, "create failed");
        std::string command = "/bin/sh -c 'trap \"printf HUP_OK > " + std::string(token) +
            "; exit 0\" HUP; printf \"JOB_%s\\n\" READY; while :; do sleep 1; done'\n";
        send(*terminal.ptr, command);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (surface(*terminal.ptr).find("JOB_READY") == std::string::npos && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        require(surface(*terminal.ptr).find("JOB_READY") != std::string::npos, "foreground fixture not ready");
        int status = terminal.ptr->finish();
        require(status >= 0, "live close lost direct child status");
        assertReaped(terminal.ptr->childPid());
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        std::string marker;
        do {
            std::ifstream input(token); input >> marker;
            if (marker == "HUP_OK") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        unlink(token);
        require(marker == "HUP_OK", "foreground job did not observe terminal hangup");
    }
    requireResources(baseThreads, baseDescriptors, "resource baseline not restored");
    std::puts("PASS: controller ownership, state and direct-child assertions");
    return 0;
}
static int blockedWrite()
{
    tvterm::EnvironmentVar env[] = {{"SHELL", "/bin/sh"}, {"ENV", "/dev/null"}};
    tvterm::PtyDescriptor descriptor;
    require(tvterm::createPty(descriptor, {40, 12}, env, onError), "create failed");
    tvterm::PtyMaster terminal(descriptor);
    // Canonical terminals may discard excess line input rather than block.
    // Put the real child into raw mode, then a foreground sleep that never reads.
    std::string setup = "stty raw -echo; printf 'BLOCK_%s\\n' READY; sleep 30\n";
    terminal.writeToClient({setup.data(), setup.size()});
    char buffer[4096]; size_t count; std::string ready;
    while (ready.find("BLOCK_READY") == std::string::npos && terminal.readFromClient(buffer, count) && count)
        ready.append(buffer, count);
    require(ready.find("BLOCK_READY") != std::string::npos, "blocked-write fixture not ready");
    std::string data(2 * 1024 * 1024, 'x');
    bool written = true;
    std::thread writer([&] { written = terminal.writeToClient({data.data(), data.size()}); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto start = std::chrono::steady_clock::now();
    terminal.requestStop(); writer.join(); terminal.disconnect();
    require(!written, "blocked write unexpectedly completed");
    require(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "blocked write shutdown not bounded");
    require(fcntl(descriptor.masterFd, F_GETFD) == -1 && errno == EBADF, "master FD remains open");
    assertReaped(descriptor.clientPid);
    std::puts("PASS: saturated nonblocking write cancels and releases direct child/FD");
    return 0;
}
int main(int argc, char **argv)
{
    try {
        if (argc == 2) {
            std::string which = argv[1];
#if defined(__APPLE__)
            if (which == "qualification-probes") return resourceQualificationProbes();
#endif
            if (which == "controller") return controllerShutdown();
            if (which == "blocked") return blockedWrite();
            if (which != "fallback") return controllerCases(which);
        }
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    tvterm::EnvironmentVar env[] = {{"SHELL", argc == 2 && std::string(argv[1]) == "fallback" ? "" : "/bin/sh"}, {"ENV", "/dev/null"}};
    tvterm::PtyDescriptor descriptor;
    if (!tvterm::createPty(descriptor, {40, 12}, env, onError)) return 1;
    tvterm::PtyMaster terminal(descriptor);
    const std::string command = "printf 'FINAL_OUTPUT\\n'; exit 7\n";
    terminal.writeToClient({command.data(), command.size()});
    char buffer[4096];
    size_t count;
    std::string output;
    while (terminal.readFromClient(buffer, count) && count)
        output.append(buffer, count);
    int status = 0;
    errno = 0;
    auto result = waitpid(descriptor.clientPid, &status, WNOHANG);
    bool reaped = result == -1 && errno == ECHILD;
    // Baseline cleanup never signals a PID already reaped by this test.
    if (!reaped) {
        if (result == 0) {
            kill(descriptor.clientPid, SIGKILL);
            while (waitpid(descriptor.clientPid, &status, 0) < 0 && errno == EINTR) {}
        }
        close(descriptor.masterFd);
        std::fprintf(stderr, "FAIL: direct child not reaped on natural terminal exit\n");
        return 1;
    }
    int capturedStatus = terminal.childWaitStatus();
    if (!WIFEXITED(capturedStatus) || WEXITSTATUS(capturedStatus) != 7 ||
        output.find("\r\nFINAL_OUTPUT\r\n") == std::string::npos) {
        std::fprintf(stderr, "FAIL: shell fallback, exact exit 7 or final output missing\n");
        return 1;
    }
    terminal.disconnect();
    std::puts("PASS: direct child reaped without window close");
}
