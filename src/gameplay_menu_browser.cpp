#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_save_profile.h"
#include "gameplay_match_session.hpp"
#include "gameplay_results_session.hpp"
#include "results_source_pad_schedule.hpp"
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
#include "gameplay_results_entry_packet.hpp"
#endif
#include "gameplay_prize_session.hpp"
extern "C" const char* melee_web_native_menu_match_observe();
extern "C" const char* melee_web_native_menu_source_observe();
extern "C" const char* melee_web_native_menu_memory();
extern "C" int melee_web_native_menu_phase();
extern "C" void melee_web_cpu_observation_set_event_cursor(size_t index);
extern "C" void melee_web_cpu_observation_scheduler_return(void);
extern "C" {
#include <melee/gm/types.h>
#include <melee/mn/mnitemsw.h>
#include <melee/ty/toy.h>
#include <sysdolphin/baselib/controller.h>
extern ResultsData lbl_8046DBE8;
}
#include "gameplay_match_rules.h"
#include "gameplay_bootstrap.h"
#include "gameplay_retail_recipe.hpp"
#include "gameplay_replay_completion_policy.hpp"
#include "../tests/native_menu_fighter_input.h"
#include "../tests/native_menu_stage_input.h"
#include "gameplay_audio_stream.h"
#include "runtime_archive_cache.hpp"
#include "runtime_asset_scope.hpp"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_source_files.h"
#include "gameplay_content.h"
#include "menu_preparation_state.hpp"
#include "browser_input.h"
#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"
#include "gameplay_pipeline_preparation.hpp"
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
#include "melee_pipeline_seed_identity.h"
#endif
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.h>
#include <aurora/pipeline_prepare.h>
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
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance_runtime.h"
#endif
namespace {
melee_web::RuntimeFiles files;
melee_web::RuntimeAssetScope asset_scope(files);
enum class AssetDestination {
 None, InitialMenu, Match, ReturnMenu, Replay, Results, Prize,
 OpeningScene, OpeningMatch, TitleReturn
};
AssetDestination asset_destination=AssetDestination::None;
bool scoped_assets=false,asset_committed=false;
uint32_t asset_generation=0;
std::vector<std::string> requested_assets;
MeleeWebMenuMatchSelection asset_selection{};
bool asset_selection_valid=false;
MeleeWebOpeningPreview asset_opening_preview{};
bool asset_opening_preview_valid=false;
int pending_opening_state=-1;
std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
bool source_session_owned=false;
std::unique_ptr<melee_web::GameplayMatchSession> match;
std::unique_ptr<melee_web::GameplayResultsSession> results;
std::unique_ptr<melee_web::GameplayPrizeSession> prize;
ResultsMatchInfo results_info{};
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
// Retained across teardown/failure; replaced only by the next Results entry.
melee_web::ResultsEntryPacket results_entry_packet;
#endif
// The scoped asset boundary tears down the match owner before Results can be
// constructed, so the original exit seed and retained PAD history stay here.
uint32_t results_seed=0,prize_seed=0;
std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)>
    results_input{nullptr,melee_web_pad_state_free};
