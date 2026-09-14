#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_session.hpp"
#include "gameplay_match_rules.h"
#include "gameplay_retail_recipe.hpp"
#include "../tests/native_menu_fighter_input.h"
#include "../tests/native_menu_stage_input.h"
#include "gameplay_audio_stream.h"
#include "runtime_archive_cache.hpp"
#include "gameplay_content.h"
#include "menu_preparation_state.hpp"
#include "browser_input.h"
#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"
#include "gameplay_pipeline_preparation.hpp"
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#include <malloc.h>
#include <SDL3/SDL_hints.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance_runtime.h"
#include "gameplay_bootstrap.h"
#endif
namespace {
melee_web::RuntimeFiles files;
std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
std::unique_ptr<melee_web::GameplayMatchSession> match;
std::unique_ptr<melee_web::RetailReplayRecipe> replay;
size_t replay_cursor=0;
bool replay_trace=false,replay_pending=false,replay_started=false,replay_final_draw=false;
// V2 recipes come from fresh original processes and do not carry heap history.
// Original stage callbacks can read uncleared allocation bytes (Shy Guy pattern).
// Do not reset this eligibility on unload or normalize those gameplay bytes.
bool reference_heap_used=false;
unsigned reference_menu_preparations=0;
bool replay_match_complete=false;
int replay_outcome=0,replay_winner=-1;
MeleeWebMenuHost* host=nullptr;
melee_web::FixedTickClock menu_clock;
melee_web::FixedTickClock audio_clock{melee_web::FixedTickClock::OverrunPolicy::CatchUp};
std::string message="Choose your local Melee disc image.";
std::string match_message="Original source match";
bool running=false,pending=false,host_entered=false,world_exposed=false,faulted=false;
melee_web::MenuPreparationState preparation;
unsigned audio_phase=0,diagnostic_start_ticks=0;
int stock_check=0,stock_count=4,stock_respawns=0;
unsigned stock_tick=0,completed_matches=0;
bool stock_lost=false,stock_jump=false;
bool first_use_draw_pending=false;
bool render_only_preparation=false;
bool transition_audio_continues=false;
bool menu_scene_rebuild_pending=false;
melee_web::GameplayMenuScene pending_menu_scene=melee_web::GameplayMenuScene::Characters;
unsigned render_frame=0;
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
double pipeline_bootstrap_started=0,pipeline_renderer_init_ms=0,pipeline_union_requested=0;
double pipeline_union_submit_ms=0,pipeline_union_first_ready=0;
#endif
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
uint64_t provenance_route_epoch=1,provenance_last_world=0;
uint32_t provenance_last_scene=MELEE_WEB_PIPELINE_SCENE_BOOT;
bool provenance_return_draw=false;
MeleeWebPipelineSourceContext pipeline_context(uint32_t phase_override=0,uint32_t scene_override=0){
 MeleeWebPipelineSourceContext context{};
 if(match)context=match->provenance_context();
 else if(host){
  if(!melee_web_menu_host_provenance(host,&context))
   melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT);
 }else{
  for(auto& player:context.players){player.motion_id=-1;player.stocks=-1;}
  context.scene=MELEE_WEB_PIPELINE_SCENE_BOOT;
  context.phase=MELEE_WEB_PIPELINE_PHASE_PREPARATION;
  context.owner_kind=MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
 }
 if(context.scene!=provenance_last_scene||context.world_generation!=provenance_last_world){
  ++provenance_route_epoch;provenance_last_scene=context.scene;provenance_last_world=context.world_generation;
 }
 context.route_epoch=provenance_route_epoch;
 if(!phase_override&&(preparation.busy()||pending))context.phase=MELEE_WEB_PIPELINE_PHASE_PREPARATION;
 if(provenance_return_draw&&context.scene==MELEE_WEB_PIPELINE_SCENE_CSS)
  context.phase=MELEE_WEB_PIPELINE_PHASE_RETURN;
 if(phase_override)context.phase=phase_override;
 if(scene_override)context.scene=scene_override;
 return context;
}
void drain_pipeline_provenance(){
 // Keep the bounded transport independent of browser catch-up draw count.
 // A missing collector retains records until explicit overflow; never drop.
 MeleeWebPipelineStatus status{};
 if(melee_web_pipeline_status(melee_web_provenance_recorder(),&status,nullptr,0)&&
    status.valid&&status.pending_records==0)return;
 // Recordless healthy callbacks have no evidence to transfer. Invalid status
 // must still be exported even if an error occurred without a retained event.
 if(EM_ASM_INT({return typeof window.meleePipelineProvenanceChunk==='function';})){
  if(const char* chunk=melee_web_provenance_drain())
   // The serializer owns the exact UTF-8 byte count. Avoid a second
   // JavaScript scan of the complete chunk before decoding the same bytes.
   EM_ASM({window.meleePipelineProvenanceChunk(UTF8ToString($0,$1,true));},
          chunk,melee_web_provenance_chunk_bytes());
 }
}
#endif
int32_t stat_delta(uint64_t after,uint64_t before);
void render_audio_tick(MeleeWebAudio* audio,char* error,size_t error_size);
struct PreparationProfile {
 double requested_at=0,audio_ready_at=0,constructed_at=0;
 double render_cpu_ms=0,max_callback_ms=0,max_draw_ms=0,max_end_ms=0;
 double submission_wait_started=0,submission_ready_at=0;
 unsigned submission_wait_callbacks=0,pending_staging_at_settle=0,pending_staging_at_first_arm=0;
 bool submission_polled=false;
 uint64_t texture_upload_bytes=0;
 unsigned callbacks=0,source_draws=0,max_draw_calls=0,max_queued=0;
 int32_t queued_delta=0,created_delta=0;
 bool source_transition=false;
 void begin(bool transition,double now) noexcept {
  *this={};requested_at=now;source_transition=transition;
 }
 void construction_started(double now) noexcept {audio_ready_at=now;}
 void construction_finished(double now) noexcept {constructed_at=now;}
 void observe(double callback_ms,double draw_ms,double end_ms,bool source_draw,
              const AuroraStats& before,const AuroraStats& after) noexcept {
  ++callbacks;source_draws+=source_draw?1U:0U;
  render_cpu_ms+=callback_ms;max_callback_ms=std::max(max_callback_ms,callback_ms);
  max_draw_ms=std::max(max_draw_ms,draw_ms);max_end_ms=std::max(max_end_ms,end_ms);
  texture_upload_bytes+=after.lastTextureUploadSize;
  max_draw_calls=std::max(max_draw_calls,after.drawCallCount);
  max_queued=std::max(max_queued,after.queuedPipelines);
  queued_delta+=stat_delta(after.queuedPipelines,before.queuedPipelines);
  created_delta+=stat_delta(after.createdPipelines,before.createdPipelines);
 }
 void report(double settled_at) const {
  const double audio_wait=audio_ready_at?audio_ready_at-requested_at:0;
  const double construction=constructed_at?constructed_at-audio_ready_at:0;
  const double render_wait=constructed_at?settled_at-constructed_at:settled_at-requested_at;
  char profile[1280];
  std::snprintf(profile,sizeof(profile),
   "{\"source_transition\":%s,\"total_ms\":%.3f,\"audio_wait_ms\":%.3f,"
   "\"construction_ms\":%.3f,\"render_wait_ms\":%.3f,\"render_cpu_ms\":%.3f,"
   "\"callbacks\":%u,\"source_draws\":%u,\"max_callback_ms\":%.3f,"
   "\"max_draw_ms\":%.3f,\"max_end_ms\":%.3f,\"texture_upload_bytes\":%llu,"
   "\"max_draw_calls\":%u,\"max_queued\":%u,\"queued_delta\":%d,\"created_delta\":%d,"
   "\"gpu_completion_wait_ms\":%.3f,\"gpu_completion_wait_callbacks\":%u,"
   "\"pending_staging_at_settle\":%u,\"pending_staging_at_first_arm\":%u,"
   "\"gpu_completion_ready\":%s}",
   source_transition?"true":"false",settled_at-requested_at,audio_wait,construction,
   render_wait,render_cpu_ms,callbacks,source_draws,max_callback_ms,max_draw_ms,max_end_ms,
   static_cast<unsigned long long>(texture_upload_bytes),max_draw_calls,max_queued,
   queued_delta,created_delta,
   submission_wait_started?submission_ready_at-submission_wait_started:0,
   submission_wait_callbacks,pending_staging_at_settle,pending_staging_at_first_arm,
   submission_ready_at?"true":"false");
  EM_ASM({window.menuPreparationProfile?.(JSON.parse(UTF8ToString($0)));},profile);
 }
};
PreparationProfile preparation_profile;
PADStatus diagnostic_pad{};
unsigned diagnostic_pad_port=0,diagnostic_pad_remaining=0;
std::array<float,1068> pcm;
alignas(32) unsigned char fifo[64*1024];
constexpr std::array<std::string_view,68> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm","GrNBa.dat","sp_zako.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","PlMs.dat","PlMsAJ.dat","PlMsNr.dat","PlMsRe.dat","PlMsGr.dat","PlMsBk.dat","PlMsWh.dat","EfMsData.dat","mars.ssm","GrOp.dat","greens.hps","pupupu.ssm","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sislib_font.bin"};
constexpr unsigned kDiagnosticPadButtons=PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT|PAD_BUTTON_DOWN|PAD_BUTTON_UP|
 PAD_TRIGGER_Z|PAD_TRIGGER_R|PAD_TRIGGER_L|PAD_BUTTON_A|PAD_BUTTON_B|PAD_BUTTON_X|PAD_BUTTON_Y|PAD_BUTTON_START;
