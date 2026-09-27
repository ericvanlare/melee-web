#include "gameplay_world.hpp"
#include "runtime_archive_cache.hpp"
#include "gameplay_content.h"
#include <cstdio>
#include <cstdlib>
#include "dat_archive.hpp"
#include "dat_common.hpp"
#include "dat_color_animation.hpp"
#include "dat_native_joint.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_stage.hpp"
#include "dat_trophy_data.hpp"
#include "dat_native_stage.hpp"
#include "dat_scene.hpp"
#include "gameplay_stage_last.h"
#include "gameplay_stage_profile.h"
#include "gameplay_stage_visual.h"
#include "gameplay_effect_runtime.h"
#include "dat_lights.hpp"
#include "dat_collision.hpp"
#include "dat_item_article.hpp"
#include "dat_item_registry.hpp"
#include "dat_item_registry_native.hpp"
#include "dat_stage_items.hpp"
#include "dat_effect_entries.hpp"
#include "gameplay_stage_numeric.h"
#include "gameplay_bonus_data.h"
#include "gameplay_rumble.h"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_render.h"
#include "gameplay_menu.h"
#include "gameplay_item_runtime.h"
#include "gameplay_archive_sections.h"
#include "gameplay_source_files_runtime.hpp"
#include "gameplay_stage_items.h"
#include "gameplay_font_atlas.h"
#include "gameplay_ground_data.h"
#include "gameplay_common_context.h"
#include "gameplay_crowd.h"
#include "gameplay_stage_context.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
extern "C" {
#include <sysdolphin/baselib/sislib.h>
#include <melee/lb/lbarchive.h>
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#include <melee/ty/types.h>
#include "gameplay_fighter_assets.hpp"
#include <iostream>
#include <algorithm>
#include <cstddef>
#include <set>
extern "C" void gm_801A4BD4(void);
extern "C" void lbRefract_800222A4(void);
extern "C" void Player_80036DD8(void);
using namespace melee_web;
static_assert(It_Kind_Kuriboh==MELEE_WEB_ITEM_REGISTRY_FIRST_KIND);
static_assert(It_PKind_Random-It_Kind_Kuriboh==MELEE_WEB_ITEM_REGISTRY_COUNT-1);
namespace {
int start_vs_scene_manager(char* error, size_t size)
{
    if (!melee_web_native_world_prepare_vs_manager(error, size)) return 0;
    HSD_SisLib_803A6048(0x4800);
    gm_801A4BD4();
    if (error && size) error[0] = '\0';
    return 1;
}
void stop_vs_sis(void) { HSD_SisLib_803A5FBC(); }
void check(int ok,const char* error){if(!ok)throw DatError(error);}
std::string_view melee_web_runtime_file_name_impl(const RuntimeFiles& files,
                                                  std::string_view authored_name,
                                                  DatMenuSupportLanguage setting_language,
                                                  DatMenuSupportLanguage saved_language)
{
    const auto resolved=DatMenuSupport::resolve_filename(
        authored_name,setting_language,saved_language);
    const auto found=files.find(resolved);
    return found==files.end()?std::string_view{}:found->first;
}
const std::vector<uint8_t>& file(const RuntimeFiles& files,std::string_view name){
    const auto resolved=melee_web_runtime_file_name_impl(
        files,name,DatMenuSupportLanguage::English,DatMenuSupportLanguage::English);
    auto found=resolved.empty()?files.end():files.find(resolved);
    if(found==files.end()||found->second.empty()||found->second.size()>DatArchive::max_archive_bytes)
        throw DatError("Missing or invalid runtime file: "+std::string(name));
    return found->second;
}
uint32_t symbol(const DatArchive& a,std::string_view name) {
    for(const auto& s:a.public_symbols())if(s.name==name)return s.data_offset;
    throw DatError("Required public symbol missing: "+std::string(name));
}
bool has_symbol(const DatArchive& a,std::string_view name) {
    for(const auto& s:a.public_symbols())if(s.name==name)return true;
    return false;
}
#pragma pack(push,4)
struct SourceRefractData {
    uint8_t count;
    uint8_t padding[3];
    float* parameters;
};
#pragma pack(pop)
static_assert(offsetof(SourceRefractData,parameters)==4);
MeleeWebCollision* own_collision(const melee_web::DatCollision& data,
                                 int stage_kind, float scale,
                                 bool already_loaded)
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
    MeleeWebCollision* owner=already_loaded?
        melee_web_collision_adopt_loaded(&input,error,sizeof(error)):
        melee_web_collision_create(&input, error, sizeof(error));
    if (!owner) throw std::runtime_error(error);
    return owner;
}
MeleeWebCollision* load_collision(const melee_web::DatCollision& data,
                                  int stage_kind, float scale)
{
    return own_collision(data,stage_kind,scale,false);
}
MeleeWebCollision* adopt_collision(const melee_web::DatCollision& data,
                                   int stage_kind, float scale)
{
    return own_collision(data,stage_kind,scale,true);
}
}
namespace melee_web {
std::string_view melee_web_runtime_file_name(const RuntimeFiles& files,
                                             std::string_view authored_name,
                                             DatMenuSupportLanguage setting_language,
                                             DatMenuSupportLanguage saved_language)
{
    return melee_web_runtime_file_name_impl(files,authored_name,
                                            setting_language,saved_language);
}
struct GameplayWorld::Storage {
    std::map<std::string,std::shared_ptr<const DatArchive>,std::less<>> archives;
    std::map<std::string,std::vector<uint8_t>,std::less<>> snapshots;
    RuntimeArchiveCache* archive_cache=nullptr;
    std::unique_ptr<NativeDatArena> stage_arena,bonus_arena,item_arena,rumble_arena;
    std::unique_ptr<DatItemRegistryNative> items;
    std::unique_ptr<DatItemArticle> random_item_article;
    std::unique_ptr<DatStageItems> stage_items;
    MeleeWebStageItems* stage_item_scope=nullptr;
    std::map<unsigned,std::unique_ptr<GameplayFighterAssets>> fighters;
    const MeleeWebStageContent* stage=nullptr;
    GameplayWorldPurpose purpose=GameplayWorldPurpose::Match;
    std::vector<std::unique_ptr<DatEffectEntries>> effects;
    std::unique_ptr<DatEffectEntries> common_effects;
    std::unique_ptr<DatColorAnimation> common_colors,extra_colors,item_colors;
    MeleeWebItemRuntime* item_runtime=nullptr;
    MeleeWebArchiveSections* item_scope=nullptr;
    std::unique_ptr<DatNativeJoint> stage_model;
    std::unique_ptr<DatNativeAnimation> stage_animation;
    std::unique_ptr<DatMaterialAnimation> stage_material_animation;
    MeleeWebNativeJoint* stage_native=nullptr;
    // Descriptor-only owner for Fighter_804D6514, the original EntryStart
    // accessory. The common context borrows its HSD_Joint descriptor; this
    // handle therefore outlives publication and is released after common
    // globals have been restored.
    MeleeWebNativeJoint* root16_native=nullptr;
    MeleeWebNativeJoint* respawn_native=nullptr;
    std::unique_ptr<DatNativeAnimation> respawn_animation;
    MeleeWebStageVisual* stage_visual=nullptr;
    std::unique_ptr<DatNativeStage> full_stage;
    std::unique_ptr<DatScene> quake_model;
    std::unique_ptr<DatTrophyData> trophy_data;
    std::vector<TrophyData> trophy_models,trophy_models_d;
    std::vector<ToyNameData> trophy_names;
    std::vector<TyDspEntry> trophy_display,trophy_display_us;
    MeleeWebArchiveSections* trophy_scope=nullptr;
    std::unique_ptr<DatEffectBanks> stage_effects;
    MeleeWebStageMap* stage_map=nullptr;
    MeleeWebStageLast* stage_last=nullptr;
    MeleeWebMatchRules* rules=nullptr;
    MeleeWebMatchContext* match_context=nullptr;
    MeleeWebRender* render_context=nullptr;
    MeleeWebFontAtlas* font=nullptr;
    MeleeWebCommonContext* common=nullptr;
    MeleeWebArchiveSections* common_scope=nullptr;
    HSD_Archive* common_source_archive=nullptr;
    MeleeWebStageLights* lights=nullptr;
    MeleeWebStageNumeric* numeric=nullptr;
    MeleeWebCollision* collision=nullptr;
    std::unique_ptr<DatCollision> collision_data;
    bool collision_deferred=false;
    bool source_ordered=false;
    const StartMeleeData* source_start_data=nullptr;
    MeleeWebItemRegistry* registry=nullptr;
    MeleeWebBonusData* bonus=nullptr;
    MeleeWebRumble* rumble=nullptr;
    MeleeWebArchiveSections* rumble_scope=nullptr;
    MeleeWebArchiveSections* refract_scope=nullptr;
    MeleeWebArchiveSections* bonus_scope=nullptr;
    MeleeWebSourceFileScope* source_files=nullptr;
    std::vector<float> refract_parameters;
    SourceRefractData refract_data{};
    void* previous_ground=nullptr;
    bool effect_started=false;
    bool started=false,ground_published=false,bonus_published=false;
    bool rumble_published=false;
    const RuntimeFiles* runtime_files=nullptr;
    std::map<unsigned,const FighterCostume*> identities;
    std::map<unsigned,std::set<unsigned>> selected_costumes;
    std::vector<unsigned> identity_order;
    size_t fighter_at=0;
    bool fighter_assets_ready=false;
    unsigned construction_phase=0;
    int floor_start=0;
    char error[256]{};
    std::shared_ptr<const DatArchive> archive(std::string_view name)const{return archives.at(std::string(name));}
    static void** load_common_source(void* context){
        auto& self=*static_cast<Storage*>(context);
        check(self.common_scope&&!self.common_source_archive,
              "Original common archive requires its fresh typed owner");
        void** roots=nullptr;
        self.common_source_archive=lbArchive_LoadSymbols(
            "PlCo.dat",&roots,"ftLoadCommonData",0);
        check(self.common_source_archive&&roots,
              "Original common archive load did not publish its roots");
        return roots;
    }
    void start(const RuntimeFiles& files,const GameplayWorldSelection& selection,
               RuntimeArchiveCache* cache,bool defer=false,bool source_ordered=false){
        if(selection.player_count < MELEE_WEB_MENU_MIN_PLAYERS ||
           selection.player_count > MELEE_WEB_MENU_MAX_PLAYERS)
            throw DatError("Gameplay world requires two through four active fighters");
        runtime_files=&files;
        archive_cache=cache;
        collision_deferred=source_ordered;
        this->source_ordered=source_ordered;
        purpose=selection.purpose;
        source_start_data=source_ordered&&selection.begin_source_match?
            selection.source_start_data:nullptr;
        if(purpose==GameplayWorldPurpose::Match){
            stage=melee_web_stage_content_by_ground(selection.ground_kind);
            if(!stage)throw DatError("No runtime owner for selected source ground kind");
        }
        auto load=[&](std::string_view name,DatExternalPolicy policy=DatExternalPolicy::Reject){
            if(archives.contains(name))return;
            const auto resolved=melee_web_runtime_file_name(
                files,name,DatMenuSupportLanguage::English,DatMenuSupportLanguage::English);
            if(resolved.empty())throw DatError("Missing runtime file: "+std::string(name));
            if(name=="ItCo.usd")policy=DatExternalPolicy::PreserveUnresolved;
            else if(stage&&name==stage->archive)policy=DatExternalPolicy::ResolveNull;
            auto value=archive_cache?archive_cache->archive(resolved,policy):
                std::make_shared<const DatArchive>(file(files,name),policy);
            if(!archive_cache)
                snapshots[std::string(name)]={value->data().begin(),value->data().end()};
            archives.emplace(name,std::move(value));
        };
        for(const char* name:{"PlCo.dat","ItCo.usd","EfCoData.dat","PdPm.dat","LbRb.dat"})load(name);
        if(purpose==GameplayWorldPurpose::Match)load("TyDatai.usd");
        if(selection.begin_source_match)
            load("LbRf.dat",DatExternalPolicy::ResolveNull);
        if(stage)load(stage->archive);
        for(unsigned slot=0;slot<selection.player_count;++slot)
            selected_costumes[selection.fighter_kinds[slot]].insert(selection.costume_indices[slot]);
        for(unsigned slot=0;slot<selection.player_count;++slot){
            const auto kind=selection.fighter_kinds[slot];
            if(!melee_web_fighter_content_by_kind(kind))throw DatError("No runtime owner for selected source fighter kind");
            for(const auto& costume:fighter_costumes())if(costume.fighter_kind==kind){
                if(costume.costume_index==0){
                    identities[kind]=&costume;
                    // Fighter DATs follow lbArchive_InitializeDAT, which clears
                    // every validated external chain before loading sections.
                    // Link's optional Hookshot material/shape tracks use this.
                    load(costume.fighter_filename,DatExternalPolicy::ResolveNull);
                    load(costume.model_filename);
                }
                else if(selected_costumes[kind].contains(costume.costume_index)){
                    if(melee_web_runtime_file_name(
                           files,costume.model_filename,
                           DatMenuSupportLanguage::English,
                           DatMenuSupportLanguage::English).empty())
                        throw DatError("Selected fighter costume model is missing");
                    load(costume.model_filename);
                }
            }
            if(!identities.contains(kind))throw DatError("Pinned fighter identity missing");
        }
        // Fighter effect dependencies are selected below from source identities.
        for(const auto& [kind,identity]:identities)load(melee_web_fighter_content_by_kind(kind)->effect_archive);
        for(const auto& [kind,identity]:identities)identity_order.push_back(kind);
        // Decode before acquiring the source world whenever possible.
        DatCommon common_data(*archive("PlCo.dat"));
        if(!common_data.roots[20].data_offset)throw DatError("Missing common root20");
        DatNativeJoint common_joint(archive("PlCo.dat"),*common_data.roots[20].data_offset);
        items=std::make_unique<DatItemRegistryNative>(archive("ItCo.usd"));
        bonus_arena=std::make_unique<NativeDatArena>(archive("PdPm.dat"));
        bonus=melee_web_bonus_data_decode(bonus_arena->reader(),symbol(*archive("PdPm.dat"),"plLoadCommonData"));
        if(source_start_data){
            MeleeWebArchiveSymbol bonus_symbol={
                "PdPm.dat","plLoadCommonData",melee_web_bonus_data_public_data(bonus)};
            bonus_scope=melee_web_archive_sections_register(
                &bonus_symbol,1,error,sizeof(error));
            check(bonus_scope!=nullptr,error);
        }
        rumble_arena=std::make_unique<NativeDatArena>(archive("LbRb.dat"));
        const auto rumble_root=symbol(*archive("LbRb.dat"),"lbRumbleData");
        const auto rumble_bytes=archive("LbRb.dat")->next_target_offset(rumble_root)-rumble_root;
        if(rumble_bytes!=40*8)throw DatError("GALE01r2 rumble table must contain 40 source rows");
        rumble=melee_web_rumble_decode(rumble_arena->reader(),rumble_root,40);
        if(selection.begin_source_match){
            const auto& source=*archive("LbRf.dat");
            const auto root=symbol(source,"lbRefData");
            const auto count=source.range(root,1)[0];
            if(!count)throw DatError("Original lbRefData table is empty");
            const auto parameters=source.pointer(root+4,static_cast<size_t>(count)*2*sizeof(float));
            if(!parameters)throw DatError("Original lbRefData parameter array is absent");
            refract_data.count=count;
            refract_data.padding[0]=refract_data.padding[1]=refract_data.padding[2]=0;
            refract_parameters.reserve(static_cast<size_t>(count)*2);
            for(size_t i=0;i<static_cast<size_t>(count)*2;i++)
                refract_parameters.push_back(source.f32(*parameters+static_cast<uint32_t>(i*sizeof(float))));
            refract_data.parameters=refract_parameters.data();
        }
        if(purpose==GameplayWorldPurpose::Match){
            const MeleeWebArchiveSymbol rumble_symbol={
                "LbRb.dat","lbRumbleData",melee_web_rumble_source_rows(rumble)};
            rumble_scope=melee_web_archive_sections_register(
                &rumble_symbol,1,error,sizeof(error));
            check(rumble_scope!=nullptr,error);
        }
        const auto& font_bytes=file(files,"sislib_font.bin");
        font=melee_web_font_atlas_register(font_bytes.data(),font_bytes.size(),error,sizeof(error));check(font!=nullptr,error);
        source_files=begin_source_files(files,error,sizeof(error));
        check(source_files!=nullptr,error);
        /* The source VS manager calls lb_80014534 before scene OnEnter. Publish
         * the exact decoded rows here so its loader can reuse them without a
         * duplicate main-heap archive copy; initialize the rumble interpreter
         * at its original post-startup point below. */
        if(purpose==GameplayWorldPurpose::Match){
            check(melee_web_rumble_publish_source(rumble,error,sizeof(error)),error);
            rumble_published=true;
        }
        if(purpose==GameplayWorldPurpose::Match)
            check(melee_web_gameplay_prepare_vs_startup(start_vs_scene_manager,stop_vs_sis,error,sizeof(error)),error);
        check(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);started=true;
        if(purpose==GameplayWorldPurpose::Match){
            const DatItemRegistry item_registry(*archive("ItCo.usd"));
            const auto random_index=static_cast<size_t>(
                It_PKind_Random-It_Kind_Kuriboh);
            if(!item_registry.articles[random_index])
                throw DatError("Original Random Pokémon Article root is absent");
            void* random_article=items->articles()[random_index];
            if(!random_article)
                throw DatError("Original Random Pokémon Article registration is absent");
            random_item_article=std::make_unique<DatItemArticle>(
                archive("ItCo.usd"),*item_registry.articles[random_index],
                It_PKind_Random,random_article);
            trophy_data=std::make_unique<DatTrophyData>(archive("TyDatai.usd"));
            auto models=[](auto entries){
                std::vector<TrophyData> result;
                result.reserve(entries.size());
                for(const auto& row:entries)
                    result.push_back({row.id,row.x04,row.x08,row.x0c,row.x10,row.x14,
                                      row.x18,row.x1c,row.x20,row.x21,row.x22,row.x23});
                return result;
            };
            trophy_models=models(trophy_data->init_model_table());
            trophy_models_d=models(trophy_data->init_model_d_table());
            trophy_names.reserve(trophy_data->model_sort_table().size());
            for(const auto& row:trophy_data->model_sort_table())
                trophy_names.push_back({row.x0,row.x2,row.x4,row.x6,row.x8,row.xa});
            auto displays=[](auto entries){
                std::vector<TyDspEntry> result;
                result.reserve(entries.size());
                for(const auto& row:entries)
                    result.push_back({row.x00,row.x04,row.x05,{row.pad06,row.pad07},
                                      row.x08,row.x0c});
                return result;
            };
            trophy_display=displays(trophy_data->display_model_table());
            trophy_display_us=displays(trophy_data->display_model_us_table());
            const MeleeWebArchiveSymbol symbols[]={
                {"TyDatai.usd","tyInitModelTbl",trophy_models.data()},
                {"TyDatai.usd","tyInitModelDTbl",trophy_models_d.data()},
                {"TyDatai.usd","tyModelSortTbl",trophy_names.data()},
                {"TyDatai.usd","tyExpDifferentTbl",
                 const_cast<std::int16_t*>(trophy_data->exp_different_table().data())},
                {"TyDatai.usd","tyNoGetUsTbl",
                 const_cast<std::int16_t*>(trophy_data->no_get_us_table().data())},
                {"TyDatai.usd","tyDisplayModelTbl",trophy_display.data()},
                {"TyDatai.usd","tyDisplayModelUsTbl",trophy_display_us.data()},
            };
            trophy_scope=melee_web_archive_sections_register_heap(
                symbols,std::size(symbols),error,sizeof(error));
            check(trophy_scope!=nullptr,error);
        }
        check(melee_web_rumble_begin(rumble,error,sizeof(error)),error);
        rumble_published=true;
        if(stage){rules=melee_web_match_rules_begin(error,sizeof(error));check(rules!=nullptr,error);}
        if(selection.begin_source_match){
            if(purpose!=GameplayWorldPurpose::Match||!stage||
               selection.player_count<MELEE_WEB_MENU_MIN_PLAYERS||
               selection.player_count>MELEE_WEB_MENU_MAX_PLAYERS||
               !selection.source_camera_subjects)
                throw DatError("Source match context requires an active VS world and camera pool");
            for(unsigned i=0;i<selection.player_count;i++){
                const auto& player=selection.source_players[i];
                if(player.slot!=i||player.controller>=4||player.stocks<1||player.stocks>5||
                   player.fighter_kind!=selection.fighter_kinds[i]||
                   player.costume!=selection.costume_indices[i])
                    throw DatError("Source match settings differ from the selected VS players");
            }
            match_context=melee_web_match_begin_players(selection.source_players.data(),
                selection.player_count,selection.source_camera_subjects,
                selection.source_random_seed,nullptr,error,sizeof(error));
            check(match_context!=nullptr,error);
            render_context=melee_web_render_prepare_match_camera(error,sizeof(error));
            check(render_context!=nullptr,error);
            if(source_start_data){
                check(melee_web_match_rules_prepare_from_menu(
                    rules,source_start_data,error,sizeof(error)),error);
            }
            const MeleeWebArchiveSymbol refract_symbol={
                "LbRf.dat","lbRefData",&refract_data};
            refract_scope=melee_web_archive_sections_register_heap(
                &refract_symbol,1,error,sizeof(error));
            check(refract_scope!=nullptr,error);
            /* fn_8016E730 invokes the original refraction owner directly after
             * Camera_80030688. Keep that source continuation ahead of native
             * common/root hydration; lbArchive owns its source allocations. */
            lbRefract_800222A4();
            if(source_start_data){
                check(melee_web_gameplay_initialize_vs_dynamics(error,sizeof(error)),error);
                check(melee_web_effect_runtime_begin(error,sizeof(error)),error);
                effect_started=true;
                check(melee_web_bonus_data_begin(bonus,error,sizeof(error)),error);
                bonus_published=true;
                Player_80036DD8();
                check(melee_web_bonus_data_ready(bonus),
                    "Original Player_80036DD8 did not retain its registered PdPm owner");
            }
        }
        common=melee_web_common_context_create(&common_data.scalars,&common_data.tables,&common_joint.graph(),error,sizeof(error));
        check(common!=nullptr,error);
        if(!common_data.roots[16].data_offset)throw DatError("Missing common root16 accessory");
        DatNativeJoint root16_model(archive("PlCo.dat"),*common_data.roots[16].data_offset);
        root16_native=melee_web_native_joint_hydrate(&root16_model.graph(),error,sizeof(error));
        check(root16_native!=nullptr,error);
        check(melee_web_common_context_set_root16(common,
            melee_web_native_joint_descriptor(root16_native,error,sizeof(error)),error,sizeof(error)),error);
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
        const MeleeWebArchiveSymbol common_symbol={
            "PlCo.dat","ftLoadCommonData",melee_web_common_context_source_roots()};
        common_scope=melee_web_archive_sections_register(&common_symbol,1,error,sizeof(error));
        check(common_scope!=nullptr,error);
        check(melee_web_common_context_set_source_loader(
            common,load_common_source,this,error,sizeof(error)),error);
        if(stage){
            stage_arena=std::make_unique<NativeDatArena>(archive(stage->archive));
            auto* ground=melee_web_ground_data_decode(stage_arena->reader(),symbol(*archive(stage->archive),"grGroundParam"));
            auto* markers=melee_web_stage_markers_decode(stage_arena->reader(),symbol(*archive(stage->archive),"map_head"));
            collision_data=std::make_unique<DatCollision>(*archive(stage->archive));
            DatLights light_data(*archive(stage->archive),"map_plit",true);
            lights=melee_web_stage_lights_create(light_data.lights.data(),light_data.lights.size(),error,sizeof(error));check(lights!=nullptr,error);
            for(uint32_t i=0;i<light_data.lights.size();i++){
                auto flags=read_dat_light_override(*archive(stage->archive),light_data.lights[i].source_offset);
                check(melee_web_stage_lights_set_override(lights,i,flags.has_value(),flags.value_or(0),error,sizeof(error)),error);
                if(light_data.animation_tables[i]){
                    if(!full_stage)full_stage=std::make_unique<DatNativeStage>(archive(stage->archive),stage->stage_kind);
                    check(melee_web_stage_lights_set_animations(lights,i,
                        full_stage->light_animation_table(*light_data.animation_tables[i]),error,sizeof(error)),error);
                }
            }
            check(melee_web_stage_lights_attach(lights,error,sizeof(error)),error);
            previous_ground=melee_web_ground_data_publish(ground);ground_published=true;
            numeric=melee_web_stage_numeric_begin_source_stage_kind(markers,stage->stage_kind,error,sizeof(error));check(numeric!=nullptr,error);
            // grDatFiles_801C6038 publishes this shared stage dependency before any
            // hit can request grLib_801C9CEC. Reuse checked native scene animation
            // descriptors; the original quake GObj owns animation and camera input.
            const auto& stage_archive=*archive(stage->archive);
            const auto quake_root=symbol(stage_archive,"quake_model_set");
            const auto quake_anims=stage_archive.pointer(quake_root+4,20);
            if(!quake_anims)throw DatError("Stage quake animation table is missing");
            for(unsigned i=0;i<4;i++)
                if(!stage_archive.pointer(*quake_anims+i*4,20))
                    throw DatError("Stage quake requires four authored animations");
            if(stage_archive.pointer(*quake_anims+16,4))
                throw DatError("Stage quake animation table exceeds its source variants");
            quake_model=std::make_unique<DatScene>(archive(stage->archive),"quake_model_set",DatSceneRootKind::DynamicModel);
            check(melee_web_stage_numeric_set_quake(numeric,quake_model->single_model(),error,sizeof(error)),error);
            if(!collision_deferred){
                collision=load_collision(*collision_data,stage->ground_kind,read_dat_stage_scale(*archive(stage->archive)));
                floor_start=collision_data->line_ranges[0].start;
                collision_data.reset();
            }
        }
        construction_phase=1;
        if(!defer)while(!advance_construction()){}
    }
    bool advance_construction(){
        if(construction_phase==0)return false;
        if(construction_phase==1){
            if(purpose==GameplayWorldPurpose::Results&&fighter_at<identity_order.size()){
                create_fighter_asset(identity_order[fighter_at++]);
                return false;
            }
            if(purpose==GameplayWorldPurpose::Results)fighter_assets_ready=true;
            construction_phase=2;
        }
        if(construction_phase==2){
            if(!effect_started){
                check(purpose==GameplayWorldPurpose::Results?
                    melee_web_effect_runtime_prepare(error,sizeof(error)):
                    melee_web_effect_runtime_begin(error,sizeof(error)),error);
                effect_started=true;
            }
            common_effects=std::make_unique<DatEffectEntries>(archive("EfCoData.dat"),"effCommonDataTable",0,47,true);
            check(purpose==GameplayWorldPurpose::Results?
                common_effects->publish_for_source(error,sizeof(error)):
                common_effects->load(error,sizeof(error)),error);
            std::set<unsigned> effect_banks;
            for(const auto& [kind,identity]:identities){
                const auto* dependency=melee_web_fighter_content_by_kind(kind);
                if(!effect_banks.insert(dependency->effect_bank).second)continue;
                /* Fighter effect tables may carry the original packed particle
                 * callback channel (Falco bank 3 entry 1 does). */
                auto effect=std::make_unique<DatEffectEntries>(archive(dependency->effect_archive),
                    dependency->effect_symbol,dependency->effect_bank,dependency->effect_count,true);
                check(purpose==GameplayWorldPurpose::Results?effect->publish_for_source(error,sizeof(error)):
                    effect->load(error,sizeof(error)),error);effects.push_back(std::move(effect));
            }
            construction_phase=3;
            return false;
        }
        if(construction_phase==3){
            registry=melee_web_item_registry_begin(items->articles(),MELEE_WEB_ITEM_REGISTRY_COUNT,error,sizeof(error));check(registry!=nullptr,error);
            item_arena=std::make_unique<NativeDatArena>(archive("ItCo.usd"));
            const auto& it=*archive("ItCo.usd");const auto item_root=symbol(it,"itPublicData");
            auto color_root=it.pointer(item_root+20,8);
            if(!color_root)throw DatError("Incomplete original item service data");
            const size_t color_bytes=it.next_target_offset(*color_root)-*color_root;
            if(color_bytes%8||color_bytes/8>256)throw DatError("Invalid item color table extent");
            item_colors=std::make_unique<DatColorAnimation>(archive("ItCo.usd"),*color_root,color_bytes/8);
            void* source_item=melee_web_item_public_data_decode(
                item_arena->reader(),item_root,items->articles(),
                MELEE_WEB_ITEM_REGISTRY_COUNT);
            item_runtime=purpose==GameplayWorldPurpose::Results?
                melee_web_item_runtime_prepare_source(source_item,item_colors->table(),color_bytes/8,error,sizeof(error)):
                melee_web_item_runtime_begin_source(source_item,item_colors->table(),color_bytes/8,error,sizeof(error));
            check(item_runtime!=nullptr,error);
            MeleeWebArchiveSymbol item_public={"ItCo.usd","itPublicData",source_item};
            item_scope=melee_web_archive_sections_register(&item_public,1,error,sizeof(error));
            check(item_scope!=nullptr,error);
            if(stage){
                stage_items=std::make_unique<DatStageItems>(archive(stage->archive));
                const auto stage_item_rows=stage_items->items();
                if(!stage_item_rows.empty()){
                    stage_item_scope=melee_web_stage_items_begin(stage_item_rows.data(),stage_item_rows.size(),error,sizeof(error));
                    check(stage_item_scope!=nullptr,error);
                }
            }
            if(!bonus_published){
                check(melee_web_bonus_data_begin(bonus,error,sizeof(error)),error);
                bonus_published=true;
            }
            construction_phase=4;
        }
        return construction_phase==4;
    }
    void create_fighter_asset(unsigned kind){
        const auto* identity=identities.at(kind);
        auto owner=std::make_unique<GameplayFighterAssets>(archive(identity->fighter_filename),
            archive(identity->model_filename),file(*runtime_files,identity->animation_filename),*identity);
        for(const auto& costume:fighter_costumes())
            if(costume.fighter_kind==kind&&costume.costume_index!=0&&
               selected_costumes[kind].contains(costume.costume_index)&&
               archives.contains(costume.model_filename))
                owner->add_costume(archive(costume.model_filename),costume);
        fighters.emplace(kind,std::move(owner));
    }
    void create_fighter_assets_after_source_init(){
        if(fighter_assets_ready)return;
        if(purpose!=GameplayWorldPurpose::Match||construction_phase!=4)
            throw DatError("Match fighter assets require completed source scene initialization");
        while(fighter_at<identity_order.size())
            create_fighter_asset(identity_order[fighter_at++]);
        fighter_assets_ready=true;
    }
    void enable_stage_visual(){
        if(!stage)throw DatError("Results uses the original dummy stage during scene entry");
        if(stage_visual)return;
        if(stage->stage_kind!=St_Kind_Last)
            throw DatError("The partial stage visual probe only owns Final Destination entry3; use full source stage initialization");
        check(melee_web_stage_lights_load(lights,error,sizeof(error)),error);
        auto source=archive(stage->archive);
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
    void enable_full_stage(bool defer_start){
        if(!stage)throw DatError("Results uses the original dummy stage during scene entry");
        if(stage_last)return;
        if(stage_visual)throw DatError("Close selected stage visual before full initialization");
        if(!source_ordered)
            check(melee_web_stage_lights_load(lights,error,sizeof(error)),error);
        auto source=archive(stage->archive);
        if(!full_stage)full_stage=std::make_unique<DatNativeStage>(source,stage->stage_kind);
        const auto* profile=melee_web_stage_profile(stage->stage_kind);
        check(profile!=nullptr,"Stage has no complete source callback profile");
        const bool has_commands=has_symbol(*source,"map_ptcl");
        const bool has_textures=has_symbol(*source,"map_texg");
        if(has_commands!=has_textures)
            throw DatError("Source stage particle roots are incomplete");
        if(!has_commands&&!profile->allow_absent_particle_bank)
            throw DatError("Source stage particle roots are absent for a required bank");
        if(has_commands){
            stage_effects=std::make_unique<DatEffectBanks>(source,"map_ptcl","map_texg",64);
            check(melee_web_effect_bank_attach(stage_effects->bank(),error,sizeof(error)),error);
            // grDatFiles publishes bank64 for stage loading; Ground initialization
            // publishes the same map data at bank30 for authored joint events.
            check(melee_web_effect_bank_attach(stage_effects->alias(30),error,sizeof(error)),error);
        }
        full_stage->set_particle_roots(stage_effects?stage_effects->command_root():nullptr,
                                       stage_effects?stage_effects->texture_root():nullptr);
        if(!quake_model)throw DatError("Source stage quake descriptor owner is missing");
        full_stage->set_quake_model(quake_model->single_model());
        if(source_ordered){
            const auto counts=full_stage->source_light_counts();
            check(melee_web_stage_lights_set_source_counts(
                lights,counts.data(),static_cast<uint32_t>(counts.size()),
                error,sizeof(error)),error);
        }
        for(const auto& event:full_stage->particle_events())
            if(!melee_web_effect_bank_has_command(event.bank,event.command))
                throw DatError("Stage animation requires unpublished particle bank/command "+
                    std::to_string(event.bank)+"/"+std::to_string(event.command));
        stage_map=melee_web_stage_map_publish(full_stage->map_head(),error,sizeof(error));check(stage_map!=nullptr,error);
        const auto& symbols=full_stage->public_symbols();
        check(melee_web_stage_map_set_public(stage_map,symbols.data(),symbols.size(),error,sizeof(error)),error);
        const auto& overrides=full_stage->light_overrides();
        check(melee_web_stage_map_set_overrides(stage_map,overrides.data(),overrides.size(),error,sizeof(error)),error);
        /* Non-ordered tooling retains its isolated collision seam. Match
         * startup leaves allocation and ownership to Stage_8022524C below. */
        if(!collision&&!collision_deferred){
            if(!collision_data)throw DatError("Source-ordered stage has no decoded collision input");
            collision=load_collision(*collision_data,stage->ground_kind,
                                     read_dat_stage_scale(*source));
            floor_start=collision_data->line_ranges[0].start;
            collision_data.reset();
        }
        stage_last=melee_web_stage_begin_kind(stage->stage_kind,full_stage->yakumono(),
            stage_effects?stage_effects->bank():nullptr,defer_start,source_ordered,
            error,sizeof(error));check(stage_last!=nullptr,error);
        if(!collision){
            if(!collision_data)throw DatError("Source-ordered stage has no decoded collision input");
            collision=source_ordered?
                adopt_collision(*collision_data,stage->ground_kind,
                                read_dat_stage_scale(*source)):
                load_collision(*collision_data,stage->ground_kind,
                               read_dat_stage_scale(*source));
            floor_start=collision_data->line_ranges[0].start;
            collision_data.reset();
        }
        check(melee_web_stage_numeric_source_stage_ready(numeric,error,sizeof(error)),error);
    }
    void end_stage(){
        if(numeric)check(melee_web_stage_numeric_clear_quakes(numeric,error,sizeof(error)),error);
        if(stage_last){check(melee_web_stage_last_end(stage_last,error,sizeof(error)),error);stage_last=nullptr;}
        if(stage_visual){check(melee_web_stage_visual_end(stage_visual,error,sizeof(error)),error);stage_visual=nullptr;}
    }
    void verify()const{
        for(const auto& [name,source]:archives){
            if(archive_cache){archive_cache->verify(source);continue;}
            const auto& expected=snapshots.at(name);auto actual=source->data();
            if(!std::equal(actual.begin(),actual.end(),expected.begin(),expected.end()))
                throw DatError("Original source mutated immutable archive: "+name);
        }
    }
    void close(){
        for(const auto& [kind,fighter]:fighters)
            if(fighter->live_fighters())throw DatError("Close all fighter/render contexts before the runtime world");
        end_stage();
        if(render_context){
            check(melee_web_render_end(render_context,error,sizeof(error)),error);
            render_context=nullptr;
        }
        if(match_context){
            check(melee_web_match_end(match_context,error,sizeof(error)),error);
            match_context=nullptr;
        }
        check(melee_web_crowd_end(error,sizeof(error)),error);
        if(item_runtime){check(melee_web_item_runtime_end(item_runtime,error,sizeof(error)),error);item_runtime=nullptr;}
        random_item_article.reset();
        if(item_scope){check(melee_web_archive_sections_close(item_scope,error,sizeof(error)),error);item_scope=nullptr;}
        if(stage_item_scope){check(melee_web_stage_items_end(stage_item_scope,error,sizeof(error)),error);stage_item_scope=nullptr;}
        stage_items.reset();
        item_colors.reset();item_arena.reset();
        if(effect_started){check(melee_web_effect_runtime_end(error,sizeof(error)),error);effect_started=false;}
        if(stage_map){check(melee_web_stage_map_close(stage_map,error,sizeof(error)),error);stage_map=nullptr;}
        stage_effects.reset();
        if(stage_native){check(melee_web_native_joint_destroy(stage_native,error,sizeof(error)),error);stage_native=nullptr;}
        stage_material_animation.reset();stage_animation.reset();stage_model.reset();
        if(bonus_published){check(melee_web_bonus_data_end(bonus,error,sizeof(error)),error);bonus_published=false;}
        if(rumble_published){check(melee_web_rumble_end(rumble,error,sizeof(error)),error);rumble_published=false;}
        if(registry){check(melee_web_item_registry_end(registry,error,sizeof(error)),error);registry=nullptr;}
        for(auto i=effects.rbegin();i!=effects.rend();++i)check((*i)->detach(error,sizeof(error)),error);
        effects.clear();
        if(common_effects){check(common_effects->detach(error,sizeof(error)),error);common_effects.reset();}
        for(auto i=fighters.rbegin();i!=fighters.rend();++i)i->second->close();
        fighters.clear();
        if(collision){check(melee_web_collision_destroy(collision,error,sizeof(error)),error);collision=nullptr;}
        if(numeric){check(melee_web_stage_numeric_end(numeric,error,sizeof(error)),error);numeric=nullptr;}
        quake_model.reset();
        if(common){check(melee_web_common_context_destroy(common,error,sizeof(error)),error);common=nullptr;}
        if(common_source_archive){
            lbArchive_80016EFC(common_source_archive);
            common_source_archive=nullptr;
        }
        if(common_scope){
            check(melee_web_archive_sections_close(common_scope,error,sizeof(error)),error);
            common_scope=nullptr;
        }
        if(root16_native){check(melee_web_native_joint_destroy(root16_native,error,sizeof(error)),error);root16_native=nullptr;}
        if(respawn_native){check(melee_web_native_joint_destroy(respawn_native,error,sizeof(error)),error);respawn_native=nullptr;}
        respawn_animation.reset();extra_colors.reset();common_colors.reset();
        if(rules){check(melee_web_match_rules_end(rules,error,sizeof(error)),error);rules=nullptr;}
        if(started){check(melee_web_gameplay_shutdown(error,sizeof(error)),error);started=false;}
        if(bonus_scope){
            check(melee_web_archive_sections_close(bonus_scope,error,sizeof(error)),error);
            bonus_scope=nullptr;
        }
        if(refract_scope){
            check(melee_web_archive_sections_close(refract_scope,error,sizeof(error)),error);
            refract_scope=nullptr;
        }
        if(trophy_scope){
            check(melee_web_archive_sections_close(trophy_scope,error,sizeof(error)),error);
            trophy_scope=nullptr;
        }
        if(rumble_scope){
            check(melee_web_rumble_clear_source(rumble,error,sizeof(error)),error);
            check(melee_web_archive_sections_close(rumble_scope,error,sizeof(error)),error);
            rumble_scope=nullptr;
        }
        if(ground_published){melee_web_ground_data_publish(previous_ground);ground_published=false;}
        if(lights){check(melee_web_stage_lights_destroy(lights,error,sizeof(error)),error);lights=nullptr;}
        // Global stage-light animations borrow the stage descriptor arena.
        // Keep it until both original and host light GObjs have retired.
        full_stage.reset();
        if(font){check(melee_web_font_atlas_close(font,error,sizeof(error)),error);font=nullptr;}
        if(source_files){
            check(melee_web_source_files_end(source_files,error,sizeof(error)),error);
            source_files=nullptr;
        }
        trophy_data.reset();
        trophy_models.clear();trophy_models_d.clear();trophy_names.clear();
        trophy_display.clear();trophy_display_us.clear();
        verify();
    }
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Runtime teardown: %s\n",e.what());std::abort();}}
};
GameplayWorld::GameplayWorld(const RuntimeFiles& files):GameplayWorld(files,GameplayWorldSelection{}){}
GameplayWorld::GameplayWorld(const RuntimeFiles& files,const GameplayWorldSelection& selection)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,nullptr,false);}
GameplayWorld::GameplayWorld(const RuntimeFiles& files,const GameplayWorldSelection& selection,
                             GameplayWorldConstruction construction)
    :storage_(std::make_unique<Storage>()){
    const bool deferred=construction!=GameplayWorldConstruction::Immediate;
    storage_->start(files,selection,nullptr,deferred,
                    construction==GameplayWorldConstruction::SourceOrdered);
}
GameplayWorld::GameplayWorld(const RuntimeFiles& files,const GameplayWorldSelection& selection,
                             RuntimeArchiveCache& cache)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,&cache,false);}
