#include "core_connection.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace agentvision;
using Clock=std::chrono::steady_clock;
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
static ConnectionEvent event(const std::shared_ptr<CoreConnection>&c,ConnectionEvent::Kind kind){
    auto end=Clock::now()+std::chrono::seconds(4); ConnectionEvent e;
    while(Clock::now()<end){if(c->pollEvent(e)){if(e.kind==kind)return e; check(e.kind!=ConnectionEvent::Kind::Lost,"unexpected loss");}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("event deadline");
}
static std::shared_ptr<CoreConnection> start(const char*mode){setenv("AV_CONNECTION_CASE",mode,1);auto r=CoreConnection::start(FAKE_CORE);check(bool(r.connection),"CoreConnection start owns launched core");event(r.connection,ConnectionEvent::Kind::Ready);return r.connection;}
int main(int argc,char**argv){try{
    if(argc==2 && std::string(argv[1])=="watchdog") {
        std::atomic<int> restored{0};auto then=Clock::now();std::atomic<int64_t> restorationMs{0};
        setenv("AV_CONNECTION_CASE","ack-close-stall",1);
        auto r=CoreConnection::start(FAKE_CORE,[&]{restorationMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-then).count();++restored;});
        check(bool(r.connection),"watchdog launched");event(r.connection,ConnectionEvent::Kind::Ready);then=Clock::now();r.connection->shutdown();auto outcome=r.connection->join();
        auto elapsed=Clock::now()-then;
        check(elapsed>=std::chrono::seconds(2) && elapsed<std::chrono::seconds(4),"Ack without core exit keeps same bounded Shutdown deadline");
        check(!outcome.graceful && outcome.contactError==ContactError::ShutdownTimeout,"watchdog is forced contact loss");
        check(outcome.core.kind==CoreCompletion::Kind::Signaled && outcome.core.termSent && outcome.core.killSent,"ignored TERM receives owned KILL/reap");
        check(restored==1 && restorationMs>=0 && restorationMs<2400,"local restoration before escalation completion");
        return 0;
    }
    if(argc==2 && std::string(argv[1])=="reserve") {
        auto c=start("reserve"); c->createSession(24,80); auto a=c->endpoint(event(c,ConnectionEvent::Kind::Created).session);
        auto data=a->readChunk(); a->consumed(data.bytes.size());
        auto id=a->metadata().id;
        check(c->submitInput(id,std::vector<uint8_t>(30720,'u'),InputOrigin::User)==EnqueueResult::Queued,"user first half");
        check(c->submitInput(id,std::vector<uint8_t>(30720,'u'),InputOrigin::User)==EnqueueResult::Queued,"user60KiB staging");
        check(c->submitInput(id,{'x'},InputOrigin::User)==EnqueueResult::Overflow,"user bytes cannot consume reply reserve");
        for(int i=0;i<45;++i)check(c->submitResize(id,30,100)==EnqueueResult::Queued,"47 user tickets");
        check(c->submitResize(id,30,100)==EnqueueResult::Overflow,"48th user rejected");
        check(c->submitInput(id,std::vector<uint8_t>(2048,'r'),InputOrigin::EmulatorReply)==EnqueueResult::Queued,"slot48 admits reply");
        check(c->submitInput(id,std::vector<uint8_t>(2048,'s'),InputOrigin::EmulatorReply)==EnqueueResult::Queued,"second reply segment stages while slot awaits Ack");
        check(c->submitInput(id,{'t'},InputOrigin::EmulatorReply)==EnqueueResult::Overflow,"reply reserve exhausted explicitly");
        // Wait for the prior user tickets to complete, then stage behind reply2.
        auto deadline=Clock::now()+std::chrono::seconds(2); EnqueueResult queued;
        do {queued=c->submitInput(id,{'z'},InputOrigin::User);if(queued==EnqueueResult::Queued)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(Clock::now()<deadline);
        check(queued==EnqueueResult::Queued,"user tickets reclaimed");check(c->submitResize(id,40,120)==EnqueueResult::Queued,"resize ordered behind replies and user");
        auto ordered=a->readChunk();check(ordered.bytes==std::vector<char>({'o','r','d','e','r','e','d'}),"accepted terminal emission order survives slot pressure");a->consumed(ordered.bytes.size());c->shutdown();check(c->join().graceful,"reserve cleanup");return 0;
    }
    // Missing launch/handshake/demux cannot pass: expectations are literal bytes,
    // and fake_core.py constructs framing independently of the C++ codec.
    auto c=start("streams"); check(c->createSession(24,80)!=0,"create accepted");
    auto a=c->endpoint(event(c,ConnectionEvent::Kind::Created).session);check(bool(a),"Created registers endpoint");
    check(c->createSession(24,80)!=0,"second create accepted");auto b=c->endpoint(event(c,ConnectionEvent::Kind::Created).session);
    auto ar=std::async(std::launch::async,[a]{return a->readChunk();});auto br=std::async(std::launch::async,[b]{return b->readChunk();});
    auto ac=ar.get(),bc=br.get();check(ac.kind==TransportChunk::Kind::Data && ac.bytes==std::vector<char>({'A',0,char(255)}) && ac.sequence==1,"A owns raw00ff output");
    check(bc.bytes==std::vector<char>({'B','x','y'}) && bc.sequence==1,"B reader cannot steal A");
    check(a->metadata().state==SessionState::Running,"wire Exited staged before consumption");
    a->consumed(ac.bytes.size());b->consumed(bc.bytes.size());
    auto end=a->readChunk();check(end.kind==TransportChunk::Kind::End && end.sequence==1,"End follows data with lastSequence");
    a->flushed(0);check(a->metadata().state==SessionState::Running,"wrong flush cannot expose status");
    a->flushed(1);check(a->metadata().state==SessionState::Exited && a->metadata().status.value==7,"actual final flush publishes exact status");
    event(c,ConnectionEvent::Kind::Exited);
    a->cancelLocal();check(a->readChunk().kind==TransportChunk::Kind::Lost,"detached read wakes locally");
    check(c->submitResize(b->metadata().id,30,100)==EnqueueResult::Queued,"A cancellation preserves B admissions");
    c->shutdown();check(c->join().graceful,"Shutdown Ack plus core exit graceful");
    std::cout<<"bounded endpoint ownership and staged exit passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