void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void clear_diagnostic_pad(){diagnostic_pad={};diagnostic_pad_port=0;diagnostic_pad_remaining=0;}
AuroraStats aurora_stats_snapshot(){
 AuroraStats result{};if(const AuroraStats* stats=aurora_get_stats())result=*stats;return result;
}
int32_t stat_delta(uint64_t after,uint64_t before){
 return static_cast<int32_t>(after)-static_cast<int32_t>(before);
}
void report_construction(const char* kind,double started,double constructed,double entered,
                         const AuroraStats& before,const AuroraStats& after){
 EM_ASM({if(window.menuConstructionTiming)window.menuConstructionTiming({
   kind:UTF8ToString($0),total_ms:$1,construct_ms:$2,entry_ms:$3,
   queued_delta:$4,created_delta:$5,draw_calls:$6,texture_upload_bytes:$7,
   queued_total:$8,created_total:$9,wasm_heap_bytes:$10
 });},kind,entered-started,constructed-started,entered-constructed,
        stat_delta(after.queuedPipelines,before.queuedPipelines),
        stat_delta(after.createdPipelines,before.createdPipelines),
        after.drawCallCount,after.lastTextureUploadSize,after.queuedPipelines,
        after.createdPipelines,emscripten_get_heap_size());
}
void begin_preparation(){
 if(!preparation.request())return;
 preparation_profile.begin(true,emscripten_get_now());
 clear_diagnostic_pad();pending=false;running=false;menu_clock.reset();
 bool preserve_audio=false;
 if(!match&&host_entered){
  char error[256]{};
  check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);host_entered=false;
  const int phase=melee_web_menu_host_phase(host);
  preserve_audio=phase!=5&&phase!=6;
 }
 transition_audio_continues=preserve_audio;
 if(!preserve_audio)audio_clock.reset();
 message=match?"Preparing original character select...":"Preparing original next scene...";
 EM_ASM({if(window.menuPreparation)window.menuPreparation(UTF8ToString($0),!!$1);},
        message.c_str(),preserve_audio?1:0);
}
bool prepare_deferred_pipelines(){
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 const auto selected=melee_web::pipeline_preparation::status();
 if(!selected.deferred_count)return false;
 // The lookup retained descriptor bytes only. Stop source time before the
 // renderer may construct a pipeline, then settle the unchanged scene.
 running=false;menu_clock.reset();audio_clock.reset();
 if(preparation.phase()==melee_web::MenuPreparationState::Phase::Idle){
  check(preparation.request_render_settle(),"Could not pause for pipeline preparation");
  preparation_profile.begin(false,emscripten_get_now());
  render_only_preparation=true;
 }
 message="Preparing first-use rendering...";
 check(aurora_pipeline_prepare_deferred()!=0,"Unexpected pipeline preparation failed");
 return true;
#else
 return false;
#endif
}
bool audio_ready_for_preparation(){
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 return EM_ASM_INT({return typeof window.menuAudioReadyForPreparation==='function'&&
    window.menuAudioReadyForPreparation()?1:0;})!=0;
#else
 return EM_ASM_INT({return window.menuAudioReadyForPreparation?
    (window.menuAudioReadyForPreparation()?1:0):1;})!=0;
#endif
}
void preparation_failed(const char* error){
 EM_ASM({window.menuPreparationFailed?.(UTF8ToString($0));},error?error:"Native preparation failed");
}
std::string selected_match_message(const MeleeWebMenuMatchSelection& selection){
 const auto* stage=melee_web_stage_content(selection.start.rules.stkind);
 std::string result="Original ";
 for(unsigned i=0;i<selection.player_count;++i){
  const auto* fighter=melee_web_fighter_content(selection.start.players[i].ckind);
  if(i)result+=" vs ";
  result+=fighter?fighter->name:"source fighter";
 }
 result+=" match on ";result+=stage?stage->name:"source stage";
 return result;
}
void close(){
 const bool had_lifetime=world||match||host;
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const bool had_source_world=melee_web_gameplay_generation()!=0;
 const melee_web::provenance::Scope provenance(pipeline_context(
     had_source_world?MELEE_WEB_PIPELINE_PHASE_TEARDOWN:MELEE_WEB_PIPELINE_PHASE_PREPARATION,
     had_source_world?MELEE_WEB_PIPELINE_SCENE_TEARDOWN:0));
#endif
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};running=false;pending=false;preparation.reset();menu_clock.reset();
 if(match){match->close();match.reset();}
 if(world){
  if(host_entered){check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);host_entered=false;}
  if(world_exposed)world->close();else world->close_prepared();
  world.reset();world_exposed=false;
 }
 if(host){check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;}
 if(replay&&replay_trace&&replay_final_draw&&!faulted)
  melee_web::retail_replay_end(replay->frames.size());
 replay.reset();replay_cursor=0;replay_trace=replay_pending=replay_started=replay_final_draw=false;
 replay_match_complete=false;replay_outcome=0;replay_winner=-1;
 audio_phase=0;faulted=false;diagnostic_start_ticks=0;stock_check=0;stock_tick=0;render_frame=0;first_use_draw_pending=false;render_only_preparation=false;transition_audio_continues=false;menu_scene_rebuild_pending=false;audio_clock.reset();clear_diagnostic_pad();
 match_message="Original source match";
 if(had_lifetime){
  const double finished=emscripten_get_now();
  report_construction("lifecycle-close",started,finished,finished,before,
                      aurora_stats_snapshot());
 }
}
void enter_world(){
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
 provenance_return_draw=completed_matches!=0;
#endif
 reference_heap_used=true;
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 // This owner entry is used for fresh CSS and match return. SSS uses
 // finish_menu_scene_rebuild after its explicit source transition.
 melee_web::pipeline_preparation::css();
#endif
 char error[256]{};const bool prepared=world!=nullptr;
 if(!prepared)world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 const double constructed=emscripten_get_now();
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);host_entered=true;world_exposed=true;
 const double entered=emscripten_get_now();
 report_construction("scene-enter",started,constructed,entered,before,aurora_stats_snapshot());
 first_use_draw_pending=true;
 menu_clock.reset();audio_phase=0;audio_clock.reset();running=true;
 message=melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
}
void advance(){
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
#endif
 char error[256]{};
 if(replay_pending){
  check(replay&&replay->initial_input,"Replay initialization is unavailable");
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  melee_web::pipeline_preparation::match(replay->selection);
#endif
  replay_pending=false;
  match=std::make_unique<melee_web::GameplayMatchSession>(files,replay->selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred,*replay->initial_input);
  running=false;message="Preparing reference replay...";return;
 }
 if(match){
  const bool checking_stock=stock_check==-1;
  const uint32_t seed=match->random_seed();
  {
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   const melee_web::provenance::Scope teardown(pipeline_context(
       MELEE_WEB_PIPELINE_PHASE_TEARDOWN,MELEE_WEB_PIPELINE_SCENE_TEARDOWN));
#endif
   match->close();match.reset();
  }
  if(checking_stock){
   int terminal_outcome=OUTCOME_NONE,terminal_count=0,terminal_winners[6]{};
   check(melee_web_match_rules_terminal_result(&terminal_outcome,&terminal_count,terminal_winners),
         "Stock diagnostic did not retain the source MatchEnd");
   check(terminal_outcome==OUTCOME_ELIMINATION&&terminal_count==1&&terminal_winners[0]==1&&
         stock_count==0&&stock_respawns==3,
         "Stock diagnostic: source MatchEnd winner or stock accounting was incorrect");
   stock_check=1;
  }
  ++completed_matches;
  check(melee_web_menu_host_match_finished(host,seed,error,sizeof(error)),error);
  pending=false;enter_world();return;
 }
 if(host_entered){check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);host_entered=false;}
 const int phase=melee_web_menu_host_phase(host);
 if(phase!=5&&phase!=6){
  pending=false;menu_clock.reset();
  pending_menu_scene=phase==2?melee_web::GameplayMenuScene::Stages:
                              melee_web::GameplayMenuScene::Characters;
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  world->begin_scene_rebuild();
  const double finished=emscripten_get_now();
  report_construction("scene-rebuild-step",started,finished,finished,before,
                      aurora_stats_snapshot());
  menu_scene_rebuild_pending=true;running=false;return;
 }
 world->close();world.reset();world_exposed=false;pending=false;menu_clock.reset();audio_phase=0;
 if(phase==5){
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  match_message=selected_match_message(selection);
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  melee_web::pipeline_preparation::match(selection);
#endif
  match=std::make_unique<melee_web::GameplayMatchSession>(
      files,selection,*archive_cache,melee_web::GameplayMatchConstruction::Deferred);
  const double constructed=emscripten_get_now();
  report_construction("match-enter-step",started,constructed,constructed,before,aurora_stats_snapshot());
  running=false;message="Preparing original match...";return;
 }
 if(phase==6){running=false;message="Original menu closed.";return;}
}