std::string terminal_match_observation;
std::string match_observer_error;
MeleeWebFighterInputObservation last_css_fighter_observation{};
int last_css_fighter_target=-1,last_css_fighter_drive_state=-1;
bool last_css_fighter_observation_valid=false;
int css_fighter_release_port=-1;
bool results_route_active=false;
bool prize_route_active=false;
std::unique_ptr<melee_web::RetailReplayRecipe> replay;
size_t replay_cursor=0;
melee_web::SourceFrameSequence replay_source_frames;
melee_web::ReplayCompletionState replay_completion;
bool replay_trace=false,replay_pending=false,replay_started=false,replay_final_draw=false;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
struct ResultsPadTraceRow {
 uint32_t source_frame;
 bool tick_returned;
 bool results_state_sampled;
 uint32_t results_state_frame;
 uint8_t results_phase;
 uint8_t results_stats_phase;
 uint8_t results_num_pages;
 uint8_t results_player_pages[4];
 uint8_t results_player_confirmed[4];
 PADStatus pads[4];
 HSD_PadStatus source_consumed_pads[4];
};
constexpr size_t kResultsPadTraceCapacity=8192;
std::array<ResultsPadTraceRow,kResultsPadTraceCapacity> results_pad_trace{};
size_t results_pad_trace_count=0;
uint64_t results_pad_trace_attempts=0;
bool results_pad_trace_overflow=false;
MeleeWebResultsCameraEntrySnapshot results_camera_entry_snapshot{};
void append_pointer_json(std::string& json,const void* pointer){
 char text[2+2*sizeof(void*)+4]{};
 const int length=std::snprintf(text,sizeof(text),"%p",const_cast<void*>(pointer));
 if(length<0||static_cast<size_t>(length)>=sizeof(text))
  throw std::runtime_error("Results camera-entry pointer formatting failed");
 json+='"';json.append(text,static_cast<size_t>(length));json+='"';
}
void append_camera_entry_json(std::string& json){
 json+=",\"camera_entry\":{";
 json+="\"saved_pool_at_context_begin\":";append_pointer_json(json,results_camera_entry_snapshot.saved_pool_at_context_begin);
 json+=",\"source_pool_before_onenter\":";append_pointer_json(json,results_camera_entry_snapshot.source_pool_before_onenter);
 json+=",\"owner_pool_before_onenter\":";append_pointer_json(json,results_camera_entry_snapshot.owner_pool_before_onenter);
 json+=",\"source_pool_after_onenter\":";append_pointer_json(json,results_camera_entry_snapshot.source_pool_after_onenter);
 json+=",\"source_pool_after_collision_adoption\":";append_pointer_json(json,results_camera_entry_snapshot.source_pool_after_collision_adoption);
 json+=",\"context_pool_after_adoption\":";append_pointer_json(json,results_camera_entry_snapshot.context_pool_after_adoption);
 json+=",\"owner_pool_after_adoption\":";append_pointer_json(json,results_camera_entry_snapshot.owner_pool_after_adoption);
 json+=",\"source_camera_allocation_generation_before_onenter\":"+std::to_string(results_camera_entry_snapshot.source_camera_allocation_generation_before_onenter);
 json+=",\"source_camera_allocation_generation_after_onenter\":"+std::to_string(results_camera_entry_snapshot.source_camera_allocation_generation_after_onenter);
 json+=",\"source_camera_allocation_subject_count_after_onenter\":"+std::to_string(results_camera_entry_snapshot.source_camera_allocation_subject_count_after_onenter);
 json+=",\"source_camera_allocation_generation_after_collision_adoption\":"+std::to_string(results_camera_entry_snapshot.source_camera_allocation_generation_after_collision_adoption);
 json+=",\"source_camera_allocation_subject_count_after_collision_adoption\":"+std::to_string(results_camera_entry_snapshot.source_camera_allocation_subject_count_after_collision_adoption);
 json+=",\"source_camera_pool_event_count\":"+std::to_string(results_camera_entry_snapshot.source_camera_pool_event_count);
 json+=",\"source_camera_pool_event_overflow\":";
 json+=results_camera_entry_snapshot.source_camera_pool_event_overflow?"true":"false";
 json+=",\"source_camera_pool_events\":[";
 for(uint32_t i=0;i<results_camera_entry_snapshot.source_camera_pool_event_count;++i){
  if(i)json+=',';
  const auto& event=results_camera_entry_snapshot.source_camera_pool_events[i];
  json+="{\"phase\":\"";json+=event.phase;json+="\"";
  json+=",\"source_tick\":"+std::to_string(event.source_tick);
  json+=",\"scene_entered\":"+std::to_string(event.scene_entered);
  json+=",\"subject_count\":"+std::to_string(event.subject_count);
  json+=",\"source_free\":";append_pointer_json(json,event.source_free);
  json+=",\"source_pool\":";append_pointer_json(json,event.source_pool);
  json+=",\"source_active\":";append_pointer_json(json,event.source_active);
  json+=",\"source_tail\":";append_pointer_json(json,event.source_tail);
  json+=",\"owner_pool\":";append_pointer_json(json,event.owner_pool);
  json+=",\"context_pool\":";append_pointer_json(json,event.context_pool);
  json+=",\"expected_pool\":";append_pointer_json(json,event.expected_pool);
  json+=",\"allocation_generation\":"+std::to_string(event.allocation_generation);
  json+=",\"generation_before_onenter\":"+std::to_string(event.generation_before_onenter);
  json+=",\"expected_generation\":"+std::to_string(event.expected_generation);
  json+='}';
 }
 json+=']';
 json+=",\"source_camera_subject_event_count\":";
 json+=std::to_string(results_camera_entry_snapshot.source_camera_subject_event_count);
 json+=",\"source_camera_subject_event_overflow\":";
 json+=results_camera_entry_snapshot.source_camera_subject_event_overflow?"true":"false";
 json+=",\"source_camera_subject_events\":[";
 for(uint32_t i=0;i<results_camera_entry_snapshot.source_camera_subject_event_count;++i){
  if(i)json+=',';
  const auto& event=results_camera_entry_snapshot.source_camera_subject_events[i];
  json+="{\"phase\":\"";json+=event.phase;json+="\"";
  json+=",\"source_tick\":"+std::to_string(event.source_tick);
  json+=",\"scene_entered\":"+std::to_string(event.scene_entered);
  json+=",\"subject\":";append_pointer_json(json,event.subject);
  json+=",\"subject_prev\":";append_pointer_json(json,event.subject_prev);
  json+=",\"subject_next\":";append_pointer_json(json,event.subject_next);
  json+=",\"source_free\":";append_pointer_json(json,event.source_free);
  json+=",\"source_pool\":";append_pointer_json(json,event.source_pool);
  json+=",\"source_active\":";append_pointer_json(json,event.source_active);
  json+=",\"source_tail\":";append_pointer_json(json,event.source_tail);
  json+=",\"owner_pool\":";append_pointer_json(json,event.owner_pool);
  json+=",\"context_pool\":";append_pointer_json(json,event.context_pool);
  json+=",\"allocation_generation\":"+std::to_string(event.allocation_generation);
  json+=",\"subject_in_pool\":";json+=event.subject_in_pool?"true":"false";
  json+=",\"subject_prev_in_pool\":";json+=event.subject_prev_in_pool?"true":"false";
  json+=",\"subject_next_in_pool\":";json+=event.subject_next_in_pool?"true":"false";
  json+=",\"free_in_pool\":";json+=event.free_in_pool?"true":"false";
  json+=",\"active_in_pool\":";json+=event.active_in_pool?"true":"false";
  json+=",\"tail_in_pool\":";json+=event.tail_in_pool?"true":"false";
  json+='}';
 }
 json+=']';
 json+='}';
}
size_t retain_results_pad_sample(uint32_t source_frame,const PADStatus pads[4]){
 ++results_pad_trace_attempts;
 if(results_pad_trace_count==results_pad_trace.size()){
  results_pad_trace_overflow=true;
  return results_pad_trace.size();
 }
 const size_t index=results_pad_trace_count++;
 auto& row=results_pad_trace[index];
 row={};row.source_frame=source_frame;
 for(size_t port=0;port<4;++port)row.pads[port]=pads[port];
 return index;
}
void retain_results_state_after_tick(size_t index,uint32_t source_frame){
 if(index>=results_pad_trace_count)return;
 auto& row=results_pad_trace[index];
 const ResultsData& state=lbl_8046DBE8;
 for(size_t port=0;port<4;++port)row.source_consumed_pads[port]=HSD_PadCopyStatus[port];
 row.results_state_sampled=true;
 row.results_state_frame=source_frame;
 row.results_phase=state.x1;
 row.results_stats_phase=state.x0_23;
 row.results_num_pages=state.num_pages;
 for(size_t slot=0;slot<4;++slot){
  row.results_player_pages[slot]=state.player_data[slot].page;
  row.results_player_confirmed[slot]=state.player_data[slot].x0_0?1U:0U;
 }
}
#endif
// V2 recipes come from fresh original processes and do not carry heap history.
// Original stage callbacks can read uncleared allocation bytes (Shy Guy pattern).
// Do not reset this eligibility on unload or normalize those gameplay bytes.
bool reference_heap_used=false;
unsigned reference_menu_preparations=0;
bool replay_match_complete=false;
int replay_outcome=0,replay_winner=-1;
MeleeWebMenuHost* host=nullptr;
int configured_save_mode=MELEE_WEB_SAVE_MODE_EVERYTHING;
std::vector<uint8_t> configured_save_profile;
constexpr size_t kSaveProfileBytes=MELEE_WEB_SAVE_PROFILE_CARD_BYTES;
// The owner that is running right now, expressed in the recipe's scene codes.
// Zero means the host is between scenes and cannot consume a replay sample.
int observed_replay_scene(){
 if(match)return melee_web::kRetailReplayMatch;
 if(results)return melee_web::kRetailReplayResults;
 if(prize)return melee_web::kRetailReplayPrize;
 if(host){
  const int phase=melee_web_menu_host_phase(host);
  if(phase==MELEE_WEB_MENU_CSS||phase==MELEE_WEB_MENU_CSS_READY)return melee_web::kRetailReplayCss;
  if(phase==MELEE_WEB_MENU_SSS||phase==MELEE_WEB_MENU_SSS_READY)return melee_web::kRetailReplaySss;
 }
 return 0;
}
int expected_replay_scene(const melee_web::RetailReplayRecipe& recipe,size_t cursor){
 for(const auto& span:recipe.spans)
  if(cursor>=span.first_frame&&cursor<=span.last_frame)return span.scene;
 return 0;
}
std::string replay_scene_mismatch(const char* boundary,size_t cursor,
                                 int expected,int observed){
 std::string message="Whole-session replay ";message+=boundary;
 message+=" scene mismatch at input ";message+=std::to_string(cursor);
 message+=" (expected ";message+=std::to_string(expected);
 message+=", observed ";message+=std::to_string(observed);
 if(match){message+=", match source frame ";message+=std::to_string(match->source_frames());}
 if(results){message+=", Results source frame ";message+=std::to_string(results->source_frames());}
 if(prize){message+=", Prize source frame ";message+=std::to_string(prize->source_frames());}
 message+=")";return message;
}
melee_web::FixedTickClock menu_clock;
melee_web::FixedTickClock audio_clock{melee_web::FixedTickClock::OverrunPolicy::CatchUp};
bool diagnostic_source_paused=false;
int diagnostic_source_frame(){
 return match?static_cast<int>(match->source_frames()):
        results?static_cast<int>(results->source_frames()):
        prize?static_cast<int>(prize->source_frames()):-1;
}
void diagnostic_incident(int reason,double value=0,double threshold=0,int clock_owner=0){
 EM_ASM({try{window.menuDiagnosticIncident?.($0,$1,$2,$3,$4,$5);}catch(_){}},
        reason,value,threshold,diagnostic_source_frame(),melee_web_native_menu_phase(),clock_owner);
}
void diagnostic_clock_stall(const melee_web::FixedTickClock::Tick& event,int owner){
 using Reason=melee_web::FixedTickClock::StallReason;
 const int reason=event.reason==Reason::Debt?(owner==2?2:1):
                  event.reason==Reason::ClockRegression?8:3;
 diagnostic_incident(reason,event.triggering_value,event.threshold,owner);
}
template<class... Values>
void diagnostic_sample(Values... values){
 static_assert(sizeof...(values)==19);
 // Pinned Emscripten's EM_ASM signature supports at most 16 arguments. This
 // fixed stack row crosses once; JavaScript reads only its 19 named scalars.
 const double data[]={static_cast<double>(values)...};
 EM_ASM({try{const p=$0>>3;window.menuDiagnosticSample?.(
  HEAPF64[p],HEAPF64[p+1],HEAPF64[p+2],HEAPF64[p+3],HEAPF64[p+4],
  HEAPF64[p+5],HEAPF64[p+6],HEAPF64[p+7],HEAPF64[p+8],HEAPF64[p+9],
  HEAPF64[p+10],HEAPF64[p+11],HEAPF64[p+12],HEAPF64[p+13],HEAPF64[p+14],
  HEAPF64[p+15],HEAPF64[p+16],HEAPF64[p+17],HEAPF64[p+18]);}catch(_){}},data);
}
std::string message="Choose your local Melee disc image.";
std::string match_message="Original source match";
bool running=false,pending=false,host_entered=false,world_exposed=false,faulted=false;
bool startup_pipeline_service_scheduled=false;
bool startup_pipeline_service_failed=false;
unsigned startup_pipeline_service_skip_frames=0;
constexpr int kStartupPipelineServiceDelayMs=8;
melee_web::MenuPreparationState preparation;
unsigned audio_phase=0,diagnostic_start_ticks=0;
int stock_check=0,stock_count=4,stock_respawns=0;
unsigned stock_tick=0,completed_matches=0;
bool stock_lost=false,stock_jump=false;
bool first_use_draw_pending=false;
bool render_only_preparation=false;
bool transition_audio_continues=false;
bool menu_scene_rebuild_pending=false;
int pending_menu_source_scene=0;
melee_web::GameplayMenuScene pending_menu_scene=melee_web::GameplayMenuScene::Characters;
enum class MenuRouteEntry { Session, Main, Title, ParentCss, TrainingCss };
MenuRouteEntry pending_menu_entry=MenuRouteEntry::Session;
const char* source_menu_message(){
 switch(pending_menu_scene){
 case melee_web::GameplayMenuScene::Characters:
  return host&&melee_web_menu_host_mode_kind(host)==GM_TRAINING?
      "Original Training character select":"Original character select";
 case melee_web::GameplayMenuScene::Stages:return "Original stage select";
 case melee_web::GameplayMenuScene::Main:return "Original main menu";
 case melee_web::GameplayMenuScene::Title:return "Original title";
 }
 return "Original menu";
}
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
 if(prize_route_active){
  for(auto& player:context.players){player.motion_id=-1;player.stocks=-1;}
  context.scene=MELEE_WEB_PIPELINE_SCENE_PRIZE;
  context.phase=MELEE_WEB_PIPELINE_PHASE_INTERACTIVE;
  context.world_generation=melee_web_gameplay_generation();
  context.source_tick=melee_web_gameplay_provenance_tick();
  context.owner_kind=MELEE_WEB_PIPELINE_OWNER_MENU_SCENE;
  context.owner_id=MELEE_WEB_PIPELINE_SCENE_PRIZE;
 }
 else if(results_route_active){
  for(auto& player:context.players){player.motion_id=-1;player.stocks=-1;}
  context.scene=MELEE_WEB_PIPELINE_SCENE_RESULTS;
  context.phase=MELEE_WEB_PIPELINE_PHASE_INTERACTIVE;
  context.world_generation=melee_web_gameplay_generation();
  context.source_tick=melee_web_gameplay_provenance_tick();
  context.owner_kind=MELEE_WEB_PIPELINE_OWNER_MENU_SCENE;
  context.owner_id=MELEE_WEB_PIPELINE_SCENE_RESULTS;
  for(unsigned i=0;i<4;++i){
   const auto& source=results_info.match_end.player_standings[i];
   if(source.slot_type==Gm_PKind_NA)continue;
   ++context.active_player_count;
   auto& player=context.players[i];
   player.character=source.ckind;player.costume=source.x3;player.stocks=source.stocks;
   const auto* content=melee_web_fighter_content(source.ckind);
   player.fighter_kind=content?content->fighter_kind:UINT32_MAX;
   player.effect_bank=content?content->effect_bank:UINT32_MAX;
  }
 }
 else if(match)context=match->provenance_context();
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
melee_web::ResultsSourcePadSchedule scheduled_results_pad;
melee_web::ResultsSourceFramePauseSchedule scheduled_results_pauses;
std::array<float,1068> pcm;
alignas(32) unsigned char fifo[64*1024];
// Keep the expanded fighter/Kirby archive inventory separate from the
// original title/main route inventory below. Both are accepted at the native
// file boundary; neither list substitutes for the other's source assets.
constexpr std::array<std::string_view,379> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","LbRf.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm","GrNBa.dat","sp_zako.hps","hyaku.hps","hyaku2.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","PlMs.dat","PlMsAJ.dat","PlMsNr.dat","PlMsRe.dat","PlMsGr.dat","PlMsBk.dat","PlMsWh.dat","EfMsData.dat","mars.ssm","GrOp.dat","old_kb.hps","pupupu.ssm","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","menu3.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sislib_font.bin",
 "PlDr.dat","PlDrAJ.dat","PlDrNr.dat","PlDrRe.dat","PlDrBu.dat","PlDrGr.dat","PlDrBk.dat","drmario.ssm",
 "PlFe.dat","PlFeAJ.dat","PlFeNr.dat","PlFeRe.dat","PlFeBu.dat","PlFeGr.dat","PlFeYe.dat","EfFeData.dat","emblem.ssm",
 "PlLk.dat","PlLkAJ.dat","PlLkNr.dat","PlLkRe.dat","PlLkBu.dat","PlLkBk.dat","PlLkWh.dat",
 "PlCl.dat","PlClAJ.dat","PlClNr.dat","PlClRe.dat","PlClBu.dat","PlClWh.dat","PlClBk.dat","EfLkData.dat","link.ssm","clink.ssm",
 "PlGn.dat","PlGnAJ.dat","PlGnNr.dat","PlGnRe.dat","PlGnBu.dat","PlGnGr.dat","PlGnLa.dat","EfGnData.dat","ganon.ssm",
 "PlCa.dat","PlCaAJ.dat","PlCaNr.dat","PlCaGy.dat","PlCaRe.usd","PlCaWh.dat","PlCaGr.dat","PlCaBu.dat","EfCaData.dat","captain.ssm",
 "GrSh.dat","shrine.hps","akaneia.hps","GrIz.dat","izumi.hps",
 "PlLg.dat","PlLgAJ.dat","PlLgNr.dat","PlLgWh.dat","PlLgAq.dat","PlLgPi.dat","EfLgData.dat","luigi.ssm",
 "PlPk.dat","PlPkAJ.dat","PlPkNr.dat","PlPkRe.dat","PlPkBu.dat","PlPkGr.dat",
 "PlPc.dat","PlPcAJ.dat","PlPcNr.dat","PlPcRe.dat","PlPcBu.dat","PlPcGr.dat",
 "EfPkData.dat","pikachu.ssm","pichu.ssm","GrOy.dat","old_ys.hps",
 "PlPr.dat","PlPrAJ.dat","PlPrNr.dat","PlPrRe.dat","PlPrBu.dat","PlPrGr.dat","PlPrYe.dat","EfPrData.dat","purin.ssm",
 "PlDk.dat","PlDkAJ.dat","PlDkNr.dat","PlDkBk.dat","PlDkRe.dat","PlDkBu.dat","PlDkGr.dat","EfDkData.dat","dk.ssm",
 "PlKp.dat","PlKpAJ.dat","PlKpNr.dat","PlKpRe.dat","PlKpBu.dat","PlKpBk.dat","EfKpData.dat","koopa.ssm",
 "GmRst.usd",
 "SdRst.usd",
 "TyDatai.usd",
 "IfPrize.usd",
 "SdPrize.usd",
 "s_info1.hps",
 "s_info2.hps",
 "s_info3.hps",
 "GmRstMMr.dat",
 "GmRstMDr.dat",
 "GmRstMFx.dat",
 "GmRstMFc.dat",
 "GmRstMMs.dat",
 "GmRstMFe.dat",
 "GmRstMLk.dat",
 "GmRstMCl.dat",
 "GmRstMCa.dat",
 "GmRstMDk.dat",
 "GmRstMGn.dat",
 "GmRstMKp.dat",
 "GmRstMLg.dat",
 "GmRstMMt.dat",
 "GmRstMPk.dat",
 "GmRstMPc.dat",
 "GmRstMPr.dat","GmRstMGw.dat",
 "ff_mario.hps",
 "ff_fox.hps",
 "ff_emb.hps",
 "ff_link.hps",
 "ff_fzero.hps",
 "ff_dk.hps",
 "ff_poke.hps","GmRstMNs.dat","GmRstMPe.dat","ff_nes.hps",
 "PlMt.dat","PlMtAJ.dat","PlMtNr.dat","PlMtRe.dat","PlMtBu.dat","PlMtGr.dat","EfMtData.dat","mewtwo.ssm",
 "PlNs.dat",
 "PlNsAJ.dat",
 "PlNsNr.dat",
 "PlNsYe.dat",
 "PlNsBu.dat",
 "PlNsGr.dat",
 "EfNsData.dat",
 "ness.ssm",
 "PlPe.dat",
 "PlPeAJ.dat",
 "PlPeNr.dat",
 "PlPeYe.dat",
 "PlPeWh.dat",
 "PlPeBu.dat",
 "PlPeGr.dat",
 "EfPeData.dat",
 "peach.ssm",
 "PlKb.dat","PlKbAJ.dat","PlKbNr.dat","PlKbYe.dat","PlKbBu.dat","PlKbRe.dat",
 "PlKbGr.dat","PlKbWh.dat","EfKbData.dat","kirby.ssm","GmRstMKb.dat",
 // Kirby's source copy move and hat roots, plus copy-specific effect banks.
 "PlKbCpMr.dat","PlKbCpFx.dat","PlKbCpCa.dat","PlKbCpDk.dat","PlKbCpKp.dat",
 "PlKbCpLk.dat","PlKbCpSk.dat","PlKbCpNs.dat","PlKbCpPe.dat","PlKbCpPp.dat",
 "PlKbCpPk.dat","PlKbCpSs.dat","PlKbCpYs.dat","PlKbCpPr.dat","PlKbCpMt.dat",
 "PlKbCpLg.dat","PlKbCpMs.dat","PlKbCpZd.dat","PlKbCpCl.dat","PlKbCpDr.dat",
 "PlKbCpFc.dat","PlKbCpPc.dat","PlKbCpGw.dat","PlKbCpGn.dat","PlKbCpFe.dat",
 "PlKbNrCpDk.dat","PlKbNrCpPr.dat","PlKbNrCpMt.dat","PlKbNrCpFc.dat","PlKbNrCpGw.dat",
 "PlKbYeCpDk.dat","PlKbBuCpDk.dat","PlKbReCpDk.dat","PlKbGrCpDk.dat","PlKbWhCpDk.dat",
 "PlKbYeCpPr.dat","PlKbBuCpPr.dat","PlKbReCpPr.dat","PlKbGrCpPr.dat","PlKbWhCpPr.dat",
 "PlKbYeCpMt.dat","PlKbBuCpMt.dat","PlKbReCpMt.dat","PlKbGrCpMt.dat","PlKbWhCpMt.dat",
 "PlKbYeCpFc.dat","PlKbBuCpFc.dat","PlKbReCpFc.dat","PlKbGrCpFc.dat","PlKbWhCpFc.dat",
 "EfKbMs.dat","EfKbZd.dat","EfKbMr.dat","EfKbFx.dat","EfKbSs.dat","EfKbPk.dat",
 "EfKbLg.dat","EfKbCa.dat","EfKbDk.dat","EfKbKp.dat","EfKbIc.dat","EfKbGn.dat","EfKbFe.dat",
 "samus.ssm","yoshi.ssm","zs.ssm","GmRstMSs.dat","GmRstMZd.dat","GmRstMSk.dat",
 "PlGw.dat","PlGwAJ.dat","PlGwNr.dat","gw.ssm",
 "PlSs.dat","PlSsAJ.dat","PlSsNr.dat","PlSsPi.dat","PlSsBk.dat","PlSsGr.dat","PlSsLa.dat","EfSsData.dat",
 "PlYs.dat","PlYsAJ.dat","PlYsNr.dat","PlYsRe.dat","PlYsBu.dat","PlYsYe.dat","PlYsPi.dat","PlYsAq.dat","EfYsData.dat","GmRstMYs.dat",
 "PlZd.dat","PlZdAJ.dat","PlZdNr.dat","PlZdRe.dat","PlZdBu.dat","PlZdGr.dat","PlZdWh.dat",
 "PlSk.dat","PlSkAJ.dat","PlSkNr.dat","PlSkRe.dat","PlSkBu.dat","PlSkGr.dat","PlSkWh.dat","EfZdData.dat",
 "PlPp.dat","PlPpAJ.dat","PlPpNr.dat","PlPpGr.dat","PlPpOr.dat","PlPpRe.dat",
 "PlNn.dat","PlNnAJ.dat","PlNnNr.dat","PlNnYe.dat","PlNnAq.dat","PlNnWh.dat","EfIcData.dat","GmRstMPn.dat","ice.ssm",
 "ff_flat.hps","ff_ice.hps","ff_kirby.hps","ff_samus.hps","ff_yoshi.hps",
};
constexpr std::array<std::string_view,18> zelda_sheik_keys={
 "PlZd.dat","PlZdAJ.dat","PlZdNr.dat","PlZdRe.dat","PlZdBu.dat","PlZdGr.dat","PlZdWh.dat",
 "PlSk.dat","PlSkAJ.dat","PlSkNr.dat","PlSkRe.dat","PlSkBu.dat","PlSkGr.dat","PlSkWh.dat",
 "EfZdData.dat","GmRstMZd.dat","GmRstMSk.dat","zs.ssm",
};
// PR #96's route-specific source inventory includes title, main-menu, and all
// SSM table entries needed before those scenes can select their next route.
constexpr std::array<std::string_view,278> route_asset_keys={
 "LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat",
 "PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","LbAd.dat",
 "LbRf.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat",
 "PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm",
 "GrNBa.dat","sp_zako.hps","hyaku.hps","hyaku2.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat",
 "PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","PlMs.dat","PlMsAJ.dat",
 "PlMsNr.dat","PlMsRe.dat","PlMsGr.dat","PlMsBk.dat","PlMsWh.dat","EfMsData.dat","mars.ssm",
 "GrOp.dat","old_kb.hps","pupupu.ssm","MnSlChr.usd","MnSlMap.usd","MnMaAll.usd","GmTtAll.usd",
 "SdMenu.usd","SdToy.dat","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","LbMcSnap.usd",
 "GmEvent.dat","menu01.hps","menu3.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm",
 "smash2.sem","main.ssm","kongo.ssm","mario.ssm","nr_1p.ssm","nr_vs.ssm","gkoopa.ssm","ice.ssm",
 "kirby.ssm","samus.ssm","zs.ssm","yoshi.ssm","gw.ssm","mhands.ssm","kirbytm.ssm","castle.ssm",
 "corneria.ssm","greatbay.ssm","mutecity.ssm","onett.ssm","zebes.ssm","garden.ssm","klaid.ssm",
 "greens.ssm","venom.ssm","bigblue.ssm","fourside.ssm","pstadium.ssm","1padv.ssm","ending.ssm",
 "1pend.ssm","last.ssm","dsp_coef.bin","sislib_font.bin","PlDr.dat","PlDrAJ.dat","PlDrNr.dat",
 "PlDrRe.dat","PlDrBu.dat","PlDrGr.dat","PlDrBk.dat","drmario.ssm","PlFe.dat","PlFeAJ.dat",
 "PlFeNr.dat","PlFeRe.dat","PlFeBu.dat","PlFeGr.dat","PlFeYe.dat","EfFeData.dat","emblem.ssm",
 "PlLk.dat","PlLkAJ.dat","PlLkNr.dat","PlLkRe.dat","PlLkBu.dat","PlLkBk.dat","PlLkWh.dat",
 "PlCl.dat","PlClAJ.dat","PlClNr.dat","PlClRe.dat","PlClBu.dat","PlClWh.dat","PlClBk.dat",
 "EfLkData.dat","link.ssm","clink.ssm","PlGn.dat","PlGnAJ.dat","PlGnNr.dat","PlGnRe.dat",
 "PlGnBu.dat","PlGnGr.dat","PlGnLa.dat","EfGnData.dat","ganon.ssm","PlCa.dat","PlCaAJ.dat",
 "PlCaNr.dat","PlCaGy.dat","PlCaRe.usd","PlCaWh.dat","PlCaGr.dat","PlCaBu.dat","EfCaData.dat",
 "captain.ssm","GrSh.dat","shrine.hps","akaneia.hps","GrIz.dat","izumi.hps","PlLg.dat",
 "PlLgAJ.dat","PlLgNr.dat","PlLgWh.dat","PlLgAq.dat","PlLgPi.dat","EfLgData.dat","luigi.ssm",
 "PlPk.dat","PlPkAJ.dat","PlPkNr.dat","PlPkRe.dat","PlPkBu.dat","PlPkGr.dat","PlPc.dat",
 "PlPcAJ.dat","PlPcNr.dat","PlPcRe.dat","PlPcBu.dat","PlPcGr.dat","EfPkData.dat","pikachu.ssm",
 "pichu.ssm","GrOy.dat","old_ys.hps","PlPr.dat","PlPrAJ.dat","PlPrNr.dat","PlPrRe.dat",
 "PlPrBu.dat","PlPrGr.dat","PlPrYe.dat","EfPrData.dat","purin.ssm","PlDk.dat","PlDkAJ.dat",
 "PlDkNr.dat","PlDkBk.dat","PlDkRe.dat","PlDkBu.dat","PlDkGr.dat","EfDkData.dat","dk.ssm",
 "PlKp.dat","PlKpAJ.dat","PlKpNr.dat","PlKpRe.dat","PlKpBu.dat","PlKpBk.dat","EfKpData.dat",
 "koopa.ssm","GmRst.usd","SdRst.usd","TyDatai.usd","TyDatai.dat","IfPrize.usd","SdPrize.usd","s_info1.hps",
 "s_info2.hps","s_info3.hps","GmRstMMr.dat","GmRstMDr.dat","GmRstMFx.dat","GmRstMFc.dat",
 "GmRstMMs.dat","GmRstMFe.dat","GmRstMLk.dat","GmRstMCl.dat","GmRstMCa.dat","GmRstMDk.dat",
 "GmRstMGn.dat","GmRstMKp.dat","GmRstMLg.dat","GmRstMMt.dat","GmRstMPk.dat","GmRstMPc.dat",
 "GmRstMPr.dat","ff_mario.hps","ff_fox.hps","ff_emb.hps","ff_link.hps","ff_fzero.hps","ff_dk.hps",
 "ff_poke.hps","GmRstMNs.dat","GmRstMPe.dat","ff_nes.hps","PlMt.dat","PlMtAJ.dat","PlMtNr.dat",
 "PlMtRe.dat","PlMtBu.dat","PlMtGr.dat","EfMtData.dat","mewtwo.ssm","PlNs.dat","PlNsAJ.dat",
 "PlNsNr.dat","PlNsYe.dat","PlNsBu.dat","PlNsGr.dat","EfNsData.dat","ness.ssm","PlPe.dat",
 "PlPeAJ.dat","PlPeNr.dat","PlPeYe.dat","PlPeWh.dat","PlPeBu.dat","PlPeGr.dat","EfPeData.dat",
 "peach.ssm"};
