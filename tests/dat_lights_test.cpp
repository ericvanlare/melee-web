#include "dat_lights.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace melee_web;
using Bytes=std::vector<uint8_t>;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void put(Bytes& b,uint32_t p,uint32_t v){for(int i=0;i<4;i++)b.at(p+i)=uint8_t(v>>(24-8*i));}
struct Fixture {
    Bytes data=Bytes(256);std::vector<uint32_t> rel;bool overrides=false;
    void link(uint32_t p,uint32_t v){put(data,p,v);rel.push_back(p);}
    Fixture(){link(0,16);link(4,24);link(16,32);link(24,60);put(data,40,0x00040000);put(data,44,0xb3b3b3ff);put(data,68,0x000d0000);put(data,72,0xffc0ffff);link(76,96);put(data,100,0x3f000000);put(data,104,0xbf800000);put(data,108,0x3f800000);link(84,120);put(data,120,0x42800000);}
    DatArchive archive(){
        std::string name="map_plit";uint32_t sym=32+data.size()+rel.size()*4;uint32_t pubs=overrides?2:1;Bytes b(sym+8*pubs+name.size()+1+(overrides?9:0));
        put(b,0,b.size());put(b,4,data.size());put(b,8,rel.size());put(b,12,pubs);
        std::copy(data.begin(),data.end(),b.begin()+32);
        for(uint32_t i=0;i<rel.size();i++)put(b,32+data.size()+i*4,rel[i]);
        std::copy(name.begin(),name.end(),b.begin()+sym+pubs*8);
        if(overrides){put(b,sym+8,128);put(b,sym+12,name.size()+1);std::string map="map_head";std::copy(map.begin(),map.end(),b.begin()+sym+pubs*8+name.size()+1);}
        return DatArchive(b);
    }
};
template<class F>void rejects(F&& f){try{f();}catch(const DatError&){return;}throw std::runtime_error("Expected DatError");}
int main(int argc,char** argv){try{
    if(argc==2){std::ifstream f(argv[1],std::ios::binary);check(bool(f),"Open local stage");Bytes b((std::istreambuf_iterator<char>(f)),{});DatLights l{DatArchive(b)};check(l.lights.size()==2,"FD two player lights");check(l.lights[0].flags==4&&l.lights[1].flags==13,"FD source flags");check(read_dat_light_override(DatArchive(b),l.lights[0].source_offset)==0,"FD ambient override match");check(read_dat_light_override(DatArchive(b),l.lights[1].source_offset)==0xe0,"FD infinite override match");rejects([&]{(void)read_dat_light_override(DatArchive(b),0);});std::cout<<"Final Destination map_plit: two decoded lights\n";return 0;}
    Fixture f;DatLights l(f.archive());check(l.root_offset==0&&l.lights.size()==2,"Root and explicit terminator");
    check(l.lights[0].flags==4&&l.lights[0].color[0]==0xb3&&!l.lights[0].has_position,"Ambient color");
    check(l.lights[1].position[0]==.5f&&l.lights[1].position[1]==-1&&l.lights[1].has_shininess&&l.lights[1].shininess==64,"World position and optional shininess");
    f=Fixture();f.link(20,128);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();f.link(96,128);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();f.link(112,128);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();f.link(64,60);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();put(f.data,68,0x000e0000);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();put(f.data,100,0x7fc00000);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();put(f.data,76,97);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();put(f.data,24,32);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();f.link(8,248);f.link(248,240);rejects([&]{DatLights bad(f.archive());});
    f=Fixture();f.overrides=true;f.link(152,192);put(f.data,156,3);f.link(192,60);put(f.data,196,0xe0000000);f.link(200,32);f.link(160,208);
    check(read_dat_light_override(f.archive(),60)==0xe0,"Exact first override bits");
    check(read_dat_light_override(f.archive(),32)==0,"Exact ambient override bits");
    rejects([&]{(void)read_dat_light_override(f.archive(),124);});
    put(f.data,156,2);check(!read_dat_light_override(f.archive(),124),"Absent ID in fully bounded count");
    std::cout<<"stage light descriptor validation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