bool advance_match_construction(){
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
#endif
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 const bool complete=match->advance_construction();
 const double constructed=emscripten_get_now();
 report_construction(complete?"match-enter":"match-enter-step",started,constructed,constructed,
                     before,aurora_stats_snapshot());
 if(complete){
  if(replay&&replay_trace)melee_web::retail_replay_initial(*replay,true);
  first_use_draw_pending=true;running=true;message=match_message;
 }
 return complete;
}

void finish_menu_scene_rebuild(){
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 if(pending_menu_scene==melee_web::GameplayMenuScene::Stages)melee_web::pipeline_preparation::sss();
 else melee_web::pipeline_preparation::css();
#endif
 world->finish_scene_rebuild(pending_menu_scene);
 const double constructed=emscripten_get_now();
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
 host_entered=true;world_exposed=true;
 const double entered=emscripten_get_now();
 report_construction("scene-rebuild",started,constructed,entered,before,
                     aurora_stats_snapshot());
 menu_scene_rebuild_pending=false;first_use_draw_pending=true;
 menu_clock.reset();running=true;
 message=melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
}

void begin_transition_construction(double& preparation_ms,int& suppress_draw){
 if(!preparation.waiting_for_audio()||
    !preparation.begin_construction(audio_ready_for_preparation()))return;
 const double started=emscripten_get_now();
 preparation_profile.construction_started(started);
 advance();
 preparation_ms+=emscripten_get_now()-started;
 const bool construction_complete=!menu_scene_rebuild_pending&&
                                  (!match||match->construction_complete());
 if(!construction_complete)return;
 preparation_profile.construction_finished(emscripten_get_now());
 preparation.finish_construction(running);
 if(running)running=false;
 suppress_draw=preparation.suppress_source_draw();
 if(!preparation.busy())EM_ASM({window.menuPreparationDone?.();});
}

