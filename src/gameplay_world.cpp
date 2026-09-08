#include "gameplay_world.hpp"
#include <cstdio>
#include <cstdlib>
#include "dat_archive.hpp"
#include "dat_common.hpp"
#include "dat_color_animation.hpp"
#include "dat_native_joint.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_stage.hpp"
#include "dat_native_stage.hpp"
#include "gameplay_stage_last.h"
#include "gameplay_stage_visual.h"
#include "gameplay_effect_runtime.h"
#include "dat_lights.hpp"
#include "dat_collision.hpp"
#include "dat_item_registry_native.hpp"
#include "dat_effect_entries.hpp"
#include "gameplay_stage_numeric.h"
#include "gameplay_bonus_data.h"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_item_runtime.h"
#include "gameplay_font_atlas.h"
#include "gameplay_ground_data.h"
#include "gameplay_common_context.h"
#include "gameplay_crowd.h"
#include "gameplay_stage_context.h"
#include "gameplay_bootstrap.h"
#include "gameplay_fighter_assets.hpp"
#include <iostream>
#include <algorithm>
using namespace melee_web;
namespace {
void check(int ok,const char* error){if(!ok)throw DatError(error);}
const std::vector<uint8_t>& file(const RuntimeFiles& files,std::string_view name){
    auto found=files.find(name);
    if(found==files.end()||found->second.empty()||found->second.size()>DatArchive::max_archive_bytes)
        throw DatError("Missing or invalid runtime file: "+std::string(name));
    return found->second;
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
}
namespace melee_web {
struct GameplayWorld::Storage {
    std::map<std::string,std::shared_ptr<const DatArchive>,std::less<>> archives;
    std::map<std::string,std::vector<uint8_t>,std::less<>> snapshots;
    std::unique_ptr<NativeDatArena> stage_arena,bonus_arena,item_arena;
    std::unique_ptr<DatItemRegistryNative> items;
    std::unique_ptr<GameplayFighterAssets> fighter;
    std::unique_ptr<DatEffectEntries> effects,common_effects;
    std::unique_ptr<DatColorAnimation> common_colors,extra_colors,item_colors;
    MeleeWebItemRuntime* item_runtime=nullptr;
    std::unique_ptr<DatNativeJoint> stage_model;
    std::unique_ptr<DatNativeAnimation> stage_animation;
    std::unique_ptr<DatMaterialAnimation> stage_material_animation;
    MeleeWebNativeJoint* stage_native=nullptr;
    MeleeWebNativeJoint* respawn_native=nullptr;
    std::unique_ptr<DatNativeAnimation> respawn_animation;
    MeleeWebStageVisual* stage_visual=nullptr;
    std::unique_ptr<DatNativeStage> full_stage;
    std::unique_ptr<DatEffectBanks> stage_effects;
    MeleeWebStageMap* stage_map=nullptr;
    MeleeWebStageLast* stage_last=nullptr;
    MeleeWebMatchRules* rules=nullptr;
    MeleeWebFontAtlas* font=nullptr;
    MeleeWebCommonContext* common=nullptr;
    MeleeWebStageLights* lights=nullptr;
    MeleeWebStageNumeric* numeric=nullptr;
    MeleeWebCollision* collision=nullptr;
    MeleeWebItemRegistry* registry=nullptr;
    MeleeWebBonusData* bonus=nullptr;
    void* previous_ground=nullptr;
    bool effect_started=false;
    bool started=false,ground_published=false,bonus_published=false;
    int floor_start=0;
    char error[256]{};
    std::shared_ptr<const DatArchive> archive(std::string_view name)const{return archives.at(std::string(name));}
    void start(const RuntimeFiles& files){
        for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat"}){
            auto value=std::make_shared<const DatArchive>(file(files,name),
                std::string_view(name)=="ItCo.usd"?DatExternalPolicy::PreserveUnresolved:DatExternalPolicy::Reject);
            snapshots[name]={value->data().begin(),value->data().end()};archives.emplace(name,std::move(value));
        }
        // Decode before acquiring the source world whenever possible.
        DatCommon common_data(*archive("PlCo.dat"));
        if(!common_data.roots[20].data_offset)throw DatError("Missing common root20");
        DatNativeJoint common_joint(archive("PlCo.dat"),*common_data.roots[20].data_offset);
        DatCollision collision_data(*archive("GrNLa.dat"));
        DatLights light_data(*archive("GrNLa.dat"));
        items=std::make_unique<DatItemRegistryNative>(archive("ItCo.usd"));
        stage_arena=std::make_unique<NativeDatArena>(archive("GrNLa.dat"));
        bonus_arena=std::make_unique<NativeDatArena>(archive("PdPm.dat"));
        auto* ground=melee_web_ground_data_decode(stage_arena->reader(),symbol(*archive("GrNLa.dat"),"grGroundParam"));
        auto* markers=melee_web_stage_markers_decode(stage_arena->reader(),symbol(*archive("GrNLa.dat"),"map_head"));
        bonus=melee_web_bonus_data_decode(bonus_arena->reader(),symbol(*archive("PdPm.dat"),"plLoadCommonData"));
        const auto& font_bytes=file(files,"sislib_font.bin");
        const auto& animations=file(files,"PlMrAJ.dat");
        font=melee_web_font_atlas_register(font_bytes.data(),font_bytes.size(),error,sizeof(error));check(font!=nullptr,error);
        check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);started=true;
        rules=melee_web_match_rules_begin(error,sizeof(error));check(rules!=nullptr,error);
        common=melee_web_common_context_create(&common_data.scalars,&common_data.tables,&common_joint.graph(),error,sizeof(error));
        check(common!=nullptr,error);
        if(!common_data.roots[6].data_offset||!common_data.roots[7].data_offset)throw DatError("Common color animation tables absent");
        common_colors=std::make_unique<DatColorAnimation>(archive("PlCo.dat"),*common_data.roots[6].data_offset,123);
        extra_colors=std::make_unique<DatColorAnimation>(archive("PlCo.dat"),*common_data.roots[7].data_offset,6);
        check(melee_web_common_context_set_color_tables(common,common_colors->table(),extra_colors->table(),error,sizeof(error)),error);
        if(!common_data.roots[8].data_offset)throw DatError("Common respawn table absent");
        const auto& co=*archive("PlCo.dat");const auto respawn=*common_data.roots[8].data_offset;
        auto joint_offset=co.pointer(respawn,64),animation_offset=co.pointer(respawn+4,20);
        if(!joint_offset||!animation_offset)throw DatError("Incomplete common respawn descriptors");
        DatNativeJoint respawn_model(archive("PlCo.dat"),*joint_offset);
        respawn_native=melee_web_native_joint_hydrate(&respawn_model.graph(),error,sizeof(error));check(respawn_native!=nullptr,error);
        respawn_animation=std::make_unique<DatNativeAnimation>(archive("PlCo.dat"),*animation_offset,respawn_model.graph());
        check(melee_web_common_context_set_respawn(common,melee_web_native_joint_descriptor(respawn_native,error,sizeof(error)),respawn_animation->descriptor(),error,sizeof(error)),error);
        check(melee_web_common_context_attach(common,error,sizeof(error)),error);
        lights=melee_web_stage_lights_create(light_data.lights.data(),light_data.lights.size(),error,sizeof(error));check(lights!=nullptr,error);
        for(uint32_t i=0;i<light_data.lights.size();i++){
            auto flags=read_dat_light_override(*archive("GrNLa.dat"),light_data.lights[i].source_offset);
            check(melee_web_stage_lights_set_override(lights,i,flags.has_value(),flags.value_or(0),error,sizeof(error)),error);
        }
        check(melee_web_stage_lights_attach(lights,error,sizeof(error)),error);
        previous_ground=melee_web_ground_data_publish(ground);ground_published=true;
        numeric=melee_web_stage_numeric_begin(markers,error,sizeof(error));check(numeric!=nullptr,error);
        collision=load_collision(collision_data,37,read_dat_stage_scale(*archive("GrNLa.dat")));
        floor_start=collision_data.line_ranges[0].start;
        check(melee_web_common_context_initialize_fighters(common,error,sizeof(error)),error);
        const FighterCostume* mario=nullptr;
        for(const auto& costume:fighter_costumes())if(costume.fighter_kind==0&&costume.costume_index==0)mario=&costume;
        if(!mario)throw DatError("Pinned Mario identity missing");
        fighter=std::make_unique<GameplayFighterAssets>(archive("PlMr.dat"),archive("PlMrNr.dat"),animations,*mario);
        check(melee_web_effect_runtime_begin(error,sizeof(error)),error);effect_started=true;
        common_effects=std::make_unique<DatEffectEntries>(archive("EfCoData.dat"),"effCommonDataTable",0,47,true);
        check(common_effects->load(error,sizeof(error)),error);
        effects=std::make_unique<DatEffectEntries>(archive("EfMrData.dat"),"effMarioDataTable",1,2);
        check(effects->load(error,sizeof(error)),error);
        registry=melee_web_item_registry_begin(items->articles(),MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error));check(registry!=nullptr,error);
        item_arena=std::make_unique<NativeDatArena>(archive("ItCo.usd"));
        const auto& it=*archive("ItCo.usd");const auto item_root=symbol(it,"itPublicData");
        auto common_root=it.pointer(item_root,0x160),bounce_root=it.pointer(item_root+16,0x1c),color_root=it.pointer(item_root+20,8);
        if(!common_root||!bounce_root||!color_root)throw DatError("Incomplete original item service data");
        const size_t color_bytes=it.next_target_offset(*color_root)-*color_root;
        if(color_bytes%8||color_bytes/8>256)throw DatError("Invalid item color table extent");
        item_colors=std::make_unique<DatColorAnimation>(archive("ItCo.usd"),*color_root,color_bytes/8);
        void* common_item=melee_web_item_common_decode(item_arena->reader(),*common_root);
        void* bounce=melee_web_item_bounce_decode(item_arena->reader(),*bounce_root);
        item_runtime=melee_web_item_runtime_begin(common_item,bounce,item_colors->table(),color_bytes/8,error,sizeof(error));check(item_runtime!=nullptr,error);
        check(melee_web_bonus_data_begin(bonus,error,sizeof(error)),error);bonus_published=true;
    }
    void enable_stage_visual(){
        if(stage_visual)return;
        check(melee_web_stage_lights_load(lights,error,sizeof(error)),error);
        auto source=archive("GrNLa.dat");
        DatStage metadata(*source);
        const auto& entry=metadata.entries.at(3);
        if(!entry.joint_offset)throw DatError("Final Destination map entry3 has no model");
        stage_model=std::make_unique<DatNativeJoint>(source,*entry.joint_offset);
        if(entry.joint_animation_table){
            auto root=source->pointer(*entry.joint_animation_table);
            if(root)stage_animation=std::make_unique<DatNativeAnimation>(source,*root,stage_model->graph());
        }
        if(entry.material_animation_table){
            auto root=source->pointer(*entry.material_animation_table);
            if(root)stage_material_animation=std::make_unique<DatMaterialAnimation>(source,*root,stage_model->graph());
        }
        stage_native=melee_web_native_joint_create(&stage_model->graph(),error,sizeof(error));check(stage_native!=nullptr,error);
        stage_visual=melee_web_stage_visual_begin(stage_native,stage_animation?stage_animation->descriptor():nullptr,stage_material_animation?stage_material_animation->descriptor():nullptr,3,1,error,sizeof(error));check(stage_visual!=nullptr,error);
    }
    void enable_full_stage(){
        if(stage_last)return;
        if(stage_visual)throw DatError("Close selected stage visual before full initialization");
        check(melee_web_stage_lights_load(lights,error,sizeof(error)),error);
        auto source=archive("GrNLa.dat");
        full_stage=std::make_unique<DatNativeStage>(source);
        stage_effects=std::make_unique<DatEffectBanks>(source,"map_ptcl","map_texg",64);
        check(melee_web_effect_bank_attach(stage_effects->bank(),error,sizeof(error)),error);
        stage_map=melee_web_stage_map_publish(full_stage->map_head(),error,sizeof(error));check(stage_map!=nullptr,error);
        const auto& overrides=full_stage->light_overrides();
        check(melee_web_stage_map_set_overrides(stage_map,overrides.data(),overrides.size(),error,sizeof(error)),error);
        stage_last=melee_web_stage_last_begin(full_stage->yakumono(),stage_effects->bank(),error,sizeof(error));check(stage_last!=nullptr,error);
    }
    void end_stage(){
        if(stage_last){check(melee_web_stage_last_end(stage_last,error,sizeof(error)),error);stage_last=nullptr;}
        if(stage_visual){check(melee_web_stage_visual_end(stage_visual,error,sizeof(error)),error);stage_visual=nullptr;}
    }
    void verify()const{
        for(const auto& [name,source]:archives){
            const auto& expected=snapshots.at(name);auto actual=source->data();
            if(!std::equal(actual.begin(),actual.end(),expected.begin(),expected.end()))
                throw DatError("Original source mutated immutable archive: "+name);
        }
    }
    void close(){
        if(fighter&&fighter->live_fighters())throw DatError("Close all fighter/render contexts before the runtime world");
        end_stage();
        check(melee_web_crowd_end(error,sizeof(error)),error);
        if(item_runtime){check(melee_web_item_runtime_end(item_runtime,error,sizeof(error)),error);item_runtime=nullptr;}
        item_colors.reset();item_arena.reset();
        if(effect_started){check(melee_web_effect_runtime_end(error,sizeof(error)),error);effect_started=false;}
        if(stage_map){check(melee_web_stage_map_close(stage_map,error,sizeof(error)),error);stage_map=nullptr;}
        stage_effects.reset();full_stage.reset();
        if(stage_native){check(melee_web_native_joint_destroy(stage_native,error,sizeof(error)),error);stage_native=nullptr;}
        stage_material_animation.reset();stage_animation.reset();stage_model.reset();
        if(bonus_published){check(melee_web_bonus_data_end(bonus,error,sizeof(error)),error);bonus_published=false;}
        if(registry){check(melee_web_item_registry_end(registry,error,sizeof(error)),error);registry=nullptr;}
        if(effects){check(effects->detach(error,sizeof(error)),error);effects.reset();}
        if(common_effects){check(common_effects->detach(error,sizeof(error)),error);common_effects.reset();}
        if(fighter){fighter->close();fighter.reset();}
        if(collision){check(melee_web_collision_destroy(collision,error,sizeof(error)),error);collision=nullptr;}
        if(numeric){check(melee_web_stage_numeric_end(numeric,error,sizeof(error)),error);numeric=nullptr;}
        if(common){check(melee_web_common_context_destroy(common,error,sizeof(error)),error);common=nullptr;}
        if(respawn_native){check(melee_web_native_joint_destroy(respawn_native,error,sizeof(error)),error);respawn_native=nullptr;}
        respawn_animation.reset();extra_colors.reset();common_colors.reset();
        if(rules){check(melee_web_match_rules_end(rules,error,sizeof(error)),error);rules=nullptr;}
        if(started){check(melee_web_gameplay_shutdown(error,sizeof(error)),error);started=false;}
        if(ground_published){melee_web_ground_data_publish(previous_ground);ground_published=false;}
        if(lights){check(melee_web_stage_lights_destroy(lights,error,sizeof(error)),error);lights=nullptr;}
        if(font){check(melee_web_font_atlas_close(font,error,sizeof(error)),error);font=nullptr;}
        verify();
    }
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Runtime teardown: %s\n",e.what());std::abort();}}
};
GameplayWorld::GameplayWorld(const RuntimeFiles& files):storage_(std::make_unique<Storage>()){storage_->start(files);}
GameplayWorld::~GameplayWorld()=default;
void GameplayWorld::enable_stage_visual(){storage_->enable_stage_visual();}
void GameplayWorld::enable_full_stage(){storage_->enable_full_stage();}
void GameplayWorld::end_stage(){storage_->end_stage();}
void GameplayWorld::close(){storage_->close();}
MeleeWebCollision* GameplayWorld::collision()const{return storage_->collision;}
float GameplayWorld::floor_height(float x)const{
    MeleeWebCollisionFloorResult floor;char error[256];
    check(melee_web_collision_floor(storage_->collision,storage_->floor_start,x,20,&floor,error,sizeof(error)),error);
    if(floor.line<0)throw DatError("No source floor at requested position");return 20+floor.displacement_y;
}
uint32_t GameplayWorld::unresolved_fighter_fields()const{return storage_->fighter?storage_->fighter->unresolved_fields():0;}
void GameplayWorld::verify_immutable_archives()const{storage_->verify();}
}
