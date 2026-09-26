#include "ipc_session.h"
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
using namespace agentvision;
static void check(bool v,const char *s){if(!v)throw std::runtime_error(s);}
static ConnectionEvent event(std::shared_ptr<CoreConnection> c,ConnectionEvent::Kind kind){
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);ConnectionEvent e;
    while(std::chrono::steady_clock::now()<end){if(c->pollEvent(e)){check(e.kind!=ConnectionEvent::Kind::Lost,"contact loss");if(e.kind==kind)return e;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}throw std::runtime_error("event timeout");
}
static std::shared_ptr<CoreConnection> start(const char *mode){setenv("AV_CONNECTION_CASE",mode,1);auto r=CoreConnection::start(FAKE_CORE);check(bool(r.connection),"start");event(r.connection,ConnectionEvent::Kind::Ready);return r.connection;}
int main(){try{
    auto c=start("binding");c->createSession(24,80);auto first=event(c,ConnectionEvent::Kind::Created);auto a=c->endpoint(first.session);
    IpcSessionTransport old(c,a);auto bytes=old.readChunk();check(bytes.bytes==std::vector<char>({'A','\0',char(255)}),"owned decoder payload survives subsequent frame storage");old.consumed(bytes.bytes.size());
    check(old.requestClose()!=0,"bound Close admitted");event(c,ConnectionEvent::Kind::Closed);
    auto req=c->createSession(24,80);auto replacement=event(c,ConnectionEvent::Kind::Created);check(replacement.session==first.session,"synthetic opaque ID reused after retirement");
    IpcSessionTransport current(c,c->endpoint(replacement.session));
    check(old.enqueueInput({"x",1},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Closed,"stale input rejected");old.resize({80,24});check(old.requestClose()==0,"stale close rejected");
    auto payload=current.readChunk();check(payload.bytes==std::vector<char>({'A','\0',char(255)}),"replacement owns unchanged output");
    current.consumed(payload.bytes.size());
    check(current.enqueueInput({"x",1},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Queued,"current input admitted");current.resize({80,24});
    auto response=current.readChunk();check(response.bytes==std::vector<char>({'o','k'}),"peer confirms no stale request/resize/ticket allocation and ordered current input resize");current.consumed(response.bytes.size());
    auto ownclose=current.requestClose();check(ownclose>req,"current Close valid");event(c,ConnectionEvent::Kind::Closed);
    auto other=start("binding");IpcSessionTransport mismatch(other,a);check(mismatch.enqueueInput({"x",1},tvterm::InputOrigin::User)==tvterm::EnqueueResult::Closed&&mismatch.requestClose()==0,"mismatched connection rejected");check(mismatch.readChunk().kind==tvterm::TransportChunk::Kind::Lost,"mismatch has no reader authority");
    c->shutdown();check(c->join().graceful,"binding connection graceful");other->shutdown();check(other->join().graceful,"other connection graceful");
    auto f=start("flush-close");f->createSession(24,80);IpcSessionTransport final(f,f->endpoint(event(f,ConnectionEvent::Kind::Created).session));auto data=final.readChunk();final.consumed(data.bytes.size());auto end=final.readChunk();check(end.kind==tvterm::TransportChunk::Kind::End,"end distinct");check(final.metadata().state!=SessionState::Exited,"status deferred");final.flushed(end.sequence);check(final.metadata().state==SessionState::Exited,"actual flush publishes exit");check(final.requestClose()!=0,"retained Exited session can Close");event(f,ConnectionEvent::Kind::Closed);f->shutdown();check(f->join().graceful,"final graceful");
    std::cout<<"PASS: identity binding, stale operations, mismatch, owned output, final flush and retained Close\n";
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
