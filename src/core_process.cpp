#include "core_process.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef __APPLE__
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace agentvision {
struct CoreProcess::State {
    int fd=-1;
    std::atomic<bool> stop{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::thread owner;
    bool ready=false, localDone=false, finished=false;
    int pid=0;
    CoreCompletion completion;
#ifdef __APPLE__
    void run() noexcept {
        using Clock=std::chrono::steady_clock;
        { std::unique_lock<std::mutex> lock(mutex); changed.wait(lock,[this] { return ready; }); }
        if(pid==0) { std::lock_guard<std::mutex> lock(mutex); localDone=finished=true; changed.notify_all(); return; }
        bool term=false, killed=false, uncertain=false;
        Clock::time_point termAt{}, killedAt{};
        for(;;) {
            int status=0;
            pid_t result=waitpid(pid,&status,WNOHANG);
            if(result==pid || (result<0 && errno!=EINTR)) {
                CoreCompletion done; done.termSent=term; done.killSent=killed;
                if(result<0) { done.kind=CoreCompletion::Kind::OwnershipUncertain; done.systemError=errno; }
                else if(WIFEXITED(status)) { done.kind=CoreCompletion::Kind::Exited; done.value=WEXITSTATUS(status); }
                else if(WIFSIGNALED(status)) { done.kind=CoreCompletion::Kind::Signaled; done.value=WTERMSIG(status); }
                else continue;
                std::lock_guard<std::mutex> lock(mutex); completion=done; localDone=finished=true; changed.notify_all(); return;
            }
            if(result<0) continue; // EINTR advances no lifecycle state.
            auto now=Clock::now();
            // Observe/reap immediately before each signal; only this owner can reap.
            // ECHILD or other uncertain wait failures exit without ever signalling.
            if(stop.load(std::memory_order_acquire) && !term) {
                int error=kill(pid,SIGTERM)==0?0:errno;
                term=true; termAt=now;
                if(error && error!=ESRCH) { std::lock_guard<std::mutex> lock(mutex); completion.systemError=error; }
            } else if(term && !killed && now-termAt>=std::chrono::milliseconds(250)) {
                int error=kill(pid,SIGKILL)==0?0:errno;
                killed=true; killedAt=now;
                if(error && error!=ESRCH) { std::lock_guard<std::mutex> lock(mutex); completion.systemError=error; }
            } else if(killed && !uncertain && now-killedAt>=std::chrono::seconds(1)) {
                uncertain=true;
                std::lock_guard<std::mutex> lock(mutex);
                completion.kind=CoreCompletion::Kind::CleanupUncertain; completion.termSent=term; completion.killSent=killed;
                localDone=true; changed.notify_all();
                // Continue wait-only ownership after bounded local restoration.
            }
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock,std::chrono::milliseconds(5));
        }
    }
#endif
};
CoreProcess::CoreProcess(std::shared_ptr<State> state):state_(std::move(state)) {}
CoreProcess::~CoreProcess() {
    requestStop(); join();
#ifdef __APPLE__
    if(state_->fd>=0) close(state_->fd);
#endif
}
int CoreProcess::ipcFd() const noexcept { return state_->fd; }
void CoreProcess::requestStop() noexcept {
#ifdef __APPLE__
    if(!state_->stop.exchange(true,std::memory_order_acq_rel)) shutdown(state_->fd,SHUT_RDWR);
#endif
    state_->changed.notify_all();
}
CoreCompletion CoreProcess::join() noexcept {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->changed.wait(lock,[this] { return state_->localDone; });
    auto result=state_->completion; bool finished=state_->finished; lock.unlock();
    // Call join on one connection/destruction thread; requestStop is thread-safe.
    if(state_->owner.joinable()) { if(finished) state_->owner.join(); else state_->owner.detach(); }
    return result;
}
LaunchResult CoreProcess::launchSibling() {
#ifdef __APPLE__
    uint32_t size=0; _NSGetExecutablePath(nullptr,&size);
    if(size==0 || size>65536) return {nullptr,LaunchError::InvalidPath,0};
    std::string path(size,'\0'); if(_NSGetExecutablePath(&path[0],&size)!=0) return {nullptr,LaunchError::InvalidPath,0};
    char *real=realpath(path.c_str(),nullptr); if(!real) return {nullptr,LaunchError::InvalidPath,errno};
    std::string executable(real); free(real);
    auto slash=executable.find_last_of('/'); if(slash==std::string::npos) return {nullptr,LaunchError::InvalidPath,0};
    return launch(executable.substr(0,slash+1)+"agentvision-core");
#else
    return {nullptr,LaunchError::UnsupportedPlatform,0};
#endif
}
LaunchResult CoreProcess::launch(const std::string &path) {
#ifdef __APPLE__
    if(path.empty() || path[0]!='/' || path.find('\0')!=std::string::npos) return {nullptr,LaunchError::InvalidPath,0};
    char *real=realpath(path.c_str(),nullptr); if(!real) return {nullptr,LaunchError::InvalidPath,errno};
    std::string canonical(real); free(real);
    struct stat st{}; if(stat(canonical.c_str(),&st)!=0) return {nullptr,LaunchError::InvalidPath,errno};
    if(!S_ISREG(st.st_mode) || access(canonical.c_str(),X_OK)!=0) return {nullptr,LaunchError::NotExecutable,errno};
    int sockets[2]; if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)!=0) return {nullptr,LaunchError::Socket,errno};
    // Backend source can be FD3. Move it first so dup2 never preserves CLOEXEC.
    int source=-1;
    auto cleanup=[&] { if(source>=0) close(source); close(sockets[0]); close(sockets[1]); };
    int one=1;
    if(fcntl(sockets[0],F_SETFD,FD_CLOEXEC)<0 || fcntl(sockets[1],F_SETFD,FD_CLOEXEC)<0 || setsockopt(sockets[1],SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one))<0 || (source=fcntl(sockets[0],F_DUPFD_CLOEXEC,4))<0) { int error=errno; cleanup(); return {nullptr,LaunchError::Socket,error}; }
    posix_spawn_file_actions_t actions; int error=posix_spawn_file_actions_init(&actions);
    if(error) { cleanup(); return {nullptr,LaunchError::Spawn,error}; }
    posix_spawnattr_t attr; error=posix_spawnattr_init(&attr);
    if(error) { posix_spawn_file_actions_destroy(&actions); cleanup(); return {nullptr,LaunchError::Spawn,error}; }
    auto record=[&error](int e) { if(!error) error=e; };
    record(posix_spawnattr_setflags(&attr,POSIX_SPAWN_CLOEXEC_DEFAULT));
    for(int fd=0;fd<=2;++fd) if(fcntl(fd,F_GETFD)>=0) record(posix_spawn_file_actions_adddup2(&actions,fd,fd));
    record(posix_spawn_file_actions_addclose(&actions,sockets[1]));
    if(sockets[0]!=3) record(posix_spawn_file_actions_addclose(&actions,sockets[0]));
    record(posix_spawn_file_actions_adddup2(&actions,source,3));
    record(posix_spawn_file_actions_addclose(&actions,source));
    std::shared_ptr<State> state; std::unique_ptr<CoreProcess> process;
    LaunchError launchError=LaunchError::Spawn;
    if(!error) {
        try {
            state=std::make_shared<State>(); process.reset(new CoreProcess(state));
            // Allocate/start the owner before a child exists; thread failure cannot orphan one.
            state->owner=std::thread([state] { state->run(); });
        } catch(...) { error=EAGAIN; launchError=LaunchError::Owner; if(process) { state->localDone=state->finished=true; } }
    }
    pid_t child=0;
    char name[]="agentvision-core", fdArg[]="--ipc-fd=3", mode[]="--mode=frontend-spawned";
    char *argv[]={name,fdArg,mode,nullptr};
    if(!error) error=posix_spawn(&child,canonical.c_str(),&actions,&attr,argv,environ);
    posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attr);
    close(source); source=-1; close(sockets[0]);
    if(state && state->owner.joinable()) {
        { std::lock_guard<std::mutex> lock(state->mutex); state->pid=error?0:child; state->fd=error?-1:sockets[1]; state->ready=true; }
        state->changed.notify_all();
    }
    if(error) { close(sockets[1]); return {nullptr,launchError,error}; }
    return {std::move(process),LaunchError::None,0};
#else
    (void)path; return {nullptr,LaunchError::UnsupportedPlatform,0};
#endif
}
} // namespace agentvision
