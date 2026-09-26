// Real libvterm with a deterministic owned transport; no socket/emulator clone.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <tvterm/termemu.h>
#define private public
#include <tvterm/termctrl.h>
#undef private
#include AGENTVISION_TERMCTRL_SOURCE
#include <tvterm/vtermemu.h>
#define Uses_THardwareInfo
#include <tvision/tv.h>
using namespace tvterm;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
struct Recorded {std::vector<char> bytes;InputOrigin origin;};
struct State {
    std::mutex mutex; std::condition_variable changed;
    std::queue<TransportChunk> chunks;std::vector<Recorded> output;
    bool cancelled=false,overflow=false,flushed=false,deadEmulator=false;
    size_t consumed=0;TPoint resized{};std::atomic<TerminalController *> controller{nullptr};
};
class Fixture final:public SessionTransport {
    std::shared_ptr<State> s;
public:
    Fixture(std::shared_ptr<State> p):s(p){}
    EnqueueResult enqueueInput(TSpan<const char> b,InputOrigin o) noexcept override {
        std::lock_guard<std::mutex> g(s->mutex);s->output.push_back({std::vector<char>(b.begin(),b.end()),o});
        return s->overflow?EnqueueResult::Overflow:EnqueueResult::Queued;
    }
    TransportChunk readChunk() noexcept override {
        std::unique_lock<std::mutex> g(s->mutex);s->changed.wait(g,[&]{return s->cancelled||!s->chunks.empty();});
        if(s->cancelled)return {};auto c=std::move(s->chunks.front());s->chunks.pop();return c;
    }
    void consumed(size_t n) noexcept override {std::lock_guard<std::mutex> g(s->mutex);s->consumed+=n;}
    void flushed(uint64_t seq) noexcept override {
        while (!s->controller.load()) std::this_thread::yield();
        bool final=s->controller.load()->lockState([](auto &st){return st.surface.size==TPoint{40,10};});
        std::lock_guard<std::mutex> g(s->mutex);s->flushed=seq==9&&final&&s->consumed==5;s->changed.notify_all();
    }
    ~Fixture() { if (s->deadEmulator) s->changed.notify_all(); }
    void resize(TPoint p) noexcept override {s->resized=p;}
    void cancelLocal() noexcept override {std::lock_guard<std::mutex> g(s->mutex);s->cancelled=true;s->changed.notify_all();}
};
static TerminalController *inert(VTermEmulatorFactory &f,std::shared_ptr<State> s){
    auto *c=new TerminalController({40,10},f,std::unique_ptr<SessionTransport>(new Fixture(s)));s->controller=c;return c;
}
static void feed(TerminalController *c,const char *bytes,size_t n){
    TerminalEvent e;e.type=TerminalEventType::ClientDataRead;e.clientDataRead={bytes,n};
    auto scope=c->eventLoop.clientDataWriter.replyScope(); c->terminalEmulator.handleEvent(e);
}
static void writerIteration(TerminalController *c){
    bool updated=false;c->eventLoop.processEvents();c->eventLoop.currentTimeout={};c->eventLoop.updateState(updated);
    auto pending=std::move(c->eventLoop.clientDataWriter.segments);c->eventLoop.writePendingData(pending,updated);
}
class LifetimeEmulator final:public TerminalEmulator {
    TerminalEmulator &inner;Writer &writer;std::shared_ptr<State> state;
public:
    LifetimeEmulator(TerminalEmulator &i,Writer &w,std::shared_ptr<State> s):inner(i),writer(w),state(s){}
    ~LifetimeEmulator(){delete &inner;writer.write({"d",1});state->deadEmulator=true;}
    void handleEvent(const TerminalEvent &e) noexcept override {inner.handleEvent(e);}
    void updateState(TerminalState &s) noexcept override {inner.updateState(s);}
};
class LifetimeFactory final:public TerminalEmulatorFactory {
    VTermEmulatorFactory inner;std::shared_ptr<State> state;
public:
    LifetimeFactory(std::shared_ptr<State> s):state(s){}
    TerminalEmulator &create(TPoint size,Writer &writer) noexcept override {return *new LifetimeEmulator(inner.create(size,writer),writer,state);}
    TSpan<const EnvironmentVar> getCustomEnvironment() noexcept override {return inner.getCustomEnvironment();}
};
static void run(){
    VTermEmulatorFactory f;
    {auto s=std::make_shared<State>();LifetimeFactory factory(s);auto *c=new TerminalController({40,10},factory,std::unique_ptr<SessionTransport>(new Fixture(s)));s->controller=c;
     c->finishPresentation();c->shutDown();check(s->deadEmulator,"emulator destroyed while Writer and transport alive");}
    {auto s=std::make_shared<State>();auto *c=inert(f,s);
     TerminalEvent e;e.type=TerminalEventType::ViewportResize;e.viewportResize={65,17};c->sendEvent(e);writerIteration(c);
     check(c->lockState([](auto &st){return st.surface.size==TPoint{65,17};}),"silent resize publishes immediately");
     check(s->resized==TPoint{65,17},"resize forwarded");c->shutDown();}
    {auto s=std::make_shared<State>();auto *c=inert(f,s);
     feed(c,"\033[5n\033[6n",8);TerminalEvent k;k.type=TerminalEventType::KeyDown;k.keyDown={};k.keyDown.text[0]='x';k.keyDown.textLength=1;c->sendEvent(k);writerIteration(c);
     check(s->output.size()==2,"reply segments retain emission order and user provenance");
     check(s->output[0].bytes==std::vector<char>({'\033','[','0','n','\033','[','1',';','1','R'})&&s->output[0].origin==InputOrigin::EmulatorReply,"literal real DSR/CPR reply bytes");
     check(s->output[1].bytes==std::vector<char>({'x'})&&s->output[1].origin==InputOrigin::User,"scope restores user input");
     c->disconnected=true;k.type=TerminalEventType::ScrollBackOffsetChange;k.scrollBackOffsetChange={0};c->sendEvent(k);writerIteration(c);
     check(c->eventLoop.eventQueue.empty(),"local event consumed after disconnect");
     size_t before=s->output.size();k.type=TerminalEventType::KeyDown;k.keyDown={};k.keyDown.text[0]='q';k.keyDown.textLength=1;c->sendEvent(k);writerIteration(c);
     check(s->output.size()==before,"process input suppressed after disconnect");c->shutDown();}
    {auto s=std::make_shared<State>();s->overflow=true;auto *c=inert(f,s);feed(c,"\033[5n",4);writerIteration(c);
     check(c->clientIsDisconnected(),"reply overflow explicit failure");
     check(c->lockState([](auto &st){return st.surface.size==TPoint{40,10};}),"overflow does not block publication");c->shutDown();}
    {auto s=std::make_shared<State>();TransportChunk d;d.kind=TransportChunk::Kind::Data;d.bytes={'f','i','n','a','l'};d.sequence=9;s->chunks.push(std::move(d));
     TransportChunk end;end.kind=TransportChunk::Kind::End;end.sequence=9;s->chunks.push(std::move(end));
     auto *c=TerminalController::createWithTransport({40,10},f,std::unique_ptr<SessionTransport>(new Fixture(s)));s->controller=c;
     // Fixture publication may race controller assignment; hold no source/state locks while waiting.
     {std::unique_lock<std::mutex> g(s->mutex);check(s->changed.wait_for(g,std::chrono::seconds(2),[&]{return s->flushed;}),"final consume/publication precedes flushed");}
     c->finishPresentation();c->finishPresentation();
     TerminalEvent e;e.type=TerminalEventType::ViewportResize;e.viewportResize={55,12};c->sendEvent(e);c->stateHasBeenUpdated();
     check(c->lockState([](auto &st){return st.surface.size==TPoint{55,12};}),"local resize after workers join");c->shutDown();}
}
int main(){THardwareInfo hardware;try{run();std::puts("PASS: owned transport, literal DSR/CPR, silent resize, exit flush, retained interaction, overflow");}catch(const std::exception &e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