void log_message(AuroraLogLevel level,const char* module,const char* text,unsigned length){
 std::fprintf(level>=LOG_ERROR?stderr:stdout,"[%s] %.*s\n",module,int(length),text);
 if(level==LOG_FATAL)std::abort();
}
void render_audio_tick(MeleeWebAudio* audio,char* error,size_t error_size){
 audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
 check(melee_web_audio_render(audio,pcm.data(),count,error,error_size),error);
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 EM_ASM({window.menuAudio?.(HEAPF32.slice($0>>2,($0>>2)+$1*2));},pcm.data(),count);
#endif
}
bool render_cache_can_flush(){
 const AuroraStats* stats=aurora_get_stats();
 return !world&&!match&&!host&&!running&&!pending&&!preparation.busy()&&
        stats&&stats->queuedPipelines==0;
}
void service_render_cache_writes(){
 // SQLite fsync may Asyncify-yield. Run at the top-level main-loop boundary,
 // after source ownership is gone, never from a nested JS command/export or
 // between source scenes. Persistence time is separate from live callbacks.
 static bool failure_reported=false;
 if(!render_cache_can_flush())return;
 const auto state=aurora_pipeline_cache_status();
 if(state==AURORA_PIPELINE_CACHE_READY)return;
 if(state==AURORA_PIPELINE_CACHE_ERROR&&failure_reported)return;
 const bool flushed=state==AURORA_PIPELINE_CACHE_PENDING;
 const double started=emscripten_get_now();
 const bool ok=flushed&&aurora_flush_pipeline_cache();
 const double duration=emscripten_get_now()-started;
 if(!ok)failure_reported=true;
 EM_ASM({window.menuCacheWritesFlushed?.({ok:!!$0,flushed:!!$1,duration_ms:$2});},
        ok,flushed,duration);
}
void tick(){
 EM_ASM({window.menuServiceCommands?.();});
 service_render_cache_writes();
 const double started=emscripten_get_now();
 const bool running_at_callback_start=running;
 const AuroraStats stats_before=aurora_stats_snapshot();
 double input_done=started,simulation_done=started,begin_done=started,draw_done=started,end_done=started;
 double preparation_ms=0,preparation_started=0;
 double render_begin_ms=0,render_draw_ms=0,render_end_ms=0,render_total_ms=0,simulation_cpu_ms=0;
 uint32_t callback_draw_calls=0,callback_texture_upload=0,callback_staging_used=0;
 AuroraStats callback_begin_stats{};
 AuroraStats callback_end_stats{};
 melee_web::SourceFrameSequence source_frames;
 int began=0,drawn=1,timing_valid=1,first_use=0;
 // A transition request owns the whole callback in which it is observed.
 // Keep the source presenter out of both the request and audio-ack waits.
 int suppress_draw=preparation.suppress_source_draw(pending)||replay_final_draw;
 bool actual_source_draw=false;
 bool replay_completed_now=false;
 unsigned replay_steps=0;
 try{
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  (void)melee_web::pipeline_preparation::status();
  (void)prepare_deferred_pipelines();
#endif
  for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event){
   if(event->type==AURORA_EXIT){close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();return;}
  }
  const auto* input=melee_web_input_poll();
  input_done=emscripten_get_now();
  const double clock_now=emscripten_get_now();
  char error[256]{};
  const auto present_source_impl=[&](){
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
  const melee_web::provenance::Scope provenance(pipeline_context());
#endif
  bool drew_source=false;
  bool began_this_frame=false;
  const double render_started=emscripten_get_now();
  if(aurora_begin_frame()){
   began=1;began_this_frame=true;begin_done=emscripten_get_now();
   GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
   if(!suppress_draw){
    if(!faulted){
     if(match){
      match->draw();actual_source_draw=true;drew_source=true;
      if(replay&&replay_trace&&!replay_final_draw){
       if(replay_cursor)melee_web::retail_replay_draw(*replay,replay_cursor-1);
       else melee_web::retail_replay_preparation_draw(*replay);
      }
     }
     else if(world&&host_entered){drawn=melee_web_menu_host_draw(host,error,sizeof(error));actual_source_draw=drawn!=0;drew_source=drawn!=0;}
    }
   }
   draw_done=emscripten_get_now();
   aurora_end_frame();end_done=emscripten_get_now();check(drawn,error);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   melee_web_provenance_frame(0);
   if(drew_source)provenance_return_draw=false;
#endif
   if(replay&&!replay_final_draw&&replay_cursor==replay->frames.size()&&drew_source){
    /* The final source frame is drawn before this close-boundary publication;
     * report the canonical MatchEnd winner after its source ranking exists. */
    (void)melee_web_match_rules_publish_result();
    replay_outcome=melee_web_match_rules_outcome(&replay_winner);
    replay_final_draw=true;replay_completed_now=true;running=false;menu_clock.reset();
    message="Reference replay complete; all input consumed and final frame drawn.";
   }
   if(drew_source&&running){
    if(first_use_draw_pending){first_use=1;first_use_draw_pending=false;}
   }
  }
  else{begin_done=draw_done=end_done=emscripten_get_now();}

  render_begin_ms+=begin_done-render_started;
  render_draw_ms+=draw_done-begin_done;
  render_end_ms+=end_done-draw_done;
  render_total_ms+=end_done-render_started;
  const auto rendered=aurora_stats_snapshot();
  callback_begin_stats.lastBeginFrameId=rendered.lastBeginFrameId;
  callback_begin_stats.lastBeginFrameFrameSlotMs+=rendered.lastBeginFrameFrameSlotMs;
  callback_begin_stats.lastBeginFrameFrameSlotWaitMs+=rendered.lastBeginFrameFrameSlotWaitMs;
  callback_begin_stats.lastBeginFrameFrameSlotWaitCount+=rendered.lastBeginFrameFrameSlotWaitCount;
  callback_begin_stats.lastBeginFrameStagingSlotMs+=rendered.lastBeginFrameStagingSlotMs;
  callback_begin_stats.lastBeginFrameStagingSlotWaitMs+=rendered.lastBeginFrameStagingSlotWaitMs;
  callback_begin_stats.lastBeginFrameStagingSlotWaitCount+=rendered.lastBeginFrameStagingSlotWaitCount;
  callback_begin_stats.lastBeginFramePacketMs+=rendered.lastBeginFramePacketMs;
  callback_begin_stats.lastBeginFrameRecordMs+=rendered.lastBeginFrameRecordMs;
  callback_begin_stats.lastBeginFramePipelineMs+=rendered.lastBeginFramePipelineMs;
  callback_begin_stats.lastBeginFrameWorkerMs+=rendered.lastBeginFrameWorkerMs;
  callback_begin_stats.lastBeginFrameEncoderMs+=rendered.lastBeginFrameEncoderMs;
  callback_begin_stats.lastBeginFrameTotalMs+=rendered.lastBeginFrameTotalMs;
  callback_begin_stats.lastBeginFrameResidualMs+=rendered.lastBeginFrameResidualMs;
  callback_begin_stats.lastBeginFrameStartMs=rendered.lastBeginFrameStartMs;
  callback_begin_stats.lastBeginFrameEndMs=rendered.lastBeginFrameEndMs;
  if(rendered.lastBeginFrameMaxWaitMs>callback_begin_stats.lastBeginFrameMaxWaitMs){
   callback_begin_stats.lastBeginFrameMaxWaitMs=rendered.lastBeginFrameMaxWaitMs;
   callback_begin_stats.lastBeginFrameMaxWaitStartMs=rendered.lastBeginFrameMaxWaitStartMs;
   callback_begin_stats.lastBeginFrameMaxWaitEndMs=rendered.lastBeginFrameMaxWaitEndMs;
   callback_begin_stats.lastBeginFrameMaxWaitKind=rendered.lastBeginFrameMaxWaitKind;
  }
  callback_begin_stats.lastBeginFrameOuterSurfaceMs+=rendered.lastBeginFrameOuterSurfaceMs;
  callback_begin_stats.lastBeginFrameOuterImguiMs+=rendered.lastBeginFrameOuterImguiMs;
  callback_begin_stats.lastBeginFrameOuterFifoMs+=rendered.lastBeginFrameOuterFifoMs;
  callback_begin_stats.lastBeginFrameOuterTotalMs+=rendered.lastBeginFrameOuterTotalMs;
  callback_begin_stats.lastBeginFrameOuterResidualMs+=rendered.lastBeginFrameOuterResidualMs;
  if(began_this_frame){
   callback_draw_calls+=rendered.drawCallCount;
   callback_texture_upload+=rendered.lastTextureUploadSize;
   callback_staging_used+=rendered.lastVertSize+rendered.lastUniformSize+
                         rendered.lastIndexSize+rendered.lastStorageSize+
                         rendered.lastTextureUploadSize;
   callback_end_stats.lastEndFrameId=rendered.lastEndFrameId;
   callback_end_stats.lastEndFrameFifoTextureMs+=rendered.lastEndFrameFifoTextureMs;
   callback_end_stats.lastEndFrameGfxFinishMs+=rendered.lastEndFrameGfxFinishMs;
   callback_end_stats.lastEndFrameStagingWritesMs+=rendered.lastEndFrameStagingWritesMs;
   callback_end_stats.lastEndFrameSurfaceEncodeMs+=rendered.lastEndFrameSurfaceEncodeMs;
   callback_end_stats.lastEndFrameEncoderFinishMs+=rendered.lastEndFrameEncoderFinishMs;
   callback_end_stats.lastEndFrameQueueSubmitMs+=rendered.lastEndFrameQueueSubmitMs;
   callback_end_stats.lastEndFrameCleanupMs+=rendered.lastEndFrameCleanupMs;
   callback_end_stats.lastEndFrameOuterPrepMs+=rendered.lastEndFrameOuterPrepMs;
   callback_end_stats.lastEndFrameRecordMs+=rendered.lastEndFrameRecordMs;
   callback_end_stats.lastEndFramePacketMs+=rendered.lastEndFramePacketMs;
   callback_end_stats.lastEndFrameCallbackMs+=rendered.lastEndFrameCallbackMs;
   callback_end_stats.lastEndFrameCallbackPostSubmitMs+=rendered.lastEndFrameCallbackPostSubmitMs;
   callback_end_stats.lastEndFrameObserverMs+=rendered.lastEndFrameObserverMs;
   callback_end_stats.lastEndFrameCallbackResidualMs+=rendered.lastEndFrameCallbackResidualMs;
   callback_end_stats.lastEndFrameTailMs+=rendered.lastEndFrameTailMs;
   callback_end_stats.lastEndFrameWorkerMs+=rendered.lastEndFrameWorkerMs;
   callback_end_stats.lastEndFrameWorkerResidualMs+=rendered.lastEndFrameWorkerResidualMs;
   callback_end_stats.lastEndFrameTotalMs+=rendered.lastEndFrameTotalMs;
   callback_end_stats.lastEndFrameResidualMs+=rendered.lastEndFrameResidualMs;
  }
  return drew_source;
  };
  const auto present_source=[&](){
   const bool drew_source=present_source_impl();
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   // The source draw scope is closed before exporting this ordered chunk.
   drain_pipeline_provenance();
#endif
   return drew_source;
  };
  const bool audio_before_construction=transition_audio_continues&&
                                       (!running||preparation.busy());
  MeleeWebAudio* const audio_owner=match?match->audio():world?world->audio():nullptr;
  const auto audio_elapsed=audio_clock.tick(
      clock_now,audio_owner&&input->visible&&(running||transition_audio_continues));
  if(audio_elapsed.stalled){
   // Keep the shared timing-pause prefix understood by the development host.
   // Its state capture may resume and records every resume; performance capture
   // still fails on the pause. The audio guard and clock policy are unchanged.
   running=false;message="Paused after a timing disruption in the audio clock. Resume to continue.";
  }else if(audio_before_construction){
   for(unsigned step=0;step<audio_elapsed.steps;step++)
    render_audio_tick(audio_owner,error,sizeof(error));
  }
  if(preparation.waiting_for_audio()){
   begin_transition_construction(preparation_ms,suppress_draw);
  }else if(preparation.phase()==melee_web::MenuPreparationState::Phase::Constructing){
   preparation_started=emscripten_get_now();
   const bool construction_complete=menu_scene_rebuild_pending?
       (finish_menu_scene_rebuild(),true):advance_match_construction();
   if(construction_complete){
    preparation_profile.construction_finished(emscripten_get_now());
    preparation.finish_construction(true);running=false;
   }
   preparation_ms=emscripten_get_now()-preparation_started;preparation_started=0;
   suppress_draw=preparation.suppress_source_draw();
  }else if(preparation.arming()){
   // The final preparation draw is already submitted. Keep its image and let
   // the browser deliver completion callbacks; never redraw or advance source
   // state just to wait for a staging buffer to become reusable.
   const auto submitted=aurora_browser_submission_status();
   if(!preparation_profile.submission_polled){
    preparation_profile.pending_staging_at_first_arm=submitted.pendingStagingBuffers;
    preparation_profile.submission_polled=true;
   }
   const bool complete=submitted.pendingFramePackets==0&&submitted.pendingStagingBuffers==0&&
                       submitted.workerBusy==0;
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
   const bool pipelines_ready=melee_web::pipeline_preparation::status().ready&&audio_ready_for_preparation();
#else
   const bool pipelines_ready=true;
#endif
   if(preparation.arm(complete&&pipelines_ready)){
    const double ready_at=emscripten_get_now();
    preparation_profile.submission_ready_at=ready_at;
    preparation_profile.report(ready_at);
    EM_ASM({window.menuRenderCacheSettled?.();});
    if(render_only_preparation)render_only_preparation=false;
    else EM_ASM({window.menuPreparationDone?.();});
    running=!pending;menu_clock.reset();suppress_draw=preparation.suppress_source_draw(pending);
    message=match?match_message:melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
   }else{
    ++preparation_profile.submission_wait_callbacks;
    check(emscripten_get_now()-preparation_profile.submission_wait_started<10000,
          "GPU completion timed out during scene preparation");
   }
  }else if(pending){
   begin_preparation();
  }
  // Preparation can reset the simulation clock after doing substantial native
  // construction work. Sample again here so that work is not charged to the
  // first active gameplay interval on the following callback.
  const double simulation_clock_now=emscripten_get_now();
  const auto elapsed=menu_clock.tick(
      simulation_clock_now,running&&(world||match)&&input->visible);
  if(elapsed.stalled){running=false;message="Paused after a timing disruption. Resume to continue.";}
  if(elapsed.steps&&transition_audio_continues){
   transition_audio_continues=false;
  }
  for(unsigned step=0;step<elapsed.steps;step++){
   if(replay&&replay_cursor==replay->frames.size())break;
   source_frames.before_step(present_source);
   if(prepare_deferred_pipelines())break;
   PADStatus checked_input[4];const PADStatus* sample=input->raw;bool copied_input=false;
   bool diagnostic_start_pulse=false;
   if(diagnostic_start_ticks){
    std::copy(input->raw,input->raw+4,checked_input);checked_input[0].button|=PAD_BUTTON_START;
    sample=checked_input;copied_input=true;diagnostic_start_pulse=true;--diagnostic_start_ticks;
   }
   if(match&&stock_check==-1){
    std::fill(checked_input,checked_input+4,PADStatus{});checked_input[2].err=checked_input[3].err=PAD_ERR_NO_CONTROLLER;
    if(stock_tick>=20&&!stock_lost)checked_input[0].stickX=80;
    if(stock_jump)checked_input[0].button=PAD_BUTTON_X;
    sample=checked_input;copied_input=true;
   }
   if(diagnostic_pad_remaining){
    if(!copied_input)std::copy(input->raw,input->raw+4,checked_input);
    // Keep a queued diagnostic sample scoped to its selected port.  If an
    // older command has already put a Start pulse in flight, preserve that
    // pulse when the selected port is replaced.
    const PADStatus queued_sample=diagnostic_pad;
    checked_input[diagnostic_pad_port]=queued_sample;
    if(diagnostic_start_pulse)checked_input[diagnostic_pad_port].button|=PAD_BUTTON_START;
    sample=checked_input;
    --diagnostic_pad_remaining;
   }
   int result=1;
   if(match){
    if(replay){
     check(!match->paused()&&!match->complete(),"Replay reached an unsupported source pause/exit");
     if(!replay_started){
      replay_started=true;
      EM_ASM({window.menuReplayStarted?.($0,!!$1);},replay->frames.size(),replay_trace);
     }
     sample=replay->frames[replay_cursor].pads.data();
    }
    match->tick(sample);source_frames.did_step();
    int winner=-1;const int outcome=match->outcome(winner);
    if(replay){
     replay_match_complete=match->complete();replay_outcome=outcome;replay_winner=winner;
     if(replay_trace)melee_web::retail_replay_frame(*replay,replay_cursor);
     ++replay_cursor;
     ++replay_steps;
     check(!match->complete()||replay_cursor==replay->frames.size(),"Replay source match exited before all input was consumed");
    }
    if(stock_check==-1){
     const auto player=match->player_stats(0);
     check(match->player_stats(1).stocks==4,"Stock diagnostic: stationary opponent lost a stock");
     stock_jump=!stock_lost&&stock_count<4&&player.ground_or_air==0&&player.position[0]>65;
     if(player.stocks<stock_count){stock_lost=true;stock_count=player.stocks;}
     if(stock_lost&&player.motion_id==14&&player.ground_or_air==0){stock_lost=false;++stock_respawns;}
     ++stock_tick;
     if(outcome)check(outcome==OUTCOME_ELIMINATION&&stock_count==0&&stock_respawns==3,"Stock diagnostic: unexpected source outcome");
     if(!match->complete())check(stock_tick<4000,"Stock diagnostic: no source exit after 4000 ticks");
    }
    if(match->complete()&&!replay){check(outcome,"Original match transitioned without an outcome");pending=true;result=3;}
   }
   else{result=melee_web_menu_host_tick(host,sample,error,sizeof(error));check(result==1||result==3,error);source_frames.did_step();}
   if(result==3){pending=true;clear_diagnostic_pad();break;}
  }
  if(!audio_before_construction&&!audio_elapsed.stalled)
   for(unsigned step=0;step<audio_elapsed.steps;step++)
    render_audio_tick(audio_owner,error,sizeof(error));
  simulation_done=emscripten_get_now();
  simulation_cpu_ms=std::max(0.0,simulation_done-input_done-preparation_ms-render_total_ms);
  source_frames.finish(present_source);
  (void)prepare_deferred_pipelines();
  // Camera callbacks mutate source state (including magnifier damage flags).
  // A callback without a source tick must retain the last image; preparation
  // alone may redraw a frozen scene to settle its explicitly measured resources.
  if(source_frames.steps()==0&&(preparation.warming()||(!world&&!match)))present_source();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  (void)melee_web::pipeline_preparation::status();
#endif
  check(!replay_steps||source_frames.draws()==replay_steps,
        "Reference replay did not draw every consumed source tick");
 }catch(const std::exception& e){running=false;faulted=true;preparation.reset();render_only_preparation=false;pending=false;clear_diagnostic_pad();menu_clock.reset();message=e.what();if(preparation_started)preparation_ms=emscripten_get_now()-preparation_started;preparation_failed(e.what());timing_valid=0;std::fprintf(stderr,"Native menu: %s\n",e.what());
  const double failed=emscripten_get_now();
  if(input_done<started)input_done=failed;
  if(simulation_done<input_done)simulation_done=failed;
  if(begin_done<simulation_done)begin_done=simulation_done;
  if(draw_done<begin_done)draw_done=begin_done;
  if(end_done<draw_done)end_done=draw_done;
 }
 const double finished=emscripten_get_now();
 const AuroraStats stats_after=aurora_stats_snapshot();
 const bool render_preparation_activity=
  stat_delta(stats_after.queuedPipelines,stats_before.queuedPipelines)!=0||
  stat_delta(stats_after.createdPipelines,stats_before.createdPipelines)!=0||
  stats_after.lastTextureUploadSize!=0;
 const bool was_warming=preparation.warming();
 if(was_warming)preparation_profile.observe(finished-started,render_draw_ms,render_end_ms,
                                             actual_source_draw,stats_before,stats_after);
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 AuroraPipelinePrepareStatus selected_pipelines{};
 aurora_pipeline_prepare_status(&selected_pipelines);
 if(selected_pipelines.ready&&!pipeline_union_first_ready)pipeline_union_first_ready=finished;
 EM_ASM({Module.pipelinePreparation=({state:$0,ready:!!$1,selected:$2,pending:$3,
   unexpected_count:$4,deferred_count:$5,error_count:$6,binding_sha256:UTF8ToString($7),
   renderer_init_ms:$8,union_submit_ms:$9,union_ready_ms:$10,bootstrap_total_ms:$11});},
   selected_pipelines.state,selected_pipelines.ready,selected_pipelines.unique_count,
   selected_pipelines.pending_count,selected_pipelines.unexpected_count,
   selected_pipelines.deferred_count,selected_pipelines.error_count,
   MELEE_WEB_PIPELINE_PREPARATION_BINDING_SHA256,pipeline_renderer_init_ms,pipeline_union_submit_ms,
   pipeline_union_first_ready?pipeline_union_first_ready-pipeline_union_requested:0,
   pipeline_union_first_ready?pipeline_union_first_ready-pipeline_bootstrap_started:0);
 const unsigned pending_selected=selected_pipelines.ready?0:1;
