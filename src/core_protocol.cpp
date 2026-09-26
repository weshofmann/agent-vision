#include "core_protocol.h"
#include <algorithm>
namespace agentvision {
namespace {
uint64_t be(const uint8_t *p, size_t n) noexcept {
    uint64_t value=0; while(n--) value=(value<<8)|*p++; return value;
}
void put(std::vector<uint8_t> &b, uint64_t v, size_t n) {
    for(size_t i=n;i>0;--i) b.push_back(uint8_t(v>>((i-1)*8)));
}
bool utf8(const uint8_t *p, size_t n) noexcept {
    for(size_t i=0;i<n;) {
        uint32_t c=p[i++]; if(c==0) return false; if(c<128) continue;
        size_t more; uint32_t minimum;
        if(c>=0xc2 && c<=0xdf) { more=1; minimum=0x80; c&=0x1f; }
        else if(c>=0xe0 && c<=0xef) { more=2; minimum=0x800; c&=0x0f; }
        else if(c>=0xf0 && c<=0xf4) { more=3; minimum=0x10000; c&=7; }
        else return false;
        if(more>n-i) return false;
        while(more--) { uint8_t next=p[i++]; if((next&0xc0)!=0x80) return false; c=(c<<6)|(next&0x3f); }
        if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return false;
    }
    return true;
}
DecodeError header(uint16_t version, MessageType type) noexcept {
    auto t=uint16_t(type); if(t<1 || t>14) return DecodeError::Type;
    bool handshake=t==1 || t==2;
    if(handshake?version!=0:t==12?(version!=0 && version!=1):version!=1) return DecodeError::Version;
    return DecodeError::None;
}
DecodeError schema(const Frame &f) noexcept {
    auto h=header(f.version,f.type); if(h!=DecodeError::None) return h;
    size_t n=f.payload.size(); if(n>65536) return DecodeError::Oversize;
    unsigned t=unsigned(f.type); auto p=f.payload.data();
    if((t==9 || t==10)?f.request!=0:(t!=11 && t!=12 && f.request==0)) return DecodeError::IDs;
    if(t==1 || t==2 || t==3 || t==8) { if(f.session!=0) return DecodeError::IDs; }
    else if(t!=12 && t!=13 && f.session==0) return DecodeError::IDs;
    auto dimensions=[p]() { return be(p,2)>0 && be(p,2)<=4096 && be(p+2,2)>0 && be(p+2,2)<=4096; };
    bool valid=false;
    switch(t) {
    case 1: valid=n==4 && be(p,2)<=be(p+2,2); break;
    case 2: valid=n==26 && be(p,2)==1 && be(p+2,4)==65536 && be(p+6,4)==0; break;
    case 3: case 4: valid=n==8 && dimensions() && be(p+4,4)==262144; break;
    case 5: valid=n>=1 && n<=32768; break;
    case 6: valid=n==4 && dimensions(); break;
    case 7: case 8: valid=n==0; break;
    case 9: valid=n>=9 && n<=32776 && be(p,8)!=0; break;
    case 10:
        if(n==15 && p[5]<=1 && p[14]>=1 && p[14]<=7) {
            auto value=be(p+1,4);
            valid=p[0]==1?(value<=255 && p[5]==0):p[0]==2?value!=0:p[0]==3?(value==0 && p[5]==0):false;
        }
        break;
    case 11: valid=n==8; break;
    case 12: valid=n>=8 && n<=1032 && be(p,2)>=1 && be(p,2)<=11 && be(p+2,4)<=32768 && be(p+6,2)==n-8 && utf8(p+8,n-8); break;
    case 13:
        if(n==2) { auto completed=be(p,2); valid=completed==8?f.session==0:(completed==5 || completed==6 || completed==14) && f.session!=0; }
        break;
    case 14: valid=n==4; break; // Amount validity belongs to session accounting.
    }
    return valid?DecodeError::None:DecodeError::Schema;
}
}
DecodeResult decodeFrame(const std::vector<uint8_t> &bytes) {
    DecodeResult r;
    auto fail=[&r](DecodeError e) { r.error=e; return r; };
    if(bytes.size()<32) return fail(DecodeError::Truncated);
    auto p=bytes.data();
    if(p[0]!='A' || p[1]!='V' || p[2]!='C' || p[3]!='P') return fail(DecodeError::Magic);
    r.frame.version=uint16_t(be(p+4,2)); r.frame.type=MessageType(be(p+6,2));
    auto e=header(r.frame.version,r.frame.type); if(e!=DecodeError::None) return fail(e);
    if(be(p+8,2)!=0 || be(p+10,2)!=0) return fail(DecodeError::Flags);
    auto length=be(p+12,4); if(length>65536) return fail(DecodeError::Oversize);
    if(bytes.size()!=32+length) return fail(bytes.size()<32+length?DecodeError::Truncated:DecodeError::Length);
    r.frame.request=be(p+16,8); r.frame.session=be(p+24,8);
    r.frame.payload.assign(bytes.begin()+32,bytes.end());
    e=schema(r.frame); if(e!=DecodeError::None) return fail(e);
    r.success=true; return r;
}
std::vector<uint8_t> encodeFrame(const Frame &f) {
    if(schema(f)!=DecodeError::None) return {};
    std::vector<uint8_t> b; b.reserve(32+f.payload.size());
    b.insert(b.end(),{'A','V','C','P'}); put(b,f.version,2); put(b,uint16_t(f.type),2); put(b,0,4);
    put(b,f.payload.size(),4); put(b,f.request,8); put(b,f.session,8); b.insert(b.end(),f.payload.begin(),f.payload.end()); return b;
}
DecodeError validateDirection(const Frame &f, Direction direction) {
    auto e=schema(f); if(e!=DecodeError::None) return e;
    auto t=unsigned(f.type); bool front=t==1 || t==3 || t==5 || t==6 || t==7 || t==8 || t==14;
    return ((direction==Direction::FrontendToBackend && front) || (direction==Direction::BackendToFrontend && !front))?DecodeError::None:DecodeError::Direction;
}
bool RequestIDs::accept(RequestId id) noexcept { if(id==0 || id<=last_) return false; last_=id; return true; }
} // namespace agentvision
