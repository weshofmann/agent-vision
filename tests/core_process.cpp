#include "core_process.h"
#include <cerrno>
#include <atomic>
#include <dlfcn.h>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <string>
// Test-only libc interposition keeps production ownership APIs free of test hooks.
// Default behavior delegates to native Darwin syscalls; only observation is delayed.
namespace {
std::atomic<bool> deferObservation{false}, interruptObservation{false};
std::atomic<int> signalCalls{0}, wrongOwnerCalls{0};
using WaitFunction=pid_t (*)(pid_t,int *,int);
using KillFunction=int (*)(pid_t,int);
WaitFunction nativeWait() { static auto f=reinterpret_cast<WaitFunction>(dlsym(RTLD_NEXT,"waitpid")); if(!f) std::abort(); return f; }
KillFunction nativeKill() { static auto f=reinterpret_cast<KillFunction>(dlsym(RTLD_NEXT,"kill")); if(!f) std::abort(); return f; }
}
extern "C" pid_t waitpid(pid_t pid,int *status,int options) {
    if(deferObservation.load()) {
        if(pid<=0 || options!=WNOHANG) ++wrongOwnerCalls;
        if(interruptObservation.exchange(false)) { errno=EINTR; return -1; }
        return 0;
    }
    return nativeWait()(pid,status,options);
}
extern "C" int kill(pid_t pid,int signal) {
    ++signalCalls;
    if(deferObservation.load() && (pid<=0 || (signal!=SIGTERM && signal!=SIGKILL))) ++wrongOwnerCalls;
    return nativeKill()(pid,signal);
}
using namespace agentvision;
static void check(bool ok, const char *why) { if(!ok) throw std::runtime_error(why); }
static size_t fdCount() { size_t n=0; for(int fd=0;fd<1024;++fd) if(fcntl(fd,F_GETFD)>=0) ++n; return n; }
static void copyExecutable(const std::string &from,const std::string &to) {
    std::ifstream in(from,std::ios::binary); std::ofstream out(to,std::ios::binary); out<<in.rdbuf(); out.close();
    check(bool(in) && bool(out) && chmod(to.c_str(),0700)==0,"copy fixture executable");
}
static char readByte(int fd) { char b=0; check(read(fd,&b,1)==1,"child ready"); return b; }
int main(int argc,char **argv) { try {
    if(argc==2 && std::string(argv[1])=="sibling-check") {
        // Run this copied executable from an unrelated cwd with poisoned PATH.
        check(chdir("/")==0,"independent cwd"); setenv("PATH","/no-such-synthetic-directory",1);
        auto missing=CoreProcess::launchSibling(); check(!missing.process && missing.error==LaunchError::InvalidPath,"missing packaged sibling");
        std::string sibling=std::string(argv[0]).substr(0,std::string(argv[0]).find_last_of('/')+1)+"agentvision-core";
        copyExecutable(argv[0],sibling); check(chmod(sibling.c_str(),0600)==0,"nonexec sibling fixture");
        check(CoreProcess::launchSibling().error==LaunchError::NotExecutable,"nonexecutable packaged sibling");
        check(chmod(sibling.c_str(),0700)==0,"executable sibling fixture"); unsetenv("AV_TEST_MODE");
        auto found=CoreProcess::launchSibling(); check(bool(found.process),"packaged sibling independent of cwd/PATH");
        check(readByte(found.process->ipcFd())=='N',"packaged sibling IPC"); check(found.process->join().value==23,"packaged sibling reap");
        unlink(sibling.c_str()); return 0;
    }
    if(argc==3) {
        check(std::string(argv[1])=="--ipc-fd=3" && std::string(argv[2])=="--mode=frontend-spawned","exact child argv");
        check(fcntl(3,F_GETFD)>=0 && !(fcntl(3,F_GETFD)&FD_CLOEXEC),"FD3 survives exec");
        int type=0; socklen_t size=sizeof(type); check(getsockopt(3,SOL_SOCKET,SO_TYPE,&type,&size)==0 && type==SOCK_STREAM,"connected stream");
        for(int fd=4;fd<1024;++fd) check(fcntl(fd,F_GETFD)==-1 && errno==EBADF,"unintended exec descriptor");
        std::string mode=getenv("AV_TEST_MODE")?getenv("AV_TEST_MODE"):"natural";
        if(mode=="ignore" || mode=="stopped") signal(SIGTERM,SIG_IGN);
        char ready='N'; check(write(3,&ready,1)==1,"native ready");
        if(mode=="stopped") { raise(SIGSTOP); for(;;) pause(); }
        if(mode=="signal") { raise(SIGUSR1); return 92; }
        if(mode=="eof") { char b; while(read(3,&b,1)>0){} return 24; }
        if(mode=="ignore") { for(;;) pause(); }
        return 23;
    }
    check(!CoreProcess::launch("relative").process,"relative rejected");
    check(!CoreProcess::launch("/does-not-exist/agentvision-core").process,"missing rejected");
    check(!CoreProcess::launch("/").process,"directory rejected");

    // Native self exec catches FD3 CLOEXEC, inherited descriptor leaks, and exact argv.
    char resolved[4096]; check(realpath(argv[0],resolved),"self path");
    int saved3=fcntl(3,F_DUPFD_CLOEXEC,10); close(3);
    check(fcntl(3,F_GETFD)<0 && errno==EBADF,"backend source will be FD3");
    { auto source3=CoreProcess::launch(resolved); check(bool(source3.process),"FD3 source launch"); check(readByte(source3.process->ipcFd())=='N',"source FD3 survives exec"); check(source3.process->join().value==23,"source FD3 reap"); }
    if(saved3>=0) { check(dup2(saved3,3)==3,"restore original FD3"); close(saved3); }
    size_t baselineFDs=fdCount();
    int leaked=open("/dev/null",O_RDONLY); check(leaked>=0,"synthetic leak fd");
    for(auto mode:{"natural","signal","eof","stopped","ignore"}) {
        setenv("AV_TEST_MODE",mode,1); signalCalls=0;
        auto r=CoreProcess::launch(resolved); check(bool(r.process),"launch native");
        check(fcntl(r.process->ipcFd(),F_GETFD)&FD_CLOEXEC,"parent CLOEXEC");
        int noSignal=0; socklen_t n=sizeof(noSignal); check(getsockopt(r.process->ipcFd(),SOL_SOCKET,SO_NOSIGPIPE,&noSignal,&n)==0 && noSignal==1,"SO_NOSIGPIPE");
        check(readByte(r.process->ipcFd())=='N',"native inventory qualified");
        bool forced=std::string(mode)=="stopped" || std::string(mode)=="ignore";
        auto start=std::chrono::steady_clock::now();
        if(forced) r.process->requestStop();
        if(std::string(mode)=="eof") check(shutdown(r.process->ipcFd(),SHUT_WR)==0,"peer EOF");
        auto status=r.process->join();
        check(status.kind==(std::string(mode)=="signal" || forced?CoreCompletion::Kind::Signaled:CoreCompletion::Kind::Exited),"exact status kind");
        if(status.value!=(forced?SIGKILL:std::string(mode)=="signal"?SIGUSR1:std::string(mode)=="eof"?24:23)) std::cerr<<"status mode "<<mode<<" value "<<status.value<<"\n";
        check(status.value==(forced?SIGKILL:std::string(mode)=="signal"?SIGUSR1:std::string(mode)=="eof"?24:23),"exact status value");
        check(status.termSent==forced && status.killSent==forced && signalCalls==(forced?2:0),"only watchdog signals");
        check(std::chrono::steady_clock::now()-start<std::chrono::seconds(2),"bounded stop/join");
        r.process->requestStop(); auto repeated=r.process->join(); check(repeated.termSent==status.termSent && repeated.killSent==status.killSent && signalCalls==(forced?2:0),"no post reap signal");
    }
    // Delay wait observation, not actual kernel death: bounded join must report
    // uncertainty and preserve the sole reaper until it can observe the status.
    setenv("AV_TEST_MODE","stopped",1); signalCalls=0; wrongOwnerCalls=0;
    deferObservation=true; interruptObservation=true;
    auto delayed=CoreProcess::launch(resolved); check(bool(delayed.process),"deferred native launch"); readByte(delayed.process->ipcFd());
    auto watchdogStart=std::chrono::steady_clock::now(); delayed.process->requestStop(); auto pending=delayed.process->join();
    auto watchdogElapsed=std::chrono::steady_clock::now()-watchdogStart;
    check(pending.kind==CoreCompletion::Kind::CleanupUncertain && pending.termSent && pending.killSent,"watchdog cannot invent reap");
    check(watchdogElapsed>=std::chrono::milliseconds(1250) && watchdogElapsed<std::chrono::seconds(2),"bounded post KILL observation");
    check(signalCalls==2 && wrongOwnerCalls==0,"one TERM/KILL and only owned PID WNOHANG");
    delayed.process->requestStop(); check(delayed.process->join().kind==CoreCompletion::Kind::CleanupUncertain && signalCalls==2,"uncertain reaper never signals again");
    deferObservation=false;
    auto observationDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(1); CoreCompletion eventual;
    do { eventual=delayed.process->join(); if(eventual.kind!=CoreCompletion::Kind::CleanupUncertain) break; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while(std::chrono::steady_clock::now()<observationDeadline);
    check(eventual.kind==CoreCompletion::Kind::Signaled && eventual.value==SIGKILL,"retained reaper eventually observes native status");
    delayed.process->requestStop(); check(signalCalls==2,"no signal after retained reap"); delayed.process.reset();
    close(leaked); unsetenv("AV_TEST_MODE");
    auto python=CoreProcess::launch(FAKE_CORE); check(bool(python.process),"python fixture"); check(readByte(python.process->ipcFd())=='P',"python exact args IPC"); check(python.process->join().value==23,"python reap");
    // A failing executable loader is a spawn failure, not a fabricated child status.
    std::string bad=std::string(FAKE_CORE)+".invalid"; int fd=open(bad.c_str(),O_CREAT|O_WRONLY|O_TRUNC,0700); check(fd>=0,"bad exec fixture"); check(write(fd,"invalid\n",8)==8,"bad exec write"); close(fd);
    size_t beforeFailure=fdCount();
    auto failed=CoreProcess::launch(bad); check(!failed.process && failed.error==LaunchError::Spawn,"exec loader failure"); chmod(bad.c_str(),0600); check(CoreProcess::launch(bad).error==LaunchError::NotExecutable,"nonexecutable rejected"); unlink(bad.c_str()); check(fdCount()==beforeFailure,"spawn failure closes all endpoints");
    // Kernel auto-reap supplies ECHILD without a competing terminal-shell reaper.
    struct sigaction old{}, ignored{}; ignored.sa_handler=SIG_IGN; sigemptyset(&ignored.sa_mask); sigaction(SIGCHLD,&ignored,&old);
    auto lost=CoreProcess::launch(resolved); check(bool(lost.process),"auto reap launch"); readByte(lost.process->ipcFd()); auto uncertain=lost.process->join(); sigaction(SIGCHLD,&old,nullptr);
    check(uncertain.kind==CoreCompletion::Kind::OwnershipUncertain && !uncertain.termSent && !uncertain.killSent,"ECHILD no invented status or signal"); lost.process->requestStop(); check(!lost.process->join().termSent,"uncertain ownership never signalled");
    lost.process.reset(); python.process.reset();
    check(fdCount()==baselineFDs,"all native owner descriptors reclaimed");
    std::string fixtureDir=std::string(FAKE_CORE)+".sibling-XXXXXX";
    std::vector<char> temp(fixtureDir.begin(),fixtureDir.end()); temp.push_back(0);
    check(mkdtemp(temp.data()),"isolated sibling directory");
    std::string copied=std::string(temp.data())+"/frontend"; copyExecutable(resolved,copied);
    pid_t siblingTester=fork(); check(siblingTester>=0,"sibling harness fork");
    if(siblingTester==0) { execl(copied.c_str(),copied.c_str(),"sibling-check",nullptr); _exit(95); }
    int siblingStatus=0; check(waitpid(siblingTester,&siblingStatus,0)==siblingTester && WIFEXITED(siblingStatus) && WEXITSTATUS(siblingStatus)==0,"isolated sibling qualification");
    unlink(copied.c_str()); rmdir(temp.data());
    std::cout<<"native exec descriptors, spawn errors, exact status, stop escalation, ECHILD and no post-reap signals passed\n";
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
