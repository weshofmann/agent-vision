#include "core_protocol.h"
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
using namespace agentvision;
static void check(bool ok, const std::string &why) { if (!ok) throw std::runtime_error(why); }
static std::vector<uint8_t> fixture(const std::string &name) {
    std::ifstream in(std::string(AVCP_FIXTURES)+"/"+name+".hex");
    check(bool(in), "fixture readable: "+name);
    std::vector<uint8_t> b; std::string word;
    while(in>>word) { check(word.size()%2==0,"hex pairs"); for(size_t i=0;i<word.size();i+=2) b.push_back(uint8_t(std::stoul(word.substr(i,2),nullptr,16))); }
    return b;
}
static Frame want(uint16_t type, std::vector<uint8_t> payload, bool async=false, bool connection=false, uint16_t version=1) {
    Frame f; f.version=version; f.type=MessageType(type); f.request=async?0:UINT64_C(0x0102030405060708);
    f.session=connection?0:UINT64_C(0x8899aabbccddeeff); f.payload=std::move(payload); return f;
}
int main() { try {
    // Wrong endian/schema/IDs or encoder fields must fail literal comparisons; no roundtrip-derived expectations.
    std::vector<std::pair<std::string,Frame>> cases{
        {"hello",want(1,{0,1,0,1},false,true,0)},
        {"hello_ack",want(2,{0,1,0,1,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},false,true,0)},
        {"create",want(3,{0,24,0,80,0,4,0,0},false,true)}, {"created",want(4,{0,24,0,80,0,4,0,0})},
        {"input",want(5,{0,255,27})}, {"resize",want(6,{16,0,0,1})}, {"close",want(7,{})},
        {"shutdown",want(8,{},false,true)}, {"output",want(9,{1,2,3,4,5,6,7,8,0,255,27},true)},
        {"closed",want(11,{1,2,3,4,5,6,7,8})}, {"closed_async",want(11,std::vector<uint8_t>(8),true)},
        {"ack",want(13,{0,5})}, {"ack_resize",want(13,{0,6})}, {"ack_credit",want(13,{0,14})},
        {"ack_shutdown",want(13,{0,8},false,true)}, {"credit",want(14,{0,0,128,0})},
        {"exited_signal",want(10,{2,0,0,0,9,1,0,0,0,0,0,0,0,0,1},true)},
        {"exited_signal_no_core",want(10,{2,0,0,0,15,0,0,0,0,0,0,0,0,0,7},true)},
        {"exited_unavailable",want(10,{3,0,0,0,0,0,0,0,0,0,0,0,0,0,1},true)},
        {"handshake_error",want(12,{0,1,0,0,0,0,0,7,'v','e','r','s','i','o','n'},false,true,0)},
        {"error_async",want(12,{0,11,0,0,0,0,0,0},true,true)},
        {"error_utf8",want(12,{0,11,0,0,0,0,0,2,0xc3,0xa9})},
        {"input_max",want(5,std::vector<uint8_t>(32768,255))},
    };
    std::vector<uint8_t> output(32776); output[7]=1; cases.push_back({"output_max",want(9,output,true)});
    std::vector<uint8_t> error{0,11,0,0,128,0,4,0}; error.insert(error.end(),1024,'x'); cases.push_back({"error_max",want(12,error)});
    for(int i=1;i<=11;++i) { char n[20]; snprintf(n,sizeof(n),"error_%02d",i); cases.push_back({n,want(12,{0,uint8_t(i),0,0,0,3,0,5,'e','r','r','o','r'})}); }
    for(int i=1;i<=7;++i) cases.push_back({"exited_drain_"+std::to_string(i),want(10,{1,0,0,0,255,0,1,2,3,4,5,6,7,8,uint8_t(i)},true)});
    std::set<std::string> used;
    for(auto &c:cases) {
        auto raw=fixture(c.first); auto d=decodeFrame(raw); check(d.success,c.first+" decode");
        check(d.frame.version==c.second.version && d.frame.type==c.second.type && d.frame.request==c.second.request && d.frame.session==c.second.session && d.frame.payload==c.second.payload,c.first+" fields");
        check(encodeFrame(c.second)==raw,c.first+" encoded bytes");
        bool front=c.second.type==MessageType::Hello || c.second.type==MessageType::CreateSession || c.second.type==MessageType::InputBytes || c.second.type==MessageType::ResizeSession || c.second.type==MessageType::CloseSession || c.second.type==MessageType::Shutdown || c.second.type==MessageType::OutputCredit;
        check(validateDirection(d.frame,front?Direction::FrontendToBackend:Direction::BackendToFrontend)==DecodeError::None,c.first+" direction");
        check(validateDirection(d.frame,front?Direction::BackendToFrontend:Direction::FrontendToBackend)==DecodeError::Direction,c.first+" reverse");
        used.insert(c.first+".hex");
        for(size_t i=0;i<raw.size();++i) { if(raw.size()>1100 && i>40 && i+1<raw.size()) continue; auto truncated=std::vector<uint8_t>(raw.begin(),raw.begin()+i); check(!decodeFrame(truncated).success,c.first+" truncation"); }
    }
    check(!decodeFrame(fixture("frame_max")).success,"schema maximum"); used.insert("frame_max.hex");
    DIR *dir=opendir(AVCP_FIXTURES); check(dir,"fixture directory");
    while(auto e=readdir(dir)) { std::string n=e->d_name; if(n.size()>4 && n.substr(n.size()-4)==".hex") check(used.count(n),"uncovered literal "+n); } closedir(dir);
    struct Mutation { const char *name; size_t offset; uint8_t value; };
    for(auto m:std::vector<Mutation>{{"input",0,0},{"input",5,2},{"input",7,15},{"input",9,1},{"input",11,1},{"create",32,255},{"create",39,1},{"resize",35,0},{"hello",35,0},{"hello_ack",33,2},{"hello_ack",37,1},{"hello_ack",41,1},{"exited_signal",32,4},{"exited_signal",37,2},{"exited_signal",36,0},{"exited_signal",46,8},{"exited_unavailable",36,1},{"exited_drain_1",35,1},{"exited_drain_1",37,1},{"error_01",33,12},{"error_01",40,255},{"error_01",40,0},{"error_01",39,4},{"ack",33,7}}) { auto b=fixture(m.name); b[m.offset]=m.value; check(!decodeFrame(b).success,std::string(m.name)+" mutation"); }
    for(auto name:{"hello","hello_ack","create","created","input","resize","close","shutdown","credit","ack"}) { auto b=fixture(name); std::fill(b.begin()+16,b.begin()+24,0); check(!decodeFrame(b).success,"zero request"); }
    for(auto name:{"created","input","resize","close","output","exited_signal","closed","credit"}) { auto b=fixture(name); std::fill(b.begin()+24,b.begin()+32,0); check(!decodeFrame(b).success,"zero session"); }
    for(auto name:{"hello","hello_ack","create","created","resize","close","shutdown","closed","exited_signal","error_01","ack","credit"}) { auto b=fixture(name); ++b[15]; b.push_back(0); check(!decodeFrame(b).success,"schema trailing"); }
    auto b=fixture("input"); b.resize(32); b[13]=1; b[14]=0; b[15]=1; check(decodeFrame(b).error==DecodeError::Oversize,"reject oversize before missing body");
    b=fixture("output"); std::fill(b.begin()+32,b.begin()+40,0); check(!decodeFrame(b).success,"zero sequence");
    for(auto amount:std::vector<uint8_t>{0,255}) { b=fixture("credit"); std::fill(b.begin()+32,b.end(),amount); check(decodeFrame(b).success,"credit accounting deferred"); }
    b=fixture("input"); auto owned=decodeFrame(b); b[32]=99; check(owned.frame.payload[0]==0,"owned payload");
    auto invalid=want(5,std::vector<uint8_t>(32769)); check(encodeFrame(invalid).empty(),"encoder schema cap");
    RequestIDs ids; check(ids.accept(5) && !ids.accept(0) && !ids.accept(4) && !ids.accept(5) && ids.accept(6) && ids.accept(UINT64_MAX) && !ids.accept(UINT64_MAX),"watermark");
    std::cout<<"all "<<used.size()<<" literal frames, fields, encoding, rejection and ownership checks passed\n";
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; } }
