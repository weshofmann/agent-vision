#pragma once
#include <memory>
#include <string>
namespace agentvision {
enum class LaunchError { None, UnsupportedPlatform, InvalidPath, NotExecutable, Socket, Spawn, Owner };
struct CoreCompletion {
    enum class Kind { Exited, Signaled, OwnershipUncertain, CleanupUncertain };
    Kind kind=Kind::CleanupUncertain; int value=0; int systemError=0;
    bool termSent=false; bool killSent=false;
};
class CoreProcess;
struct LaunchResult { std::unique_ptr<CoreProcess> process; LaunchError error=LaunchError::None; int systemError=0; };
// Owns only its direct core child. Never waitpid(-1), a session shell, or a process group.
class CoreProcess {
public:
    static LaunchResult launch(const std::string &absolutePath);
    static LaunchResult launchSibling();
    ~CoreProcess();
    CoreProcess(const CoreProcess &)=delete;
    CoreProcess &operator=(const CoreProcess &)=delete;
    // Borrowed endpoint remains stable until destruction. Join all I/O users first.
    int ipcFd() const noexcept;
    // Wakes IPC users with shutdown, never closes/reuses the descriptor. Idempotent.
    // Task8 owns the preceding Hello/Shutdown 2-second protocol deadline.
    void requestStop() noexcept;
    // Local completion is bounded after requestStop: TERM, 250ms, KILL, 1s observation.
    // Cleanup uncertainty retains the child in a background owner; never claims reap.
    // One caller serializes join/destruction; never call under UI/state locks.
    // Without requestStop, join observes natural completion and has no deadline.
    CoreCompletion join() noexcept;
private:
    struct State;
    explicit CoreProcess(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
};
} // namespace agentvision
