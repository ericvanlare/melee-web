#include "dat_native_joint.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <vector>
using Bytes = std::vector<uint8_t>;
static void check(bool c, const char* m) { if (!c) throw std::runtime_error(m); }
static void put32(Bytes& b, uint32_t o, uint32_t v) { for (unsigned i=0;i<4;++i) b.at(o+i)=uint8_t(v>>(24-8*i)); }
struct Fixture {
    Bytes data = Bytes(544); std::vector<uint32_t> relocations;
    void link(uint32_t s,uint32_t t) { put32(data,s,t); relocations.push_back(s); }
    void number(uint32_t o,float f) { put32(data,o,std::bit_cast<uint32_t>(f)); }
    Fixture() {
        // Two DObj occurrences share a material at valid relocated offset0 and
        // a PObj. A texture makes duplicate material hydration observable.
        put32(data,4,0x14); link(8,380); link(12,184);
        for (auto j : {24U,88U}) {
            put32(data,j+4,0x80); for(unsigned i=0;i<3;++i) number(j+32+4*i,1);
        }
        link(24+8,88); link(24+16,152);
        link(152+4,168); link(152+8,0); link(152+12,204);
        link(168+8,0); link(168+12,204);
        put32(data,184,0x01020304); put32(data,188,0x11223344); put32(data,192,0x55667788);
        number(196,1); number(200,32);
        link(204+8,228); put32(data,204+12,0x80000001); link(204+16,300);
        for(unsigned i=0;i<2;++i) {
            const unsigned a=228+i*24;
            put32(data,a,9+i); put32(data,a+4,2); put32(data,a+8,i?0:1); put32(data,a+12,4);
            data[a+19]=12; link(a+20,i?368:332);
        }
        put32(data,276,255);
        const uint8_t packet[]={0x90,0,3,0,0,1,0,2,0}; std::copy(std::begin(packet),std::end(packet),data.begin()+300);
        number(344,1); number(360,1); number(376,1);
        put32(data,380+12,4); for(unsigned i=0;i<3;++i)number(380+28+4*i,1);
        data[380+60]=data[380+61]=1; put32(data,380+64,0x40011); number(380+68,1);
        put32(data,380+72,1); link(380+76,472);
        link(472,512); put32(data,476,0x00080008); // I4, 8x8, one32-byte tile.
    }
    std::shared_ptr<const melee_web::DatArchive> archive() const {
        const uint32_t table=32+uint32_t(data.size()), pubs=table+uint32_t(relocations.size())*4;
        Bytes b(pubs+13); put32(b,0,uint32_t(b.size())); put32(b,4,uint32_t(data.size()));
        put32(b,8,uint32_t(relocations.size())); put32(b,12,1);
        std::copy(data.begin(),data.end(),b.begin()+32);
        for(unsigned i=0;i<relocations.size();++i)put32(b,table+4*i,relocations[i]);
        put32(b,pubs,24); b[pubs+8]='r'; b[pubs+9]='o'; b[pubs+10]='o'; b[pubs+11]='t';
        return std::make_shared<melee_web::DatArchive>(b);
    }
};
int main() {
    try {
        Fixture f; auto archive=f.archive();
        melee_web::DatNativeJoint native(archive,24); const auto& g=native.graph();
        check(g.joint_count==2 && g.dobj_count==2 && g.pobj_count==1 && g.material_count==1,"complete graph identities");
        check(g.joints[0].source_offset==24 && g.joints[0].child==1 && g.joints[1].next==UINT32_MAX,"child/sibling identity");
        check(g.materials[0].source_offset==0 && g.materials[0].material.texture_count==1,"shared relocated-zero material hydrated exactly once");
        check(g.dobjs[0].material==g.dobjs[1].material && g.dobjs[0].pobj==g.dobjs[1].pobj,"shared descriptors retain original identity");
        check(g.materials[0].textures[0].source_offset==380 && g.materials[0].textures[0].texture.image_bytes==32,"texture metadata and byte bound survive");
        archive.reset(); // Native input owner must retain all borrowed payloads.
        check(g.pobjs[0].geometry.display_byte_size==32 && static_cast<const uint8_t*>(g.pobjs[0].geometry.display)[0]==0x90,"archive lifetime retained");
        f.link(88+8,24);
        bool rejected=false;
        try { melee_web::DatNativeJoint cycle(f.archive(),24); } catch(const melee_web::DatError&) {rejected=true;}
        check(rejected,"cyclic graph rejected before native loader");
        Fixture unsupported; put32(unsupported.data,24+4,0x1080);
        rejected=false;
        try { melee_web::DatNativeJoint instance(unsupported.archive(),24); } catch(const melee_web::DatError&) {rejected=true;}
        check(rejected,"instance graph rejected before native loader");
        std::cout<<"typed native graph identity/lifetime/rejection checks passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
