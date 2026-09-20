#include "native_dat.hpp"
#include "gameplay_fighter_data.h"
#include "gameplay_pikachu_schema.h"
#include "gameplay_purin_schema.h"
#include "gameplay_donkey_schema.h"
#include "gameplay_article_data.h"
#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace fighter_runtime_test;
extern "C" void melee_web_test_fighter_data(void*,int);
extern "C" void melee_web_test_guard_data(const MeleeWebNativeDat*,uint32_t,void*,uint32_t*);
extern "C" void melee_web_test_purin_data(void*);
extern "C" void melee_web_test_donkey_data(void*);
namespace {
/* Keep this focused check independent of the full source header graph. These
 * are the two native ABI prefixes needed to inspect ftData->x2C->x10. */
struct NativeDynamicsView {
    int32_t dynamics_num;
    void* bones;
    int32_t auxiliary_count;
    void* auxiliary;
    void*** modes;
};
struct NativeFighterDataView {
    std::byte prefix[0x2c];
    NativeDynamicsView* dynamics;
};
static_assert(offsetof(NativeFighterDataView,dynamics)==0x2c);
struct NativeFighterDataX48View {
    std::byte prefix[0x48];
    void** x48_items;
};
static_assert(offsetof(NativeFighterDataX48View,x48_items)==0x48);
struct NativeFighterDataX28View {
    std::byte prefix[0x28];
    void* squat_wait_choices;
};
static_assert(offsetof(NativeFighterDataX28View,squat_wait_choices)==0x28);
struct PurinWaitChoiceView { int32_t motion_id; int32_t weight; };
struct PurinCountedView { uint32_t count; void* data; };
struct PurinPartsDescView { uint32_t model_num; void* (*vis_table)[4]; };
struct PurinPartsView { uint32_t unused; PurinPartsDescView desc; };
static_assert(sizeof(PurinPartsView)==12);

Bytes read_file(const char* path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    auto n=f.tellg(); check(n>0 && n<64*1024*1024,"Invalid fighter input size");
    f.seekg(0); Bytes bytes((std::istreambuf_iterator<char>(f)),{}); check(bytes.size()==size_t(n),"Truncated input"); return bytes;
}
FighterFixture native_fixture() {
    FighterFixture f;
    put32(f.data,f.command_a,0); f.unlink(f.command_a+4);put32(f.data,f.command_b,0);
    auto append=[&](size_t n) { uint32_t at=uint32_t((f.data.size()+3)&~size_t(3));f.data.resize(at+n);return at; };
    auto model=append(24), vis=append(80), textures=append(20);
    f.link(8,model);f.link(model+4,vis);f.link(model+12,textures);
    for(auto [slot,size]:std::initializer_list<std::pair<uint32_t,size_t>>{
        {0x34,8},{0x38,40},{0x3c,24},{0x44,28},{0x4c,56},{0x58,28}})f.link(slot,append(size));
    const auto effect_parts=append(5*4); f.link(0x54,effect_parts);
    for(unsigned i=0;i<5;++i)put32(f.data,effect_parts+i*4,i+1);
    auto items=append(16);f.link(0x48,items);
    for(auto i:{0U,2U}){auto article=append(24),attr=append(0x84);f.link(items+i*4,article);f.link(article,attr);}
    return f;
}
void verify(std::shared_ptr<const DatArchive> archive,const Bytes& container,int actual) {
    uint32_t root=UINT32_MAX;for(const auto&s:archive->public_symbols())if(s.name=="ftDataMario")root=s.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataMario");
    GameplayActionStore actions(archive,mario(),container);
    NativeDatArena owner(archive);uint32_t unresolved;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,0,5,
        static_cast<uint32_t>(actions.runtime().actions().size()), actions.action_rows(),
        actions.blend_rows(),actions.wait_choices(),&unresolved);
    archive.reset(); // Reader and action store retain the backing archive independently.
    melee_web_test_fighter_data(data,actual);
    check((unresolved&((1U<<3)|(1U<<4)|(1U<<9)|(1U<<18)|(1U<<22)))==0,"Reached fields remain unresolved");
    if(actual){
        check(unresolved==0x8001e0,"Unexpected unhydrated fields changed");
        melee_web_test_guard_data(owner.reader(),root,data,&unresolved);
        check(unresolved==0x8000e0,"Guard descriptor was not published");
    }
    std::cout<<"Native fighter data unresolved mask: "<<std::hex<<unresolved<<std::dec<<'\n';
}
void verify_roy(const std::shared_ptr<const DatArchive>& archive,const Bytes& container) {
    const auto& identity=resolve_fighter_costume("PlyEmblem5K_Share_joint");
    check(identity.fighter_kind==26 && identity.motion_count==327,"Missing source Roy identity");
    GameplayActionStore actions(archive,identity,container);
    check(actions.runtime().actions().size()==identity.motion_count && actions.runtime().mars_attributes(),
          "Roy action metadata or Mars extension is incomplete");
    const auto symbols=archive->public_symbols();
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:symbols)if(symbol.name=="ftDataEmblem")root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataEmblem");
    NativeDatArena owner(archive);uint32_t unresolved;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,26,5,
        identity.motion_count,actions.action_rows(),actions.blend_rows(),actions.wait_choices(),&unresolved);
    const auto* decoded=static_cast<const NativeFighterDataView*>(data);
    check(decoded && decoded->dynamics && decoded->dynamics->dynamics_num==3 && decoded->dynamics->modes,
          "Roy dynamics table was not decoded");
    const auto* blends=static_cast<const uint8_t*>(actions.blend_rows());
    check(blends && blends[239*2]==0 && blends[239*2+1]==5 &&
          blends[240*2+1]==5 && blends[241*2+1]==5,
          "Roy selector-5 source rows were not retained");
    const std::array<std::array<uintptr_t,3>,6> expected{{
        {{2,2,2}},{{0,0,2}},{{2,0,0}},{{1,1,1}},{{2,1,1}},{{3,0,0}}}};
    for(size_t mode=0;mode<expected.size();++mode)
        for(size_t bone=0;bone<expected[mode].size();++bone)
            check(reinterpret_cast<uintptr_t>(decoded->dynamics->modes[mode][bone])==expected[mode][bone],
                  "Roy dynamics mode cutoff changed");
    std::cout<<"Native Roy dynamics selector-5 rows and six authored modes: passed\n";
}
void verify_ganon(const std::shared_ptr<const DatArchive>& archive,const Bytes& container) {
    const auto& identity=resolve_fighter_costume("PlyGanon5K_Share_joint");
    check(identity.fighter_kind==25 && identity.motion_count==318 && identity.material_animation_symbol.empty(),
          "Missing source Ganondorf identity or authored null material animation");
    const auto runtime=std::make_shared<const DatFighterRuntime>(archive,identity);
    DatFighterActions(*archive,identity).validate_container(container);
    check(runtime->actions().size()==identity.motion_count && runtime->captain_attributes(),
          "Ganondorf action metadata or Captain extension is incomplete");
    const auto& captain=*runtime->captain_attributes();
    check(std::isfinite(captain.specialn_stick_range_y_neg) &&
          std::isfinite(captain.specialhi_air_friction_mul) &&
          std::isfinite(captain.speciallw_air_landing_traction),
          "Ganondorf Captain extension contains a nonfinite scalar");
    const auto symbols=archive->public_symbols();
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:symbols)if(symbol.name=="ftDataGanon")root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataGanon");
    NativeDatArena owner(archive); uint32_t unresolved;
    std::vector<std::uint8_t> ganon_blends(identity.motion_count * 2, 0);
    for (const auto& action:runtime->actions()) {
        ganon_blends[action.motion_id*2]=action.blend_dynamics[0];
        ganon_blends[action.motion_id*2+1]=action.blend_dynamics[1];
    }
    void* data=melee_web_fighter_data_decode(owner.reader(),root,25,5,
        identity.motion_count,nullptr,ganon_blends.data(),nullptr,&unresolved);
    check(data && !melee_web_fighter_data_article(data,25,0) && !melee_web_fighter_data_article(data,25,5),
          "Ganondorf source ftData unexpectedly published an Article");
    check((unresolved&((1U<<18)|(1U<<22)))==0,
          "Ganondorf source fields remain unresolved");
    std::cout<<"Native Ganondorf Captain extension and null Article table: passed\n";
}
void verify_captain(const std::shared_ptr<const DatArchive>& archive,const Bytes& container) {
    const auto& identity=resolve_fighter_costume("PlyCaptain5K_Share_joint");
    check(identity.fighter_kind==2 && identity.motion_count==318 && identity.material_animation_symbol.empty(),
          "Missing source Captain identity or authored null material animation");
    const auto runtime=std::make_shared<const DatFighterRuntime>(archive,identity);
    DatFighterActions(*archive,identity).validate_container(container);
    check(runtime->actions().size()==identity.motion_count && runtime->captain_attributes(),
          "Captain action metadata or Captain extension is incomplete");
    check(runtime->dynamics().bones.empty() && runtime->dynamics().spheres.empty() &&
          !runtime->dynamics().animation_table_offset,
          "Captain source dynamics descriptor was changed or fabricated");
    const auto& captain=*runtime->captain_attributes();
    check(std::isfinite(captain.specialn_stick_range_y_neg) &&
          std::isfinite(captain.specialhi_air_friction_mul) &&
          std::isfinite(captain.speciallw_air_landing_traction),
          "Captain extension contains a nonfinite scalar");
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:archive->public_symbols())if(symbol.name=="ftDataCaptain")root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataCaptain");
    NativeDatArena owner(archive);uint32_t unresolved;
    std::vector<std::uint8_t> captain_blends(identity.motion_count*2,0);
    for(const auto& action:runtime->actions()) {
        captain_blends[action.motion_id*2]=action.blend_dynamics[0];
        captain_blends[action.motion_id*2+1]=action.blend_dynamics[1];
    }
    void* data=melee_web_fighter_data_decode(owner.reader(),root,2,6,
        identity.motion_count,nullptr,captain_blends.data(),nullptr,&unresolved);
    const auto* decoded=static_cast<const NativeFighterDataView*>(data);
    check(decoded && decoded->dynamics && decoded->dynamics->dynamics_num==0 &&
          decoded->dynamics->bones==nullptr && decoded->dynamics->auxiliary_count==0 &&
          decoded->dynamics->modes==nullptr,
          "Captain source zero-count dynamics were not decoded exactly");
    check(data && !melee_web_fighter_data_article(data,2,0) &&
          !melee_web_fighter_data_article(data,2,5),
          "Captain source ftData unexpectedly published an Article");
    check((unresolved&((1U<<18)|(1U<<22)))==0,
          "Captain source fields remain unresolved");
    std::cout<<"Native Captain extension, six-costume bounds, zero dynamics and null Article table: passed\n";
}
void verify_pikachu(const std::shared_ptr<const DatArchive>& archive,const Bytes& container,
                    const char* costume_name, const char* root_name, uint32_t kind,
                    int32_t ground_item, int32_t air_item, uint32_t thunder_effects,
                    int32_t zip_duration) {
    const auto& identity=resolve_fighter_costume(costume_name);
    check(identity.fighter_kind==kind && identity.motion_count==320,
          "Missing source Pikachu-family identity");
    const auto runtime=std::make_shared<const DatFighterRuntime>(archive,identity);
    DatFighterActions(*archive,identity).validate_container(container);
    check(runtime->actions().size()==identity.motion_count && runtime->pikachu_attributes(),
          "Pikachu-family action metadata or shared extension is incomplete");
    const auto& attributes=*runtime->pikachu_attributes();
    check(attributes.specialn_itkind==ground_item && attributes.specialairn_itkind==air_item &&
          attributes.x60==zip_duration && attributes.xDC==thunder_effects,
          "Pikachu-family typed attributes changed");
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:archive->public_symbols())if(symbol.name==root_name)root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing Pikachu-family ftData root");
    NativeDatArena owner(archive); uint32_t unresolved=UINT32_MAX;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,kind,4,
        identity.motion_count,runtime->actions().empty()?nullptr:
        nullptr,nullptr,nullptr,&unresolved);
    /* The shared native decoder owns the exact source extension ABI. The
     * action store is intentionally omitted here because this boundary test
     * isolates extension and nullable Article admission. */
    auto** words=static_cast<void**>(data);
    check(data && words[1], "Pikachu-family native extension was not allocated");
    const auto* decoded=static_cast<const MeleeWebPikachuAttributes*>(words[1]);
    check(decoded->specialn_itkind==ground_item && decoded->specialairn_itkind==air_item &&
          decoded->x60==zip_duration && decoded->xDC==thunder_effects,
          "Pikachu-family native extension did not retain typed source fields");
    for(unsigned slot=0;slot<3;++slot) {
        void* article=melee_web_fighter_data_article(data,kind,slot);
        check(article && melee_web_article_unresolved(article)==
              ((1U<<1)|(1U<<3)|(1U<<4)),
              "Pikachu-family Article registration identity is incomplete");
    }
    check((unresolved&(1U<<18))==0,
          "Pikachu-family Article registration roots remain unresolved");
    std::cout<<"Native "<<(kind==12?"Pikachu":"Pichu")<<
        " shared 0xf8 attributes and three Article registrations: passed\n";
}
void verify_donkey(const Bytes& source,const Bytes& container) {
    const auto archive=std::make_shared<const DatArchive>(source);
    const auto& identity=resolve_fighter_costume("PlyDonkey5K_Share_joint");
    check(identity.fighter_kind==3 && identity.motion_count==337,
          "Missing source Donkey identity");
    const auto runtime=std::make_shared<const DatFighterRuntime>(archive,identity);
    DatFighterActions(*archive,identity).validate_container(container);
    check(runtime->actions().size()==identity.motion_count && runtime->donkey_attributes(),
          "Donkey action metadata or 0x74 extension is incomplete");
    const auto& attributes=*runtime->donkey_attributes();
    check(attributes.motion_state==341 && attributes.x4_motion_state==351 &&
          attributes.specialn_x2C_MAX_ARM_SWINGS==10 &&
          attributes.specialn_x30_DAMAGE_PER_SWING==2 &&
          attributes.cargo_hold_x20_TURN_SPEED==6.0f &&
          attributes.cargo_hold_x24_JUMP_STARTUP_LAG==3.0f &&
          attributes.cargo_hold_x28_LANDING_LAG==15.0f,
          "Donkey portable ABI values changed");
    check(runtime->dynamics().active_bone_count==1 && runtime->dynamics().bones.size()==1 &&
          runtime->dynamics().spheres.size()==1 && runtime->dynamics().animation_table_offset,
          "Donkey authored dynamics descriptor was omitted or fabricated");
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:archive->public_symbols())if(symbol.name=="ftDataDonkey")root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataDonkey root");
    std::vector<std::uint8_t> donkey_blends(identity.motion_count * 2, 0);
    for (const auto& action : runtime->actions()) {
        donkey_blends[action.motion_id * 2] = action.blend_dynamics[0];
        donkey_blends[action.motion_id * 2 + 1] = action.blend_dynamics[1];
    }
    check(donkey_blends.size() == identity.motion_count * 2,
          "Donkey source dynamics rows were not retained");
    for (std::size_t motion = 0; motion < identity.motion_count; ++motion)
        check(donkey_blends[motion * 2 + 1] == 0,
              "Donkey source dynamics selector changed");
    NativeDatArena owner(archive); uint32_t unresolved=UINT32_MAX;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,3,5,
        identity.motion_count,nullptr,donkey_blends.data(),nullptr,&unresolved);
    check(data,"Donkey native ftData was not decoded");
    const auto* decoded=static_cast<const NativeFighterDataView*>(data);
    check(decoded->dynamics && decoded->dynamics->dynamics_num==1 &&
          decoded->dynamics->modes &&
          reinterpret_cast<std::uintptr_t>(decoded->dynamics->modes[0][0])==1,
          "Donkey native dynamics mode selector or cutoff changed");
    melee_web_test_donkey_data(data);
    auto malformed=source;
    put32(malformed,0x20+runtime->extension_offset()+0x20,0x7fc00000U);
    rejects([&] {
        NativeDatArena rejected(std::make_shared<const DatArchive>(malformed));
        uint32_t mask=0;
        (void)melee_web_fighter_data_decode(rejected.reader(),root,3,5,
            identity.motion_count,nullptr,donkey_blends.data(),nullptr,&mask);
    });
    auto bad_cutoff=source;
    const auto cutoff_row=archive->pointer(*runtime->dynamics().animation_table_offset,4);
    check(cutoff_row.has_value(),"Donkey source cutoff row is missing");
    put32(bad_cutoff,0x20+*cutoff_row,
          static_cast<uint32_t>(runtime->dynamics().bones[0].parameters.size()+1));
    rejects([&] {
        NativeDatArena rejected(std::make_shared<const DatArchive>(bad_cutoff));
        uint32_t mask=0;
        (void)melee_web_fighter_data_decode(rejected.reader(),root,3,5,
            identity.motion_count,nullptr,donkey_blends.data(),nullptr,&mask);
    });
    auto bad_blends=donkey_blends;
    bad_blends[1]=1; // Source table has only selector zero; reject an adjacent row.
    rejects([&] {
        NativeDatArena rejected(archive);
        uint32_t mask=0;
        (void)melee_web_fighter_data_decode(rejected.reader(),root,3,5,
            identity.motion_count,nullptr,bad_blends.data(),nullptr,&mask);
    });
    std::cout<<"Native Donkey 0x74 ABI, exact dynamics and null Article table: passed\n";
}
void verify_purin(const Bytes& source,const Bytes& container) {
    const auto archive=std::make_shared<const DatArchive>(source);
    const auto& identity=resolve_fighter_costume("PlyPurin5K_Share_joint");
    check(identity.fighter_kind==15 && identity.motion_count==327,
          "Missing source Purin identity");
    const auto runtime=std::make_shared<const DatFighterRuntime>(archive,identity);
    DatFighterActions(*archive,identity).validate_container(container);
    check(runtime->actions().size()==identity.motion_count && runtime->purin_attributes(),
          "Purin action metadata or 0x100 extension is incomplete");
    const auto& attributes=*runtime->purin_attributes();
    check(attributes.x2C==341 && attributes.x30==-1 && attributes.x34==90 &&
          attributes.x38==20 && attributes.x70==8 && attributes.x9C==32 &&
          attributes.specialn_vel.x==-0.13f && attributes.specialn_vel.y==1.6f &&
          attributes.xE8==0x3e800000U && attributes.xEC==0x3e4ccccdU &&
          attributes._48[0]==0x3d && attributes._48[1]==0x4c &&
          attributes._48[2]==0xcc && attributes._48[3]==0xcd &&
          attributes._60[0]==0x3f && attributes._60[1]==0x80 &&
          attributes._60[4]==0x40 && attributes._B0[0]==0x41 &&
          attributes._B0[1]==0xa0 && attributes._F8[0]==0 && attributes._F8[7]==0,
          "Purin portable ABI values or padding changed");
    {
        static const std::uint32_t ids[5]={7,3,7,3,9};
        static const std::size_t parameter_counts[5]={3,3,3,5,5};
        static const float z[5]={0.00001f,0.145f,0.145f,0.145f,0.145f};
        const auto& bones=runtime->dynamics().bones;
        check(runtime->dynamics().active_bone_count==1 && bones.size()==5 &&
              runtime->dynamics().spheres.empty() &&
              !runtime->dynamics().animation_table_offset,
              "Purin authored five-row dynamics table was not retained");
        for(std::size_t i=0;i<5;++i)
            check(bones[i].bone_index==ids[i] && bones[i].parameters.size()==parameter_counts[i] &&
                  bones[i].position[0]==1.0f && bones[i].position[1]==1.0f &&
                  bones[i].position[2]==z[i], "Purin dynamics descriptor row changed");
    }
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:archive->public_symbols())if(symbol.name=="ftDataPurin")root=symbol.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataPurin root");
    const auto dynamics=archive->pointer(root+0x2c,20);
    check(dynamics.has_value(),"Purin dynamics root is missing");
    for(const auto active_count:{0U,2U}) {
        auto malformed=source;
        put32(malformed,0x20+*dynamics,active_count);
        rejects([&] {
            NativeDatArena rejected(std::make_shared<const DatArchive>(malformed));
            uint32_t mask=0;
            (void)melee_web_fighter_data_decode(rejected.reader(),root,15,5,
                identity.motion_count,nullptr,nullptr,nullptr,&mask);
        });
    }
    NativeDatArena owner(archive); uint32_t unresolved=UINT32_MAX;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,15,5,
        identity.motion_count,nullptr,nullptr,nullptr,&unresolved);
    check(data,"Purin native ftData was not decoded");
    const auto* wait_view=static_cast<const NativeFighterDataX28View*>(data);
    check(wait_view->squat_wait_choices,
          "Purin crouch Wait choices were not decoded");
    const auto* waits=static_cast<const PurinWaitChoiceView*>(wait_view->squat_wait_choices);
    check(waits[0].motion_id==31 && waits[0].weight==80 &&
          waits[1].motion_id==32 && waits[1].weight==20 &&
          waits[2].motion_id==-1 && waits[2].weight==-1,
          "Purin native crouch Wait choices changed");
    melee_web_test_purin_data(data);
    const auto* words=static_cast<const void* const*>(data);
    check(words[1],"Purin native extension was not allocated");
    const auto* decoded=static_cast<const MeleeWebPurinAttributes*>(words[1]);
    check(decoded->x2C==341 && decoded->specialn_vel.x==-0.13f &&
          decoded->specialn_vel.y==1.6f && decoded->_48[0]==0x3d &&
          decoded->_60[4]==0x40 && decoded->_B0[1]==0xa0 && decoded->_F8[7]==0,
          "Purin C decoder did not retain typed fields or padding");
    check((unresolved&(1U<<18))==0,
          "Purin custom-part registration root remains unresolved after decode");
    check((unresolved&(1U<<10))==0,
          "Purin crouch Wait choices remain unresolved after decode");
    for(unsigned slot=0;slot<6;++slot)
        check(!melee_web_fighter_data_article(data,15,slot),
              "Purin custom part was exposed as an Article");
    const auto* view=static_cast<const NativeFighterDataX48View*>(data);
    check(view->x48_items && !view->x48_items[0] && view->x48_items[1],
          "Purin custom-part slots were not retained");
    auto* parts=static_cast<PurinPartsView*>(view->x48_items[1]);
    check(parts->desc.model_num==1 && parts->desc.vis_table,
          "Purin custom-part descriptor is incomplete");
    for(unsigned costume=0;costume<5;++costume)
        for(unsigned category=0;category<4;++category)
            (void)parts->desc.vis_table[costume][category];
    char error[128]{};
    for(unsigned costume=1;costume<5;++costume) {
        std::fill(std::begin(error),std::end(error),char(0));
        check(melee_web_fighter_data_check_purin_part(data,costume,32,error,sizeof(error)),
              "Purin source visibility bounds rejected a valid costume");
    }
    check(!melee_web_fighter_data_check_purin_part(data,0,32,error,sizeof(error)) && error[0],
          "Purin costume-zero custom-part check was accepted");
    check(!melee_web_fighter_data_check_purin_part(data,1,0,error,sizeof(error)) && error[0],
          "Purin zero-DObj custom-part check was accepted");
    auto* table=parts->desc.vis_table;
    parts->desc.vis_table=nullptr;
    check(!melee_web_fighter_data_check_purin_part(data,1,32,error,sizeof(error)) && error[0],
          "Purin malformed visibility table was accepted");
    parts->desc.vis_table=table;
    const auto model_count=parts->desc.model_num;
    parts->desc.model_num=0;
    check(!melee_web_fighter_data_check_purin_part(data,1,32,error,sizeof(error)) && error[0],
          "Purin zero model count was accepted");
    parts->desc.model_num=model_count;
    auto* groups=static_cast<PurinCountedView*>(parts->desc.vis_table[1][0]);
    check(groups && groups[0].data,"Purin visibility group fixture is missing");
    const auto group_count=groups[0].count;
    groups[0].count=129;
    check(!melee_web_fighter_data_check_purin_part(data,1,32,error,sizeof(error)) && error[0],
          "Purin oversized visibility group was accepted");
    groups[0].count=group_count;
    auto* variants=static_cast<PurinCountedView*>(groups[0].data);
    check(variants && variants[0].data && variants[0].count>0,
          "Purin visibility variant fixture is missing");
    auto* indices=static_cast<std::uint8_t*>(variants[0].data);
    const auto old_index=indices[0]; indices[0]=32;
    check(!melee_web_fighter_data_check_purin_part(data,1,32,error,sizeof(error)) && error[0],
          "Purin out-of-range visibility index was accepted");
    indices[0]=old_index;
    std::cout<<"Native Purin 0x100 ABI, five-costume custom-part visibility, and no Article exposure: passed\n";
}
}
int main(int argc,char**argv) {
    try {
        auto fixture=native_fixture();
        verify(std::make_shared<const DatArchive>(fixture.file()),fixture.container,0);
        // Missing required pointers and malformed typed values fail in the C decoder
        // and unwind across its frames into the arena owner without publication.
        for(auto slot:{0U,4U,8U,0x30U,0x48U}) {
            auto bad=fixture;bad.unlink(slot);
            rejects([&]{NativeDatArena owner(std::make_shared<const DatArchive>(bad.file()));uint32_t mask;
                (void)melee_web_fighter_data_decode(owner.reader(),0,0,5,6,nullptr,nullptr,nullptr,&mask);});
        }
        for(auto at:{FighterFixture::co+0x5c,fixture.hurt_rows+12}) {
            auto bad=fixture;put32(bad.data,at,0x7f800000);
            rejects([&]{NativeDatArena owner(std::make_shared<const DatArchive>(bad.file()));uint32_t mask;
                (void)melee_web_fighter_data_decode(owner.reader(),0,0,5,6,nullptr,nullptr,nullptr,&mask);});
        }
        NativeDatArena owner(std::make_shared<const DatArchive>(fixture.file()));auto r=owner.reader();
        rejects([&]{(void)r->word(r->context,1);});rejects([&]{(void)r->half(r->context,1);});
        rejects([&]{(void)r->word(r->context,0);});
        rejects([&]{(void)r->allocate(r->context,SIZE_MAX,4);});
        rejects([&]{(void)r->region(r->context,0,FighterFixture::co+4);});
        std::cout<<"Owned native ftData synthetic and rejection checks: passed\n";
        if(argc==3)verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
        if(argc==5) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
        }
        if(argc==7) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
            verify_ganon(std::make_shared<const DatArchive>(read_file(argv[5])),read_file(argv[6]));
        }
        if(argc==9) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
            verify_ganon(std::make_shared<const DatArchive>(read_file(argv[5])),read_file(argv[6]));
            verify_captain(std::make_shared<const DatArchive>(read_file(argv[7])),read_file(argv[8]));
        }
        if(argc==13) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
            verify_ganon(std::make_shared<const DatArchive>(read_file(argv[5])),read_file(argv[6]));
            verify_captain(std::make_shared<const DatArchive>(read_file(argv[7])),read_file(argv[8]));
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[9])),read_file(argv[10]),
                          "PlyPikachu5K_Share_joint","ftDataPikachu",12,0x59,0x5a,0x51,5);
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[11])),read_file(argv[12]),
                          "PlyPichu5K_Share_joint","ftDataPichu",23,0x5b,0x5c,0x52,8);
        }
        if(argc==15) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
            verify_ganon(std::make_shared<const DatArchive>(read_file(argv[5])),read_file(argv[6]));
            verify_captain(std::make_shared<const DatArchive>(read_file(argv[7])),read_file(argv[8]));
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[9])),read_file(argv[10]),
                          "PlyPikachu5K_Share_joint","ftDataPikachu",12,0x59,0x5a,0x51,5);
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[11])),read_file(argv[12]),
                          "PlyPichu5K_Share_joint","ftDataPichu",23,0x5b,0x5c,0x52,8);
            verify_purin(read_file(argv[13]),read_file(argv[14]));
        }
        if(argc==17) {
            verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
            verify_roy(std::make_shared<const DatArchive>(read_file(argv[3])),read_file(argv[4]));
            verify_ganon(std::make_shared<const DatArchive>(read_file(argv[5])),read_file(argv[6]));
            verify_captain(std::make_shared<const DatArchive>(read_file(argv[7])),read_file(argv[8]));
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[9])),read_file(argv[10]),
                          "PlyPikachu5K_Share_joint","ftDataPikachu",12,0x59,0x5a,0x51,5);
            verify_pikachu(std::make_shared<const DatArchive>(read_file(argv[11])),read_file(argv[12]),
                          "PlyPichu5K_Share_joint","ftDataPichu",23,0x5b,0x5c,0x52,8);
            verify_purin(read_file(argv[13]),read_file(argv[14]));
            verify_donkey(read_file(argv[15]),read_file(argv[16]));
        }
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