constexpr unsigned kDiagnosticPadButtons=PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT|PAD_BUTTON_DOWN|PAD_BUTTON_UP|
 PAD_TRIGGER_Z|PAD_TRIGGER_R|PAD_TRIGGER_L|PAD_BUTTON_A|PAD_BUTTON_B|PAD_BUTTON_X|PAD_BUTTON_Y|PAD_BUTTON_START;
void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void begin_source_session(){
 if(source_session_owned)return;
 char error[256]{};
 check(melee_web_gameplay_session_begin(32U*1024U*1024U,error,sizeof(error)),error);
 source_session_owned=true;
}
void clear_diagnostic_pad(){diagnostic_pad={};diagnostic_pad_port=0;diagnostic_pad_remaining=0;}
void clear_scheduled_results_pad(){scheduled_results_pad.clear();}
void clear_scheduled_results_pauses(){scheduled_results_pauses.clear();}
void activate_scheduled_results_pad(unsigned source_frame,const PADStatus* raw){
 melee_web::ResultsSourcePadEvent scheduled{};
 const auto boundary=scheduled_results_pad.before_tick(source_frame,scheduled);
 if(boundary==melee_web::ResultsSourcePadSchedule::Boundary::missed)
  throw std::runtime_error("Missed scheduled Results PAD source tick; refusing late input");
 if(boundary!=melee_web::ResultsSourcePadSchedule::Boundary::due)return;
 check(raw&&raw[scheduled.port].err==PAD_ERR_NONE,
       "Scheduled Results P1 PAD event requires the original connected port");
 check(diagnostic_pad_remaining==0,
       "Scheduled Results PAD event overlapped an active raw PAD sample");
 diagnostic_pad={};diagnostic_pad.err=raw[scheduled.port].err;
 diagnostic_pad.button=static_cast<u16>(scheduled.buttons);
 diagnostic_pad_port=scheduled.port;diagnostic_pad_remaining=scheduled.duration;
 EM_ASM({window.menuResultsSourcePadConsumed?.($0,$1,$2,$3);},
        scheduled.source_frame,scheduled.port,scheduled.buttons,scheduled.duration);
}
void report_owner_lifetime(const char* boundary){
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 // Opt-in diagnostics run only at owner boundaries, never per live tick.
 if(EM_ASM_INT({return typeof window.menuOwnerLifetime==='function';}))
  EM_ASM({window.menuOwnerLifetime(UTF8ToString($0),JSON.parse(UTF8ToString($1)));},
         boundary,melee_web_native_menu_memory());
#endif
}
AuroraStats aurora_stats_snapshot(){
 AuroraStats result{};if(const AuroraStats* stats=aurora_get_stats())result=*stats;return result;
}
int32_t stat_delta(uint64_t after,uint64_t before){
 return static_cast<int32_t>(after)-static_cast<int32_t>(before);
}
void report_construction(const char* kind,double started,double constructed,double entered,
                         const AuroraStats& before,const AuroraStats& after){
 const auto allocation=melee_web_gameplay_allocation();
 EM_ASM({if(window.menuConstructionTiming)window.menuConstructionTiming({
   kind:UTF8ToString($0),total_ms:$1,construct_ms:$2,entry_ms:$3,
   queued_delta:$4,created_delta:$5,draw_calls:$6,texture_upload_bytes:$7,
   queued_total:$8,created_total:$9,wasm_heap_bytes:$10,
   source_allocation_identity:$11,source_allocation_generation:$12,source_allocation_bytes:$13
 });},kind,entered-started,constructed-started,entered-constructed,
        stat_delta(after.queuedPipelines,before.queuedPipelines),
        stat_delta(after.createdPipelines,before.createdPipelines),
        after.drawCallCount,after.lastTextureUploadSize,after.queuedPipelines,
        after.createdPipelines,emscripten_get_heap_size(),
        double(allocation.identity),double(allocation.generation),double(allocation.bytes));
}
void begin_preparation(){
 if(!preparation.request())return;
 preparation_profile.begin(true,emscripten_get_now());
 clear_diagnostic_pad();pending=false;running=false;menu_clock.reset();
 bool preserve_audio=false;
 if(!match&&!results&&!prize&&host_entered){
  char error[256]{};
  pending_menu_source_scene=melee_web_menu_host_source_scene(host);
  check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);host_entered=false;
  const int phase=melee_web_menu_host_phase(host);
  preserve_audio=phase!=5&&phase!=6;
 }
 transition_audio_continues=preserve_audio;
 if(!preserve_audio)audio_clock.reset();
 message=match?"Preparing original Results...":prize?"Preparing original character select...":"Preparing original next scene...";
 EM_ASM({if(window.menuPreparation)window.menuPreparation(UTF8ToString($0),!!$1);},
        message.c_str(),preserve_audio?1:0);
}
bool preparation_uses_source_draws(){
 // Match camera and subject callbacks mutate gameplay state. Complete-draw
 // pipeline lookup lets the first real tick draw its full image; preparation
 // can then service the renderer without traversing the game again.
 return (!match&&!results&&!prize)||!aurora_pipeline_complete_draws_enabled();
}
bool prepare_deferred_pipelines(){
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 const auto selected=melee_web::pipeline_preparation::status();
 if(!selected.deferred_count)return false;
 // The lookup retained descriptor bytes only. Stop source time before the
 // renderer may construct a pipeline, then settle the unchanged scene.
 diagnostic_incident(7);running=false;menu_clock.reset();audio_clock.reset();
 if(preparation.phase()==melee_web::MenuPreparationState::Phase::Idle){
  check(preparation.request_render_settle(preparation_uses_source_draws()),"Could not pause for pipeline preparation");
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
 const bool had_lifetime=world||match||results||prize||host;
 if(had_lifetime)report_owner_lifetime("session-before-unload");
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const bool had_source_world=melee_web_gameplay_generation()!=0;
 const melee_web::provenance::Scope provenance(pipeline_context(
     had_source_world?MELEE_WEB_PIPELINE_PHASE_TEARDOWN:MELEE_WEB_PIPELINE_PHASE_PREPARATION,
     had_source_world?MELEE_WEB_PIPELINE_SCENE_TEARDOWN:0));
#endif
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};running=false;pending=false;preparation.reset();menu_clock.reset();
 if(match){
  const bool opening_demo=match->opening_demo();
  match->close();match.reset();
  if(opening_demo&&host){
   check(melee_web_menu_host_opening_match_abort(host,error,sizeof(error)),error);
   pending_opening_state=-1;
  }
 }
 if(results){
  results->close();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_camera_entry_snapshot=results->camera_entry_snapshot();
#endif
  results.reset();
 }
 if(prize){prize->close();prize.reset();}
 if(world){
  if(host_entered){check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);host_entered=false;pending_opening_state=-1;}
  if(world_exposed)world->close();else world->close_prepared();
  world.reset();world_exposed=false;
 }
 if(host&&!host_entered&&pending_opening_state>=0){
  check(melee_web_menu_host_opening_match_abort(host,error,sizeof(error)),error);
  pending_opening_state=-1;
 }
 if(host){check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;}
