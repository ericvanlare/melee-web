#include "native_dat.hpp"
#include "gameplay_fighter_data.h"
#include "gameplay_pikachu_schema.h"
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
    check(data && !melee_web_fighter_data_article(data,0) && !melee_web_fighter_data_article(data,5),
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
    check(data && !melee_web_fighter_data_article(data,0) &&
          !melee_web_fighter_data_article(data,5),
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
        void* article=melee_web_fighter_data_article(data,slot);
        check(article && melee_web_article_unresolved(article)==
              ((1U<<1)|(1U<<3)|(1U<<4)),
              "Pikachu-family Article registration identity is incomplete");
    }
    check((unresolved&(1U<<18))==0,
          "Pikachu-family Article registration roots remain unresolved");
    std::cout<<"Native "<<(kind==12?"Pikachu":"Pichu")<<
        " shared 0xf8 attributes and three Article registrations: passed\n";
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
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
