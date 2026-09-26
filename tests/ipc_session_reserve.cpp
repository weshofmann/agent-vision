// Real libvterm DSR/CPR -> tagged Writer -> adapter -> literal Python wire peer.
#include "ipc_session.h"
#include <tvterm/vtermemu.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#define Uses_THardwareInfo
#include <tvision/tv.h>
using namespace agentvision;
static void check(bool v,const char *s){if(!v)throw std::runtime_error(s);}
struct Gate {std::mutex mutex;std::condition_variable cv;bool ready=false,release=false;std::atomic<int> reads{0};};
class Observed final:public tvterm::TerminalEmulator {
    tvterm::TerminalEmulator &inner;Gate &gate;
public:
    Observed(tvterm::TerminalEmulator &i,Gate &g):inner(i),gate(g){}
    ~Observed(){delete &inner;}
    void handleEvent(const tvterm::TerminalEvent &e) noexcept override {
        inner.handleEvent(e);
        if(e.type==tvterm::TerminalEventType::ClientDataRead){
            ++gate.reads;
            if(e.clientDataRead.size==5&&std::string(e.clientDataRead.data,5)=="ready"){
                std::unique_lock<std::mutex> l(gate.mutex);gate.ready=true;gate.cv.notify_all();
                if(!gate.cv.wait_for(l,std::chrono::seconds(3),[&]{return gate.release;}))std::abort();
            }
        }
    }
    void updateState(tvterm::TerminalState &s) noexcept override {inner.updateState(s);}
};
class Factory final:public tvterm::TerminalEmulatorFactory {
    tvterm::VTermEmulatorFactory inner;Gate &gate;
public:
    Factory(Gate &g):gate(g){}
    tvterm::TerminalEmulator &create(TPoint p,tvterm::Writer &w) noexcept override {return *new Observed(inner.create(p,w),gate);}
    TSpan<const tvterm::EnvironmentVar> getCustomEnvironment() noexcept override {return inner.getCustomEnvironment();}
};
static ConnectionEvent event(std::shared_ptr<CoreConnection> c,ConnectionEvent::Kind kind){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);ConnectionEvent e;while(std::chrono::steady_clock::now()<end){if(c->pollEvent(e)){check(e.kind!=ConnectionEvent::Kind::Lost,"loss before saturation");if(e.kind==kind)return e;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("event deadline");}
int main(){THardwareInfo hardware;try{
    setenv("AV_CONNECTION_CASE","dsr-reserve",1);auto c=CoreConnection::start(FAKE_CORE).connection;check(bool(c),"launch");event(c,ConnectionEvent::Kind::Ready);c->createSession(24,80);auto ep=c->endpoint(event(c,ConnectionEvent::Kind::Created).session);
    auto owned=std::unique_ptr<IpcSessionTransport>(new IpcSessionTransport(c,ep));auto *transport=owned.get();std::vector<char> full(30720,'u');
    check(transport->enqueueInput({full.data(),full.size()},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Queued,"first 30KiB user");check(transport->enqueueInput({full.data(),full.size()},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Queued,"full 60KiB user staging");
    for(int i=0;i<45;++i)transport->resize({100,30});
    check(transport->enqueueInput({"x",1},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Overflow,"47 ordinary/user capacity reached");
    Gate gate;Factory factory(gate);auto *controller=tvterm::TerminalController::createWithTransport({80,24},factory,std::move(owned));
    {std::unique_lock<std::mutex> l(gate.mutex);check(gate.cv.wait_for(l,std::chrono::seconds(3),[&]{return gate.ready;}),"peer accepted exact DSR/CPR as slot48 behind saturated users");
     std::vector<char> reserve(4086,'r');check(transport->enqueueInput({reserve.data(),reserve.size()},tvterm::InputOrigin::EmulatorReply)==tvterm::EnqueueResult::Queued,"10-byte pending reply leaves 4086-byte reserve");gate.release=true;gate.cv.notify_all();}
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!controller->clientIsDisconnected()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(controller->clientIsDisconnected()&&gate.reads==3,"second DSR fails explicitly at full reply reserve");
    check(controller->lockState([](auto &s){return s.surface.size==TPoint{80,24};}),"render state accessible after reply overflow");
    controller->finishPresentation();controller->shutDown();c->shutdown();check(c->join().graceful,"saturated connection cleanup");std::puts("PASS: actual DSR/CPR slot48, 60KiB users, 4KiB reply reserve, overflow publication and joins");
}catch(const std::exception &e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