if(scoped_assets){
  archive_cache.reset();
  if(asset_scope.pending_generation())asset_scope.abort(asset_scope.pending_generation());
  asset_scope.release();
  asset_destination=AssetDestination::None;asset_generation=0;asset_committed=false;
  scoped_assets=false;
  requested_assets.clear();
  asset_selection_valid=false;
  asset_opening_preview_valid=false;
 }
 results_route_active=false;
 prize_route_active=false;
 results_input.reset();
 if(source_session_owned){
  check(melee_web_gameplay_session_end(error,sizeof(error)),error);
  source_session_owned=false;
 }
 if(replay&&replay_trace&&replay_final_draw&&!faulted)
  melee_web::retail_replay_end(replay->frames.size(),replay->whole_session());
 replay.reset();replay_completion={};replay_cursor=0;replay_trace=replay_pending=replay_started=replay_final_draw=false;
 replay_match_complete=false;replay_outcome=0;replay_winner=-1;
 audio_phase=0;faulted=false;diagnostic_start_ticks=0;stock_check=0;stock_tick=0;render_frame=0;first_use_draw_pending=false;render_only_preparation=false;transition_audio_continues=false;menu_scene_rebuild_pending=false;pending_menu_source_scene=0;pending_opening_state=-1;audio_clock.reset();clear_diagnostic_pad();clear_scheduled_results_pad();clear_scheduled_results_pauses();
 css_fighter_release_port=-1;last_css_fighter_observation_valid=false;
 match_message="Original source match";
 terminal_match_observation.clear();
 if(had_lifetime){
  report_owner_lifetime("session-after-unload");
  const double finished=emscripten_get_now();
  report_construction("lifecycle-close",started,finished,finished,before,
                      aurora_stats_snapshot());
 }
}
void request_assets(AssetDestination destination,
                    const MeleeWebMenuMatchSelection* selection=nullptr,
                    const MeleeWebOpeningPreview* opening_preview=nullptr,
                    int opening_state=-1){
 check(!world&&!match&&!host_entered&&!results&&!prize,
       "Close source asset owners before requesting a scope");
 std::vector<std::string> names;
 switch(destination){
 case AssetDestination::Match:
 case AssetDestination::Replay:
  check(selection!=nullptr,"This asset scope requires an original source selection");
  names=melee_web::match_asset_names(*selection);break;
 case AssetDestination::Results:
  check(asset_selection_valid,"Results assets require the completed match selection");
  names=melee_web::results_asset_names(asset_selection);break;
 case AssetDestination::Prize: names=melee_web::prize_asset_names();break;
 case AssetDestination::OpeningScene:
  check(opening_state>=0,"Opening scene asset scope requires a source state");
  names=melee_web::opening_state_asset_names(static_cast<unsigned>(opening_state));break;
 case AssetDestination::OpeningMatch:
  check(opening_preview!=nullptr,"Opening VS asset scope requires a source preview");
  names=melee_web::opening_match_asset_names(*opening_preview);break;
 case AssetDestination::TitleReturn: names=melee_web::menu_asset_names();break;
 default: names=melee_web::menu_asset_names();break;
 }
 // The host retains only its copied selection and RNG lease. All owners that
 // borrow archive bytes are gone before clearing the cache and source vectors.
 const auto released_files=files.size();size_t released_bytes=0;
 for(const auto& entry:files)released_bytes+=entry.second.size();
 archive_cache.reset();asset_scope.release();
 requested_assets=std::move(names);
 asset_generation=asset_scope.request(requested_assets);
 asset_destination=destination;asset_committed=false;
 if(selection){asset_selection=*selection;asset_selection_valid=true;}
 if(opening_preview){asset_opening_preview=*opening_preview;asset_opening_preview_valid=true;}
 if(opening_state>=0)pending_opening_state=opening_state;
 running=false;menu_clock.reset();audio_clock.reset();
 EM_ASM({window.menuAssetScopeReleased?.({files:$0,bytes:$1,remainingFiles:$2});},
        released_files,released_bytes,files.size());
 if(destination!=AssetDestination::InitialMenu)
  EM_ASM({window.menuAssetsRequested?.($0);},asset_generation);
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
 if(replay&&replay->whole_session()&&
    melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED){
  check(replay->initial_css&&replay->initial_input,
        "Whole-session replay is missing its first-CSS source context");
  check(melee_web_menu_host_apply_replay_context(
      host,replay->seed,replay->pad_bytes.data(),
      replay->initial_css->css_data.data(),replay->initial_css->ko_counts.data(),
      replay->initial_css->game_rules.data(),replay->initial_css->save_data.data(),
      error,sizeof(error)),error);
 }
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);host_entered=true;world_exposed=true;
 const double entered=emscripten_get_now();
 report_construction("scene-enter",started,constructed,entered,before,aurora_stats_snapshot());
 first_use_draw_pending=true;
 menu_clock.reset();audio_phase=0;audio_clock.reset();running=true;
 message=melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
}
void release_menu_world(){
 if(!world)return;
 check(!host_entered,"Release a source menu scene before retiring its world");
 report_owner_lifetime("opening-before-menu-world-retire");
 world->close();world.reset();world_exposed=false;
 report_owner_lifetime("opening-after-menu-world-retire");
}
void enter_title_after_opening(){
 char error[256]{};
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 check(melee_web_menu_host_enter_title(host,world->audio(),error,sizeof(error)),error);
 host_entered=true;world_exposed=true;pending_opening_state=-1;
 report_construction("opening-title-return",started,emscripten_get_now(),
                     emscripten_get_now(),before,aurora_stats_snapshot());
 first_use_draw_pending=true;menu_clock.reset();audio_phase=0;audio_clock.reset();
 running=true;message="Original title";
}
void request_opening_match_assets(int state,
                                  const MeleeWebOpeningPreview& preview){
 try{
  request_assets(AssetDestination::OpeningMatch,nullptr,&preview,state);
 }catch(const std::exception& cause){
  const std::string route=state==1?"Original Title idle":"Original Opening";
  throw std::runtime_error(route+" reached source GM_OPENING_MV state "+
      std::to_string(state)+" (the four-player VS demo), but its source-selected "+
      "asset scope is unsupported: "+cause.what()+". Eject to recover.");
 }
}
void enter_opening_state(int state){
 char error[256]{};
 check(state>=0,"Original Opening route has no source-selected state");
 pending_opening_state=state;
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 check(melee_web_menu_host_enter_opening(host,world->audio(),error,sizeof(error)),error);
 host_entered=true;world_exposed=true;
 report_construction("opening-state-enter",started,emscripten_get_now(),
                     emscripten_get_now(),before,aurora_stats_snapshot());
 if(state==1||state==3){
  MeleeWebMenuMatchSelection selection{};
  check(melee_web_menu_host_opening_selection(host,&selection,error,sizeof(error)),error);
  if(asset_opening_preview_valid){
   check(selection.start.rules.stkind==asset_opening_preview.stage_kind&&
         selection.start.rules.match_kind==asset_opening_preview.match_kind,
         "Opening VS assets differ from the retained source-selected preview");
   for(unsigned i=0;i<4;++i)
    check(selection.start.players[i].ckind==asset_opening_preview.characters[i]&&
          selection.start.players[i].color==asset_opening_preview.costumes[i],
          "Opening VS fighter assets differ from the retained source-selected preview");
  }
  asset_opening_preview_valid=false;
  check(melee_web_menu_host_opening_match_suspend(host,error,sizeof(error)),error);
  host_entered=false;
  const MeleeWebPadState* input=melee_web_menu_host_opening_input(host);
  check(input!=nullptr,"Opening source state did not retain its original PAD history");
  world->close();world.reset();world_exposed=false;
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  melee_web::pipeline_preparation::match(selection);
#endif
  const double match_started=emscripten_get_now();const AuroraStats match_before=aurora_stats_snapshot();
  match=std::make_unique<melee_web::GameplayMatchSession>(
      files,selection,*archive_cache,melee_web::GameplayMatchConstruction::Deferred,*input);
  report_construction("opening-match-enter-step",match_started,
                      emscripten_get_now(),emscripten_get_now(),match_before,
                      aurora_stats_snapshot());
  match_message=selected_match_message(selection);
  running=false;message="Preparing original Title demo...";
  return;
 }
 first_use_draw_pending=true;menu_clock.reset();audio_phase=0;audio_clock.reset();
 running=true;message=state==2?"Original title":"Original Opening movie";
 asset_opening_preview_valid=false;
}
void begin_opening_state(int state){
 check(state>=0,"Original Title timeout did not select an Opening state");
 release_menu_world();
 pending_opening_state=state;
 if(state==1||state==3){
  MeleeWebOpeningPreview preview{};char error[256]{};
  check(melee_web_menu_host_opening_preview(host,&preview,error,sizeof(error)),error);
  if(scoped_assets){request_opening_match_assets(state,preview);return;}
  asset_opening_preview=preview;asset_opening_preview_valid=true;
  enter_opening_state(state);return;
 }
 asset_opening_preview_valid=false;
 if(scoped_assets){request_assets(AssetDestination::OpeningScene,nullptr,nullptr,state);return;}
 enter_opening_state(state);
}
void begin_title_return(){
 release_menu_world();
 pending_opening_state=-1;
 if(scoped_assets){request_assets(AssetDestination::TitleReturn);return;}
 enter_title_after_opening();
}
void begin_menu_scene_rebuild(melee_web::GameplayMenuScene scene,
                              MenuRouteEntry entry){
 pending=false;menu_clock.reset();
 pending_menu_scene=scene;pending_menu_entry=entry;
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 report_owner_lifetime("menu-before-scene-teardown");
 world->begin_scene_rebuild();
 report_owner_lifetime("menu-after-scene-teardown");
 const double finished=emscripten_get_now();
 report_construction("scene-rebuild-step",started,finished,finished,before,
                     aurora_stats_snapshot());
 menu_scene_rebuild_pending=true;running=false;
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
  if(scoped_assets){request_assets(AssetDestination::Replay,&replay->selection);return;}
  match=std::make_unique<melee_web::GameplayMatchSession>(files,replay->selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred,*replay->initial_input);
  running=false;message="Preparing reference replay...";return;
 }
 if(prize){
  report_owner_lifetime("prize-before-teardown");
  prize->exit_scene();
  check(melee_web_menu_host_prize_exit(host,error,sizeof(error)),error);
  const uint32_t seed=prize->random_seed();
  uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(final_input);
  prize->close();prize.reset();
  report_owner_lifetime("prize-after-teardown");
  check(melee_web_menu_host_prize_end(host,seed,final_input,error,sizeof(error)),error);
  if(replay_completion.final_input_drawn)
   replay_completion.final_results_or_prize_transitioned=true;
  prize_route_active=false;results_route_active=false;
  if(scoped_assets){pending=false;request_assets(AssetDestination::ReturnMenu);return;}
  pending=false;enter_world();return;
 }
 if(results){
  report_owner_lifetime("results-before-teardown");
  results->exit_scene();
  check(melee_web_menu_host_results_exit(host,error,sizeof(error)),error);
  const uint32_t seed=results->random_seed();
  uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(final_input);
  results->close();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_camera_entry_snapshot=results->camera_entry_snapshot();
#endif
  results.reset();
  report_owner_lifetime("results-after-teardown");
  check(melee_web_menu_host_results_end(host,seed,final_input,error,sizeof(error)),error);
  results_route_active=false;
  if(replay_completion.final_input_drawn){
   check(melee_web_menu_host_results_destination(host)!=192,
         "Whole-session timeline ended before an original Prize scene");
   replay_completion.final_results_or_prize_transitioned=true;
  }
  if(melee_web_menu_host_results_destination(host)==192){
   prize_route_active=true;
   prize_seed=seed;
   if(scoped_assets){pending=false;request_assets(AssetDestination::Prize);return;}
   const MeleeWebPadState* input=melee_web_menu_host_input(host);
   check(input!=nullptr,"Original Results did not retain its source PAD history");
   const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
   prize=std::make_unique<melee_web::GameplayPrizeSession>(files,host,seed,*input);
   const double constructed=emscripten_get_now();
   report_construction("prize-enter",started,constructed,constructed,before,aurora_stats_snapshot());
   first_use_draw_pending=true;pending=false;running=true;audio_phase=0;
   menu_clock.reset();audio_clock.reset();message="Original unlock notification";return;
  }
  if(scoped_assets){pending=false;request_assets(AssetDestination::ReturnMenu);return;}
  pending=false;enter_world();return;
 }
 if(match){
  if(match->opening_demo()){
   report_owner_lifetime("opening-match-before-teardown");
   const uint32_t seed=match->random_seed();
   uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(final_input);
   match->close();match.reset();
   report_owner_lifetime("opening-match-after-teardown");
   check(melee_web_menu_host_opening_match_finish(
       host,seed,final_input,error,sizeof(error)),error);
   const int target=melee_web_menu_host_route_target_mode(host);
   if(target==GM_TITLE){begin_title_return();return;}
   check(target==GM_OPENING_MV,
         "Original Opening demo selected an unsupported source mode");
   const int state=melee_web_menu_host_opening_target_state(host);
   begin_opening_state(state);return;
  }
  terminal_match_observation=melee_web_native_menu_match_observe();
  report_owner_lifetime("match-before-teardown");
  const bool checking_stock=stock_check==-1;
  const uint32_t seed=match->random_seed();
  uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(final_input);
  {
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   const melee_web::provenance::Scope teardown(pipeline_context(
       MELEE_WEB_PIPELINE_PHASE_TEARDOWN,MELEE_WEB_PIPELINE_SCENE_TEARDOWN));
#endif
   match->close();match.reset();
   report_owner_lifetime("match-after-teardown");
  }
  // Original MatchEnd computes its ranking during close. Preserve that
  // separate terminal boundary rather than inferring a winner from stocks.
  int observed_outcome=OUTCOME_NONE,observed_count=0,observed_winners[6]{};
  if(melee_web_match_rules_terminal_result(&observed_outcome,&observed_count,observed_winners)&&
     !terminal_match_observation.empty()&&terminal_match_observation.back()=='}'){
   terminal_match_observation.pop_back();
   terminal_match_observation+=",\"terminal\":{\"outcome\":"+std::to_string(observed_outcome)+",\"winners\":[";
   for(int i=0;i<observed_count;++i){
    if(i)terminal_match_observation+=',';
    terminal_match_observation+=std::to_string(observed_winners[i]);
   }
   terminal_match_observation+="]}}";
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
  MatchExitInfo terminal{};
  check(melee_web_match_rules_terminal_data(&terminal),"Original VS exit payload is unavailable");
  check(melee_web_menu_host_results_begin(host,&terminal,seed,&results_info,error,sizeof(error)),error);
  results_route_active=true;
  results_input.reset(melee_web_pad_state_decode(final_input,sizeof(final_input),error,sizeof(error)));
  check(results_input!=nullptr,error);
  results_seed=seed;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_entry_packet.capture(completed_matches,terminal,results_info,results_seed,final_input);
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_pad_trace_count=0;results_pad_trace_attempts=0;results_pad_trace_overflow=false;
  results_camera_entry_snapshot={};
#endif
  if(scoped_assets){pending=false;request_assets(AssetDestination::Results);return;}
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  results=std::make_unique<melee_web::GameplayResultsSession>(files,results_info,seed,*results_input);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_camera_entry_snapshot=results->camera_entry_snapshot();
#endif
  results_input.reset();
  const double constructed=emscripten_get_now();
  report_construction("results-enter",started,constructed,constructed,before,aurora_stats_snapshot());
  first_use_draw_pending=true;pending=false;running=true;audio_phase=0;
  menu_clock.reset();audio_clock.reset();message="Original Results";return;
 }
 const int previous_source_scene=pending_menu_source_scene!=0?
     pending_menu_source_scene:melee_web_menu_host_source_scene(host);
 pending_menu_source_scene=0;
 if(host_entered){check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);host_entered=false;}
 const int route_target=melee_web_menu_host_route_target_mode(host);
 if(previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_TITLE&&
    route_target!=GM_OPENING_MV)pending_opening_state=-1;
 if((previous_source_scene==1&&route_target==1)||
    (previous_source_scene==3&&route_target==1)){
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Main,MenuRouteEntry::Main);
  return;
 }
 if(previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_SSS&&route_target==GM_MENU){
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Main,MenuRouteEntry::Main);
  return;
 }
 if(previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_MAIN&&
    route_target==GM_TRAINING){
  check(melee_web_menu_host_mode_kind(host)==GM_TRAINING,
        "Original Main menu lost its checked GM_TRAINING owner");
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Characters,
                           MenuRouteEntry::TrainingCss);
  return;
 }
 if(previous_source_scene==4&&route_target==0){
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Title,MenuRouteEntry::Title);
  return;
 }
 if(previous_source_scene==4&&route_target==1){
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Main,MenuRouteEntry::Main);
  return;
 }
 if(previous_source_scene==4&&route_target==2){
  const int phase=melee_web_menu_host_phase(host);
  const MenuRouteEntry entry=phase==MELEE_WEB_MENU_CLOSED?
      MenuRouteEntry::ParentCss:MenuRouteEntry::Session;
  begin_menu_scene_rebuild(melee_web::GameplayMenuScene::Characters,entry);
  return;
 }
 if((previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_TITLE||
     previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_OPENING)&&
    route_target==GM_OPENING_MV){
  begin_opening_state(melee_web_menu_host_opening_target_state(host));
  return;
 }
 if(previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_OPENING&&
    route_target==GM_TITLE){begin_title_return();return;}
 if((previous_source_scene==1||previous_source_scene==3||previous_source_scene==4||
     previous_source_scene==MELEE_WEB_MENU_HOST_SCENE_OPENING)&&
    route_target>=0){
  if(previous_source_scene==3){
   const int opening_state=melee_web_menu_host_route_target_state(host);
   if(opening_state>=0)
    throw std::runtime_error("Original Title idle reached source GM_OPENING_MV state "+
      std::to_string(opening_state)+" (the four-player VS demo); its randomized full-roster "+
      "gameplay owner is not admitted by this browser runtime yet. Eject to recover.");
  }
  throw std::runtime_error("Original title/main route requested an unsupported destination");
 }
 const int phase=melee_web_menu_host_phase(host);
 if(phase!=5&&phase!=6){
  begin_menu_scene_rebuild(phase==2?melee_web::GameplayMenuScene::Stages:
                                    melee_web::GameplayMenuScene::Characters,
                          MenuRouteEntry::Session);
  return;
 }
 report_owner_lifetime("menu-before-teardown");
 world->close();world.reset();world_exposed=false;pending=false;menu_clock.reset();audio_phase=0;
 report_owner_lifetime("menu-after-teardown");
 if(phase==5){
  if(melee_web_menu_host_mode_kind(host)==GM_TRAINING){
   check(melee_web_menu_host_training_start_pending(host),
         "Original Training SSS reached its simulation state without a checked start handoff");
   throw std::runtime_error(
       "Original Training stage selection reached GM_TRAINING state 2. Its one-player simulation, Training HUD/options, item controls, and reset/CPU services are not integrated yet; Eject to recover.");
  }
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  if(replay&&replay->version==melee_web::kRetailReplayVersion)
   melee_web::retail_replay_validate_match_setup(
       *replay,melee_web::retail_replay_next_match_index(*replay,replay_cursor),selection.start);
  match_message=selected_match_message(selection);
  if(scoped_assets){request_assets(AssetDestination::Match,&selection);return;}
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  melee_web::pipeline_preparation::match(selection);
#endif
  const MeleeWebPadState* input=melee_web_menu_host_input(host);
  check(input!=nullptr,"Original menu did not retain its source PAD history");
  match=std::make_unique<melee_web::GameplayMatchSession>(
      files,selection,*archive_cache,melee_web::GameplayMatchConstruction::Deferred,*input);
  const double constructed=emscripten_get_now();
  report_construction("match-enter-step",started,constructed,constructed,before,aurora_stats_snapshot());
  running=false;message="Preparing original match...";return;
 }
 if(phase==6){running=false;message="Original menu closed.";return;}
}

