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
#include "gameplay_kirby_copy_assets.hpp"
#include "gameplay_hud_assets.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <set>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance_runtime.h"
#include "gameplay_bootstrap.h"
#include <melee/ft/kinds/ftCommon/forward.h>
#endif
extern "C" {
#include <melee/gm/types.h>
#include <melee/gm/gmvsmelee.h>
}
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
    std::unique_ptr<GameplayKirbyCopyAssets> kirby_copy_assets;
    MeleeWebHud* hud=nullptr;
    MeleeWebMatchFlow* flow=nullptr;
    bool stadium_diagnostic=false;
    bool mode_owned=false;
    bool profile_owned=false;
    bool sudden_death_claimed=false;
    bool sudden_death_scene_active=false;
    MeleeWebMenuHost* sudden_death_host=nullptr;
    uint64_t sudden_death_owner_id=0;
    uint16_t saved_characters=0,saved_stages=0;
    ~Storage(){try{close();}catch(const std::exception& e){std::fprintf(stderr,"Match session teardown: %s\n",e.what());std::abort();}}
    const RuntimeFiles* runtime_files=nullptr;
    RuntimeArchiveCache* runtime_cache=nullptr;
    MeleeWebMenuMatchSelection selected{};
    GameplayWorldSelection content{};
    const MeleeWebStageContent* stage=nullptr;
    unsigned construction_phase=0;
    void begin(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
               RuntimeArchiveCache* archive_cache,const MeleeWebPadState* initial_input=nullptr,
               MeleeWebMenuHost* sd_host=nullptr,
               const MeleeWebMenuMatchContinuation* sd_continuation=nullptr){
        MeleeWebMenuMatchSelection observed_selection{};
        const MeleeWebMenuMatchSelection* resolved_selection=&selection;
        const bool sudden_death=sd_host!=nullptr||sd_continuation!=nullptr;
        if(sudden_death){
            check(sd_continuation&&
                  sd_continuation->kind==MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH&&
                  sd_continuation->owner_id!=0,
                  "Sudden Death match requires its exact typed host continuation");
            char error[256]{};
            check(melee_web_menu_host_sudden_death_selection(
                      sd_host,sd_continuation,&observed_selection,error,sizeof(error)),error);
            resolved_selection=&observed_selection;
        }else{
            check(!selection.sudden_death,
                  "Sudden Death source payload requires a checked host-owned match claim");
        }
        const MeleeWebMenuMatchSelection& selected_input=*resolved_selection;
        const bool opening_demo = selected_input.opening_demo != 0;
        unsigned active_source_players=0;
        active_source_players=static_cast<unsigned>(
            melee_web_menu_active_player_count(&selected_input.start));
        unsigned player_count = selected_input.player_count != 0
                                    ? selected_input.player_count
                                    : active_source_players;
        check(player_count >= MELEE_WEB_MENU_MIN_PLAYERS &&
              player_count <= MELEE_WEB_MENU_MAX_PLAYERS,
              "Match requires two through four active source players");
        check(active_source_players==player_count,
              sudden_death?
                "Sudden Death player count does not match its active source slots":
                "Match player count does not match active source slots");
        if(!sudden_death)
            check(melee_web_menu_active_player_count(&selected_input.start)==
                  static_cast<int>(player_count),
                  "Match player count does not match active source slots");
        check(selected_input.hud_layout == selected_input.start.rules.x0_3,
              "Match compatibility settings differ from source payload");
        if(sudden_death)
            check(selected_input.sudden_death&&selected_input.start.rules.x6,
                  "Sudden Death source scene setup must precede match preparation");
        else
            check(!selected_input.start.rules.x6,
                  "Sudden Death source payload cannot enter through ordinary VS construction");
        if(opening_demo){
            const auto& rules=selected_input.start.rules;
            check(player_count==4&&rules.match_kind<=3&&!rules.timer_enabled&&
                  rules.time_limit==0&&rules.x1_0==0&&rules.x1_2&&rules.x1_3&&
                  rules.disable_pausing&&rules.x7==0&&rules.game_speed==1.0f&&
                  rules.on_match_start!=nullptr&&rules.on_frame_start==nullptr&&
                  rules.on_frame_end==nullptr&&rules.on_match_end==nullptr&&
                  rules.x54==nullptr,
                  "Opening demo requires the authored four-player source VS setup");
        }
        runtime_files=&files;runtime_cache=archive_cache;selected=selected_input;
        if(selection_uses_kirby(selected_input))
            kirby_copy_assets=std::make_unique<GameplayKirbyCopyAssets>(files,selected_input);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if(stadium_diagnostic){
            check(!opening_demo && !sudden_death && !selected_input.sudden_death &&
                  player_count==2 && !selected_input.start.rules.is_teams &&
                  selected_input.start.rules.stkind==St_Kind_PStadium,
                  "Diagnostic session requires the exact two-Human-Mario Stadium profile");
            for(unsigned i=0;i<2;++i)
                check(selected_input.start.players[i].slot_type==Gm_PKind_Human &&
                      selected_input.start.players[i].ckind==CKIND_MARIO,
                      "Diagnostic Stadium session requires two Human Mario players");
            stage=melee_web_stage_content_for_profile(St_Kind_PStadium);
        }else
#endif
        stage=melee_web_stage_content(selected_input.start.rules.stkind);
        check(stage!=nullptr,"Match stage has no source runtime owner");
        content.ground_kind=stage->ground_kind;
        content.player_count=player_count;
        unsigned compact_player=0;
        for(unsigned source_slot=0;source_slot<MELEE_WEB_MENU_MAX_PLAYERS;++source_slot){
            const auto& source=selected_input.start.players[source_slot];
            if(source.slot_type==Gm_PKind_NA)continue;
            const auto& settings=selected_input.players[source_slot];
            const unsigned source_port=source.slot?source.slot-1u:source_slot;
            check(settings.controller == source_port &&
                  settings.stocks == source.stocks && settings.costume == source.color &&
                  settings.sub_color == source.sub_color,
                  "Match compatibility settings differ from source payload");
            const auto* fighter=melee_web_fighter_content(source.ckind);
            check(fighter&&settings.controller==source_port&&
                  settings.stocks>=1&&
                  settings.stocks<=(opening_demo?99u:5u)&&
                  settings.costume<fighter->costumes&&settings.sub_color<=4,
                  "Match requires supported source player stock/costume selections");
            if(opening_demo){
                check(source.slot_type==Gm_PKind_Cpu&&source.cpu_kind==4&&
                      source.cpu_level==9&&source.team==0&&
                      source_port==source_slot&&source_slot==compact_player&&
                      source.stocks==settings.stocks,
                      "Opening demo player differs from its source four-CPU setup");
            }
            check(source_port==source_slot,
                  "Match controller mapping changed its source slot identity");
            content.fighter_kinds[compact_player]=fighter->fighter_kind;
            content.costume_indices[compact_player]=settings.costume;
            content.source_players[compact_player]={source_slot,settings.controller,
                settings.stocks,{0,0,0},1.0f,
                settings.costume,settings.sub_color,
                content.fighter_kinds[compact_player]};
            ++compact_player;
        }
        check(compact_player==player_count,
              "Match source-slot compaction changed its active player count");
        content.begin_source_match=true;
        content.opening_demo=opening_demo;
        content.stadium_diagnostic=stadium_diagnostic;
        content.sudden_death=sudden_death;
        content.source_camera_subjects=70;
        content.source_random_seed=selected_input.random_seed;
        content.source_start_data=&selected.start;
        content.source_initial_input=initial_input;
        if(sudden_death){
            MeleeWebMenuMatchSelection claimed{};
            char error[256]{};
            check(melee_web_menu_host_sudden_death_match_claim(
                      sd_host,sd_continuation,&claimed,error,sizeof(error)),error);
            sudden_death_host=sd_host;
            sudden_death_owner_id=sd_continuation->owner_id;
            sudden_death_claimed=true;
            check(claimed.player_count==selected.player_count&&
                  claimed.random_seed==selected.random_seed&&
                  claimed.hud_layout==selected.hud_layout&&
                  claimed.unlocked_characters==selected.unlocked_characters&&
                  claimed.unlocked_stages==selected.unlocked_stages&&
                  claimed.sudden_death==selected.sudden_death&&
                  std::memcmp(&claimed.start,&selected.start,sizeof(selected.start))==0&&
                  std::memcmp(claimed.players,selected.players,sizeof(selected.players))==0,
                  "Sudden Death match claim changed the observed source selection");
            check(melee_web_menu_host_sudden_death_scene_begin(
                      sudden_death_host,sudden_death_owner_id,error,sizeof(error)),error);
            sudden_death_scene_active=true;
            check(std::memcmp(&gmVsMelee_StartData,&selected.start,
                              sizeof(selected.start))==0,
                  "Sudden Death scene global differs from the copied source payload");
        }else if(!opening_demo){
            check(melee_web_vs_mode_begin(),"Original VS mode is already owned");
            mode_owned=true;
        }
        if(selected.save_profile_present){
            saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
            saved_stages=*gmMainLib_8015EDA4();
            *gmMainLib_GetUnlockedCharactersBitmaskPtr()=selected.unlocked_characters;
            *gmMainLib_8015EDA4()=selected.unlocked_stages;
            profile_owned=true;
        }
        if(archive_cache)
            world=std::make_unique<GameplayWorld>(files,content,*archive_cache,
                                                  GameplayWorldConstruction::SourceOrdered);
        else
            world=std::make_unique<GameplayWorld>(files,content,
                                                  GameplayWorldConstruction::SourceOrdered);
        content.source_initial_input=nullptr;
        match=world->take_match_context();
        render=world->take_render_context();
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
            if(!world->advance_construction())return false;
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
            check(match!=nullptr,"Source VS match context was not created at scene entry");
            /* Keep source RNG initialization ahead of stage on_init, then hand
             * spawn lookup to the authored map JObjs before any Fighter exists. */
            world->enable_full_stage(true);
            check(melee_web_match_attach_collision(match,world->collision(),error,sizeof(error)),error);
            world->initialize_match(selected.start);
            construction_phase=3;
            return false;
        }
        if(construction_phase==3){
            check(melee_web_match_create_fighters_intro(match,error,sizeof(error)),error);
            if(kirby_copy_assets)kirby_copy_assets->activate();
            construction_phase=4;
            return false;
        }
        if(construction_phase==4){
            MeleeWebRenderSettings settings{640,480,{0,25,180},{0,15,0},30,1,1000,(uint64_t(1)<<5)|(uint64_t(1)<<3)};
            check(melee_web_render_finish_match_camera(render,&settings,error,sizeof(error)),error);
            check(melee_web_render_use_match_passes(render,error,sizeof(error)),error);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
            if(stadium_diagnostic)world->prepare_stadium_ready(match);
#endif
            auto prepare_music=[](void* context,char* message,size_t size)->int{
                try{static_cast<Storage*>(context)->prepare_music();return 1;}
                catch(const std::exception& e){if(message&&size)std::snprintf(message,size,"%s",e.what());return 0;}
            };
            /* The Sudden Death scene enters status 1 after the complete
             * original match-start boundary, rather than VS status 3. */
            hud=content.sudden_death?
                melee_web_hud_begin_sudden_death_with_music(
                    selected.hud_layout,prepare_music,this,error,sizeof(error)):
                melee_web_hud_begin_with_music(
                    selected.hud_layout,prepare_music,this,error,sizeof(error));
            check(hud!=nullptr,error);
            check(melee_web_render_use_scene_cameras(render,error,sizeof(error)),error);
            if(selected.opening_demo){
                check(selected.start.rules.on_match_start!=nullptr,
                      "Opening demo source callback was lost before its start boundary");
                selected.start.rules.on_match_start();
            }
            flow=melee_web_match_flow_begin(selected.opening_demo,
                                            error,sizeof(error));
            check(flow!=nullptr,error);
            construction_phase=5;
        }
        return true;
    }
    void start(const RuntimeFiles& files,const MeleeWebMenuMatchSelection& selection,
               RuntimeArchiveCache* archive_cache,const MeleeWebPadState* initial_input=nullptr,
               MeleeWebMenuHost* sd_host=nullptr,
               const MeleeWebMenuMatchContinuation* sd_continuation=nullptr){
        begin(files,selection,archive_cache,initial_input,sd_host,sd_continuation);
        while(!advance_construction()){}
    }
    void end_flow(){
        char error[256]{};
        if(flow){check(melee_web_match_flow_end(flow,error,sizeof(error)),error);flow=nullptr;}
    }
    void close(){
        char error[256]{};
        end_flow();
        /* The browser's final source draw has completed before close(). Keep
         * the original fighter state resident while publishing MatchEnd, then
         * let melee_web_match_end tear down the source objects. The rules
         * module retains a bounded terminal snapshot for post-close reports. */
        if(match&&!selected.opening_demo)
            (void)melee_web_match_rules_publish_result();
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
            world->verify_immutable_archives();
            world->close([this]{
                /* Kirby donor effect tables share HSD's global live-generator
                 * guard. Release their banks only after the world's original
                 * particle runtime has removed stage/fighter generators. */
                if(kirby_copy_assets){kirby_copy_assets->close();kirby_copy_assets.reset();}
            });
            world.reset();
        }
        if(kirby_copy_assets){kirby_copy_assets->close();kirby_copy_assets.reset();}
        if(hud_assets){hud_assets->close();hud_assets.reset();}
        bank.reset();
        if(sudden_death_scene_active){
            check(melee_web_menu_host_sudden_death_scene_end(
                      sudden_death_host,sudden_death_owner_id,error,sizeof(error)),error);
            sudden_death_scene_active=false;
        }
        if(sudden_death_claimed){
            check(melee_web_menu_host_sudden_death_match_release(
                      sudden_death_host,sudden_death_owner_id,error,sizeof(error)),error);
            sudden_death_claimed=false;
        }
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
    storage_->start(files,selection,nullptr,&initial_input);
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
    const MeleeWebMenuMatchSelection& selection,RuntimeArchiveCache& cache,
    GameplayMatchConstruction construction,const MeleeWebPadState& input,
    GameplayMatchDiagnostic diagnostic):storage_(std::make_unique<Storage>())
{
    check(diagnostic==GameplayMatchDiagnostic::StadiumReady,"Unknown diagnostic match boundary");
    storage_->stadium_diagnostic=true;
    try{
        if(construction==GameplayMatchConstruction::Deferred)
            storage_->begin(files,selection,&cache,&input);
        else storage_->start(files,selection,&cache,&input);
    }catch(const std::exception& first){
        // Preserve the first preparation refusal before the existing checked
        // Storage destructor attempts retirement of any partial source owners.
        std::fprintf(stderr,"STADIUM_READY_CONSTRUCTION_FIRST_FAILURE %s\n",first.what());
        std::fflush(stderr);throw;
    }
}
#endif
GameplayMatchSession::~GameplayMatchSession()=default;
GameplayMatchSession::GameplayMatchSession(const RuntimeFiles& files,
                                           const MeleeWebMenuMatchSelection& selection,
                                           RuntimeArchiveCache& archive_cache,
                                           GameplayMatchConstruction construction,
                                           const MeleeWebPadState& initial_input)
    :storage_(std::make_unique<Storage>()){
    if(construction==GameplayMatchConstruction::Deferred)
        storage_->begin(files,selection,&archive_cache,&initial_input);
    else
        storage_->start(files,selection,&archive_cache,&initial_input);
}
GameplayMatchSession::GameplayMatchSession(
    const RuntimeFiles& files,MeleeWebMenuHost* host,
    const MeleeWebMenuMatchContinuation& continuation,
    RuntimeArchiveCache& archive_cache,GameplayMatchConstruction construction,
    const MeleeWebPadState& initial_input)
    :storage_(std::make_unique<Storage>()){
    const MeleeWebMenuMatchSelection unused{};
    if(construction==GameplayMatchConstruction::Deferred)
        storage_->begin(files,unused,&archive_cache,&initial_input,host,&continuation);
    else
        storage_->start(files,unused,&archive_cache,&initial_input,host,&continuation);
}
void GameplayMatchSession::close(){if(storage_){storage_->close();storage_.reset();}}
void GameplayMatchSession::finish_vs(
    uint32_t& seed,uint8_t input[MELEE_WEB_PAD_STATE_BYTES]){
    check(input&&storage_&&storage_->match&&storage_->flow&&
          !storage_->selected.opening_demo&&!storage_->selected.sudden_death&&
          !storage_->sudden_death_claimed,
          "VS handoff requires its live ordinary match owner and complete PAD output");
    check(complete(),"VS handoff requires completed original source flow");
    storage_->end_flow();
    check(melee_web_match_rules_publish_result(),
          "Original VS could not publish its complete terminal data");
    char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_stats(storage_->match,&stats,error,sizeof(error)),error);
    seed=stats.random_seed;
    melee_web_pad_state_capture(input);
    storage_->close();storage_.reset();
}
void GameplayMatchSession::finish_sudden_death(
    MeleeWebMenuMatchContinuation& results){
    std::memset(&results,0,sizeof(results));
    check(storage_&&storage_->selected.sudden_death&&
          storage_->sudden_death_host&&storage_->sudden_death_claimed,
          "Sudden Death Results handoff requires its live typed match owner");
    check(complete(),
          "Sudden Death Results handoff requires the completed original source flow");
    // Keep canonical close order through publication while the match-owned
    // RNG/PAD are still live. OnExit may change either; capture afterward.
    storage_->end_flow();
    check(melee_web_match_rules_publish_result(),
          "Original Sudden Death could not publish its complete terminal data");
    uint32_t final_seed=0;
    uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];
    capture_handoff(final_seed,final_input);
    MeleeWebMenuHost* host=storage_->sudden_death_host;
    const uint64_t owner_id=storage_->sudden_death_owner_id;
    storage_->close();
    MatchExitInfo exit_info{};
    check(melee_web_match_rules_terminal_data(&exit_info),
          "Original Sudden Death scene did not publish its complete MatchExitInfo");
    char error[256]{};
    check(melee_web_menu_host_sudden_death_finish(
              host,owner_id,&exit_info,final_seed,final_input,
              &results,error,sizeof(error)),error);
    check(results.kind==MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS,
          "Original Sudden Death mode did not continue to its Results scene");
    storage_.reset();
}
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
        const auto source_slot=storage_->content.source_players[i].slot;
        const auto& selected=storage_->selected.start.players[source_slot];
        const auto* content=melee_web_fighter_content(selected.ckind);
        auto& player=context.players[i];
        player.character=selected.ckind;player.fighter_kind=storage_->content.fighter_kinds[i];
        player.costume=selected.color;player.subcolor=selected.sub_color;
        player.effect_bank=content?content->effect_bank:UINT32_MAX;
        player.motion_id=-1;player.stocks=selected.stocks;
        if(construction_complete()){
            const auto state=player_stats(i);
            player.fighter_kind=state.fighter_kind;
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
bool GameplayMatchSession::opening_demo()const{return storage_&&storage_->selected.opening_demo!=0;}
bool GameplayMatchSession::sudden_death()const{return storage_&&storage_->selected.sudden_death!=0;}
bool GameplayMatchSession::paused()const{return storage_&&melee_web_match_flow_paused(storage_->flow);}
uint32_t GameplayMatchSession::source_frames()const{return storage_?melee_web_match_flow_frames(storage_->flow):0;}
int GameplayMatchSession::hud_damage(unsigned player)const{
    if(!storage_||player>=storage_->content.player_count)return -1;
    return melee_web_hud_damage(storage_->hud,
        storage_->content.source_players[player].slot);
}
bool GameplayMatchSession::ready()const{return storage_&&melee_web_hud_ready(storage_->hud);}
int GameplayMatchSession::outcome(int& winner)const{
    check(storage_&&storage_->match,"Match session is closed");
    if(storage_->selected.opening_demo){winner=-1;return 0;}
    const int result=melee_web_match_flow_result(storage_->flow);
    if(result==OUTCOME_NO_CONTEST){winner=-1;return result;}
    return melee_web_match_rules_outcome(&winner);
}
uint32_t GameplayMatchSession::random_seed()const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_stats(storage_->match,&stats,error,sizeof(error)),error);return stats.random_seed;
}
void GameplayMatchSession::capture_handoff(
    uint32_t& seed,uint8_t input[MELEE_WEB_PAD_STATE_BYTES])const{
    check(storage_&&storage_->match&&storage_->selected.sudden_death,
          "Only a live Sudden Death match can capture its next-route handoff");
    check(input!=nullptr,"Sudden Death handoff requires the complete source PAD bank");
    char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_stats(storage_->match,&stats,error,sizeof(error)),error);
    seed=stats.random_seed;
    melee_web_pad_state_capture(input);
}
int GameplayMatchSession::fighter_kind(unsigned index)const{
    check(storage_&&storage_->match&&index<storage_->content.player_count,"Match player index is outside the active source match");
    return player_stats(index).fighter_kind;
}
const StartMeleeData& GameplayMatchSession::start_data()const{
    check(storage_&&storage_->match,"Match session is closed");
    return storage_->selected.start;
}
const StartMeleeData* GameplayMatchSession::diagnostic_start_data()const noexcept{
    return storage_?&storage_->selected.start:nullptr;
}
MeleeWebMatchStats GameplayMatchSession::player_stats(unsigned index)const{
    check(storage_&&storage_->match,"Match session is closed");char error[256]{};MeleeWebMatchStats stats{};
    check(melee_web_match_player_stats(storage_->match,index,&stats,error,sizeof(error)),error);return stats;
}
MeleeWebAudio* GameplayMatchSession::audio()const{return storage_&&storage_->bank?storage_->bank->get():nullptr;}
bool GameplayMatchSession::advance_construction(){return storage_&&storage_->advance_construction();}
bool GameplayMatchSession::construction_complete()const{return storage_&&storage_->construction_phase==5;}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
bool GameplayMatchSession::diagnostic_toy_owner_preflight()const{
    return storage_&&storage_->world&&storage_->world->diagnostic_toy_owner_preflight();
}
void GameplayMatchSession::diagnostic_stadium_go_alignment_arm(
    int expected_branch,char* error,size_t size){
    check(storage_&&construction_complete()&&storage_->world,
          "Stadium GO trace requires a complete live source match owner");
    storage_->world->diagnostic_stadium_go_alignment_arm(expected_branch,error,size);
}
bool GameplayMatchSession::diagnostic_stadium_go_alignment_snapshot(
    MeleeWebStadiumGoAlignmentSnapshot* out,char* error,size_t size)const{
    return storage_&&construction_complete()&&storage_->world&&
        storage_->world->diagnostic_stadium_go_alignment_snapshot(out,error,size);
}
#endif
}