GameplayWorld::GameplayWorld(const RuntimeFiles& files,const GameplayWorldSelection& selection,
                             RuntimeArchiveCache& cache,GameplayWorldConstruction construction)
    :storage_(std::make_unique<Storage>()){
    const bool deferred=construction!=GameplayWorldConstruction::Immediate;
    storage_->start(files,selection,&cache,deferred,
                    construction==GameplayWorldConstruction::SourceOrdered);
}
GameplayWorld::~GameplayWorld()=default;
void GameplayWorld::enable_stage_visual(){storage_->enable_stage_visual();}
void GameplayWorld::enable_full_stage(bool defer_start){storage_->enable_full_stage(defer_start);}
void GameplayWorld::end_stage(){storage_->end_stage();}
MeleeWebMatchContext* GameplayWorld::take_match_context(){
    if(!storage_||!storage_->match_context)
        throw DatError("Gameplay world has no unclaimed source match context");
    auto* match=storage_->match_context;
    storage_->match_context=nullptr;
    return match;
}
MeleeWebRender* GameplayWorld::take_render_context(){
    if(!storage_||!storage_->render_context)
        throw DatError("Gameplay world has no unclaimed source render context");
    auto* render=storage_->render_context;
    storage_->render_context=nullptr;
    return render;
}
void GameplayWorld::install_result_demo(unsigned kind,std::shared_ptr<const DatArchive> archive){
    if(storage_->purpose!=GameplayWorldPurpose::Results||!construction_complete()||!archive)
        throw DatError("Result demo publication requires a prepared Results world");
    const auto found=storage_->fighters.find(kind);
    if(found==storage_->fighters.end())throw DatError("Result demo fighter is not owned by this world");
    found->second->set_result_demo_archive(std::move(archive));
}
void GameplayWorld::verify_result_source_loads()const{
    if(storage_->purpose!=GameplayWorldPurpose::Results||!construction_complete())
        throw DatError("Result source verification requires a prepared Results world");
    char error[256]{};
    check(melee_web_effect_runtime_complete_source_init(error,sizeof(error)),error);
    check(storage_->common_effects->verify_source_load(error,sizeof(error)),error);
    for(const auto& effect:storage_->effects)check(effect->verify_source_load(error,sizeof(error)),error);
}
void GameplayWorld::close(){storage_->close();}
MeleeWebCollision* GameplayWorld::collision()const{return storage_->collision;}
float GameplayWorld::floor_height(float x)const{
    MeleeWebCollisionFloorResult floor;char error[256];
    check(melee_web_collision_floor(storage_->collision,storage_->floor_start,x,20,&floor,error,sizeof(error)),error);
    if(floor.line<0)throw DatError("No source floor at requested position");return 20+floor.displacement_y;
}
std::array<float, 3> GameplayWorld::player_spawn(unsigned slot)const{
    float position[3];
    char error[256];
    check(melee_web_stage_numeric_spawn(storage_->numeric,slot,position,error,sizeof(error)),error);
    return {position[0],position[1],position[2]};
}
uint32_t GameplayWorld::unresolved_fighter_fields()const{
    uint32_t result=0;for(const auto& [kind,fighter]:storage_->fighters)result|=fighter->unresolved_fields();return result;
}
void GameplayWorld::verify_immutable_archives()const{storage_->verify();}
bool GameplayWorld::advance_construction(){return storage_->advance_construction();}
bool GameplayWorld::construction_complete()const{return storage_->construction_phase==4;}
void GameplayWorld::initialize_match(const StartMeleeData& start) {
    check(storage_!=nullptr,"Gameplay world is closed");char error[256]{};
    if(storage_->source_start_data&&storage_->source_start_data!=&start)
        throw DatError("Match source initialization must finish with its published menu payload owner");
    /* fn_8016E2BC calls Player_80036DA4 (Fighter_FirstInitialize) before
     * fn_8016DEEC computes the authored spawn matrices and player directions. */
    check(melee_web_common_context_initialize_fighters(storage_->common,error,sizeof(error)),error);
    if(storage_->source_start_data){
        check(melee_web_match_rules_finish_from_menu(storage_->rules,error,sizeof(error)),error);
    }else{
        check(melee_web_match_rules_init_from_menu(storage_->rules,&start,error,sizeof(error)),error);
    }
    storage_->create_fighter_assets_after_source_init();
}

}