bool finish_asset_handoff(){
 if(asset_destination==AssetDestination::None)return true;
 if(!asset_committed)return false;
 check(!world&&!match&&!archive_cache,"Asset handoff found a live source owner");
 archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 const auto destination=asset_destination;
 asset_destination=AssetDestination::None;asset_committed=false;
 if(destination==AssetDestination::ReturnMenu){enter_world();return true;}
 if(destination==AssetDestination::TitleReturn){enter_title_after_opening();return true;}
 if(destination==AssetDestination::OpeningScene||
    destination==AssetDestination::OpeningMatch){
  check(pending_opening_state>=0,
        "Opening asset handoff lost its source-selected state");
  if(destination==AssetDestination::OpeningMatch)
   check(asset_opening_preview_valid,
         "Opening VS asset handoff lost its source-selected preview");
  enter_opening_state(pending_opening_state);
  return destination==AssetDestination::OpeningScene;
 }
 if(destination==AssetDestination::Results){
  check(results_input!=nullptr,"Original Results input was not retained across the asset handoff");
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  results=std::make_unique<melee_web::GameplayResultsSession>(files,results_info,results_seed,*results_input);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_camera_entry_snapshot=results->camera_entry_snapshot();
#endif
  results_input.reset();
  const double constructed=emscripten_get_now();
  report_construction("results-enter",started,constructed,constructed,before,aurora_stats_snapshot());
  first_use_draw_pending=true;pending=false;running=true;audio_phase=0;menu_clock.reset();audio_clock.reset();
  message="Original Results";return true;
 }
 if(destination==AssetDestination::Prize){
  const MeleeWebPadState* input=melee_web_menu_host_input(host);
  check(input!=nullptr,"Original Results did not retain its source PAD history");
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  prize=std::make_unique<melee_web::GameplayPrizeSession>(files,host,prize_seed,*input);
  const double constructed=emscripten_get_now();
  report_construction("prize-enter",started,constructed,constructed,before,aurora_stats_snapshot());
  first_use_draw_pending=true;pending=false;running=true;audio_phase=0;menu_clock.reset();audio_clock.reset();
  message="Original unlock notification";return true;
 }
 check(destination==AssetDestination::Match||destination==AssetDestination::Replay,
       "Initial menu scope must be prepared through the import boundary");
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 melee_web::pipeline_preparation::match(asset_selection);
#endif
 if(destination==AssetDestination::Replay){
  check(replay&&replay->initial_input,"Replay initialization is unavailable");
  match=std::make_unique<melee_web::GameplayMatchSession>(files,asset_selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred,*replay->initial_input);
 }else{
  const MeleeWebPadState* input=melee_web_menu_host_input(host);
  check(input!=nullptr,"Original SSS did not retain PAD history for asset match handoff");
  match=std::make_unique<melee_web::GameplayMatchSession>(files,asset_selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred,*input);
 }
 running=false;
 return false;
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
  if(replay&&replay_trace)melee_web::retail_replay_initial(*replay,true,match->start_data());
  first_use_draw_pending=true;running=true;message=match_message;
 }
 return complete;
}

void finish_menu_scene_rebuild(){
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 char error[256]{};
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 if(pending_menu_scene==melee_web::GameplayMenuScene::Stages)melee_web::pipeline_preparation::sss();
 else if(pending_menu_scene==melee_web::GameplayMenuScene::Characters)
  melee_web::pipeline_preparation::css();
#endif
 world->finish_scene_rebuild(pending_menu_scene);
 const double constructed=emscripten_get_now();
 int source_entered=0;
 switch(pending_menu_entry){
 case MenuRouteEntry::Session:
  source_entered=melee_web_menu_host_enter(host,world->audio(),error,sizeof(error));break;
 case MenuRouteEntry::Main:
  source_entered=melee_web_menu_host_enter_main(host,world->audio(),error,sizeof(error));break;
 case MenuRouteEntry::Title:
  source_entered=melee_web_menu_host_enter_title(host,world->audio(),error,sizeof(error));break;
 case MenuRouteEntry::ParentCss:
  source_entered=melee_web_menu_host_reenter_css_after_parent(host,world->audio(),error,sizeof(error));break;
 case MenuRouteEntry::TrainingCss:
  source_entered=melee_web_menu_host_enter_training_css(host,world->audio(),error,sizeof(error));break;
 }
 check(source_entered,error);
 host_entered=true;world_exposed=true;
 const double entered=emscripten_get_now();
 report_construction("scene-rebuild",started,constructed,entered,before,
                     aurora_stats_snapshot());
 menu_scene_rebuild_pending=false;first_use_draw_pending=true;
 menu_clock.reset();running=true;
 switch(pending_menu_entry){
 case MenuRouteEntry::Title:message="Original title";break;
 case MenuRouteEntry::Main:message="Original main menu";break;
 case MenuRouteEntry::ParentCss:message="Original character select";break;
 case MenuRouteEntry::TrainingCss:message="Original Training character select";break;
 case MenuRouteEntry::Session:
  if(melee_web_menu_host_phase(host)==1&&
     melee_web_menu_host_mode_kind(host)==GM_TRAINING)
   message="Original Training character select";
  else
   message=melee_web_menu_host_phase(host)==1?"Original character select":"Original stage select";
  break;
 }
 pending_menu_entry=MenuRouteEntry::Session;
}

void begin_transition_construction(double& preparation_ms,int& suppress_draw){
 if(!preparation.waiting_for_audio()||
    !preparation.begin_construction(audio_ready_for_preparation()))return;
 const double started=emscripten_get_now();
 preparation_profile.construction_started(started);
 advance();
 preparation_ms+=emscripten_get_now()-started;
 const bool construction_complete=asset_destination==AssetDestination::None&&!menu_scene_rebuild_pending&&
                                  (!match||match->construction_complete());
 if(!construction_complete)return;
 preparation_profile.construction_finished(emscripten_get_now());
 preparation.finish_construction(running,preparation_uses_source_draws());
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
 return !world&&!match&&!results&&!prize&&!host&&!running&&!pending&&!preparation.busy()&&
        stats&&stats->queuedPipelines==0;
}
bool startup_pipeline_service_allowed(){
 // Startup seed rows are ownerless and do not need a source draw. Keep this
 // pump outside imported-disc scopes and live owners; ordinary first-use
 // pipelines retain their existing source-frame scheduling.
 return !world&&!match&&!results&&!prize&&!host&&!running&&!pending&&!scoped_assets&&
        !preparation.busy()&&!faulted;
}
void schedule_startup_pipeline_service();
void startup_pipeline_service_callback(void*){
 startup_pipeline_service_scheduled=false;
 // This independent timer can run before tick consumes the terminal signal.
 // Query owner state without draining commands or changing lifecycle intent.
 if(EM_ASM_INT({return window.menuOwnerStopped?.()?1:0;}))return;
 if(!startup_pipeline_service_allowed())return;
 const AuroraStats* stats=aurora_get_stats();
 if(!stats||stats->queuedPipelines==0)return;
 // Emscripten invokes async_call through a delayed timer task. Schedule one
 // bounded Aurora batch between every sixth eligible rendered callback
 // instead of chaining timer tasks or adding work to every display interval.
 if(!aurora_pipeline_service_preparation()){
  startup_pipeline_service_failed=true;
 }
}
void schedule_startup_pipeline_service(){
 if(startup_pipeline_service_scheduled||startup_pipeline_service_failed||
    !startup_pipeline_service_allowed())return;
 const AuroraStats* stats=aurora_get_stats();
 if(!stats||stats->queuedPipelines==0)return;
 if(startup_pipeline_service_skip_frames<5){
  ++startup_pipeline_service_skip_frames;
  return;
 }
 startup_pipeline_service_skip_frames=0;
 startup_pipeline_service_scheduled=true;
 emscripten_async_call(startup_pipeline_service_callback,nullptr,
                       kStartupPipelineServiceDelayMs);
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
 // Consume owner failure and lifecycle suspension before source or clock work.
 // A hidden/frozen interval may contain no callback at all; its handoff leaves
 // running/manual pause intent unchanged and excludes only inactive wall time.
 const int owner_action=EM_ASM_INT({return window.menuServiceCommands?.() || 0;});
 if(owner_action<0){
  // A terminal JS/audio failure is consumed only at this source-free boundary.
  // Preserve the first owner error and frozen scene for diagnostics; recovery
  // requires a new document, so no native preparation or source work may follow.
  running=false;faulted=true;
  menu_clock.reset();audio_clock.reset();
  emscripten_cancel_main_loop();
  return;
 }
 if(owner_action==1){
  menu_clock.reset();audio_clock.reset();
 }
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
 melee_web::SourceFrameSequence callback_source_frames;
 auto& source_frames=replay?replay_source_frames:callback_source_frames;
 source_frames.begin_callback();
 int began=0,drawn=1,timing_valid=1,first_use=0;
 // A transition request owns the whole callback in which it is observed.
 // Keep the source presenter out of both the request and audio-ack waits.
 int suppress_draw=preparation.suppress_source_draw(pending)||replay_final_draw;
 bool actual_source_draw=false;
 bool replay_completed_now=false;
 const bool replay_input_already_drawn=replay_completion.final_input_drawn;
 unsigned replay_steps=0;
 unsigned replay_draw_boundaries=0;
 try{
  if(startup_pipeline_service_failed){
   startup_pipeline_service_failed=false;
   throw std::runtime_error("Renderer startup pipeline preparation failed");
  }
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
       if(source_frames.pending())melee_web::retail_replay_draw(*replay,replay_cursor-1);
       else melee_web::retail_replay_preparation_draw(*replay);
      }
     }
     else if(results){results->draw();actual_source_draw=true;drew_source=true;}
     else if(prize){prize->draw();actual_source_draw=true;drew_source=true;}
     else if(world&&host_entered){drawn=melee_web_menu_host_draw(host,error,sizeof(error));actual_source_draw=drawn!=0;drew_source=drawn!=0;}
    }
   }
   draw_done=emscripten_get_now();
   aurora_end_frame();end_done=emscripten_get_now();check(drawn,error);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   melee_web_provenance_frame(0);
   if(drew_source)provenance_return_draw=false;
