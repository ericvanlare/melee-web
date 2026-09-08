#include "dat_material_animation.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
static void put32(Bytes& b,uint32_t o,uint32_t v) { for(unsigned i=0;i<4;++i)b.at(o+i)=uint8_t(v>>(24-8*i)); }
struct Fixture {
    Bytes data=Bytes(320); std::vector<uint32_t> relocations;
    void link(uint32_t s,uint32_t t) {put32(data,s,t);relocations.push_back(s);}
    Fixture() {
        link(8,12); link(20,28); link(36,52); link(40,68); put32(data,48,2U<<16);
        put32(data,56,std::bit_cast<uint32_t>(10.0f)); link(60,76);
        link(68,100);link(72,124);
        put32(data,80,4);data[88]=1;data[89]=0x85;link(92,96);
        data[96]=0x11;data[97]=0;data[98]=1;data[99]=32;
        link(100,256);put32(data,104,0x00080008);
        link(124,288);put32(data,128,0x00080008);
    }
    std::shared_ptr<const melee_web::DatArchive> archive() const {
        const auto table=32+uint32_t(data.size()),pub=table+uint32_t(relocations.size()*4);
        Bytes b(pub+10);put32(b,0,uint32_t(b.size()));put32(b,4,uint32_t(data.size()));
        put32(b,8,uint32_t(relocations.size()));put32(b,12,1);
        std::copy(data.begin(),data.end(),b.begin()+32);
        for(unsigned i=0;i<relocations.size();++i)put32(b,table+4*i,relocations[i]);
        b[pub+8]='r';return std::make_shared<melee_web::DatArchive>(b);
    }
};
int main() {
    try {
        MeleeWebNativeJointDesc joint{}; joint.child=joint.next=UINT32_MAX;
        MeleeWebNativeDObjDesc dobj{}; dobj.next=dobj.pobj=UINT32_MAX;
        MeleeWebNativeTextureDesc texture{};
        MeleeWebNativeMaterialDesc material{};material.textures=&texture;material.material.texture_count=1;
        MeleeWebNativeGraph model{&joint,&dobj,nullptr,&material,1,1,0,1,0};
        Fixture f;auto archive=f.archive();
        melee_web::DatMaterialAnimation valid(archive,0,model);archive.reset();
        check(valid.descriptor()&&valid.texture_animation_count()==1&&valid.image_count()==2,"owned native animation graph");
        auto rejected=[&](Fixture bad) {
            bool failed=false;
            try {melee_web::DatMaterialAnimation invalid(bad.archive(),0,model);}catch(const melee_web::DatError&){failed=true;}
            check(failed,"malformed material animation must reject before native evaluation");
        };
        for(unsigned channel=2;channel<=9;channel++){
            Fixture numeric;numeric.data[88]=channel;numeric.data[96]=0x12;
            melee_web::DatMaterialAnimation transform(numeric.archive(),0,model);
            check(transform.texture_animation_count()==1,"numeric texture transforms and blend permit checked interpolation");
        }
        Fixture unsupported_texture;unsupported_texture.data[88]=11;rejected(unsupported_texture);
        Fixture alpha;
        alpha.relocations.erase(std::remove(alpha.relocations.begin(),alpha.relocations.end(),20),alpha.relocations.end());
        put32(alpha.data,20,0);alpha.link(16,52);alpha.data[88]=10;
        melee_web::DatMaterialAnimation native_alpha(alpha.archive(),0,model);
        check(native_alpha.descriptor()&&native_alpha.texture_animation_count()==0,"native material alpha owns numeric stream without texture tables");
        Fixture unsupported_alpha=alpha;unsupported_alpha.data[88]=11;rejected(unsupported_alpha);
        Fixture color=alpha;color.data[88]=9;
        melee_web::DatMaterialAnimation native_color(color.archive(),0,model);
        check(native_color.descriptor(),"bounded material RGB channel");
        Fixture color_overflow=color;color_overflow.data[99]=64;
        melee_web::DatMaterialAnimation runtime_checked_color(color_overflow.archive(),0,model);
        Fixture color_interpolation=color;color_interpolation.data[96]=0x12;
        melee_web::DatMaterialAnimation interpolated_color(color_interpolation.archive(),0,model);
        check(interpolated_color.descriptor()&&runtime_checked_color.descriptor(),"original runtime guards validate produced color values");
        Fixture broken_alpha=alpha;put32(broken_alpha.data,80,2);rejected(broken_alpha);
        Fixture bad;bad.data[99]=64;rejected(bad); // table index2 into two entries
        bad=Fixture();bad.data[96]=0x12;rejected(bad); // interpolated index could overshoot
        bad=Fixture();put32(bad.data,80,2);rejected(bad); // packet advertises two values
        bad=Fixture();put32(bad.data,32,1);rejected(bad); // missing texture ID
        bad=Fixture();put32(bad.data,48,0);rejected(bad); // absent image table count
        bad=Fixture();put32(bad.data,84,0x7fc00000);rejected(bad); // nonfinite start
        bad=Fixture();bad.link(0,0);rejected(bad); // relocated-zero cycle
        bad=Fixture();bad.link(24,52);rejected(bad); // active render channel
        std::cout<<"owned material animation topology/index/bounds checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
