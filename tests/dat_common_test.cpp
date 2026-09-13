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
    Fixture() { link(root,0);for(auto i:{6U,7U,8U,16U,17U,20U})link(root+i*4,other+i*4); }
    void link(std::uint32_t p,std::uint32_t n) { put32(data,p,n); if(std::find(slots.begin(),slots.end(),p)==slots.end())slots.push_back(p); }
    void unlink(std::uint32_t p) { put32(data,p,0);std::erase(slots,p); }
    std::uint32_t allocate(std::uint32_t bytes) {
        const auto offset=std::uint32_t(data.size());data.resize(offset+((bytes+3)&~3U));return offset;
    }
    DatArchive archive(std::string name="ftLoadCommonData", std::uint32_t symbol=root) const {
        const auto publics=std::uint32_t(32+data.size()+slots.size()*4);Bytes bytes(publics+8+name.size()+1);
        put32(bytes,0,std::uint32_t(bytes.size()));put32(bytes,4,std::uint32_t(data.size()));put32(bytes,8,std::uint32_t(slots.size()));put32(bytes,12,1);
        std::copy(data.begin(),data.end(),bytes.begin()+32);
        for(std::size_t i=0;i<slots.size();++i)put32(bytes,32+data.size()+4*i,slots[i]);
        put32(bytes,publics,symbol);std::copy(name.begin(),name.end(),bytes.begin()+publics+8);return DatArchive(bytes);
    }
    DatCommon read() const { return DatCommon(archive()); }
};
struct TablesFixture : Fixture {
    std::uint32_t maps,part_desc,joints,names,alternates,alternate_desc,entries,shake_desc,samples,scale,crowd;
    TablesFixture() {
        for (auto [index,count]:{std::pair{1U,78U},{2U,30U},{3U,9U},{12U,39U},{13U,15U},{14U,9U},{15U,2U}}) {
            const auto values=allocate(count*4);link(root+index*4,values);
            for(unsigned i=0;i<count;++i)putf(data,values+i*4,float(index*100+i));
            if(index==12)scale=values;
        }
        maps=allocate(MELEE_WEB_COMMON_PART_TABLES*4);part_desc=allocate(12);joints=allocate(3);names=allocate(54);
        link(root+4*4,maps);link(part_desc,joints);link(part_desc+4,names);put32(data,part_desc+8,3);
        for(unsigned kind=0;kind<MELEE_WEB_COMMON_PART_TABLES;++kind)link(maps+kind*4,part_desc);
        data[joints]=2;data[joints+1]=255;data[joints+2]=53;
        std::fill_n(data.begin()+names,54,255);data[names+2]=0;data[names+53]=2;
        alternates=allocate(33*4);alternate_desc=allocate(8);entries=allocate(4);
        link(root+5*4,alternates);link(alternates+4*4,alternate_desc);link(alternate_desc,entries);
        put32(data,alternate_desc+4,1);data[entries]=2;data[entries+1]=0;data[entries+2]=3;data[entries+3]=255;
        shake_desc=allocate(24);samples=allocate(16);link(root+9*4,shake_desc);
        putf(data,samples,1.25f);putf(data,samples+4,-2.5f);putf(data,samples+8,-3.75f);putf(data,samples+12,4.5f);
        for(unsigned i=0;i<3;++i){link(shake_desc+i*8,samples);put32(data,shake_desc+i*8+4,2);}
        for(unsigned index:{10U,11U}) { const auto desc=allocate(8);link(root+index*4,desc);link(desc,samples);put32(data,desc+4,2); }
        for(unsigned index:{18U,19U}) {
            const auto colors=allocate(20);link(root+index*4,colors);
            for(unsigned i=0;i<20;++i)data[colors+i]=std::uint8_t(index+i);
        }
        crowd=allocate(0x44);link(root+21*4,crowd);
        putf(data,crowd,50.5f);put32(data,crowd+0x1c,0xfffffff3);putf(data,crowd+0x40,-100.25f);
    }
};
void static_graphs_and_ownership() {
    TablesFixture fixture;auto common=fixture.read();
    check(common.tables.ready_mask==MELEE_WEB_COMMON_STATIC_ROOT_MASK,"all supported static roots are decoded");
    for(unsigned i=1;i<23;++i)if(MELEE_WEB_COMMON_STATIC_ROOT_MASK&(1U<<i))
        check(common.roots[i].readiness==DatCommonReadiness::StaticTablesDecoded,"static readiness follows decoded graph");
    check(common.roots[20].readiness==DatCommonReadiness::Unresolved,"HSD root is not declared constructed");
    check(common.tables.item_throw[25].heavy_mul==177&&common.tables.swing[5][4]==229&&
          common.tables.stale[8]==308&&common.tables.scale_modifiers[38]==1238&&
          common.tables.bunny_modifiers[14]==1314&&common.tables.metal_modifiers[8]==1408&&
          common.tables.gravity_weight[1]==1501,"source table boundaries retain values");
    check(common.tables.parts[32].part_count==3&&common.tables.parts[32].part_to_joint[53]==2&&
          common.tables.parts[32].joint_to_part[1]==255,"all 33 fighter slots include the final named mapping and sentinel");
    check(common.tables.none_parts.part_count==3&&common.tables.none_parts.part_to_joint[53]==2&&
          common.tables.none_parts.joint_to_part[1]==255,"the source-only FTKIND_NONE slot retains its part map");
    check(!common.tables.alternates[0].has_descriptor&&common.tables.alternates[4].count==1&&
          common.tables.alternates[4].entries[0].insertion==3&&common.tables.alternates[4].entries[0].source_joint==255,
          "nullable alternate table and typed insertion record");
    check(common.tables.damage_shake[2].samples[1].x==-3.75f&&common.tables.grab_shake.count==2&&
          common.tables.smash_shake.samples[0].y==-2.5f,"aliased shake descriptors decode independently");
    check(common.tables.primary_colors[4].a==37&&common.tables.secondary_colors[0].r==19&&
          common.tables.crowd.kb_threshold_low==50.5f&&common.tables.crowd.x1C==-13&&
          common.tables.crowd.blastzone_y_offset==-100.25f,"colors and mixed crowd types retain source encoding");
    fixture.data.clear();auto copy=common;common=DatCommon(Fixture().archive());
    check(copy.tables.parts[32].part_to_joint[53]==2&&copy.tables.damage_shake[2].samples[1].x==-3.75f,
          "copied graphs own all nested values after archive and prior owner destruction");
    TablesFixture packed;const auto byte_map=packed.allocate(55)+1;
    std::copy_n(packed.data.begin()+packed.names,54,packed.data.begin()+byte_map);
    packed.data.resize(byte_map+54);packed.link(packed.part_desc+4,byte_map);
    check(packed.read().tables.parts[32].part_to_joint[53]==2,
          "byte maps may be unaligned and end without word padding");
}
void malformed_static_graphs() {
    for(unsigned value:{0U,141U}){TablesFixture f;put32(f.data,f.part_desc+8,value);rejects([&]{(void)f.read();});}
    TablesFixture f;f.data[f.names+53]=3;rejects([&]{(void)f.read();});
    f=TablesFixture();f.data[f.joints]=54;rejects([&]{(void)f.read();});
    f=TablesFixture();f.unlink(f.maps+32*4);rejects([&]{(void)f.read();});
    f=TablesFixture();f.link(f.part_desc+8,0);rejects([&]{(void)f.read();});
    f=TablesFixture();f.link(f.joints,0);rejects([&]{(void)f.read();});
    f=TablesFixture();f.link(f.part_desc+4,f.names+4);rejects([&]{(void)f.read();}); // Range ends at next descriptor.
    f=TablesFixture();f.unlink(Fixture::root+4*4);rejects([&]{(void)f.read();});
    f=TablesFixture();put32(f.data,f.alternate_desc+4,33);rejects([&]{(void)f.read();});
    f=TablesFixture();f.data[f.entries+2]=4;rejects([&]{(void)f.read();});
    f=TablesFixture();f.data[f.entries+1]=3;rejects([&]{(void)f.read();});
    f=TablesFixture();const auto duplicate=f.allocate(8);f.link(f.alternate_desc,duplicate);put32(f.data,f.alternate_desc+4,2);
    f.data[duplicate]=f.data[duplicate+4]=2;rejects([&]{(void)f.read();});
    for(unsigned value:{0U,256U}){f=TablesFixture();put32(f.data,f.shake_desc+4,value);rejects([&]{(void)f.read();});}
    f=TablesFixture();f.unlink(f.shake_desc);rejects([&]{(void)f.read();});
    f=TablesFixture();f.link(f.shake_desc+4,0);rejects([&]{(void)f.read();});
    f=TablesFixture();put32(f.data,f.scale+38*4,0x7f800000);rejects([&]{(void)f.read();});
    f=TablesFixture();put32(f.data,f.crowd+0x40,0x7fc00001);rejects([&]{(void)f.read();});
}
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
    const std::array<std::pair<unsigned,std::string_view>,3> checks={{{6,"Fighter_804D653C"},{8,"Fighter_804D6534"},
        {16,"Fighter_804D6514"}}};
    for(const auto& [i,name]:checks)check(c.roots[i].index==i&&c.roots[i].source_global==name&&
        c.roots[i].data_offset==Fixture::other+i*4&&c.roots[i].readiness==DatCommonReadiness::Unresolved,
        "known global identities stay unresolved offsets, not guessed native objects");
    check(c.roots[4].readiness==DatCommonReadiness::Missing&&c.tables.ready_mask==0,"missing static roots are not marked decoded");
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
 {"static_graphs_and_ownership",static_graphs_and_ownership},{"malformed_static_graphs",malformed_static_graphs},
 {"internal_relocations_and_nonfinite",internal_relocations_and_nonfinite}};
 if(argc!=2||!cases.contains(argv[1]))return 2;
 try{cases.at(argv[1])();}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