#endif
   if(replay&&!replay_final_draw&&replay_cursor==replay->frames.size()&&
      drew_source&&source_frames.pending()){
    replay_completion.input_consumed=true;
    replay_completion.final_input_drawn=true;
    replay_completion.final_input_owner=static_cast<melee_web::ReplayCompletionOwner>(observed_replay_scene());
    if(replay->whole_session()){
     check(pending&&(results||prize),
           "Whole-session timeline must end at the final Results or Prize return request");
    }else{
     /* The final source frame is drawn before this close-boundary publication;
      * report the canonical MatchEnd winner after its source ranking exists. */
     (void)melee_web_match_rules_publish_result();
     replay_outcome=melee_web_match_rules_outcome(&replay_winner);
     replay_final_draw=true;replay_completed_now=true;running=false;menu_clock.reset();
     message="Reference replay complete; all input consumed and final frame drawn.";
    }
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
  MeleeWebAudio* const audio_owner=match?match->audio():results?results->audio():prize?prize->audio():world?world->audio():nullptr;
  const auto audio_elapsed=audio_clock.tick(
      clock_now,audio_owner&&input->visible&&(running||transition_audio_continues),
      [](const auto& event) noexcept {diagnostic_clock_stall(event,2);});
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
   const bool construction_complete=asset_destination!=AssetDestination::None?
       finish_asset_handoff():menu_scene_rebuild_pending?
       (finish_menu_scene_rebuild(),true):advance_match_construction();
   if(construction_complete){
    preparation_profile.construction_finished(emscripten_get_now());
    preparation.finish_construction(true,preparation_uses_source_draws());running=false;
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
    message=match?match_message:results?"Original Results":prize?"Original unlock notification":source_menu_message();
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
      simulation_clock_now,running&&(world||match||results||prize)&&input->visible,
      [](const auto& event) noexcept {diagnostic_clock_stall(event,1);});
  if(elapsed.stalled){running=false;message="Paused after a timing disruption. Resume to continue.";}
  if(elapsed.steps&&transition_audio_continues){
   transition_audio_continues=false;
  }
  for(unsigned step=0;step<elapsed.steps;step++){
   if(replay&&replay_cursor==replay->frames.size())break;
   if(results){
    const unsigned results_source_frame=results->source_frames();
    const auto boundary=scheduled_results_pauses.before_tick(results_source_frame);
    if(boundary==melee_web::ResultsSourceFramePauseSchedule::Boundary::missed)
     throw std::runtime_error("Missed scheduled Results source-frame pause; refusing a late boundary");
    if(boundary==melee_web::ResultsSourceFramePauseSchedule::Boundary::due){
     diagnostic_incident(9);
     running=false;menu_clock.reset();
     message="Paused at scheduled Results source frame "+std::to_string(results_source_frame)+".";
     break;
    }
   }
   source_frames.before_step(present_source);
   if(prepare_deferred_pipelines())break;
   PADStatus checked_input[4];const PADStatus* sample=input->raw;bool copied_input=false;
   bool diagnostic_start_pulse=false;
   if(results)activate_scheduled_results_pad(results->source_frames(),input->raw);
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
   const bool replay_whole=replay&&replay->whole_session();
   if(replay_whole){
    check(replay_cursor<replay->frames.size(),"Whole-session replay ran past its declared timeline");
    const int expected=expected_replay_scene(*replay,replay_cursor);
    const int observed=observed_replay_scene();
    if(observed!=expected){
     const char* owner=match?"match":results?"results":prize?"prize":host?"menu-host":"none";
     const int host_phase=host?melee_web_menu_host_phase(host):-1;
     throw std::runtime_error(
         "Whole-session replay source scene disagrees before input consumption: cursor="+
         std::to_string(replay_cursor)+" expected="+std::to_string(expected)+
         " observed="+std::to_string(observed)+" owner="+owner+
         " host_phase="+std::to_string(host_phase)+
         " asset_destination="+std::to_string(static_cast<int>(asset_destination))+
         " preparation_phase="+std::to_string(static_cast<int>(preparation.phase()))+
         " pending="+std::to_string(pending?1:0)+
         " running="+std::to_string(running?1:0));
    }
    sample=replay->frames[replay_cursor].pads.data();
    if(!replay_started){
     if(replay_trace)melee_web::retail_replay_session_initial(*replay);
     replay_started=true;
     EM_ASM({window.menuReplayStarted?.($0,!!$1,$2,$3);},replay->frames.size(),replay_trace,replay->expected_draws(),replay->scheduling_mode());
    }
   }
   if(match){
    if(replay&&!replay_whole){
     check(!match->paused()&&!match->complete(),"Replay reached an unsupported source pause/exit");
     if(!replay_started){
      replay_started=true;
      EM_ASM({window.menuReplayStarted?.($0,!!$1,$2,$3);},replay->frames.size(),replay_trace,replay->expected_draws(),replay->scheduling_mode());
     }
     sample=replay->frames[replay_cursor].pads.data();
    }
    else if(replay_whole)
     check(!match->paused(),"Whole-session replay reached an unsupported source pause");
    if(replay_whole)melee_web_cpu_observation_set_event_cursor(replay_cursor);
    match->tick(sample);
    if(replay_whole)melee_web_cpu_observation_scheduler_return();
    source_frames.did_step(!replay||replay->closes_draw_batch(replay_cursor));
    int winner=-1;const int outcome=match->outcome(winner);
    if(replay){
     replay_match_complete=match->complete();replay_outcome=outcome;replay_winner=winner;
     if(!replay_whole){
      if(replay_trace)melee_web::retail_replay_frame(*replay,replay_cursor);
      ++replay_cursor;
      ++replay_steps;
      replay_draw_boundaries+=replay->closes_draw_batch(replay_cursor-1)?1U:0U;
      check(!match->complete()||replay_cursor==replay->frames.size(),"Replay source match exited before all input was consumed");
     }
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
    if(match->complete()&&(!replay||replay_whole)){
     if(match->opening_demo()){
      pending=true;result=3;
     }else{
      check(outcome,"Original match transitioned without an outcome");pending=true;result=3;
     }
    }
   }
   else if(results){
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    const size_t trace_index=retain_results_pad_sample(results->source_frames(),sample);
#endif
    results->tick(sample);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    results_camera_entry_snapshot=results->camera_entry_snapshot();
    if(trace_index<results_pad_trace_count)results_pad_trace[trace_index].tick_returned=true;
    retain_results_state_after_tick(trace_index,results->source_frames());
#endif
    source_frames.did_step();if(results->requested())result=3;
   }
   else if(prize){prize->tick(sample);source_frames.did_step();if(prize->requested())result=3;}
   else{result=melee_web_menu_host_tick(host,sample,error,sizeof(error));check(result==1||result==3,error);source_frames.did_step();}
   if(replay_whole){
    // One continuous timeline: the cursor advances once per simulation step,
    // whichever owner consumed that step, and the owner must be the scene the
    // recipe declared for this frame.
    const int expected=expected_replay_scene(*replay,replay_cursor);
    const int observed=observed_replay_scene();
    if(observed!=expected)
     throw std::runtime_error(replay_scene_mismatch("post-consumption",replay_cursor,
                                                    expected,observed));
    if(replay_trace)melee_web::retail_replay_frame(*replay,replay_cursor,observed);
    ++replay_cursor;
    ++replay_steps;
    replay_draw_boundaries+=replay->closes_draw_batch(replay_cursor-1)?1U:0U;
   }
   if(result==3){
    if(results){
     check(scheduled_results_pad.all_consumed(),
           "Results returned before every scheduled source PAD sample was consumed");
     check(scheduled_results_pauses.all_consumed(),
           "Results returned before every scheduled source-frame pause was reached");
     clear_scheduled_results_pad();
     clear_scheduled_results_pauses();
    }
    pending=true;clear_diagnostic_pad();break;
   }
  }
  if(!audio_before_construction&&!audio_elapsed.stalled)
   for(unsigned step=0;step<audio_elapsed.steps;step++)
    render_audio_tick(audio_owner,error,sizeof(error));
  simulation_done=emscripten_get_now();
  simulation_cpu_ms=std::max(0.0,simulation_done-input_done-preparation_ms-render_total_ms);
  source_frames.finish(present_source);
  (void)prepare_deferred_pipelines();
  // Camera callbacks mutate source state (including magnifier damage flags).
  // A callback without a source tick retains the last match image when the
  // renderer guarantees complete draws. Menu priming retains its existing path.
  if(source_frames.steps()==0&&(preparation.preparation_draws_source()||(!world&&!match&&!results&&!prize)))present_source();
  if(preparation.warming()&&!preparation.preparation_draws_source()){
   const double service_started=emscripten_get_now();
   check(aurora_pipeline_service_preparation(),"Renderer preparation overlapped an active frame");
   preparation_ms+=emscripten_get_now()-service_started;
  }
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  (void)melee_web::pipeline_preparation::status();
#endif
  check(!replay||source_frames.draws()==replay_draw_boundaries,
        "Reference replay source draws disagree with clock batch boundaries");
  if(replay&&replay->whole_session()&&replay_completion.final_input_drawn){
   replay_completion.live_css_entered=world&&host&&host_entered&&!match&&!results&&!prize&&
       melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CSS;
   replay_completion.preparation_settled=!preparation.busy()&&!pending&&
       asset_destination==AssetDestination::None&&!menu_scene_rebuild_pending;
   replay_completion.source_tick_after_input|=replay_input_already_drawn&&source_frames.steps()!=0;
   replay_completion.source_draw_after_input|=replay_input_already_drawn&&source_frames.draws()!=0;
   if(melee_web::replay_completion_ready(replay_completion)){
    replay_final_draw=true;replay_completed_now=true;running=false;menu_clock.reset();
    message="Whole-session replay complete; final original character select entered.";
   }
  }
 }catch(const std::exception& e){diagnostic_incident(4);running=false;faulted=true;preparation.reset();render_only_preparation=false;pending=false;clear_diagnostic_pad();clear_scheduled_results_pad();clear_scheduled_results_pauses();menu_clock.reset();message=e.what();if(preparation_started)preparation_ms=emscripten_get_now()-preparation_started;preparation_failed(e.what());timing_valid=0;std::fprintf(stderr,"Native menu: %s\n",e.what());
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
  (actual_source_draw&&stats_after.lastTextureUploadSize!=0);
 const bool was_warming=preparation.warming();
 if(was_warming)preparation_profile.observe(finished-started,render_draw_ms,render_end_ms,
                                             actual_source_draw,stats_before,stats_after);
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 AuroraPipelinePrepareStatus selected_pipelines{};
 aurora_pipeline_prepare_status(&selected_pipelines);
 if(selected_pipelines.ready&&!pipeline_union_first_ready)pipeline_union_first_ready=finished;
 EM_ASM({Module.pipelinePreparation=({policy:'catalog',state:$0,ready:!!$1,selected:$2,pending:$3,
   unexpected_count:$4,deferred_count:$5,error_count:$6,binding_sha256:UTF8ToString($7),
   renderer_init_ms:$8,union_submit_ms:$9,union_ready_ms:$10,bootstrap_total_ms:$11});},
   selected_pipelines.state,selected_pipelines.ready,selected_pipelines.unique_count,
   selected_pipelines.pending_count,selected_pipelines.unexpected_count,
   selected_pipelines.deferred_count,selected_pipelines.error_count,
   MELEE_WEB_PIPELINE_SEED_SHA256,pipeline_renderer_init_ms,pipeline_union_submit_ms,
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
 // A browser pipeline handle may be returned before driver compilation. The
 // submitted complete draw owns that work even when Aurora's queue is empty.
 // Stop before the next source callback and retain its image until completion;
 // uploads and unassociated foreground stalls keep their existing guard.
 const int created_pipelines=stat_delta(stats_after.createdPipelines,stats_before.createdPipelines);
 const bool complete_draws=aurora_pipeline_complete_draws_enabled()!=0;
 const bool created_gpu_pending=created_pipelines>0&&complete_draws&&
     aurora_browser_submission_status().pendingStagingBuffers!=0;
 if(preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&running&&actual_source_draw&&
    melee_web::MenuPreparationState::needs_live_render_settle(
        stats_after.queuedPipelines,created_pipelines,complete_draws,created_gpu_pending)){
  if(preparation.request_render_settle(created_gpu_pending?false:preparation_uses_source_draws())){
   preparation_profile.begin(false,finished);
   render_only_preparation=false;
   diagnostic_incident(7);running=false;menu_clock.reset();audio_clock.reset();
   message="Preparing first-use rendering...";
   EM_ASM({window.menuPreparation?.(UTF8ToString($0),false);},message.c_str());
  }
 }
 schedule_startup_pipeline_service();
 const bool source_paused=match&&match->paused();
 if(match&&source_paused!=diagnostic_source_paused)diagnostic_incident(source_paused?5:6);
 diagnostic_source_paused=source_paused;
 const auto* diagnostic_start=match?match->diagnostic_start_data():nullptr;
 const auto diagnostic_character=[diagnostic_start](unsigned slot){
  if(!diagnostic_start||diagnostic_start->players[slot].slot_type==Gm_PKind_NA)return -1;
  return static_cast<int>(diagnostic_start->players[slot].ckind);
 };
 // A compact, read-only feed reuses existing timing/resource counters. Public
 // builds omit the development JSON profiler and its unrestricted source text.
 diagnostic_sample(
        started,diagnostic_source_frame(),melee_web_native_menu_phase(),
        diagnostic_start?diagnostic_start->rules.stkind:-1,
        diagnostic_character(0),diagnostic_character(1),
        diagnostic_character(2),diagnostic_character(3),
        menu_clock.pending_ticks(),simulation_cpu_ms,render_total_ms,finished-started,preparation_ms,
        stat_delta(stats_after.queuedPipelines,stats_before.queuedPipelines),
        stat_delta(stats_after.createdPipelines,stats_before.createdPipelines),
        callback_texture_upload,source_frames.steps(),source_frames.draws(),running);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
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
#else
 ++render_frame;
#endif
 if(replay_completed_now)EM_ASM({window.menuReplayCompleted?.($0,!!$1,$2,$3,$4);},
                                replay_cursor,replay_match_complete?1:0,
                                replay_outcome,replay_winner,observed_replay_scene());
 EM_ASM({window.menuFrame?.(!!$0);},running_at_callback_start?1:0);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 // Preserve lifecycle records from callbacks that did not draw a source frame.
 drain_pipeline_provenance();
#endif
}
}
extern "C" {
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
EMSCRIPTEN_KEEPALIVE const char* melee_web_native_menu_results_entry_packet(){
 // Read-only observer: serialize copied diagnostic storage, never live source
 // globals. KEEPALIVE exports only this getter; there is no injection endpoint.
 static std::string json;
 try{json=results_entry_packet.json();return json.c_str();}
 catch(...){return "{\"error\":\"Results entry packet serialization failed\"}";}
}
EMSCRIPTEN_KEEPALIVE const char* melee_web_native_menu_results_pad_trace(){
 static std::string json;
 json.clear();
 if(results){
  try{results_camera_entry_snapshot=results->camera_entry_snapshot();}
  catch(...){/* Preserve the last copied observer packet after source failure. */}
 }
 json.reserve(128+results_pad_trace_count*1024+
              results_camera_entry_snapshot.source_camera_subject_event_count*512);
 json+="{\"schema\":\"melee-web-results-pad-trace-v1\",\"source_frame_semantics\":\"zero-based Results source frame immediately before this tick attempt\",\"attempts\":";
 json+=std::to_string(results_pad_trace_attempts);
 json+=",\"retained\":";json+=std::to_string(results_pad_trace_count);
 json+=",\"capacity\":";json+=std::to_string(kResultsPadTraceCapacity);
 json+=",\"overflow\":";json+=results_pad_trace_overflow?"true":"false";
 append_camera_entry_json(json);
 json+=",\"samples\":[";
 for(size_t index=0;index<results_pad_trace_count;++index){
  if(index)json+=',';
  const auto& row=results_pad_trace[index];
  char sample[512];
  int length=std::snprintf(sample,sizeof(sample),
    "{\"source_frame\":%u,\"tick_returned\":%s,\"pads\":[",
    row.source_frame,row.tick_returned?"true":"false");
  if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
  json.append(sample,static_cast<size_t>(length));
  for(size_t port=0;port<4;++port){
   if(port)json+=',';
   const PADStatus& pad=row.pads[port];
   length=std::snprintf(sample,sizeof(sample),
    "{\"button\":%u,\"stick_x\":%d,\"stick_y\":%d,\"substick_x\":%d,\"substick_y\":%d,\"trigger_left\":%u,\"trigger_right\":%u,\"analog_a\":%u,\"analog_b\":%u,\"err\":%d"
#if defined(TARGET_PC)
    ",\"ext_button\":%u"
#endif
    "}",static_cast<unsigned>(pad.button),static_cast<int>(pad.stickX),
    static_cast<int>(pad.stickY),static_cast<int>(pad.substickX),
    static_cast<int>(pad.substickY),static_cast<unsigned>(pad.triggerLeft),
    static_cast<unsigned>(pad.triggerRight),static_cast<unsigned>(pad.analogA),
    static_cast<unsigned>(pad.analogB),static_cast<int>(pad.err)
#if defined(TARGET_PC)
    ,static_cast<unsigned>(pad.extButton)
#endif
   );
   if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
   json.append(sample,static_cast<size_t>(length));
  }
  json+="],\"source_consumed_pads\":";
  if(!row.results_state_sampled)json+="null";
  else{
   json+='[';
   for(size_t port=0;port<4;++port){
    if(port)json+=',';
    const HSD_PadStatus& consumed=row.source_consumed_pads[port];
    length=std::snprintf(sample,sizeof(sample),
     "{\"button\":%u,\"trigger\":%u,\"release\":%u,\"err\":%d}",
     static_cast<unsigned>(consumed.button),static_cast<unsigned>(consumed.trigger),
     static_cast<unsigned>(consumed.release),static_cast<int>(consumed.err));
    if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
    json.append(sample,static_cast<size_t>(length));
   }
   json+=']';
  }
  json+=",\"results_state_after_tick\":";
  if(!row.results_state_sampled)json+="null";
  else{
   char state_sample[256];
   int state_length=std::snprintf(state_sample,sizeof(state_sample),
    "{\"source_frame\":%u,\"phase\":%u,\"stats_phase\":%u,\"num_pages\":%u,\"players\":[",
    row.results_state_frame,static_cast<unsigned>(row.results_phase),
    static_cast<unsigned>(row.results_stats_phase),
    static_cast<unsigned>(row.results_num_pages));
   if(state_length>=0&&static_cast<size_t>(state_length)<sizeof(state_sample))
    json.append(state_sample,static_cast<size_t>(state_length));
   for(size_t slot=0;slot<4;++slot){
    if(slot)json+=',';
    state_length=std::snprintf(state_sample,sizeof(state_sample),
     "{\"page\":%u,\"confirmed\":%u}",
     static_cast<unsigned>(row.results_player_pages[slot]),
     static_cast<unsigned>(row.results_player_confirmed[slot]));
    if(state_length>=0&&static_cast<size_t>(state_length)<sizeof(state_sample))
     json.append(state_sample,static_cast<size_t>(state_length));
   }
   json+="]}";
  }
  json+="}";
 }
 json+="]}";
 return json.c_str();
}
#endif
unsigned melee_web_native_asset_begin(){try{
 close();scoped_assets=true;
 request_assets(AssetDestination::InitialMenu);
 return asset_generation;
}catch(const std::exception& e){message=e.what();return 0;}}
unsigned melee_web_native_asset_count(unsigned generation){
 return generation&&generation==asset_scope.pending_generation()?requested_assets.size():0;
}
const char* melee_web_native_asset_name(unsigned generation,unsigned index){
 return generation&&generation==asset_scope.pending_generation()&&index<requested_assets.size()?
        requested_assets[index].c_str():nullptr;
}
int melee_web_native_asset_file(unsigned generation,const char* name,const uint8_t* data,unsigned size){try{
 check(scoped_assets&&!world&&!match&&!archive_cache&&name&&data,
       "Native asset transfer requires closed source owners");
 asset_scope.put(generation,name,{data,size});return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_asset_commit(unsigned generation){try{
 check(scoped_assets&&!world&&!match&&!results&&!prize&&!archive_cache,
      "Close source owners before asset commit");
 asset_scope.commit(generation);asset_committed=true;return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_asset_abort(unsigned generation){try{
 asset_scope.abort(generation);return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_set_save_profile(int mode,const uint8_t* data,unsigned size){try{
 check(!host&&!world&&!match&&!results&&!prize&&!host_entered,
       "Unload source owners before changing the staged save profile");
 check(mode==MELEE_WEB_SAVE_MODE_EVERYTHING||mode==MELEE_WEB_SAVE_MODE_PERSONAL,
       "Unsupported save mode");
 check((size==0&&!data)||(size==kSaveProfileBytes&&data),
       "Save profile must be empty or the exact original card-manifest extent");
 check(mode!=MELEE_WEB_SAVE_MODE_EVERYTHING||size==0,
       "Everything unlocked uses its original source baseline");
 std::vector<uint8_t> next;
 if(size)next.assign(data,data+size);
 configured_save_profile=std::move(next);configured_save_mode=mode;
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_snapshot_save_profile(uint8_t* output,unsigned size,int baseline){try{
 check(host!=nullptr,"A live source profile is unavailable for export");
 char error[256]{};
 check(melee_web_menu_host_snapshot_card_data(host,baseline,output,size,error,sizeof(error)),error);
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_snapshot_unlocked_baseline(uint8_t* output,unsigned size){try{
 check(output&&size==MELEE_WEB_SAVE_PROFILE_CARD_BYTES,
       "Baseline export requires the exact original card-manifest extent");
 if(host){
  char error[256]{};
  check(melee_web_menu_host_snapshot_card_data(host,1,output,size,error,sizeof(error)),error);
  return 1;
 }
 check(!world&&!match&&!results&&!prize&&!source_session_owned,
       "Close source owners before establishing the original save baseline");
 check(!archive_cache,
       "Release the native archive cache before establishing an unowned save baseline");
 begin_source_session();
 char error[256]{};
 MeleeWebSaveProfileOwner* profile=melee_web_save_profile_owner_create(error,sizeof(error));
 if(!profile)throw std::runtime_error(error);
 bool active=false;
 std::unique_ptr<melee_web::GameplayMenuWorld> baseline_world;
 try{
  check(melee_web_save_profile_owner_activate(profile,error,sizeof(error)),error);active=true;
  check(melee_web_save_profile_owner_initialize_default(profile,error,sizeof(error)),error);
  archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
  baseline_world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
  Toy_803124BC();
  check(melee_web_save_profile_owner_initialize_everything(profile,error,sizeof(error)),error);
  check(melee_web_save_profile_owner_snapshot_card_data(profile,output,size,error,sizeof(error)),error);
  check(melee_web_save_profile_owner_deactivate(profile,error,sizeof(error)),error);active=false;
  check(melee_web_save_profile_owner_destroy(profile,error,sizeof(error)),error);profile=nullptr;
  baseline_world->close_prepared();baseline_world.reset();
  archive_cache.reset();
  check(melee_web_gameplay_session_end(error,sizeof(error)),error);source_session_owned=false;
 }catch(...){
  if(baseline_world){
   try{baseline_world->close_prepared();}catch(...){}
   baseline_world.reset();
  }
  archive_cache.reset();
  if(active&&!melee_web_save_profile_owner_deactivate(profile,nullptr,0))std::abort();
  if(profile&&!melee_web_save_profile_owner_destroy(profile,nullptr,0))std::abort();
  if(source_session_owned){
   if(!melee_web_gameplay_session_end(nullptr,0))std::abort();
   source_session_owned=false;
  }
  throw;
 }
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_file(const char* name,const uint8_t* data,unsigned size){try{
if(scoped_assets)throw std::runtime_error("Scoped disc imports require an asset transaction");
 if(world||match||results||prize||!name||!data||!size||size>64*1024*1024)
  throw std::runtime_error("Unload before importing valid local files");
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 if(std::string_view{name}=="dsp_coef.bin")throw std::runtime_error("Public audio-disabled runtime does not accept DSP coefficient input");
#endif
 bool known=false;
 for(auto key:keys)known|=key==name;
 for(auto key:route_asset_keys)known|=key==name;
 for(auto key:zelda_sheik_keys)known|=key==name;
 if(!known)throw std::runtime_error("Unknown native menu file: "+std::string(name));
 archive_cache.reset();
 files[name]={data,data+size};return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_source_file_external_set(const char* name,unsigned size){try{
 if(world||match||results||prize||host_entered)
  throw std::runtime_error("Close native source owners before configuring streamed disc files");
 char error[256]{};
 check(melee_web_source_files_external_set(name,size,error,sizeof(error)),
       error[0]?error:"Native streamed disc file configuration failed");
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_source_files_external_clear(){try{
 if(world||match||results||prize||host_entered)
  throw std::runtime_error("Close native source owners before clearing streamed disc files");
 char error[256]{};
 check(melee_web_source_files_external_clear(error,sizeof(error)),
       error[0]?error:"Native streamed disc file catalog release failed");
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_prepare(){try{
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
#endif
 if(match||results||prize||host_entered)throw std::runtime_error("Unload before preparing native menu resources");
 if(world&&host){message="Native menu resources already prepared.";return 1;}
 if(scoped_assets){
  check(asset_destination==AssetDestination::InitialMenu&&asset_committed,
        "Import the complete menu asset scope before preparation");
  asset_destination=AssetDestination::None;asset_committed=false;
 }
 if(world||host)close();
 char error[256]{};const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 // Preserve the source ownership order used by launch: the menu host claims
 // RNG/session ownership before the SDK world is started. No source scene or
 // simulation callback runs during this preparation phase.
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 begin_source_session();
 host=melee_web_menu_host_create_with_profile(
     configured_save_mode,configured_save_profile.empty()?nullptr:configured_save_profile.data(),
     configured_save_profile.size(),error,sizeof(error));check(host!=nullptr,error);
 // One unentered menu preparation belongs to the canonical fresh import.
 // Repeating it changes allocation history even without entering a source scene.
 if(reference_menu_preparations++)reference_heap_used=true;
 world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 check(melee_web_menu_host_initialize_profile_baseline(host,error,sizeof(error)),error);
 const double constructed=emscripten_get_now();
 report_construction("scene-prepare",started,constructed,constructed,before,aurora_stats_snapshot());
 message="Native menu resources prepared.";return 1;
}catch(const std::exception& e){
 diagnostic_incident(4);
 if(world&&!host_entered){try{world->close_prepared();}catch(...){}}
 world.reset();
 if(host&&!host_entered){char ignored[256]{};melee_web_menu_host_destroy(host,ignored,sizeof(ignored));host=nullptr;}
 if(source_session_owned&&!world&&!host){
  char ignored[256]{};
  if(melee_web_gameplay_session_end(ignored,sizeof(ignored)))source_session_owned=false;
 }
 message=e.what();return 0;
}}
int melee_web_native_menu_launch(){try{
 if(preparation.busy()||pending)
  throw std::runtime_error("Native menu transition is still preparing");
 if(match||results||prize||host_entered||(host&&!world))close();
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 begin_source_session();
 char error[256]{};if(!host){host=melee_web_menu_host_create_with_profile(
     configured_save_mode,configured_save_profile.empty()?nullptr:configured_save_profile.data(),
     configured_save_profile.size(),error,sizeof(error));check(host!=nullptr,error);}
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
}catch(const std::exception& e){diagnostic_incident(4);message=e.what();running=false;return 0;}}
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_replay(const uint8_t* data,unsigned size,int observe){try{
 replay_source_frames=melee_web::SourceFrameSequence{};
 check(data&&size<=melee_web::kRetailReplayMaxBytes,"Invalid reference replay bytes");
 check(observe==0||observe==1,"Invalid replay observation mode");
 auto candidate=std::make_unique<melee_web::RetailReplayRecipe>(melee_web::read_retail_replay({data,size}));
 check(candidate->version!=6||observe==1,"Recorded input-queue replay requires state-capture mode; live timing is not admitted");
 check(candidate->version>=2&&candidate->initial_input,"Browser reference playback requires a PAD history recipe (v2 or v3)");
 // Both replay forms start in a fresh application. A whole-session recipe
 // retains the canonical unentered CSS preparation and its scoped assets;
 // close() would release them before launch can enter the original scene.
 check(!reference_heap_used,"Reference replay requires a fresh application. Use Reload application state, import the disc, then play the recipe before entering menus.");
 if(candidate->whole_session())
  check(world&&host&&!host_entered&&!world_exposed&&!match&&!results&&!prize&&
        melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED,
        "Whole-session replay requires the fresh prepared character-select owner");
 reference_heap_used=true;
 if(!candidate->whole_session())close();
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 replay=std::move(candidate);replay_trace=observe;replay_pending=!replay->whole_session();
 replay_completion={};replay_completion.whole_session=replay->whole_session();
 match_message="Reference replay: "+selected_match_message(replay->selection);
 if(replay->whole_session()){
  // A whole-session timeline starts at CSS, so its entry is the ordinary menu
  // launch the caller already uses: the caller prepares the menu resources and
  // enters the world, and the timeline then drives CSS, SSS, the match,
  // Results and the return through one cursor.
  message="Whole-session reference replay ready; launch to enter character select.";
  return 1;
 }
 check(preparation.request(),"Replay preparation is already active");
 preparation_profile.begin(true,emscripten_get_now());
 VISetFrameBufferScale(1);
 message="Preparing reference replay...";
 EM_ASM({window.menuPreparation?.(UTF8ToString($0));},message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
unsigned melee_web_native_menu_replay_cursor(){return static_cast<unsigned>(replay_cursor);}
int melee_web_native_menu_replay_whole_session(){return replay&&replay->whole_session()?1:0;}
void melee_web_native_menu_pause(int paused){
 if(faulted||replay_final_draw||preparation.busy()||pending||(!host_entered&&!match&&!results&&!prize))return;
 diagnostic_incident(paused?5:6);
 running=(world||match||results||prize)&&!paused;menu_clock.reset();
 message=running?(match?match_message:results?"Original Results":prize?"Original unlock notification":source_menu_message()):"Paused.";
}
void melee_web_native_menu_confirm_check(){
 if(!replay&&(host_entered||match)&&!faulted&&!preparation.busy()&&!pending&&stock_check!=-1&&diagnostic_pad_remaining==0)
  diagnostic_start_ticks=3;
}
int melee_web_native_menu_pad_sample_full(unsigned port,unsigned buttons,int stick_x,int stick_y,
                                          int cstick_x,int cstick_y,unsigned trigger_l,
                                          unsigned trigger_r,unsigned duration){try{
 if(replay||faulted||preparation.busy()||pending||stock_check==-1||diagnostic_start_ticks!=0||!running||(!host_entered&&!match&&!results&&!prize))
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
int melee_web_native_menu_results_pad_schedule(unsigned source_frame,unsigned port,
                                                unsigned buttons,unsigned duration){try{
 const int phase=host&&host_entered?melee_web_menu_host_phase(host):-1;
 if(replay||faulted||preparation.busy()||pending||!running||!host_entered||match||
    results||prize||(phase!=MELEE_WEB_MENU_CSS&&phase!=MELEE_WEB_MENU_CSS_READY&&
                     phase!=MELEE_WEB_MENU_SSS&&phase!=MELEE_WEB_MENU_SSS_READY)||
    diagnostic_pad_remaining||diagnostic_start_ticks||stock_check==-1)
  throw std::runtime_error("Results PAD schedule requires an idle original CSS/SSS before Match construction");
 if(port!=0||buttons!=PAD_BUTTON_START||duration<1||duration>120||source_frame>8191)
  throw std::runtime_error("Results PAD schedule is outside the P1 Start source-tick bounds");
 if(scheduled_results_pad.started()||scheduled_results_pad.full())
  throw std::runtime_error("Results PAD schedule is already running or full");
 if(!scheduled_results_pad.enqueue({source_frame,port,buttons,duration}))
  throw std::runtime_error("Results PAD source tick is invalid, duplicated, or unordered");
 message="Results P1 Start scheduled at an exact future source tick.";
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_results_pause_schedule(unsigned source_frame){try{
 const int phase=host&&host_entered?melee_web_menu_host_phase(host):-1;
 if(replay||faulted||preparation.busy()||pending||!running||!host_entered||match||
    results||prize||(phase!=MELEE_WEB_MENU_CSS&&phase!=MELEE_WEB_MENU_CSS_READY&&
                     phase!=MELEE_WEB_MENU_SSS&&phase!=MELEE_WEB_MENU_SSS_READY)||
    diagnostic_pad_remaining||diagnostic_start_ticks||stock_check==-1)
  throw std::runtime_error("Results source-frame pause schedule requires idle original CSS/SSS before Match construction");
 if(source_frame>8191)
  throw std::runtime_error("Results source-frame pause is outside the supported source cursor bounds");
 if(scheduled_results_pauses.started()||scheduled_results_pauses.full())
  throw std::runtime_error("Results source-frame pause schedule is already running or full");
 if(!scheduled_results_pauses.enqueue(source_frame))
  throw std::runtime_error("Results source-frame pause is invalid, duplicated, or unordered");
 message="Results source-frame pause scheduled at an exact future cursor.";
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_player_state(unsigned player,int* fighter_kind,int* motion_id,
                                       int* ground_or_air,unsigned* source_frame,
                                       float* position_x,float* position_y){try{
 if(!fighter_kind||!motion_id||!ground_or_air||!source_frame||!position_x||!position_y||
    player>=4||!match||!match->ready())
  throw std::runtime_error("Player state requires a ready source match and valid output storage");
 const auto stats=match->player_stats(player);*fighter_kind=stats.fighter_kind;
 *motion_id=stats.motion_id;*ground_or_air=stats.ground_or_air;*source_frame=match->source_frames();
 *position_x=stats.position[0];*position_y=stats.position[1];return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
const char* melee_web_native_menu_match_observe(){
 static char text[1536];
 if(!match)return terminal_match_observation.empty()?"{}":terminal_match_observation.c_str();
 if(!match->construction_complete())return "{}";
 try{
 match_observer_error.clear();
 const auto p0=match->player_stats(0),p1=match->player_stats(1);
 const StartMeleeData& start=match->start_data();
 int winner=-1;const int outcome=match->outcome(winner);
 std::snprintf(text,sizeof(text),
  "{\"ready\":%s,\"paused\":%s,\"ending\":%s,\"complete\":%s,"
  "\"frame\":%u,\"rng\":%u,\"outcome\":%d,\"winner\":%d,"
  "\"rules\":{\"match_kind\":%d,\"stage\":%u,\"timer_enabled\":%u,"
  "\"time_limit\":%u,\"item_frequency\":%d,\"item_mask_hex\":\"%016llx\","
  "\"is_teams\":%u,\"player_teams\":[%d,%d],\"player_stocks\":[%d,%d]},\"players\":["
  "{\"fighter\":%d,\"stocks\":%d,\"motion\":%d,\"groundAir\":%d,\"x\":%.9g,\"y\":%.9g},"
  "{\"fighter\":%d,\"stocks\":%d,\"motion\":%d,\"groundAir\":%d,\"x\":%.9g,\"y\":%.9g}]}",
  match->ready()?"true":"false",match->paused()?"true":"false",
  match->ending()?"true":"false",match->complete()?"true":"false",
  match->source_frames(),match->random_seed(),outcome,winner,
  (int) start.rules.match_kind,(unsigned) start.rules.stkind,
  (unsigned) start.rules.timer_enabled,(unsigned) start.rules.time_limit,
  (int) (int8_t) start.rules.xB,(unsigned long long) start.rules.x20,
  (unsigned) start.rules.is_teams,(int) start.players[0].team,(int) start.players[1].team,
  (int) start.players[0].stocks,(int) start.players[1].stocks,
  p0.fighter_kind,p0.stocks,p0.motion_id,p0.ground_or_air,p0.position[0],p0.position[1],
  p1.fighter_kind,p1.stocks,p1.motion_id,p1.ground_or_air,p1.position[0],p1.position[1]);
 return text;
 }catch(const std::exception& e){
  match_observer_error=e.what();
  std::snprintf(text,sizeof(text),"{\"ready\":false,\"observer_error\":true}");
  return text;
 }
}
int melee_web_native_menu_drive_fighter(int character_kind){try{
 if(!host||melee_web_menu_host_phase(host)!=1)throw std::runtime_error("Fighter selection drive requires the original CSS");
 if(css_fighter_release_port>=0){
  const unsigned port=static_cast<unsigned>(css_fighter_release_port);
  check(melee_web_native_menu_pad_sample_full(port,0,0,0,0,0,0,0,1),message.c_str());
  css_fighter_release_port=-1;
  return 1;
 }
 MeleeWebFighterInputObservation observed{};check(melee_web_fighter_input_observe(character_kind,&observed),"CSS target observation is unavailable");
 last_css_fighter_observation=observed;last_css_fighter_target=character_kind;
 last_css_fighter_observation_valid=true;
 PADStatus raw[PAD_MAX_CONTROLLERS]{};const int state=melee_web_fighter_input_drive(raw,&observed,character_kind);
 last_css_fighter_drive_state=state;
 check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,"CSS target observation is invalid");
 if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED)return 2;
 const bool pressed=state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||
                    state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY;
 if(pressed)raw[observed.cursor_port].button|=PAD_BUTTON_A;
 check(melee_web_native_menu_pad_sample_full(observed.cursor_port,raw[observed.cursor_port].button,
       raw[observed.cursor_port].stickX,raw[observed.cursor_port].stickY,
       raw[observed.cursor_port].substickX,raw[observed.cursor_port].substickY,
       raw[observed.cursor_port].triggerLeft,raw[observed.cursor_port].triggerRight,1),message.c_str());
 if(pressed)css_fighter_release_port=observed.cursor_port;
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
const char* melee_web_native_menu_source_observe(){
 static char text[3072];
 MeleeWebMenuSourceObservation observed{};
 StartMeleeData start{};
 int source_valid=0,start_valid=0;
 int live_items_valid=0,live_item_cursor=-1,live_item_enabled=-1,live_item_frequency=-1;
 char error[256]{};
 if(host&&host_entered&&melee_web_menu_host_source_observe(
       host,&observed,error,sizeof(error)))source_valid=1;
 if(host&&!host_entered&&melee_web_menu_host_phase(host)==MELEE_WEB_MENU_READY&&
    melee_web_menu_host_raw_selection(host,&start,error,sizeof(error)))start_valid=1;
 if(!source_valid&&!start_valid)return "{}";
 /* The item mask is copied into global match preferences when the original
  * Items routine commits its private MnItemSwData. Observe that live row
  * separately so a source-confirmed A input is not mistaken for that commit. */
 if(source_valid&&observed.source_scene==MELEE_WEB_MENU_HOST_SCENE_MAIN&&
    observed.menu_kind==0x10&&mnItemSw_804D6BE8&&
    mnItemSw_804D6BE8->user_data){
  const auto* item_data=static_cast<const MnItemSwData*>(mnItemSw_804D6BE8->user_data);
  if(item_data->cursor<=0x20&&item_data->x21<=5&&
     item_data->cursor==static_cast<uint8_t>(observed.hovered_selection)){
   live_items_valid=1;
   live_item_cursor=item_data->cursor;
   if(item_data->cursor<0x1f)live_item_enabled=item_data->items[item_data->cursor];
   else live_item_frequency=item_data->x21;
  }
 }
 char start_players[1280]="[]";
 if(start_valid){
  size_t used=0;start_players[used++]='[';start_players[used]='\0';
  for(unsigned i=0;i<GM_MAX_PLAYERS;++i){
   const auto& player=start.players[i];
   const int written=std::snprintf(start_players+used,sizeof(start_players)-used,
    "%s{\"character_kind\":%d,\"slot_type\":%d,\"stocks\":%d,"
    "\"color\":%d,\"team\":%d,\"rumble_enabled\":%d,"
    "\"cpu_kind\":%d,\"cpu_level\":%d}",
    i?",":"",(int)player.ckind,(int)player.slot_type,(int)player.stocks,
    (int)player.color,(int)player.team,(int)player.rumble_enabled,
    (int)player.cpu_kind,(int)player.cpu_level);
   if(written<0||(size_t)written>=sizeof(start_players)-used)
    return "{\"observer_error\":true,\"observer_error_reason\":\"StartMeleeData roster buffer overflow\"}";
   used+=(size_t)written;
  }
  if(used+2>sizeof(start_players))
   return "{\"observer_error\":true,\"observer_error_reason\":\"StartMeleeData roster terminator overflow\"}";
  start_players[used++]=']';start_players[used]='\0';
 }
 std::snprintf(text,sizeof(text),
  "{\"source\":{\"valid\":%s,\"scene\":%d,\"menu_kind\":%d,"
  "\"previous_menu_kind\":%d,\"hovered_selection\":%d,"
  "\"confirmed_selection\":%d,\"buttons\":%llu,\"item_input_locked\":%d,"
  "\"rules\":{\"mode\":%d,\"stock_count\":%d,\"time_limit\":%d,"
  "\"stock_time_limit\":%d,\"handicap\":%d,\"damage_ratio\":%d,"
  "\"friendly_fire\":%d},\"items\":{\"frequency\":%d,"
  "\"mask_hex\":\"%016llx\"},"
  "\"css_setup\":{\"valid\":%s,\"is_teams\":%d,"
  "\"player_teams\":[%d,%d]}},"
  "\"items_menu\":{\"valid\":%s,\"cursor\":%d,"
  "\"selected_item_enabled\":%d,\"frequency_selector\":%d},"
  "\"start\":{\"valid\":%s,\"match_kind\":%d,\"stage\":%u,"
  "\"item_frequency\":%d,\"item_mask_hex\":\"%016llx\","
  "\"player_stocks\":[%d,%d],\"players\":%s}}",
  source_valid?"true":"false",observed.source_scene,observed.menu_kind,
  observed.previous_menu_kind,observed.hovered_selection,
  observed.confirmed_selection,(unsigned long long) observed.menu_buttons,
  observed.item_input_locked,
  observed.rule_mode,observed.stock_count,observed.time_limit,
  observed.stock_time_limit,observed.handicap,observed.damage_ratio,
  observed.friendly_fire,observed.item_frequency,
  (unsigned long long) observed.item_mask,
  observed.css_setup_valid ? "true" : "false", observed.css_is_teams,
  observed.css_player_teams[0], observed.css_player_teams[1],
  live_items_valid?"true":"false",live_item_cursor,live_item_enabled,live_item_frequency,
  start_valid?"true":"false",(int) start.rules.match_kind,
  (unsigned) start.rules.stkind,(int) (int8_t) start.rules.xB,
  (unsigned long long) start.rules.x20,
  (int) start.players[0].stocks,(int) start.players[1].stocks,start_players);
 return text;
}
const char* melee_web_native_menu_memory(){
 // Lifecycle diagnostics only: mallinfo walks the allocator's free lists.
 // Reserved linear memory is not the same as live allocations and cannot shrink.
 const auto info=mallinfo();
const auto allocation=melee_web_gameplay_allocation();
 const auto source=melee_web_gameplay_stats();
 static char text[2048];
 std::snprintf(text,sizeof(text),
  "{\"wasm_heap_bytes\":%zu,\"allocator_arena_bytes\":%zu,"
  "\"allocator_live_bytes\":%zu,\"allocator_free_bytes\":%zu,"
  "\"allocator_top_free_bytes\":%zu,"
"\"source_allocation_identity\":%llu,\"source_allocation_generation\":%llu,"
  "\"source_allocation_bytes\":%llu,\"source_session_owned\":%s,"
  "\"source_world_generation\":%llu,\"source_objects\":%u,\"source_processes\":%u,"
  "\"source_heap_free_bytes\":%d,\"cached_archives\":%zu,\"cached_audio_banks\":%zu,"
  "\"match_present\":%s,\"menu_present\":%s,\"results_present\":%s,\"prize_present\":%s,"
  "\"menu_host_entered\":%s,\"menu_source_scene\":%d,\"menu_mode_kind\":%d,"
  "\"menu_route_target\":%d,\"menu_phase\":%d,\"menu_scene_rebuild_pending\":%s,"
  "\"css_input\":{\"valid\":%s,\"target\":%d,\"state\":%d,"
  "\"cursor_port\":%d,\"held_door\":%d,\"selected_character\":%d,"
  "\"source_active_port\":%d,\"source_start_cooldown\":%d,"
  "\"source_active_cursor_count\":%d,\"source_pending_scene\":%d,"
  "\"source_start_ready\":%d,\"source_selected_model_state\":%d,"
  "\"source_confirm_callback_count\":%d,\"source_last_start_trigger\":%d,"
  "\"source_last_start_ready\":%d,\"source_last_start_pending\":%d,"
  "\"cursor\":[%.3f,%.3f],\"model\":[%.3f,%.3f],"
  "\"target_bounds\":[%.3f,%.3f,%.3f,%.3f]},"
  "\"source_pad0\":{\"err\":%d,\"button\":%u,\"trigger\":%u,\"last_button\":%u},"
  "\"scoped_assets\":%s,\"asset_files\":%zu,\"asset_bytes\":%zu,"
  "\"asset_source_bytes\":%zu,\"staged_asset_files\":%zu,"
  "\"staged_asset_bytes\":%zu,\"asset_generation\":%u}",
  emscripten_get_heap_size(),info.arena,info.uordblks,info.fordblks,info.keepcost,
  (unsigned long long)allocation.identity,(unsigned long long)allocation.generation,
  (unsigned long long)allocation.bytes,source_session_owned?"true":"false",
  (unsigned long long)source.generation,source.objects,source.processes,source.heap_free_bytes,
  archive_cache?archive_cache->archive_count():0,archive_cache?archive_cache->audio_bank_count():0,
  match?"true":"false",world?"true":"false",results?"true":"false",prize?"true":"false",
  host_entered?"true":"false",host?melee_web_menu_host_source_scene(host):0,
  host?melee_web_menu_host_mode_kind(host):-1,
  host?melee_web_menu_host_route_target_mode(host):-1,
  host?melee_web_menu_host_phase(host):0,
  menu_scene_rebuild_pending?"true":"false",
  last_css_fighter_observation_valid?"true":"false",
  last_css_fighter_target,last_css_fighter_drive_state,
  last_css_fighter_observation.cursor_port,last_css_fighter_observation.held_door,
  last_css_fighter_observation.selected_character_kind,
  last_css_fighter_observation.source_active_port,
  last_css_fighter_observation.source_start_cooldown,
  last_css_fighter_observation.source_active_cursor_count,
  last_css_fighter_observation.source_pending_scene,
  last_css_fighter_observation.source_start_ready,
  last_css_fighter_observation.source_selected_model_state,
  last_css_fighter_observation.source_confirm_callback_count,
  last_css_fighter_observation.source_last_start_trigger,
  last_css_fighter_observation.source_last_start_ready,
  last_css_fighter_observation.source_last_start_pending,
  last_css_fighter_observation.cursor_x,last_css_fighter_observation.cursor_y,
  last_css_fighter_observation.model_x,last_css_fighter_observation.model_y,
  last_css_fighter_observation.target_left,last_css_fighter_observation.target_right,
  last_css_fighter_observation.target_top,last_css_fighter_observation.target_bottom,
  HSD_PadCopyStatus[0].err,static_cast<unsigned>(HSD_PadCopyStatus[0].button),
  static_cast<unsigned>(HSD_PadCopyStatus[0].trigger),
  static_cast<unsigned>(HSD_PadCopyStatus[0].last_button),
  scoped_assets?"true":"false",
  asset_scope.active_file_count(),asset_scope.active_byte_count(),asset_scope.active_source_bytes(),
  asset_scope.staged_file_count(),asset_scope.staged_byte_count(),asset_scope.pending_generation());
 return text;
}
const char* melee_web_native_menu_diagnostics(){
 static char text[640];
 std::snprintf(text,sizeof(text),"Completed matches: %u · stock check: %d · ticks: %u · stocks: %d · respawns: %d",
  completed_matches,stock_check,stock_tick,stock_count,stock_respawns);
 if(!match_observer_error.empty()){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · match observer error: %.320s",match_observer_error.c_str());
  return text;
 }
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
 else if(results){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · Results source frame: %u",results->source_frames());
 }
 else if(prize){
  const auto length=std::char_traits<char>::length(text);
  std::snprintf(text+length,sizeof(text)-length," · Prize source frame: %u",prize->source_frames());
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
int melee_web_native_menu_phase(){
 if(match)return 7;
 if(results)return 8;
 if(prize)return 9;
 if(host&&host_entered){
  /* Keep source-owned Title/Main/Opening distinct from a closed CSS session. */
  switch(melee_web_menu_host_source_scene(host)){
  case MELEE_WEB_MENU_HOST_SCENE_TITLE:return 10;
  case MELEE_WEB_MENU_HOST_SCENE_MAIN:return 11;
  case MELEE_WEB_MENU_HOST_SCENE_OPENING:return 12;
  case MELEE_WEB_MENU_HOST_SCENE_OPENING_VS:return 13;
  }
 }
 return host?melee_web_menu_host_phase(host):0;
}
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
#if !defined(MELEE_WEB_SELECTIVE_PIPELINES)
 // Development replay must draw every requested primitive. Its first-use
 // compilation remains measured; it must not silently skip geometry and then
 // repair the image by invoking extra source camera callbacks.
 if(!aurora_pipeline_set_complete_draws(1))return 1;
#endif
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