#else
 const unsigned pending_selected=0;
#endif
 if(preparation.observe_render(actual_source_draw,stats_after.queuedPipelines+pending_selected,render_preparation_activity)){
  preparation_profile.submission_wait_started=finished;
  preparation_profile.pending_staging_at_settle=
      aurora_browser_submission_status().pendingStagingBuffers;
 }
 // A texture upload is complete by the time it is reported here, so pausing
 // source simulation afterward cannot hide its cost. Newly constructed scenes
 // still settle both uploads and pipelines above. During live play, only an
 // outstanding asynchronous pipeline compilation justifies stopping the clock.
 if(preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&running&&actual_source_draw&&
    melee_web::MenuPreparationState::needs_live_render_settle(stats_after.queuedPipelines)){
  if(preparation.request_render_settle()){
   preparation_profile.begin(false,finished);
   render_only_preparation=true;
   running=false;menu_clock.reset();message="Preparing first-use rendering...";
  }
 }
 char timing[4096];
 const uint32_t staging_used_bytes=callback_staging_used;
 const int timing_written=std::snprintf(timing,sizeof(timing),
  "{\"frame\":%u,\"started\":%.3f,\"valid\":%d,\"first_use\":%d,"
  "\"input_ms\":%.3f,\"simulation_audio_ms\":%.3f,\"preparation_ms\":%.3f,"
  "\"begin_phases\":{\"frame_slot_ms\":%.3f,\"frame_slot_wait_ms\":%.3f,"
  "\"frame_slot_wait_count\":%u,\"staging_slot_ms\":%.3f,"
  "\"staging_slot_wait_ms\":%.3f,\"staging_slot_wait_count\":%u,"
  "\"packet_ms\":%.3f,\"record_ms\":%.3f,\"pipeline_ms\":%.3f,"
  "\"worker_ms\":%.3f,\"encoder_ms\":%.3f,\"total_ms\":%.3f,"
  "\"residual_ms\":%.3f,\"outer_surface_ms\":%.3f,"
  "\"outer_imgui_ms\":%.3f,\"outer_fifo_ms\":%.3f,"
  "\"outer_total_ms\":%.3f,\"outer_residual_ms\":%.3f,"
  "\"start_ms\":%.3f,\"end_ms\":%.3f,\"max_wait_ms\":%.3f,"
  "\"max_wait_start_ms\":%.3f,\"max_wait_end_ms\":%.3f,\"max_wait_kind\":%u,"
  "\"last_frame_id\":%llu},"
  "\"begin_ms\":%.3f,\"draw_ms\":%.3f,\"end_ms\":%.3f,\"total_ms\":%.3f,"
  "\"end_phases\":{\"last_frame\":%llu,\"fifo_texture_ms\":%.3f,\"gfx_finish_ms\":%.3f,"
  "\"staging_writes_ms\":%.3f,\"surface_encode_ms\":%.3f,\"encoder_finish_ms\":%.3f,"
  "\"queue_submit_ms\":%.3f,\"cleanup_ms\":%.3f,"
  "\"outer_prep_ms\":%.3f,"
  "\"record_ms\":%.3f,"
  "\"packet_ms\":%.3f,"
  "\"callback_ms\":%.3f,"
  "\"callback_post_submit_ms\":%.3f,"
  "\"observer_ms\":%.3f,"
  "\"callback_residual_ms\":%.3f,"
  "\"tail_ms\":%.3f,"
  "\"worker_ms\":%.3f,"
  "\"worker_residual_ms\":%.3f,"
  "\"total_ms\":%.3f,"
  "\"residual_ms\":%.3f},"
  "\"began\":%d,\"drawn\":%d,\"queued_delta\":%d,\"created_delta\":%d,"
  "\"queued_total\":%u,\"created_total\":%u,\"draw_calls\":%u,"
  "\"texture_upload_bytes\":%u,\"staging_used_bytes\":%u,"
  "\"wasm_heap_bytes\":%zu,\"draw_suppressed\":%d,\"source_steps\":%zu,\"source_draws\":%zu}",
  ++render_frame,started,timing_valid,first_use,input_done-started,
  simulation_cpu_ms,preparation_ms,
  callback_begin_stats.lastBeginFrameFrameSlotMs,callback_begin_stats.lastBeginFrameFrameSlotWaitMs,
  callback_begin_stats.lastBeginFrameFrameSlotWaitCount,
  callback_begin_stats.lastBeginFrameStagingSlotMs,callback_begin_stats.lastBeginFrameStagingSlotWaitMs,
  callback_begin_stats.lastBeginFrameStagingSlotWaitCount,
  callback_begin_stats.lastBeginFramePacketMs,callback_begin_stats.lastBeginFrameRecordMs,
  callback_begin_stats.lastBeginFramePipelineMs,callback_begin_stats.lastBeginFrameWorkerMs,
  callback_begin_stats.lastBeginFrameEncoderMs,callback_begin_stats.lastBeginFrameTotalMs,
  callback_begin_stats.lastBeginFrameResidualMs,callback_begin_stats.lastBeginFrameOuterSurfaceMs,
  callback_begin_stats.lastBeginFrameOuterImguiMs,callback_begin_stats.lastBeginFrameOuterFifoMs,
  callback_begin_stats.lastBeginFrameOuterTotalMs,callback_begin_stats.lastBeginFrameOuterResidualMs,
  callback_begin_stats.lastBeginFrameStartMs,callback_begin_stats.lastBeginFrameEndMs,
  callback_begin_stats.lastBeginFrameMaxWaitMs,callback_begin_stats.lastBeginFrameMaxWaitStartMs,
  callback_begin_stats.lastBeginFrameMaxWaitEndMs,callback_begin_stats.lastBeginFrameMaxWaitKind,
  static_cast<unsigned long long>(callback_begin_stats.lastBeginFrameId),
  render_begin_ms,render_draw_ms,render_end_ms,
  finished-started,static_cast<unsigned long long>(callback_end_stats.lastEndFrameId),
  callback_end_stats.lastEndFrameFifoTextureMs,callback_end_stats.lastEndFrameGfxFinishMs,
  callback_end_stats.lastEndFrameStagingWritesMs,callback_end_stats.lastEndFrameSurfaceEncodeMs,
  callback_end_stats.lastEndFrameEncoderFinishMs,callback_end_stats.lastEndFrameQueueSubmitMs,
  callback_end_stats.lastEndFrameCleanupMs,
  callback_end_stats.lastEndFrameOuterPrepMs,
  callback_end_stats.lastEndFrameRecordMs,
  callback_end_stats.lastEndFramePacketMs,
  callback_end_stats.lastEndFrameCallbackMs,
  callback_end_stats.lastEndFrameCallbackPostSubmitMs,
  callback_end_stats.lastEndFrameObserverMs,
  callback_end_stats.lastEndFrameCallbackResidualMs,
  callback_end_stats.lastEndFrameTailMs,
  callback_end_stats.lastEndFrameWorkerMs,
  callback_end_stats.lastEndFrameWorkerResidualMs,
  callback_end_stats.lastEndFrameTotalMs,
  callback_end_stats.lastEndFrameResidualMs,
  began,drawn,
  stat_delta(stats_after.queuedPipelines,stats_before.queuedPipelines),
  stat_delta(stats_after.createdPipelines,stats_before.createdPipelines),
  stats_after.queuedPipelines,stats_after.createdPipelines,
  callback_draw_calls,callback_texture_upload,staging_used_bytes,
  emscripten_get_heap_size(),suppress_draw,source_frames.steps(),source_frames.draws());
 if(timing_written<0||static_cast<size_t>(timing_written)>=sizeof(timing)){
  timing_valid=0;
  std::snprintf(timing,sizeof(timing),"{\"frame\":%u,\"valid\":0,\"timing_truncated\":true}",render_frame);
  EM_ASM({
   const text=UTF8ToString($0);
   if(window.menuRuntimeTimingError)window.menuRuntimeTimingError(text);
   else console.error(text);
  },timing_written<0?"Native menu timing JSON formatting failed":"Native menu timing JSON exceeded 4096 bytes");
 }
 EM_ASM({if(window.menuRuntimeTiming)window.menuRuntimeTiming(JSON.parse(UTF8ToString($0)));},timing);
 EM_ASM({window.menuFrame?.(!!$0);},running_at_callback_start?1:0);
 if(replay_completed_now)EM_ASM({window.menuReplayCompleted?.($0,!!$1,$2,$3);},
                                replay_cursor,replay_match_complete?1:0,
                                replay_outcome,replay_winner);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 // Preserve lifecycle records from callbacks that did not draw a source frame.
 drain_pipeline_provenance();
