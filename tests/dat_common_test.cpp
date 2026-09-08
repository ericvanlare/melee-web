#include "dat_common.hpp"
#include <algorithm>
#include <bit>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>
using namespace melee_web;
using Bytes = std::vector<std::uint8_t>;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); } catch (const DatError&) { return; } throw std::runtime_error("Expected DatError"); }
void put32(Bytes& b, std::size_t p, std::uint32_t n) { for (int i=0;i<4;++i) b.at(p+i)=std::uint8_t(n>>(24-8*i)); }
void putf(Bytes& b, std::size_t p, float n) { put32(b,p,std::bit_cast<std::uint32_t>(n)); }
struct Fixture {
    static constexpr std::uint32_t root=0x900, other=0x960;
    Bytes data=Bytes(0xa00); std::vector<std::uint32_t> slots;
    Fixture() { for (std::uint32_t i=0;i<23;++i) link(root+i*4,i ? other+i*4 : 0); }
    void link(std::uint32_t p,std::uint32_t n) { put32(data,p,n); if(std::find(slots.begin(),slots.end(),p)==slots.end())slots.push_back(p); }
    void unlink(std::uint32_t p) { put32(data,p,0);std::erase(slots,p); }
    DatArchive archive(std::string name="ftLoadCommonData", std::uint32_t symbol=root) const {
        const auto publics=std::uint32_t(32+data.size()+slots.size()*4);Bytes bytes(publics+8+name.size()+1);
        put32(bytes,0,std::uint32_t(bytes.size()));put32(bytes,4,std::uint32_t(data.size()));put32(bytes,8,std::uint32_t(slots.size()));put32(bytes,12,1);
        std::copy(data.begin(),data.end(),bytes.begin()+32);
        for(std::size_t i=0;i<slots.size();++i)put32(bytes,32+data.size()+4*i,slots[i]);
        put32(bytes,publics,symbol);std::copy(name.begin(),name.end(),bytes.begin()+publics+8);return DatArchive(bytes);
    }
    DatCommon read() const { return DatCommon(archive()); }
};
void typed_fields_and_lifetime() {
    Fixture f;
    putf(f.data,0,.2875f);put32(f.data,0x1c,0xfffffff3);put32(f.data,0x4d8,0xfedcba98);
    putf(f.data,0x32c,1.25f);putf(f.data,0x330,-2.5f);
    putf(f.data,0x4e4,3);putf(f.data,0x4e8,4);putf(f.data,0x4ec,5);
    put32(f.data,0x380,0xffffffff);put32(f.data,0x384,0xf1234567);put32(f.data,0x388,0xfffffe97);put32(f.data,0x3a0,0x12345678);
    for(std::uint8_t i=0;i<16;++i)f.data[0x6dc+i]=std::uint8_t(17+i);
    put32(f.data,0x6ec,0x89abcdef);put32(f.data,0x7d8,0x12345678);
    putf(f.data,0x804,123);putf(f.data,0x808,2);putf(f.data,0x80c,3);putf(f.data,0x810,4);put32(f.data,0x814,0xffffffe7);
    put32(f.data,0x23c,100);put32(f.data,0x500,0x7fc00001); // Opaque word is not decoded as float or pointer.
    const auto c=f.read();const auto& s=c.scalars;
    static_assert(std::is_same_v<decltype(s.x500), std::uint32_t>);
    check(s.horizontal_stick_deadzone==.2875f&&s.x1C==-13&&s.x4D8==0xfedcba98,"floats and signed/unsigned integers decode distinctly");
    check(s.escapeair_deadzone.x==1.25f&&s.escapeair_deadzone.y==-2.5f&&s.x4E4.z==5,"Vec2/Vec3 components use source float layout");
    check(s.x380.state==-1&&s.x380.damage==0xf1234567&&s.x380.kb_angle==-361&&s.x380.sfx_kind==0x12345678,"nested hitbox fields retain their source types");
    check(s.x6DC_colorsByPlayer[0].r==17&&s.x6DC_colorsByPlayer[3].a==32&&s.x7D8.r==0x12&&s.x7D8.a==0x78&&
          s.x6EC[0]==0x89&&s.x6EC[3]==0xef,"byte colors and padding retain order without whole-word byte swapping");
    check(s.x804==123&&s.x808.x==2&&s.x808.y==3&&s.x808.z==4&&s.x814==-25,"x808 follows actual field layout despite source comment typo");
    check(s.x23C==100&&s.x500==0x7fc00001,"opaque UNK_T slots remain integer words with no address conversion");
    f.data.assign(f.data.size(),0);
    check(c.scalars.x804==123&&c.scalar_offset==0,"typed block owns its values after archive/input destruction");
}
void root_identity_and_readiness() {
    auto c=Fixture().read();
    check(c.descriptor_offset==Fixture::root&&c.roots.size()==23&&c.roots[0].data_offset==0&&
          c.roots[0].source_global=="p_ftCommonData"&&c.roots[0].readiness==DatCommonReadiness::ScalarsDecoded,
          "relocated root zero is the typed scalar block");
    const std::array<std::pair<unsigned,std::string_view>,7> checks={{{1,"Fighter_804D6550"},{4,"ftPartsTable"},{5,"Fighter_804D6540"},
        {10,"Fighter_GrabMashShake"},{16,"Fighter_804D6514"},{21,"gCrowdConfig"},{22,"Fighter_804D64FC"}}};
    for(const auto& [i,name]:checks)check(c.roots[i].index==i&&c.roots[i].source_global==name&&
        c.roots[i].data_offset==Fixture::other+i*4&&c.roots[i].readiness==DatCommonReadiness::Unresolved,
        "known global identities stay unresolved offsets, not guessed native objects");
    Fixture f;f.unlink(Fixture::root+22*4);c=f.read();
    check(!c.roots[22].data_offset&&c.roots[22].readiness==DatCommonReadiness::Missing,
          "absent optional unresolved service stays visibly missing");
}
void pointer_and_layout_bounds() {
    Fixture f;f.unlink(Fixture::root);rejects([&]{(void)f.read();});
    f=Fixture();f.link(Fixture::root,0x200);rejects([&]{(void)f.read();}); // Scalar block now crosses root inventory.
    f=Fixture();f.link(Fixture::root,1);rejects([&]{(void)f.read();});
    f=Fixture();f.link(Fixture::root+4,0xa00);rejects([&]{(void)f.read();});
    f=Fixture();f.unlink(Fixture::root+4);put32(f.data,Fixture::root+4,Fixture::other);rejects([&]{(void)f.read();});
    f=Fixture();f.link(0x9f0,0x800);rejects([&]{(void)f.read();}); // Referenced target bisects scalar block.
    f=Fixture();rejects([&]{(void)DatCommon(f.archive("other"));});
    rejects([&]{(void)DatCommon(f.archive("ftLoadCommonData",0x9f0));});
    rejects([&]{(void)DatCommon(f.archive("ftLoadCommonData",Fixture::root+1));});
}
void internal_relocations_and_nonfinite() {
    for(auto offset:{0U,0x23cU,0x6dcU,0x6ecU}){
        Fixture f;f.link(offset,0);rejects([&]{(void)f.read();});
    }
    for(auto offset:{0U,0x32cU,0x4e8U,0x808U})for(auto bits:{0x7f800000U,0xff800000U,0x7fc00001U}){
        Fixture f;put32(f.data,offset,bits);rejects([&]{(void)f.read();});
    }
}
}
int main(int argc,char**argv){
 const std::map<std::string,std::function<void()>>cases={{"typed_fields_and_lifetime",typed_fields_and_lifetime},
 {"root_identity_and_readiness",root_identity_and_readiness},{"pointer_and_layout_bounds",pointer_and_layout_bounds},
 {"internal_relocations_and_nonfinite",internal_relocations_and_nonfinite}};
 if(argc!=2||!cases.contains(argv[1]))return 2;
 try{cases.at(argv[1])();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
