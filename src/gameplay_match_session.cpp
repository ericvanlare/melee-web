#include "gameplay_match_session.hpp"
#include "runtime_archive_cache.hpp"
#include "gameplay_content.h"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_menu.h"
#include "gameplay_render.h"
#include "gameplay_hud.h"
#include "gameplay_match_flow.h"
#include "gameplay_fighter_assets.h"
#include "gameplay_hud_assets.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <set>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance_runtime.h"
#include "gameplay_bootstrap.h"
#include <melee/ft/kinds/ftCommon/forward.h>
#endif
extern "C" int lbAudioAx_80023F28(int);
extern "C" int melee_web_vs_mode_begin(void);
extern "C" int melee_web_vs_mode_end(void);
extern "C" uint16_t* gmMainLib_GetUnlockedCharactersBitmaskPtr(void);
extern "C" uint16_t* gmMainLib_8015EDA4(void);
extern "C" int fn_8016E5C0(StartMeleeData*);
extern "C" int melee_web_stage_select_music(int, int*, int*);
extern "C" void melee_web_stage_commit_music(int, int);
extern "C" const char* melee_web_audio_music_path(int);
namespace melee_web {
namespace {
void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void check_fighter_asset_ownership(const char* phase){
    char error[256]{};
    if(!melee_web_fighter_assets_check_owned(phase,error,sizeof(error)))
        throw std::runtime_error(error);
}
}
struct GameplayMatchSession::Storage {
    std::unique_ptr<GameplayWorld> world;
    std::unique_ptr<GameplayAudioBank> bank;
    std::unique_ptr<GameplayAudioStream> music;
    std::string music_path;
    MeleeWebMatchContext* match=nullptr;
    MeleeWebRender* render=nullptr;
    std::unique_ptr<GameplayHudAssets> hud_assets;
    MeleeWebHud* hud=nullptr;
    MeleeWebMatchFlow* flow=nullptr;
    bool mode_owned=false;
    bool profile_owned=false;
    uint16_t saved_characters=0,saved_stages=0;
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Match session teardown: %s\n",e.what());std::abort();}}
    const RuntimeFiles* runtime_files=nullptr;
    RuntimeArchiveCache* runtime_cache=nullptr;
    MeleeWebMenuMatchSelection selected{};
    GameplayWorldSelection content{};
    const MeleeWebStageContent* stage=nullptr;
    unsigned construction_phase=0;
    const MeleeWebPadState* initial_input=nullptr;
    void begin(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
               RuntimeArchiveCache* archive_cache){
        unsigned player_count = selection.player_count != 0
                                    ? selection.player_count
                                    : melee_web_menu_active_player_count(&selection.start);
        check(player_count >= MELEE_WEB_MENU_MIN_PLAYERS &&
              player_count <= MELEE_WEB_MENU_MAX_PLAYERS,
              "Match requires two through four active source players");
        check(melee_web_menu_active_player_count(&selection.start) ==
              static_cast<int>(player_count),
              "Match player count does not match contiguous source slots");
        check(selection.hud_layout == selection.start.rules.x0_3,
              "Match compatibility settings differ from source payload");
        runtime_files=&files;runtime_cache=archive_cache;selected=selection;
        stage=melee_web_stage_content(selection.start.rules.stkind);
        check(stage!=nullptr,"Match stage has no source runtime owner");
        content.ground_kind=stage->ground_kind;
        content.player_count=player_count;
        for(unsigned i=0;i<player_count;i++){
            const auto& source = selection.start.players[i];
            const auto& settings = selection.players[i];
            check(settings.controller == (source.slot ? source.slot - 1u : i) &&
                  settings.stocks == source.stocks && settings.costume == source.color &&
                  settings.sub_color == source.sub_color,
                  "Match compatibility settings differ from source payload");
            const auto* fighter=melee_web_fighter_content(selection.start.players[i].ckind);
            check(fighter&&selection.players[i].controller==i&&
                  selection.players[i].stocks>=1&&selection.players[i].stocks<=5&&
                  selection.players[i].costume<fighter->costumes&&selection.players[i].sub_color<=4,
                  "Match requires supported source player stock/costume selections");
            content.fighter_kinds[i]=fighter->fighter_kind;
            content.costume_indices[i]=selection.players[i].costume;
        }
        check(melee_web_vs_mode_begin(),"Original VS mode is already owned");mode_owned=true;
        if(selected.save_profile_present){
            saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
            saved_stages=*gmMainLib_8015EDA4();
            *gmMainLib_GetUnlockedCharactersBitmaskPtr()=selected.unlocked_characters;
            *gmMainLib_8015EDA4()=selected.unlocked_stages;
            profile_owned=true;
        }
        if(archive_cache)
            world=std::make_unique<GameplayWorld>(files,content,*archive_cache,
                                                  GameplayWorldConstruction::Deferred);
        else
            world=std::make_unique<GameplayWorld>(files,content);
    }
    void prepare_music(){
        // fn_8016E730 selects music after constructing stage and fighters.
        // It shares gameplay RNG, even in a silent public build. Decode only
        // the chosen stream while preparation keeps the source clock stopped.
        if(!selected.start.rules.x1_4){
            int music_id=-1,alternate=0;
            melee_web_stage_select_music(fn_8016E5C0(&selected.start),&music_id,&alternate);
            const char* path=melee_web_audio_music_path(music_id);
            check(path&&std::string_view(path).starts_with("/audio/"),
                  "Original stage music has no supported disc path");
            music_path=path;
            const auto filename=music_path.substr(7);
            check(runtime_files->contains(filename),"Selected original stage music was not imported");
            music=std::make_unique<GameplayAudioStream>(bank->get(),music_path.c_str(),runtime_files->at(filename));
            melee_web_stage_commit_music(music_id,alternate);
        }
    }
    bool advance_construction(){
        char error[256]{};
        if(construction_phase==0){
            if(runtime_cache&&!world->advance_construction())return false;
            construction_phase=1;
            return false;
        }
        if(construction_phase==1){
            hud_assets=runtime_cache?std::make_unique<GameplayHudAssets>(*runtime_files,*runtime_cache):
                                     std::make_unique<GameplayHudAssets>(*runtime_files);
            std::vector<std::string_view> bank_names={"main.ssm","nr_select.ssm","nr_title.ssm",
                                                      "nr_name.ssm","pokemon.ssm","end.ssm"};
            std::set<std::string_view> fighter_banks;
            for(unsigned i=0;i<content.player_count;++i){
                const auto kind=content.fighter_kinds[i];
                const auto* dependency=melee_web_fighter_content_by_kind(kind);
                for(unsigned identity=0;identity<melee_web_fighter_kind_count(dependency->character_kind);++identity){
                    const auto* owner=melee_web_fighter_content_by_kind(
                        melee_web_fighter_kind_at(dependency->character_kind,identity));
                    if(fighter_banks.insert(owner->audio_bank).second)
                        bank_names.emplace_back(owner->audio_bank);
                }
            }
            if(stage->audio_bank)bank_names.emplace_back(stage->audio_bank);
            if(runtime_cache){
                std::vector<std::shared_ptr<const DatAudioBank>> decoded;
                decoded.reserve(bank_names.size());
                for(const auto name:bank_names)decoded.push_back(runtime_cache->audio_bank(name));
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
                bank=std::make_unique<GameplayAudioBank>(runtime_files->at("smash2.sem"),std::move(decoded),std::span<const uint8_t>{});
#else
                bank=std::make_unique<GameplayAudioBank>(runtime_files->at("smash2.sem"),std::move(decoded),runtime_files->at("dsp_coef.bin"));
#endif
            }else{
                std::vector<std::span<const uint8_t>> banks;
                banks.reserve(bank_names.size());
                for(const auto name:bank_names)banks.emplace_back(runtime_files->at(std::string(name)));
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
                bank=std::make_unique<GameplayAudioBank>(runtime_files->at("smash2.sem"),banks,std::span<const uint8_t>{});
#else
                bank=std::make_unique<GameplayAudioBank>(runtime_files->at("smash2.sem"),banks,runtime_files->at("dsp_coef.bin"));
#endif
            }
            check(melee_web_audio_enable_effects(bank->get(),error,sizeof(error)),error);
            construction_phase=2;
            return false;
        }
        if(construction_phase==2){
            MeleeWebPlayerSettings players[MELEE_WEB_MENU_MAX_PLAYERS]{};
            for(unsigned i=0;i<content.player_count;i++){
                const auto spawn=world->player_spawn(i);const auto& player=selected.players[i];
                players[i]={i,player.controller,player.stocks,{spawn[0],spawn[1],spawn[2]},spawn[0]<0?1.0f:-1.0f,
                            player.costume,player.sub_color,content.fighter_kinds[i]};
            }
            match=melee_web_match_begin_players(players,content.player_count,70,selected.random_seed,world->collision(),error,sizeof(error));check(match!=nullptr,error);
            if(initial_input){check(melee_web_match_restore_input(match,initial_input,error,sizeof(error)),error);initial_input=nullptr;}
            world->enable_full_stage(true);
            world->initialize_match(selected.start);
            construction_phase=3;
            return false;
        }
        if(construction_phase==3){
            check(melee_web_match_create_fighters_intro(match,error,sizeof(error)),error);
            construction_phase=4;
            return false;
        }
        if(construction_phase==4){
            MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
            render=melee_web_render_begin_match(&settings,error,sizeof(error));check(render!=nullptr,error);
            check(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
            hud=melee_web_hud_begin_with_music(selected.hud_layout,
                [](void* context,char* message,size_t size)->int{
                    try{static_cast<Storage*>(context)->prepare_music();return 1;}
                    catch(const std::exception& e){if(message&&size)std::snprintf(message,size,"%s",e.what());return 0;}
                },this,error,sizeof(error));check(hud!=nullptr,error);
            check(melee_web_render_use_scene_cameras(render,error,sizeof(error)),error);
            flow=melee_web_match_flow_begin(error,sizeof(error));check(flow!=nullptr,error);
            construction_phase=5;
        }
        return true;
    }
    void start(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
               RuntimeArchiveCache* archive_cache){
        begin(files,selection,archive_cache);
        while(!advance_construction()){}
    }
    void close(){
        char error[256]{};
        if(flow){check(melee_web_match_flow_end(flow,error,sizeof(error)),error);flow=nullptr;}
        /* The browser's final source draw has completed before close(). Keep
         * the original fighter state resident while publishing MatchEnd, then
         * let melee_web_match_end tear down the source objects. The rules
         * module retains a bounded terminal snapshot for post-close reports. */
        if(match)(void)melee_web_match_rules_publish_result();
        if(hud){check(melee_web_hud_end(hud,error,sizeof(error)),error);hud=nullptr;}
        if(world)world->end_stage();
        if(render){check(melee_web_render_end(render,error,sizeof(error)),error);render=nullptr;}
        if(match){
            check_fighter_asset_ownership("before-match-end");
            check(melee_web_match_end(match,error,sizeof(error)),error);match=nullptr;
            check_fighter_asset_ownership("after-match-end");
        }
        music.reset();
        if(world){
            check_fighter_asset_ownership("before-world-close");
            world->verify_immutable_archives();world->close();world.reset();
        }
        if(hud_assets){hud_assets->close();hud_assets.reset();}
        bank.reset();
        if(profile_owned){
            *gmMainLib_GetUnlockedCharactersBitmaskPtr()=saved_characters;
            *gmMainLib_8015EDA4()=saved_stages;
            profile_owned=false;
        }
        if(mode_owned){check(melee_web_vs_mode_end(),"Original VS mode lost ownership");mode_owned=false;}
    }
};
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,nullptr);}
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
                                           const MeleeWebPadState& initial_input)
    :storage_(std::make_unique<Storage>()){
    storage_->initial_input=&initial_input;storage_->start(files,selection,nullptr);
}
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
                                           const MeleeWebMenuMatchSelection& selection,
                                           RuntimeArchiveCache& archive_cache)
    :storage_(std::make_unique<Storage>()){storage_->start(files,selection,&archive_cache);}
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
                                           const MeleeWebMenuMatchSelection& selection,
                                           RuntimeArchiveCache& archive_cache,
                                           GameplayMatchConstruction construction)
    :storage_(std::make_unique<Storage>()){
    if(construction==GameplayMatchConstruction::Deferred)
        storage_->begin(files,selection,&archive_cache);
    else
        storage_->start(files,selection,&archive_cache);
}
GameplayMatchSession::~GameplayMatchSession()=default;
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
                                           const MeleeWebMenuMatchSelection& selection,
                                           RuntimeArchiveCache& archive_cache,
                                           GameplayMatchConstruction construction,
                                           const MeleeWebPadState& initial_input)
    :storage_(std::make_unique<Storage>()){
    storage_->initial_input=&initial_input;
    if(construction==GameplayMatchConstruction::Deferred)
        storage_->begin(files,selection,&archive_cache);
    else
        storage_->start(files,selection,&archive_cache);
}
void GameplayMatchSession::close(){if(storage_){storage_->close();storage_.reset();}}
void GameplayMatchSession::tick(const PADStatus raw[4]){
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};
    check(melee_web_match_step_raw_phased(storage_->match,raw,melee_web_match_flow_renew,melee_web_match_flow_pre,melee_web_match_flow_post,storage_->flow,error,sizeof(error)),error);
}
void GameplayMatchSession::draw(){
    check(storage_&&storage_->render,"Match render session is closed");char error[256]{};
    check(melee_web_render_draw(storage_->render,error,sizeof(error)),error);
    check(melee_web_match_flow_present(storage_->flow,error,sizeof(error)),error);
}
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
MeleeWebPipelineSourceContext GameplayMatchSession::provenance_context() const {
    MeleeWebPipelineSourceContext context{};
    for(auto& player:context.players){player.motion_id=-1;player.stocks=-1;}
    if(!storage_)return context;
    context.scene=MELEE_WEB_PIPELINE_SCENE_MATCH;
    context.phase=construction_complete()?MELEE_WEB_PIPELINE_PHASE_INTERACTIVE:MELEE_WEB_PIPELINE_PHASE_PREPARATION;
    context.world_generation=melee_web_gameplay_generation();
    // The match-flow clock begins after Entry/Ready. Provenance also needs
    // those original source traversals, so use the world's traversal clock.
    context.source_tick=melee_web_gameplay_provenance_tick();
    context.stage=storage_->selected.start.rules.stkind;
    context.ground=storage_->content.ground_kind;
    context.hud_layout=storage_->selected.hud_layout;
    context.active_player_count=storage_->content.player_count;
    context.owner_kind=MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
    bool entry=false,dead=false,respawn=false;
    for(unsigned i=0;i<context.active_player_count;++i){
        const auto& selected=storage_->selected.start.players[i];
        const auto* content=melee_web_fighter_content(selected.ckind);
        auto& player=context.players[i];
        player.character=selected.ckind;player.fighter_kind=storage_->content.fighter_kinds[i];
        player.costume=selected.color;player.subcolor=selected.sub_color;
        player.effect_bank=content?content->effect_bank:UINT32_MAX;
        player.motion_id=-1;player.stocks=selected.stocks;
        if(construction_complete()){
            const auto state=player_stats(i);
            player.motion_id=state.motion_id;player.stocks=state.stocks;
            entry|=state.motion_id>=ftCo_MS_Entry&&state.motion_id<=ftCo_MS_EntryEnd;
            dead|=state.motion_id>=ftCo_MS_DeadDown&&state.motion_id<=ftCo_MS_DeadUpFallHitCameraIce;
            respawn|=state.motion_id>=ftCo_MS_Rebirth&&state.motion_id<=ftCo_MS_RebirthWait;
        }
    }
    if(construction_complete()){
        if(ending())context.phase=MELEE_WEB_PIPELINE_PHASE_ENDING;
        else if(entry)context.phase=MELEE_WEB_PIPELINE_PHASE_ENTRY;
        else if(!ready())context.phase=MELEE_WEB_PIPELINE_PHASE_READY;
        else if(dead)context.phase=MELEE_WEB_PIPELINE_PHASE_DEATH;
        else if(respawn)context.phase=MELEE_WEB_PIPELINE_PHASE_RESPAWN;
    }
    return context;
}
#endif
bool GameplayMatchSession::ending()const{return storage_&&melee_web_match_flow_ending(storage_->flow);}
bool GameplayMatchSession::complete()const{return storage_&&melee_web_match_flow_complete(storage_->flow);}
bool GameplayMatchSession::paused()const{return storage_&&melee_web_match_flow_paused(storage_->flow);}
uint32_t GameplayMatchSession::source_frames()const{return storage_?melee_web_match_flow_frames(storage_->flow):0;}
int GameplayMatchSession::hud_damage(unsigned player)const{return storage_?melee_web_hud_damage(storage_->hud,player):-1;}
bool GameplayMatchSession::ready()const{return storage_&&melee_web_hud_ready(storage_->hud);}
int GameplayMatchSession::outcome(int& winner)const{
    check(storage_&&storage_->match,"Match session is closed");
    const int result=melee_web_match_flow_result(storage_->flow);
    if(result==OUTCOME_NO_CONTEST){winner=-1;return result;}
    return melee_web_match_rules_outcome(&winner);
}
uint32_t GameplayMatchSession::random_seed()const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_stats(storage_->match,&stats,error,sizeof(error)),error);return stats.random_seed;
}
int GameplayMatchSession::fighter_kind(unsigned index)const{
    check(storage_&&storage_->match&&index<storage_->content.player_count,"Match player index is outside the active source match");
    return storage_->content.fighter_kinds[index];
}
MeleeWebMatchStats GameplayMatchSession::player_stats(unsigned index)const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_player_stats(storage_->match,index,&stats,error,sizeof(error)),error);return stats;
}
MeleeWebAudio* GameplayMatchSession::audio()const{return storage_&&storage_->bank?storage_->bank->get():nullptr;}
bool GameplayMatchSession::advance_construction(){return storage_&&storage_->advance_construction();}
bool GameplayMatchSession::construction_complete()const{return storage_&&storage_->construction_phase==5;}
}
