#include "dat_archive.hpp"
#include "dat_common.hpp"
#include "dat_native_joint.hpp"
#include "dat_lights.hpp"
#include "dat_collision.hpp"
#include "dat_item_registry_native.hpp"
#include "dat_effect_entries.hpp"
#include "gameplay_stage_numeric.h"
#include "gameplay_bonus_data.h"
#include "gameplay_match_context.h"
#include "gameplay_font_atlas.h"
#include "gameplay_ground_data.h"
#include "gameplay_common_context.h"
#include "gameplay_stage_context.h"
#include "gameplay_bootstrap.h"
#include "gameplay_fighter_assets.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
using namespace melee_web;
extern "C" void melee_web_fighter_link_gate(void);
extern "C" int melee_web_match_context_trace(MeleeWebCollision*,const MeleeWebMatchSettings*,uint32_t,char*,size_t);
namespace {
std::vector<uint8_t> bytes(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);auto n=f.tellg();
    if(n<=0||n>64*1024*1024)throw DatError("Invalid local asset: "+p.string());
    f.seekg(0);std::vector<uint8_t> out(static_cast<size_t>(n));
    if(!f.read(reinterpret_cast<char*>(out.data()),n))throw DatError("Truncated local asset");return out;
}
uint32_t symbol(const DatArchive& a,std::string_view name) {
    for(const auto& s:a.public_symbols())if(s.name==name)return s.data_offset;
    throw DatError("Required public symbol missing: "+std::string(name));
}
MeleeWebCollision* load_collision(const melee_web::DatCollision& data, int stage_kind, float scale)
{
    // The C boundary copies these typed arrays into its owned SDK allocation.
    // No native pointers or bitfields are overlaid onto archive bytes.
    std::vector<MeleeWebCollisionVertex> vertices;
    std::vector<MeleeWebCollisionLine> lines;
    std::vector<MeleeWebCollisionJoint> joints;
    for (const auto& v : data.vertices) vertices.push_back({v.x, v.y});
    for (const auto& l : data.lines)
        lines.push_back({l.v0, l.v1, l.prev0, l.next0, l.prev1, l.next1, l.hi_flags, l.lo_flags});
    for (const auto& j : data.joints) {
        MeleeWebCollisionJoint value{};
        for (std::size_t k = 0; k < 5; ++k)
            value.ranges[k] = {j.line_ranges[k].start, j.line_ranges[k].count};
        value.left = j.left; value.right = j.right; value.bottom = j.bottom; value.top = j.top;
        value.vertices = {j.vertices.start, j.vertices.count};
        joints.push_back(value);
    }
    MeleeWebCollisionInput input{};
    input.vertices = vertices.data(); input.vertex_count = vertices.size();
    input.lines = lines.data(); input.line_count = lines.size();
    input.joints = joints.data(); input.joint_count = joints.size();
    input.stage_kind = stage_kind; input.stage_scale = scale;
    input.source_reserved_2c = data.source_reserved_2c;
    for (std::size_t k = 0; k < 5; ++k)
        input.ranges[k] = {data.line_ranges[k].start, data.line_ranges[k].count};
    char error[256];
    MeleeWebCollision* owner=melee_web_collision_create(&input, error, sizeof(error));
    if (!owner) throw std::runtime_error(error);
    return owner;
}
void check(int ok,const char* error){if(!ok)throw DatError(error);}
}
int main(int argc,char** argv) {
    try {
        melee_web_fighter_link_gate();
        if(argc!=2)throw DatError("Usage: fighter_runtime_probe.js LOCAL_ASSET_DIRECTORY");
        const std::filesystem::path dir=argv[1];
        auto common_archive=std::make_shared<const DatArchive>(bytes(dir/"PlCo.dat"));
        auto stage_archive=std::make_shared<const DatArchive>(bytes(dir/"GrNLa.dat"));
        auto fighter_archive=std::make_shared<const DatArchive>(bytes(dir/"PlMr.dat"));
        auto costume_archive=std::make_shared<const DatArchive>(bytes(dir/"PlMrNr.dat"));
        auto animation=bytes(dir/"PlMrAJ.dat");
        auto item_archive=std::make_shared<const DatArchive>(bytes(dir/"ItCo.usd"),DatExternalPolicy::PreserveUnresolved);
        auto effect_archive=std::make_shared<const DatArchive>(bytes(dir/"EfMrData.dat"));
        auto font_bytes=bytes(dir/"sislib_font.bin");
        auto bonus_archive=std::make_shared<const DatArchive>(bytes(dir/"PdPm.dat"));
        NativeDatArena bonus_owner(bonus_archive);
        auto* bonus=melee_web_bonus_data_decode(bonus_owner.reader(),symbol(*bonus_archive,"plLoadCommonData"));
        const std::vector<std::pair<std::string,std::shared_ptr<const DatArchive>>> archives{
            {"common",common_archive},{"stage",stage_archive},{"fighter",fighter_archive},
            {"costume",costume_archive},{"item",item_archive},{"effect",effect_archive},{"bonus",bonus_archive}};
        std::vector<std::vector<uint8_t>> original_bytes;
        for(const auto& [name,archive]:archives)
            original_bytes.emplace_back(archive->data().begin(),archive->data().end());
        const auto check_archives=[&]{
            for(size_t i=0;i<archives.size();++i){
                auto actual=archives[i].second->data();
                auto different=std::mismatch(actual.begin(),actual.end(),original_bytes[i].begin());
                if(different.first!=actual.end())
                    throw DatError("Source mutated immutable "+archives[i].first+" archive at data offset "+std::to_string(different.first-actual.begin()));
            }
        };
        DatItemRegistryNative items(item_archive);
        const DatCollision collision_data(*stage_archive);
        const float stage_scale=read_dat_stage_scale(*stage_archive);
        const DatCommon common(*common_archive);
        if(!common.roots[20].data_offset)throw DatError("Missing common root20");
        DatNativeJoint common_joint(common_archive,*common.roots[20].data_offset);
        DatLights lights(*stage_archive);
        NativeDatArena stage_owner(stage_archive);
        void* ground=melee_web_ground_data_decode(stage_owner.reader(),symbol(*stage_archive,"grGroundParam"));
        auto* markers=melee_web_stage_markers_decode(stage_owner.reader(),symbol(*stage_archive,"map_head"));
        const FighterCostume* mario=nullptr;
        for(const auto& c:fighter_costumes())if(c.fighter_kind==0&&c.costume_index==0)mario=&c;
        if(!mario)throw DatError("Pinned Mario registry missing");
        char error[256];
        auto* font=melee_web_font_atlas_register(font_bytes.data(),font_bytes.size(),error,sizeof(error));check(font!=nullptr,error);
        for(unsigned pass=0;pass<2;pass++) {
            check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
            auto* context=melee_web_common_context_create(&common.scalars,&common.tables,&common_joint.graph(),error,sizeof(error));
            check(context!=nullptr,error);check(melee_web_common_context_attach(context,error,sizeof(error)),error);
            auto* light_context=melee_web_stage_lights_create(lights.lights.data(),lights.lights.size(),error,sizeof(error));
            check(light_context!=nullptr,error);
            for(uint32_t i=0;i<lights.lights.size();i++) {
                auto flags=read_dat_light_override(*stage_archive,lights.lights[i].source_offset);
                check(melee_web_stage_lights_set_override(light_context,i,flags.has_value(),flags.value_or(0),error,sizeof(error)),error);
            }
            check(melee_web_stage_lights_attach(light_context,error,sizeof(error)),error);
            void* previous=melee_web_ground_data_publish(ground);
            auto* numeric=melee_web_stage_numeric_begin(markers,error,sizeof(error));check(numeric!=nullptr,error);
            float camera[4],blast[4],offset[2];check(melee_web_stage_numeric_bounds(numeric,camera,blast,offset,error,sizeof(error)),error);
            std::cout<<"Source stage blast bounds "<<blast[0]<<", "<<blast[1]<<", "<<blast[2]<<", "<<blast[3]<<'\n';
            auto* collision=load_collision(collision_data,37,stage_scale);
            check(melee_web_common_context_initialize_fighters(context,error,sizeof(error)),error);
            {
                GameplayFighterAssets assets(fighter_archive,costume_archive,animation,*mario);
                DatEffectEntries effects(effect_archive,"effMarioDataTable",1,2);
                check(effects.load(error,sizeof(error)),error);
                auto* registry=melee_web_item_registry_begin(items.articles(),MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error));check(registry!=nullptr,error);
                std::cout<<"Original fighter globals initialized; owned Mario assets published; unresolved mask "
                         <<std::hex<<assets.unresolved_fields()<<std::dec<<'\n';
                check(melee_web_bonus_data_begin(bonus,error,sizeof(error)),error);
                MeleeWebCollisionFloorResult floor;
                check(melee_web_collision_floor(collision,collision_data.line_ranges[0].start,0,20,&floor,error,sizeof(error)),error);
                check(floor.line>=0,"No source floor found at spawn x=0");
                MeleeWebMatchSettings settings{};settings.player={0,0,4,{0,20+floor.displacement_y+1,0},1};
                settings.camera_subjects=70;settings.random_seed=0x13579bdf;
                std::cout<<"Calling original player/Fighter_Create from source floor line "<<floor.line<<std::endl;
                check(melee_web_match_context_trace(collision,&settings,120,error,sizeof(error)),error);
                check(melee_web_bonus_data_end(bonus,error,sizeof(error)),error);
                check(melee_web_item_registry_end(registry,error,sizeof(error)),error);
                check(effects.detach(error,sizeof(error)),error);
                assets.close();
                check_archives();
            }
            check(melee_web_collision_destroy(collision,error,sizeof(error)),error);
            check(melee_web_stage_numeric_end(numeric,error,sizeof(error)),error);
            check(melee_web_common_context_destroy(context,error,sizeof(error)),error);
            check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
            check_archives();
            melee_web_ground_data_publish(previous);
            check(melee_web_stage_lights_destroy(light_context,error,sizeof(error)),error);
        }
        check(melee_web_font_atlas_close(font,error,sizeof(error)),error);
        std::cout<<"Original Fighter_Create, 120 neutral ticks, unload and restart completed in two worlds\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
