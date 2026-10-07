#include "gameplay_menu_browser_state.hpp"
#if defined(MELEE_WEB_NET_SESSION)
extern "C" int melee_web_net_publish_local_input(
    uint32_t source_tick, unsigned local_port, uint64_t poll_serial,
    const uint8_t bytes[MELEE_WEB_NET_PAD_BYTES])
{
 const auto serial_low=static_cast<uint32_t>(poll_serial);
 const auto serial_high=static_cast<uint32_t>(poll_serial>>32);
 return EM_ASM_INT({
  const callback=globalThis.__meleeWebNetLocalInputCapture;
  const serial=$0*4294967296+$1;
  if(typeof callback!=="function"||!Number.isSafeInteger(serial))return 0;
  try{return callback($2,$3,serial,HEAPU8.slice($4,$4+11))===true?1:0;}
  catch(_){return 0;}
 },serial_high,serial_low,source_tick,local_port,bytes);
}
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
extern "C" void melee_web_native_menu_pause(int paused);
#endif
namespace melee_web_menu_browser {
melee_web::RuntimeFiles files;
melee_web::RuntimeAssetScope asset_scope(files);
AssetDestination asset_destination=AssetDestination::None;
// Disc import mode survives scene unload; active asset transactions do not.
bool scoped_disc_import=false;
bool scoped_assets=false,asset_committed=false;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
bool stadium_c1a_armed=false;
std::string stadium_c1a_observation;
int stadium_c1a_raw_stkind=-1;
#endif
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
std::array<ResultsPadTraceRow,kResultsPadTraceCapacity> results_pad_trace{};
size_t results_pad_trace_count=0;
uint64_t results_pad_trace_attempts=0;
bool results_pad_trace_overflow=false;
MeleeWebResultsCameraEntrySnapshot results_camera_entry_snapshot{};
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
void diagnostic_staging_sample(bool force=false){
 if(!aurora_browser_staging_diagnostics_enabled())return;
 const auto staging=aurora_browser_staging_diagnostic_snapshot();
 EM_ASM({try{window.menuDiagnosticStagingSample?.(
  $0,$1,$2,$3,$4,$5,$6,$7);
 }catch(_){}},emscripten_get_now(),static_cast<int>(staging.activeSlots),
    static_cast<int>(staging.occupiedSlots),
    static_cast<double>(staging.queueCompletionRegistrations),
    static_cast<double>(staging.queueCompletionCallbacks),
    staging.peakQueueCompletionCallbackMs,staging.lastQueueCallbackSourceFrame,force?1:0);
}
void diagnostic_incident(int reason,double value,double threshold,int clock_owner){
 diagnostic_staging_sample(true);
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
MeleeWebPipelineSourceContext pipeline_context(uint32_t phase_override,uint32_t scene_override){
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
PreparationProfile preparation_profile;
PADStatus diagnostic_pad{};
unsigned diagnostic_pad_port=0,diagnostic_pad_remaining=0;
melee_web::ResultsSourcePadSchedule scheduled_results_pad;
melee_web::ResultsSourceFramePauseSchedule scheduled_results_pauses;
std::array<float,1068> pcm;
alignas(32) unsigned char fifo[64*1024];
void begin_source_session(){
 if(source_session_owned)return;
 char error[256]{};
#if defined(MELEE_WEB_NET_SESSION)
 check(melee_web_net_session_begin(32U*1024U*1024U,error,sizeof(error)),error);
#else
 check(melee_web_gameplay_session_begin(32U*1024U*1024U,error,sizeof(error)),error);
#endif
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
 replay.reset();melee_web_net_reset();replay_completion={};replay_cursor=0;replay_trace=replay_pending=replay_started=replay_final_draw=false;
 replay_match_complete=false;replay_outcome=0;replay_winner=-1;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 stadium_c1a_armed=false;stadium_c1a_observation.clear();stadium_c1a_raw_stkind=-1;
#endif
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
                    const MeleeWebMenuMatchSelection* selection,
                    const MeleeWebOpeningPreview* opening_preview,
                    int opening_state){
 check(!world&&!match&&!host_entered&&!results&&!prize,
       "Close source asset owners before requesting a scope");
 std::vector<std::string> names;
 switch(destination){
 case AssetDestination::Match:
 case AssetDestination::Replay:
  check(selection!=nullptr,"This asset scope requires an original source selection");
  names=melee_web::match_asset_names(*selection);break;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 case AssetDestination::StadiumC1a:
  check(stadium_c1a_armed&&selection!=nullptr,
        "Stadium C1a preparation requires its armed source selection");
  names=melee_web::stadium_c1a_asset_names(*selection);break;
#endif
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
 if(melee_web_net_active()&&melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED)
  check(melee_web_net_apply_start_context(host,error,sizeof(error)),error);
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(stadium_c1a_armed){
   StartMeleeData raw_sss{};
   check(melee_web_menu_host_stadium_c1a_raw_selection(
       host,&raw_sss,error,sizeof(error)),error);
   check(raw_sss.rules.stkind==St_Kind_PStadium,
         "Stadium C1a raw SSS payload did not retain source StKind 3");
   stadium_c1a_raw_stkind=raw_sss.rules.stkind;
   MeleeWebMenuMatchSelection selection{};
   check(melee_web_menu_host_stadium_c1a_selection(
       host,&selection,error,sizeof(error)),error);
   check(selection.start.rules.stkind==St_Kind_PStadium,
         "Stadium C1a selection did not retain source StKind 3");
   if(scoped_assets){request_assets(AssetDestination::StadiumC1a,&selection);return;}
   throw std::runtime_error(
       "Stadium C1a requires the checked local-disc asset-scope boundary; Eject to recover.");
  }
#endif
  if(melee_web_menu_host_mode_kind(host)==GM_TRAINING){
   check(melee_web_menu_host_training_start_pending(host),
         "Original Training SSS reached its simulation state without a checked start handoff");
   throw std::runtime_error(
       "Original Training stage selection reached GM_TRAINING state 2. Its one-player simulation, Training HUD/options, item controls, and reset/CPU services are not integrated yet; Eject to recover.");
  }
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  if(replay&&(replay->version==melee_web::kRetailReplayVersion||
              replay->version==melee_web::kRetailReplayFighterVersion))
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
 const auto destination=asset_destination;
 asset_destination=AssetDestination::None;asset_committed=false;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(destination==AssetDestination::StadiumC1a){
  check(stadium_c1a_armed&&stadium_c1a_raw_stkind==St_Kind_PStadium&&
        asset_selection_valid&&host&&!host_entered,
        "Stadium C1a handoff lost its armed closed source owner");
  const auto names=melee_web::stadium_c1a_asset_names(asset_selection);
  check(names==requested_assets,
        "Stadium C1a imported scope differs from its source manifest");
  check(files.size()==names.size()&&asset_scope.active_file_count()==names.size(),
        "Stadium C1a import did not publish the complete exact manifest");
  for(const auto& name:names){
   const auto found=files.find(name);
   check(found!=files.end()&&!found->second.empty(),
         "Stadium C1a import is missing a manifest file");
  }
  stadium_c1a_observation="{\"checkpoint\":\"C1a\",\"raw_sss_stkind\":"+
      std::to_string(stadium_c1a_raw_stkind)+",\"prepared_stkind\":"+
      std::to_string(asset_selection.start.rules.stkind)+
      ",\"manifest_files\":[";
  for(size_t i=0;i<names.size();++i){
   if(i)stadium_c1a_observation+=",";
   stadium_c1a_observation+="\""+names[i]+"\"";
  }
  stadium_c1a_observation+=
      "],\"manifest_committed\":true,\"resolvedname\":null,"
      "\"runtime_source_file_service_request_observed\":false,"
      "\"match_constructed\":false,\"stage_constructed\":false,"
      "\"recoverable_stop\":\"before source archive request and match admission\"}";
  message="Pokémon Stadium C1a preparation complete. Source archive request and match/stage lifecycle remain deferred; unload to recover.";
  running=false;
  EM_ASM({window.menuStadiumC1aPrepared?.(UTF8ToString($0));},
         stadium_c1a_observation.c_str());
  return true;
 }
#endif
 archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
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
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 bool first_replay_callback_probe=false;
 unsigned replay_boundary_draw_index=0;
 const auto replay_boundary_mark=[&](const char* name,int a,int b,int c){
  if(!first_replay_callback_probe)return;
  EM_ASM({try{globalThis.__meleeReplayBoundaryProbe?.mark?.(
    UTF8ToString($0),$1,$2,$3);}catch(_){}},name,a,b,c);
 };
#endif
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
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  const unsigned boundary_draw_index=first_replay_callback_probe
      ? replay_boundary_draw_index++ : 0;
  replay_boundary_mark("aurora_begin_frame_begin",static_cast<int>(boundary_draw_index),
                       static_cast<int>(source_frames.steps()),static_cast<int>(replay_cursor));
#endif
  const bool frame_began=aurora_begin_frame();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("aurora_begin_frame_returned",static_cast<int>(boundary_draw_index),
                       static_cast<int>(source_frames.steps()),frame_began?1:0);
#endif
  if(frame_began){
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
     else if(world&&host_entered){
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
      replay_boundary_mark("css_host_draw_begin",static_cast<int>(boundary_draw_index),
                           static_cast<int>(replay_cursor),melee_web_menu_host_phase(host));
#endif
      drawn=melee_web_menu_host_draw(host,error,sizeof(error));
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
      replay_boundary_mark("css_host_draw_returned",static_cast<int>(boundary_draw_index),
                           drawn,static_cast<int>(source_frames.steps()));
#endif
      actual_source_draw=drawn!=0;drew_source=drawn!=0;
     }
    }
   }
   draw_done=emscripten_get_now();
   const bool completed_input_draw=match&&drew_source&&source_frames.pending();
   const bool tag_staging_byte_draw=aurora_browser_staging_byte_hash_enabled()&&
       completed_input_draw&&replay&&replay->version==4&&replay_cursor>0&&!replay_final_draw;
   const bool collect_staging_diagnostics=aurora_browser_staging_diagnostics_enabled();
   if(tag_staging_byte_draw){
    EM_ASM({window.__meleeWebStagingByteCapture?.setSourceDrawTag($0,$1);},
           static_cast<int>(match->source_frames()),static_cast<int>(replay_cursor-1));
   }
   if(collect_staging_diagnostics)
    aurora_browser_staging_set_diagnostic_source_frame(
      drew_source?static_cast<int32_t>(diagnostic_source_frame()):-1);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
   replay_boundary_mark("aurora_end_frame_begin",static_cast<int>(boundary_draw_index),
                        static_cast<int>(source_frames.steps()),static_cast<int>(replay_cursor));
#endif
   aurora_end_frame();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
   replay_boundary_mark("aurora_end_frame_returned",static_cast<int>(boundary_draw_index),
                        static_cast<int>(source_frames.draws()),drew_source?1:0);
#endif
   if(tag_staging_byte_draw)EM_ASM({window.__meleeWebStagingByteCapture?.clearSourceDrawTag();});
   if(collect_staging_diagnostics)aurora_browser_staging_set_diagnostic_source_frame(-1);
   end_done=emscripten_get_now();check(drawn,error);
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
  const bool local_capture_clock=melee_web_net_local_capture_pending();
  const bool simulation_clock_running=running&&(world||match||results||prize)&&input->visible;
  const auto observe_simulation_clock_stall=[](const auto& event) noexcept {
   diagnostic_clock_stall(event,1);
  };
  const auto elapsed=local_capture_clock?
      menu_clock.tick_with_budget(simulation_clock_now,simulation_clock_running,1,
                                  observe_simulation_clock_stall):
      menu_clock.tick(simulation_clock_now,simulation_clock_running,
                      observe_simulation_clock_stall);
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
   if(melee_web_net_active()){
    // Agreed network input replaces the sample at the replay seam. Without
    // the next frame this is a network wait: no tick and no clock debt.
    if(!melee_web_net_capture_local_input(input->samples,input->raw)){
     menu_clock.reset();break;
    }
    const PADStatus* agreed=melee_web_net_before_step(static_cast<uint32_t>(observed_replay_scene()));
    if(!agreed){menu_clock.reset();break;}
    sample=agreed;
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
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     first_replay_callback_probe=replay_trace&&replay->version==10&&
         EM_ASM_INT({return globalThis.__meleeReplayBoundaryProbe?.enabled?1:0;});
     if(first_replay_callback_probe)
      replay_boundary_mark("header_emit_begin",static_cast<int>(replay_cursor),
                           static_cast<int>(source_frames.steps()),10);
#endif
     if(replay_trace)melee_web::retail_replay_session_initial(*replay);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     replay_boundary_mark("header_emit_returned",static_cast<int>(replay_cursor),
                          static_cast<int>(source_frames.steps()),10);
     replay_boundary_mark("menu_replay_started_dispatch_begin",static_cast<int>(replay_cursor),
                          static_cast<int>(source_frames.steps()),static_cast<int>(replay->frames.size()));
#endif
     replay_started=true;
     EM_ASM({window.menuReplayStarted?.($0,!!$1,$2,$3);},replay->frames.size(),replay_trace,replay->expected_draws(),replay->scheduling_mode());
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     replay_boundary_mark("menu_replay_started_dispatch_returned",static_cast<int>(replay_cursor),
                          static_cast<int>(source_frames.steps()),static_cast<int>(replay->frames.size()));
#endif
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
   else{
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    const bool probe_css_tick=first_replay_callback_probe&&replay_whole&&
        observed_replay_scene()==melee_web::kRetailReplayCss;
    if(probe_css_tick)
     replay_boundary_mark("css_tick_begin",static_cast<int>(source_frames.steps()),
                          static_cast<int>(replay_cursor),melee_web::kRetailReplayCss);
#endif
    result=melee_web_menu_host_tick(host,sample,error,sizeof(error));
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    if(probe_css_tick)
     replay_boundary_mark("css_tick_returned",static_cast<int>(source_frames.steps()),
                          static_cast<int>(replay_cursor),result);
#endif
    check(result==1||result==3,error);source_frames.did_step();
   }
   if(melee_web_net_active())melee_web_net_after_step();
   if(replay_whole){
    // One continuous timeline: the cursor advances once per simulation step,
    // whichever owner consumed that step, and the owner must be the scene the
    // recipe declared for this frame.
    const int expected=expected_replay_scene(*replay,replay_cursor);
    const int observed=observed_replay_scene();
    if(observed!=expected)
     throw std::runtime_error(replay_scene_mismatch("post-consumption",replay_cursor,
                                                    expected,observed));
    if(replay_trace){
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     const bool frame_zero_probe=first_replay_callback_probe&&replay_cursor==0;
     replay_boundary_mark("session_frame_emit_begin",static_cast<int>(replay_cursor),observed,
                          static_cast<int>(source_frames.steps()));
     if(frame_zero_probe)
      replay_boundary_mark("frame0_emit_begin",static_cast<int>(replay_cursor),observed,
                           static_cast<int>(source_frames.steps()));
#endif
     melee_web::retail_replay_frame(*replay,replay_cursor,observed);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     if(frame_zero_probe)
      replay_boundary_mark("frame0_emit_returned",static_cast<int>(replay_cursor),observed,
                           static_cast<int>(source_frames.steps()));
     replay_boundary_mark("session_frame_emit_returned",static_cast<int>(replay_cursor),observed,
                          static_cast<int>(source_frames.steps()));
#endif
    }
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
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("ordinary_audio_boundary_begin",static_cast<int>(audio_elapsed.steps),
                       audio_elapsed.stalled?1:0,audio_before_construction?1:0);
#endif
  if(!audio_before_construction&&!audio_elapsed.stalled)
   for(unsigned step=0;step<audio_elapsed.steps;step++){
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    replay_boundary_mark("ordinary_audio_tick_begin",static_cast<int>(step),
                         static_cast<int>(audio_elapsed.steps),static_cast<int>(replay_cursor));
#endif
    render_audio_tick(audio_owner,error,sizeof(error));
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    replay_boundary_mark("ordinary_audio_tick_returned",static_cast<int>(step),
                         static_cast<int>(audio_elapsed.steps),static_cast<int>(replay_cursor));
#endif
   }
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("ordinary_audio_boundary_returned",static_cast<int>(audio_elapsed.steps),
                       audio_elapsed.stalled?1:0,audio_before_construction?1:0);
#endif
  simulation_done=emscripten_get_now();
  simulation_cpu_ms=std::max(0.0,simulation_done-input_done-preparation_ms-render_total_ms);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("source_frames_finish_begin",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
#endif
  source_frames.finish(present_source);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  if(local_capture_clock){
   const double due_steps=elapsed.stalled?elapsed.triggering_value:
       static_cast<double>(elapsed.steps+elapsed.pending_steps);
   EM_ASM({try{globalThis.menuDiagnosticInputClockSample?.({
     callback_ms:$0,interval_ms:$1,due_steps:$2,budget_steps:1,
     returned_steps:$3,unconsumed_whole_steps:$4,pending_ticks_after:$5,
     completed_source_steps:$6,source_draws:$7,stalled:!!$8});}catch(_){}},
     simulation_clock_now,elapsed.interval_ms,due_steps,elapsed.steps,
     elapsed.pending_steps,menu_clock.pending_ticks(),source_frames.steps(),
     source_frames.draws(),elapsed.stalled?1:0);
  }
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("source_frames_finish_returned",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
#endif
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
 diagnostic_staging_sample();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 publish_runtime_timing({
  .started=started,
  .timing_valid=timing_valid,
  .first_use=first_use,
  .input_done=input_done,
  .simulation_cpu_ms=simulation_cpu_ms,
  .preparation_ms=preparation_ms,
  .callback_begin_stats=callback_begin_stats,
  .render_begin_ms=render_begin_ms,
  .render_draw_ms=render_draw_ms,
  .render_end_ms=render_end_ms,
  .finished=finished,
  .callback_end_stats=callback_end_stats,
  .began=began,
  .drawn=drawn,
  .stats_before=stats_before,
  .stats_after=stats_after,
  .callback_draw_calls=callback_draw_calls,
  .callback_texture_upload=callback_texture_upload,
  .callback_staging_used=callback_staging_used,
  .suppress_draw=suppress_draw,
  .source_frames=source_frames,
 });
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
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 if(first_replay_callback_probe){
  replay_boundary_mark("first_replay_callback_tail_begin",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
  replay_boundary_mark("native_pause_begin",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
  melee_web_native_menu_pause(1);
  replay_boundary_mark("native_pause_returned",running?1:0,static_cast<int>(source_frames.steps()),
                       static_cast<int>(replay_cursor));
  replay_boundary_mark("first_replay_callback_tail_returned",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),running?1:0);
 }
#endif
}
}
using namespace melee_web_menu_browser;
extern "C" {
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){message=e.what();return 0;}}
void melee_web_native_menu_pause(int paused){
 if(faulted||replay_final_draw||preparation.busy()||pending||(!host_entered&&!match&&!results&&!prize))return;
 diagnostic_incident(paused?5:6);
 running=(world||match||results||prize)&&!paused;menu_clock.reset();
 message=running?(match?match_message:results?"Original Results":prize?"Original unlock notification":source_menu_message()):"Paused.";
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