#endif
}
}
extern "C" {
int melee_web_native_menu_file(const char* name,const uint8_t* data,unsigned size){try{
 if(world||match||!name||!data||!size||size>64*1024*1024)throw std::runtime_error("Unload before importing valid local files");
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 if(std::string_view{name}=="dsp_coef.bin")throw std::runtime_error("Public audio-disabled runtime does not accept DSP coefficient input");
#endif
 bool known=false;for(auto key:keys)known|=key==name;
 if(!known)throw std::runtime_error("Unknown native menu file: "+std::string(name));
 archive_cache.reset();
 files[name]={data,data+size};return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_prepare(){try{
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
#endif
 if(match||host_entered)throw std::runtime_error("Unload before preparing native menu resources");
 if(world&&host){message="Native menu resources already prepared.";return 1;}
 if(world||host)close();
 char error[256]{};const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 // Preserve the source ownership order used by launch: the menu host claims
 // RNG/session ownership before the SDK world is started. No source scene or
 // simulation callback runs during this preparation phase.
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
 // One unentered menu preparation belongs to the canonical fresh import.
 // Repeating it changes allocation history even without entering a source scene.
 if(reference_menu_preparations++)reference_heap_used=true;
 world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 const double constructed=emscripten_get_now();
 report_construction("scene-prepare",started,constructed,constructed,before,aurora_stats_snapshot());
 message="Native menu resources prepared.";return 1;
}catch(const std::exception& e){
 if(world&&!host_entered){try{world->close_prepared();}catch(...){}}
 world.reset();
 if(host&&!host_entered){char ignored[256]{};melee_web_menu_host_destroy(host,ignored,sizeof(ignored));host=nullptr;}
 message=e.what();return 0;
}}
int melee_web_native_menu_launch(){try{
 if(preparation.busy()||pending)
  throw std::runtime_error("Native menu transition is still preparing");
 if(match||host_entered||(host&&!world))close();
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 char error[256]{};if(!host){host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);}
 VISetFrameBufferScale(1);enter_world();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 // The initial owner has entered but has not advanced a source tick. Settle
 // its certified union through the same draw/submission gate as transitions.
 check(preparation.request_render_settle(),"Initial pipeline preparation is already active");
 preparation_profile.begin(true,emscripten_get_now());
 running=false;menu_clock.reset();
 message="Preparing original character select...";
 EM_ASM({window.menuPreparation?.(UTF8ToString($0),false);},message.c_str());
#endif
 return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_replay(const uint8_t* data,unsigned size,int observe){try{
 check(data&&size<=melee_web::kRetailReplayMaxBytes,"Invalid reference replay bytes");
 check(observe==0||observe==1,"Invalid replay observation mode");
 auto candidate=std::make_unique<melee_web::RetailReplayRecipe>(melee_web::read_retail_replay({data,size}));
 check(candidate->version>=2&&candidate->initial_input,"Browser reference playback requires a PAD history recipe (v2 or v3)");
 check(!reference_heap_used,"Reference replay requires a fresh application. Use Reload application state, import the disc, then play the recipe before entering menus.");
 reference_heap_used=true;
 close();
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 replay=std::move(candidate);replay_trace=observe;replay_pending=true;
 match_message="Reference replay: "+selected_match_message(replay->selection);
 check(preparation.request(),"Replay preparation is already active");
 preparation_profile.begin(true,emscripten_get_now());
 VISetFrameBufferScale(1);
 message="Preparing reference replay...";
 EM_ASM({window.menuPreparation?.(UTF8ToString($0));},message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
unsigned melee_web_native_menu_replay_cursor(){return static_cast<unsigned>(replay_cursor);}
void melee_web_native_menu_pause(int paused){
 if(faulted||replay_final_draw||preparation.busy()||pending||(!host_entered&&!match))return;
 running=(world||match)&&!paused;menu_clock.reset();
 message=running?(match?match_message:melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select"):"Paused.";
}
void melee_web_native_menu_confirm_check(){
 if(!replay&&(host_entered||match)&&!faulted&&!preparation.busy()&&!pending&&stock_check!=-1&&diagnostic_pad_remaining==0)
  diagnostic_start_ticks=3;
}
int melee_web_native_menu_pad_sample_full(unsigned port,unsigned buttons,int stick_x,int stick_y,
                                          int cstick_x,int cstick_y,unsigned trigger_l,
                                          unsigned trigger_r,unsigned duration){try{
 if(replay||faulted||preparation.busy()||pending||stock_check==-1||diagnostic_start_ticks!=0||!running||(!host_entered&&!match))
  throw std::runtime_error("Raw PAD samples require an active, non-diagnostic scene");
 if(port>1||buttons>0xffffU||(buttons&~kDiagnosticPadButtons)||stick_x<-80||stick_x>80||stick_y<-80||stick_y>80||
    cstick_x<-80||cstick_x>80||cstick_y<-80||cstick_y>80||trigger_l>255||trigger_r>255||duration<1||duration>120)
  throw std::runtime_error("Raw PAD sample is outside the supported port, button, axis or duration bounds");
 if(diagnostic_pad_remaining)throw std::runtime_error("A raw PAD sample is already queued");
 diagnostic_pad={};diagnostic_pad.err=PAD_ERR_NONE;diagnostic_pad.button=static_cast<u16>(buttons);
 diagnostic_pad.stickX=static_cast<s8>(stick_x);diagnostic_pad.stickY=static_cast<s8>(stick_y);
 diagnostic_pad.substickX=static_cast<s8>(cstick_x);diagnostic_pad.substickY=static_cast<s8>(cstick_y);
 diagnostic_pad.triggerLeft=static_cast<u8>(trigger_l);diagnostic_pad.triggerRight=static_cast<u8>(trigger_r);
 diagnostic_pad_port=port;diagnostic_pad_remaining=duration;message="Raw PAD sample queued at the next source tick.";return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_pad_sample(unsigned port,unsigned buttons,int stick_x,int stick_y,unsigned duration){
 return melee_web_native_menu_pad_sample_full(port,buttons,stick_x,stick_y,0,0,0,0,duration);
}
int melee_web_native_menu_player_state(unsigned player,int* fighter_kind,int* motion_id,
                                       int* ground_or_air,unsigned* source_frame,
                                       float* position_x,float* position_y){try{
 if(!fighter_kind||!motion_id||!ground_or_air||!source_frame||!position_x||!position_y||
    player>1||!match||!match->ready())
  throw std::runtime_error("Player state requires a ready source match and valid output storage");
 const auto stats=match->player_stats(player);*fighter_kind=match->fighter_kind(player);
 *motion_id=stats.motion_id;*ground_or_air=stats.ground_or_air;*source_frame=match->source_frames();
 *position_x=stats.position[0];*position_y=stats.position[1];return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_drive_fighter(int character_kind){try{
 if(!host||melee_web_menu_host_phase(host)!=1)throw std::runtime_error("Fighter selection drive requires the original CSS");
 MeleeWebFighterInputObservation observed{};check(melee_web_fighter_input_observe(character_kind,&observed),"CSS target observation is unavailable");
 PADStatus raw[PAD_MAX_CONTROLLERS]{};const int state=melee_web_fighter_input_drive(raw,&observed,character_kind);
 check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,"CSS target observation is invalid");
 if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED)return 2;
 if(state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY)
  check(melee_web_fighter_input_button(raw,PAD_BUTTON_A),"CSS target button sample failed");
 check(melee_web_native_menu_pad_sample_full(observed.cursor_port,raw[observed.cursor_port].button,
       raw[observed.cursor_port].stickX,raw[observed.cursor_port].stickY,
       raw[observed.cursor_port].substickX,raw[observed.cursor_port].substickY,
       raw[observed.cursor_port].triggerLeft,raw[observed.cursor_port].triggerRight,1),message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_drive_stage(int stage_kind){try{
 if(!host||melee_web_menu_host_phase(host)!=3)throw std::runtime_error("Stage selection drive requires the original SSS");
 MeleeWebStageInputObservation observed{};check(melee_web_stage_input_observe(stage_kind,&observed),"SSS target observation is unavailable");
 PADStatus raw[PAD_MAX_CONTROLLERS]{};const int state=melee_web_stage_input_drive(raw,&observed,stage_kind);
 check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"SSS target observation is invalid");if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET)return 2;
 check(melee_web_native_menu_pad_sample_full(0,raw[0].button,raw[0].stickX,raw[0].stickY,
       raw[0].substickX,raw[0].substickY,raw[0].triggerLeft,raw[0].triggerRight,1),message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_stock_check_ready(){
 return !replay&&match&&!faulted&&running&&match->ready()&&!match->paused()&&
        !match->ending()&&stock_check!=-1&&diagnostic_start_ticks==0&&diagnostic_pad_remaining==0;
}
int melee_web_native_menu_stock_check(){
 if(!melee_web_native_menu_stock_check_ready()||
    match->player_stats(0).stocks!=4||match->player_stats(1).stocks!=4)return 0;
 stock_check=-1;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;
 running=true;menu_clock.reset();return 1;
}
const char* melee_web_native_menu_memory(){
 // Lifecycle diagnostics only: mallinfo walks the allocator's free lists.
 // Reserved linear memory is not the same as live allocations and cannot shrink.
 const auto info=mallinfo();
 static char text[512];
 std::snprintf(text,sizeof(text),
  "{\"wasm_heap_bytes\":%zu,\"allocator_arena_bytes\":%zu,"
  "\"allocator_live_bytes\":%zu,\"allocator_free_bytes\":%zu,"
  "\"allocator_top_free_bytes\":%zu,"
  "\"match_present\":%s,\"menu_present\":%s}",
  emscripten_get_heap_size(),info.arena,info.uordblks,info.fordblks,info.keepcost,
  match?"true":"false",world?"true":"false");
 return text;
}
const char* melee_web_native_menu_diagnostics(){
 static char text[640];
 std::snprintf(text,sizeof(text),"Completed matches: %u · stock check: %d · ticks: %u · stocks: %d · respawns: %d",
  completed_matches,stock_check,stock_tick,stock_count,stock_respawns);
 const auto length=std::char_traits<char>::length(text);
 if(diagnostic_pad_remaining)
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: port %u buttons 0x%04x stick [%d,%d] remaining %u",
                diagnostic_pad_port,static_cast<unsigned>(diagnostic_pad.button),diagnostic_pad.stickX,diagnostic_pad.stickY,diagnostic_pad_remaining);
 else
  std::snprintf(text+length,sizeof(text)-length," · raw PAD: none");
 // Ready/Go is already source gameplay, even before the HUD allows controls.
 if(match&&match->construction_complete()){
  const auto player=match->player_stats(0);
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length,
   " · source frame: %u · P1 motion: %d · anim: %.3f · ground/air: %d · position: [%.3f,%.3f] · source pause: %d · ready: %d",
   match->source_frames(),player.motion_id,player.animation_frame,player.ground_or_air,
   player.position[0],player.position[1],match->paused(),match->ready());
 }
 else if(match){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · match preparing · ready: 0");
 }
 else if(world){
  uint32_t completed=0,revisited=0;
  if(melee_web_audio_stream_progress(world->audio(),&completed,&revisited)){
   const auto length=std::char_traits<char>::length(text);
   std::snprintf(text+length,sizeof(text)-length," · menu audio blocks: %u · revisits: %u",
                 completed,revisited);
  }
 }
 return text;
}
const char* melee_web_native_menu_message(){return message.c_str();}
int melee_web_native_menu_running(){return running;}
int melee_web_native_menu_cache_idle(){
 if(!render_cache_can_flush())return 0;
 const auto state=aurora_pipeline_cache_status();
 return state==AURORA_PIPELINE_CACHE_READY?1:state==AURORA_PIPELINE_CACHE_ERROR?-1:0;
}
int melee_web_native_menu_phase(){return match?7:host?melee_web_menu_host_phase(host):0;}
}
int main(int argc,char** argv){
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 melee_web_provenance_initialize();
 // The private harness freezes this binding before module startup, so even
 // the first boot draw has a case/input identity. No game data is accepted.
 char provenance_input[65]{};
 const uint32_t provenance_case=EM_ASM_INT({
  const binding=window.meleePipelineProvenanceBinding;
  if(!binding||!Number.isInteger(binding.caseId)||binding.caseId<=0||
     binding.caseId>4294967295||typeof binding.inputSha256!=='string'||
     !/^[0-9a-f]{64}$/.test(binding.inputSha256))return 0;
  for(let i=0;i<64;i++)HEAPU8[$0+i]=binding.inputSha256.charCodeAt(i);
  HEAPU8[$0+64]=0;
  return binding.caseId;
 },provenance_input);
 if(provenance_case)melee_web_provenance_set_case(provenance_case,provenance_input);
 else melee_web_provenance_invalid(MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT);
#endif
 AuroraConfig config{};config.appName="Melee native menus";config.desiredBackend=BACKEND_WEBGPU;
 config.cachePath="/melee-render-cache";
 config.windowWidth=640;config.windowHeight=480;config.msaa=1;config.vsync=true;
 config.logCallback=log_message;config.logLevel=LOG_INFO;
 if(!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT,"#canvas"))return 1;
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 if(!aurora_pipeline_prepare_enable())return 1;
 pipeline_bootstrap_started=emscripten_get_now();
#endif
 aurora_initialize(argc,argv,&config);
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 pipeline_union_requested=emscripten_get_now();
 pipeline_renderer_init_ms=pipeline_union_requested-pipeline_bootstrap_started;
 try{melee_web::pipeline_preparation::bootstrap();}
 catch(const std::exception& e){message=e.what();preparation_failed(e.what());return 1;}
 pipeline_union_submit_ms=emscripten_get_now()-pipeline_union_requested;
#endif
 aurora_set_deferred_pipeline_cache_writes(true);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 {const melee_web::provenance::Scope provenance(pipeline_context());GXInit(fifo,sizeof(fifo));}
#else
 GXInit(fifo,sizeof(fifo));
#endif
 if(!melee_web_input_startup())return 1;
 emscripten_set_main_loop(tick,0,1);return 0;
}
