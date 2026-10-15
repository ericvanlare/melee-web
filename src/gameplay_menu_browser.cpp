#include "gameplay_menu_browser_state.hpp"
#include <emscripten/em_macros.h>
extern "C" uint32_t gm_801A4BA8(void);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
// Retain the private postset that captures actual Emscripten instance object
// identities for the short recreation canary. Ordinary/public profiles omit it.
EM_JS_DEPS(stadium_recreation_identity_v1, "$MeleeWebStadiumRecreationIdentityV1");
#endif
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern "C" int gm_GetCurrentGameMode(void);
extern "C" int gm_GetPreviousGameMode(void);
extern "C" int gm_GetCurrentSceneIndex(void);
extern "C" int gm_GetPreviousSceneIndex(void);
#include "gameplay_menu.h"
#include "stadium_first_css_diagnostic_input.hpp"
#include "stadium_first_css_diagnostic_snapshot_json.hpp"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_vs_sis.h"
#include "gameplay_stadium_display_owner.h"
#include <sstream>
#include <exception>
extern "C" void HSD_SisLib_C1TextProbeSet(int enabled);
extern "C" void* melee_web_current_scene_info(void);
#endif
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
// Startup observations only. These do not service the renderer or change its
// cache gate; samples stop at the existing getter's first terminal idle state.
double graphics_startup_started_ms=-1;
double graphics_startup_renderer_init_ms=-1;
double graphics_startup_first_callback_ms=-1;
bool graphics_startup_terminal=false;
void publish_graphics_startup(int cache_idle,bool read_stats) noexcept {
 if(graphics_startup_started_ms<0||graphics_startup_terminal)return;
 const AuroraStats* stats=read_stats?aurora_get_stats():nullptr;
 const uint32_t queued=stats?stats->queuedPipelines:0;
 // Aurora increments this on valid pipeline readiness and resets at shutdown,
 // rather than at begin_pipeline_frame. It is not a GPU-completion count.
 const uint32_t created=stats?stats->createdPipelines:0;
 const double elapsed=emscripten_get_now()-graphics_startup_started_ms;
 graphics_startup_terminal=cache_idle==1||cache_idle==-1;
 EM_ASM({
  try {
   const finite=value=>Number.isFinite(value)&&value>=0?value:null;
   Module.graphicsStartup=({version:1,renderer_init_ms:finite($0),
    queued_pipelines:$1?($2>>>0):null,created_pipelines:$1?($3>>>0):null,
    cache_idle:$4,first_native_callback_ms:finite($5),elapsed_ms:finite($6),terminal:!!$7});
  } catch (_) { /* Optional observation must not interrupt native startup. */ }
 },graphics_startup_renderer_init_ms,stats?1:0,queued,created,cache_idle,
   graphics_startup_first_callback_ms,elapsed,graphics_startup_terminal?1:0);
}
#if defined(MELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC)
// The module never resumes its normal loop after arming, including on failure.
bool match_draw_discard_terminal=false;
// Set only after successful original close/input/Aurora/cancel-loop retirement.
bool match_draw_discard_native_retired=false;
unsigned match_draw_discard_frame=0;
std::string match_draw_discard_receipt;
#endif
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
struct FirstCssDrawSample {
 std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> pad{};
 uint32_t seed=0,scene_frame=0;
 uint64_t world_generation=0;
 int source_scene=0,menu_phase=0,scene_kind=0;
 unsigned current_mode=0,previous_mode=0,current_scene_index=0,previous_scene_index=0;
 bool seed_owner_stable=false,scene_owner_stable=false,world_generation_stable=false;
};
struct FirstCssBrowserDrawState {
 bool armed=false,kicked=false,entry_captured=false,tick_captured=false;
 bool draw_captured=false,frame_end_returned=false,complete=false,failed=false;
 std::string error;
 melee_web::stadium_first_css_diagnostic::ContextInput context{};
 melee_web::stadium_first_css_diagnostic::ConsumedPadInput consumed{};
 PADStatus consumed_status[4]{};
 MeleeWebMenuCssReturnSnapshot entry{};
 FirstCssDrawSample tick{},draw{};
 const uint32_t* seed_owner=nullptr;
 const GameModeState::GameSceneInfo* scene_owner=nullptr;
 uint64_t world_generation=0;
 uint32_t frame_before=0,frame_after_tick=0;
 int tick_result=0;
 unsigned source_callbacks_before_kick=0,host_tick_calls=0,host_draw_calls=0;
 unsigned source_steps=0,source_draws=0,aurora_begin_calls=0,aurora_end_calls=0;
 unsigned draw_source_tick=0,draw_ordinal=0;
 unsigned preparation_source_callbacks=0;
 std::string observation;
};
struct FirstCssBrowserPostdrawStreamState {
 bool armed=false,kicked=false,terminal=false,failed=false,complete=false;
 bool tick_captured=false,tick_approved=false,draw_captured=false,draw_approved=false;
 bool transition_captured=false,frame_end_returned=false;
 std::string error,outcome;
 melee_web::stadium_first_css_diagnostic::PostdrawInput input{};
 PADStatus input_status[4]{};
 uint32_t next_input_index=0,current_input_index=0;
 unsigned consumed_inputs=0,host_tick_calls=0,executed_host_ticks=0;
 unsigned matched_ticks=0,matched_draws=0,host_draw_calls=0;
 unsigned aurora_begin_calls=0,aurora_end_calls=0;
 int tick_result=0;
 FirstCssDrawSample tick{},draw{},transition{};
};
FirstCssBrowserDrawState first_css_browser_draw;
FirstCssBrowserPostdrawStreamState first_css_browser_postdraw_stream;
struct FirstCssFinalPendingDrawState {
 bool armed=false,kicked=false,attempted=false,captured=false,compared=false;
 bool approved=false,failed=false,complete=false,frame_end_returned=false;
 std::string error;
 FirstCssDrawSample actual{};
 unsigned host_draw_calls=0,aurora_begin_calls=0,aurora_end_calls=0;
};
FirstCssFinalPendingDrawState first_css_final_pending_draw;
struct FirstSssConstructorPairState {
 bool armed=false,kicked=false,attempted=false,captured=false,compared=false;
 bool complete=false,failed=false,frame_terminal=false;
 bool callback_attempted=false,audio_before_rebuild_captured=false;
 std::string error,observation,actual_json;
 uint64_t audio_render_calls_before_rebuild=0;
 uint64_t audio_render_frames_before_rebuild=0;
 uint64_t audio_render_calls_after_sss_enter=0;
 uint64_t audio_render_frames_after_sss_enter=0;
 MeleeWebMenuFirstSssPairObservation native{};
};
FirstSssConstructorPairState first_sss_constructor_pair;
struct FirstSssConsumedTickState {
 bool armed=false,kicked=false,attempted=false,captured=false,compared=false;
 bool complete=false,failed=false;
 std::string error;
 melee_web::stadium_first_css_diagnostic::FirstSssTickInput input{};
 PADStatus raw[4]{};
 MeleeWebMenuFirstSssTickObservation native{};
};
FirstSssConsumedTickState first_sss_consumed_tick;

extern bool running;
extern bool first_use_draw_pending;
extern melee_web::FixedTickClock menu_clock;
extern std::string message;
extern uint64_t diagnostic_audio_render_calls,diagnostic_audio_render_frames;
std::string first_css_browser_draw_json_quote(const std::string& value){
 std::string result="\"";
 for(unsigned char byte:value){
  if(byte=='\"'||byte=='\\'){result.push_back('\\');result.push_back(static_cast<char>(byte));}
  else if(byte=='\n')result+="\\n";
  else if(byte=='\r')result+="\\r";
  else if(byte=='\t')result+="\\t";
  else if(byte<0x20){
   static constexpr char digits[]="0123456789abcdef";
   result+="\\u00";result.push_back(digits[byte>>4]);result.push_back(digits[byte&0x0f]);
  }else result.push_back(static_cast<char>(byte));
 }
 result.push_back('\"');return result;
}
void first_css_browser_draw_refresh_observation();

void first_sss_tick_sync_native(){
 auto& state=first_sss_consumed_tick;
 if(!state.armed||!host)return;
 char error[256]{};
 if(!melee_web_menu_host_first_sss_tick(host,&state.native,error,sizeof(error))){
  state.failed=true;if(state.error.empty())state.error=error;
 }else if(state.native.state==3){
  state.failed=true;if(state.error.empty())state.error=state.native.error;
 }
 state.captured=state.native.scheduler_end.captured;
}
void first_sss_tick_browser_fail(const std::string& error){
 auto& state=first_sss_consumed_tick;
 state.failed=true;if(state.error.empty())state.error=error;
 running=false;pending=false;menu_clock.reset();first_use_draw_pending=false;
 message=state.error;
}
std::string first_sss_tick_sample_json(){
 const auto& note=first_sss_consumed_tick.native.scheduler_end;
 uint8_t consumed[44]{};
 for(unsigned i=0;i<4;++i){
  const auto& p=note.consumed_pad[i];auto* b=consumed+i*11;
  b[0]=p.button>>8;b[1]=p.button;b[2]=p.stickX;b[3]=p.stickY;
  b[4]=p.substickX;b[5]=p.substickY;b[6]=p.triggerLeft;b[7]=p.triggerRight;
  b[8]=p.analogA;b[9]=p.analogB;b[10]=p.err;
 }
 std::ostringstream out;
 out<<"{\"phase\":\"scheduler_end\",\"source_scene\":"<<note.source_scene
    <<",\"scene_kind\":"<<note.scene_kind<<",\"scene_frame\":"<<note.scene_frame
    <<",\"random_seed_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex32(note.random_seed)
    <<"\",\"pad_state_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex(note.pad_state,sizeof(note.pad_state))
    <<"\",\"consumed_pad_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex(consumed,sizeof consumed)
    <<"\",\"scene_routing_getters\":{\"current_game_mode\":"<<note.scene_routing_getters[0]
    <<",\"previous_game_mode\":"<<note.scene_routing_getters[1]
    <<",\"current_scene_index\":"<<note.scene_routing_getters[2]
    <<",\"previous_scene_index\":"<<note.scene_routing_getters[3]<<"},\"owners\":{";
 static const char* names[]={"host","session","world","audio","vs_mode","scene_info","payload","seed"};
 for(unsigned i=0;i<8;++i){if(i)out<<',';out<<'"'<<names[i]<<"\":"<<(note.owners[i]?"true":"false");}
 out<<"},\"world_generation\":"<<note.world_generation<<",\"audio_generation\":"<<note.audio_generation<<'}';
 return out.str();
}
std::string first_sss_tick_observation_json(){
 const auto& state=first_sss_consumed_tick;const auto& n=state.native;
 std::ostringstream out;
 out<<"{\"armed\":"<<(state.armed?"true":"false")<<",\"kicked\":"<<(state.kicked?"true":"false")
    <<",\"attempted\":"<<(state.attempted?"true":"false")<<",\"captured\":"<<(state.captured?"true":"false")
    <<",\"compared\":"<<(state.compared?"true":"false")<<",\"complete\":"<<(state.complete?"true":"false")
    <<",\"failed\":"<<(state.failed?"true":"false")<<",\"error\":"<<(state.error.empty()?"null":first_css_browser_draw_json_quote(state.error))
    <<",\"host_tick_calls\":"<<n.host_tick_calls<<",\"host_draw_calls\":"<<n.host_draw_calls
    <<",\"tick_result\":"<<n.tick_result<<",\"clock_post_succeeded\":"<<(n.clock_post_succeeded?"true":"false")
    <<",\"transition_requested\":"<<(n.transition_requested?"true":"false")<<",\"post_host_frame\":"<<n.post_host_frame
    <<",\"post_host_frame_captured\":"<<(n.post_host_frame_captured?"true":"false")
    <<",\"scheduler_end\":"<<(state.captured?first_sss_tick_sample_json():"null")<<'}';
 return out.str();
}
void first_sss_pair_fail(const std::string& error){
 auto& state=first_sss_constructor_pair;
 if(!state.failed){state.failed=true;state.error=error;}
 state.frame_terminal=true;running=false;pending=false;menu_clock.reset();
 first_use_draw_pending=false;
 message=state.error;
}
void first_sss_pair_sync_native(){
 auto& state=first_sss_constructor_pair;
 if(!state.armed||!host)return;
 MeleeWebMenuFirstSssPairObservation observed{};
 char error[256]{};
 if(melee_web_menu_host_first_sss_pair(host,&observed,error,sizeof(error))){
  state.native=observed;
  if(observed.state==3){
   state.failed=true;state.frame_terminal=true;
   if(state.error.empty())state.error=observed.error[0]?observed.error:
       "Native SSS constructor pair failed its owner checks";
  }
 }else if(state.error.empty()){
  state.failed=true;state.frame_terminal=true;state.error=error;
 }
}
void first_sss_pair_write_note(std::ostream& out,
    const MeleeWebMenuFirstSssPairNoteSnapshot& note){
 out<<"{\"phase\":\""<<(note.phase==1?"sss_entry":note.phase==MELEE_WEB_MENU_SSS_PREFIX_EXIT?"sss_exit":"sss_return")
    <<"\",\"host_entered\":"<<(note.host_entered?"true":"false")
    <<",\"session_phase\":"<<note.session_phase
    <<",\"source_scene\":"<<note.source_scene
    <<",\"scene_kind\":"<<note.scene_kind
    <<",\"scene_frame\":"<<note.scene_frame
    <<",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(note.random_seed)
    <<"\",\"pad_state_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex(note.pad_state,sizeof(note.pad_state))
    <<"\",\"scene_routing_getters\":{\"current_game_mode\":"
    <<note.scene_routing_getters[0]
    <<",\"previous_game_mode\":"<<note.scene_routing_getters[1]
    <<",\"current_scene_index\":"<<note.scene_routing_getters[2]
    <<",\"previous_scene_index\":"<<note.scene_routing_getters[3]
    <<"},\"owners\":{\"host\":"<<(note.owners[0]?"true":"false")
    <<",\"session\":"<<(note.owners[1]?"true":"false")
    <<",\"world\":"<<(note.owners[2]?"true":"false")
    <<",\"audio\":"<<(note.owners[3]?"true":"false")
    <<",\"vs_mode\":"<<(note.owners[4]?"true":"false")
    <<",\"scene_info\":"<<(note.owners[5]?"true":"false")
    <<",\"payload\":"<<(note.owners[6]?"true":"false")
    <<",\"seed\":"<<(note.owners[7]?"true":"false")
    <<"},\"world_generation\":"<<note.world_generation
    <<",\"audio_generation\":"<<note.audio_generation
    <<",\"session_ticks\":"<<note.session_ticks
    <<",\"sss\":{\"header\":{\"unk_stage\":"
    <<unsigned(note.sss.unk_stage)<<",\"x1\":"<<unsigned(note.sss.x1)
    <<",\"no_lras\":"<<unsigned(note.sss.no_lras)
    <<",\"force_stage_id\":"<<int(note.sss.force_stage_id)
    <<",\"start_game\":"<<unsigned(note.sss.start_game)
    <<"},\"vs\":";
 melee_web::stadium_first_css_diagnostic::write_vs_mode(out,note.sss.vs);
 out<<"}}";
}
extern unsigned audio_phase;
std::string first_sss_frozen_pair_json;
struct FirstSssSequenceBrowserState {
 bool armed=false,kicked=false,attempted=false,complete=false,failed=false;
 bool frame_end_returned=false;
 unsigned aurora_begin_calls=0,aurora_end_calls=0,current_index=0;
 uint64_t audio_render_calls_before=0,audio_render_frames_before=0;
 uint64_t audio_render_calls_after=0,audio_render_frames_after=0;
 unsigned audio_phase_before=0,audio_phase_after=0;
 std::string error,prepared_output_unpaired;
 MeleeWebMenuSssSequenceObservation native{};
 melee_web::stadium_first_css_diagnostic::FirstSssPrefixInput input{};
};
FirstSssSequenceBrowserState first_sss_draw_sequence,first_sss_prefix_sequence;
struct FirstSssVsEntryState {
 bool attempted=false,captured=false,failed=false,complete=false;
 unsigned getter_calls=0;
 uint64_t audio_calls_before=0,audio_calls_after=0,audio_frames_before=0,audio_frames_after=0;
 unsigned audio_phase_before=0,audio_phase_after=0;
 std::string actual,error;
};
FirstSssVsEntryState first_sss_vs_entry;
std::string first_sss_vs_entry_json(){
 const auto& s=first_sss_vs_entry;std::ostringstream out;
 out<<"{\"attempted\":"<<(s.attempted?"true":"false")
    <<",\"captured\":"<<(s.captured?"true":"false")
    <<",\"failed\":"<<(s.failed?"true":"false")
    <<",\"complete\":"<<(s.complete?"true":"false")
    <<",\"getter_calls\":"<<s.getter_calls
    <<",\"audio_render_calls_before\":"<<s.audio_calls_before
    <<",\"audio_render_calls_after\":"<<s.audio_calls_after
    <<",\"audio_render_frames_before\":"<<s.audio_frames_before
    <<",\"audio_render_frames_after\":"<<s.audio_frames_after
    <<",\"audio_phase_before\":"<<s.audio_phase_before
    <<",\"audio_phase_after\":"<<s.audio_phase_after
    <<",\"error\":"<<(s.error.empty()?"null":first_css_browser_draw_json_quote(s.error))
    <<",\"actual\":"<<(s.actual.empty()?"null":s.actual)<<'}';return out.str();
}
struct FirstSssVsSetupState {
 bool attempted=false,requested=false,import_committed=false,captured=false,complete=false,failed=false;
 bool construction_complete=false,menu_retired=false;
 unsigned requested_generation=0,committed_generation=0,construction_steps=0,source_frames=0;
 unsigned host_tick_calls=0,host_draw_calls=0,getter_calls=0;
 uint64_t prior_world_generation=0,match_world_generation=0;
 uint64_t audio_calls_before=0,audio_calls_after=0,audio_frames_before=0,audio_frames_after=0;
 const MeleeWebPadState* input=nullptr;
 const MeleeWebPadState* host_input=nullptr;
 std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> owned_input{nullptr,melee_web_pad_state_free};
 std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> input_bytes{};
 const uint32_t* parent_seed_owner=nullptr;
 uint32_t parent_seed=0,parent_seed_after_retirement=0;
 bool parent_seed_restored=false;
 MeleeWebStadiumSetupSnapshot setup_snapshot{};
 std::string error,actual,history,stadium_setup_state;
};
FirstSssVsSetupState first_sss_vs_setup;
struct MatchedSetupCfState {
 bool opted_in=false,armed=false,queued=false,awaiting_approval=false;
 bool failed=false,complete=false,last_action_complete=false;
 bool explicit_step_mode=false,explicit_step_inflight=false;
 bool version2=false,full_schedule=false,common_fields=false;
 uint32_t cursor=0,last_schedule_index=0,last_ordinal=0,last_bundle=UINT32_MAX;
 uint32_t action_count=171,c_action_index=168,f_action_index=170,c_source_tick=84,f_source_tick=85;
 uint32_t source_ticks=0,source_draws=0,last_ticks_after=0,last_draws_after=0;
 uint32_t queued_ticks_before=0,queued_draws_before=0;
 uint64_t queued_source_frame_before=0,last_source_frame_after=0;
 int last_action_kind=0;
 struct ManifestAction {
  uint8_t kind=0;
  uint32_t ordinal=0,bundle=UINT32_MAX,draws_before=0;
  std::array<uint8_t,44> pad{};
 };
 std::vector<ManifestAction> version2_actions;
 std::array<uint8_t,44> queued_statuses{};
 const melee_web::GameplayMatchSession* owner=nullptr;
 const melee_web::RuntimeArchiveCache* cache_owner=nullptr;
 const MeleeWebMenuHost* host_owner=nullptr;
 uint32_t seed_at_queue=0,seed_after_action=0;
 uint64_t world_generation=0;
 uint32_t asset_generation=0,expected_branch=0;
 uint64_t audio_calls_before=0,audio_frames_before=0;
 uint64_t audio_calls_after=0,audio_frames_after=0;
 unsigned audio_phase_before=0,audio_phase_after=0;
 unsigned aurora_begin_calls=0,aurora_end_calls=0;
 MeleeWebStadiumIdleSnapshot idle{};
 MeleeWebStadiumGoAlignmentSnapshot go{};
 std::string error,last_action_json;
};
MatchedSetupCfState matched_setup_cf;
struct MatchedSetupCfRetirement {
 bool native_retired=false,terminal=false;
 int phase_after_close=-1,running_after_close=-1;
 int pipeline_cache_before=-1,pipeline_cache_after=-1;
 std::string receipt,error;
};
MatchedSetupCfRetirement matched_setup_cf_retirement;
std::string matched_setup_cf_json();
std::string stadium_recreation_b_json();
void stadium_recreation_b_before_retire();
void stadium_recreation_b_require_arm(bool version2,bool full_schedule,bool common_fields,unsigned action_count);
void stadium_recreation_b_after_close();
bool matched_setup_cf_service();
struct FirstSssTextTailProbeState {
 bool opted_in=false,probe_enabled=false;
 bool sis_capture_attempted=false,sis_lease_captured=false;
 bool membership_snapshot_attempted=false,membership_snapshot_started=false;
 bool membership_snapshot_active=false,membership_snapshot_ended=false;
 bool cleanup_done=false,unload_refused=false;
 uint64_t world_generation=0,allocation_generation=0,source_epoch=0;
 size_t requested_bytes=0;
 std::string error,unload_error,cleanup_reason;
};
FirstSssTextTailProbeState first_sss_text_tail_probe;
std::string first_sss_text_tail_retained_json;
std::string first_sss_vs_setup_current_json();
std::string first_sss_text_tail_probe_json(){
 const auto& p=first_sss_text_tail_probe;std::ostringstream out;
 out<<"{\"opted_in\":"<<(p.opted_in?"true":"false")
    <<",\"probe_enabled\":"<<(p.probe_enabled?"true":"false")
    <<",\"sis_capture_attempted\":"<<(p.sis_capture_attempted?"true":"false")
    <<",\"sis_lease_captured\":"<<(p.sis_lease_captured?"true":"false")
    <<",\"membership_snapshot_attempted\":"<<(p.membership_snapshot_attempted?"true":"false")
    <<",\"membership_snapshot_started\":"<<(p.membership_snapshot_started?"true":"false")
    <<",\"membership_snapshot_active\":"<<(p.membership_snapshot_active?"true":"false")
    <<",\"membership_snapshot_ended\":"<<(p.membership_snapshot_ended?"true":"false")
    <<",\"cleanup_done\":"<<(p.cleanup_done?"true":"false")
    <<",\"cleanup_reason\":"<<(p.cleanup_reason.empty()?"null":first_css_browser_draw_json_quote(p.cleanup_reason))
    <<",\"unload_refused\":"<<(p.unload_refused?"true":"false")
    <<",\"sis_lease\":{\"world_generation\":"<<p.world_generation
    <<",\"allocation_generation\":"<<p.allocation_generation
    <<",\"source_epoch\":"<<p.source_epoch
    <<",\"requested_bytes\":"<<p.requested_bytes
    <<",\"retirement_verified\":false}"
    <<",\"error\":"<<(p.error.empty()?"null":first_css_browser_draw_json_quote(p.error))
    <<",\"unload_error\":"<<(p.unload_error.empty()?"null":first_css_browser_draw_json_quote(p.unload_error))<<'}';
 return out.str();
}
void first_sss_text_tail_retain(){
 if(!first_sss_text_tail_probe.opted_in){first_sss_text_tail_retained_json.clear();return;}
 first_sss_text_tail_retained_json=
   "{\"schema\":\"melee-web-stadium-first-sss-text-tail-retained-v1\",\"first_sss_vs_setup_text_tail\":"+
   first_sss_text_tail_probe_json()+"}";
}
void first_sss_text_tail_cleanup(const char* reason){
 auto& p=first_sss_text_tail_probe;
 if(!p.opted_in||p.cleanup_done)return;
 if(p.membership_snapshot_active){
  melee_web_stadium_text_membership_snapshot_end();
  p.membership_snapshot_active=false;p.membership_snapshot_ended=true;
 }
 if(p.probe_enabled){HSD_SisLib_C1TextProbeSet(0);p.probe_enabled=false;}
 p.cleanup_done=true;
 if(p.cleanup_reason.empty())p.cleanup_reason=reason?reason:"unspecified";
}
void first_sss_text_tail_begin_construction(){
 auto& p=first_sss_text_tail_probe;
 if(!p.opted_in||p.probe_enabled||p.cleanup_done)return;
 HSD_SisLib_C1TextProbeSet(1);p.probe_enabled=true;
}
void first_sss_text_tail_capture_membership(){
 auto& p=first_sss_text_tail_probe;
 if(!p.opted_in||!p.probe_enabled||p.sis_capture_attempted)return;
 p.sis_capture_attempted=true;
 MeleeWebRetiredSisLease lease{};char error[256]{};
 if(!melee_web_diagnostic_sis_capture(&lease,error,sizeof(error))){
  if(p.error.empty())p.error=error[0]?error:"Exact SIS diagnostic lease capture refused";
  return;
 }
 p.sis_lease_captured=true;
 p.world_generation=lease.prior.world_generation;
 p.allocation_generation=lease.prior.allocation_generation;
 p.source_epoch=lease.prior.source_epoch;p.requested_bytes=lease.requested_bytes;
 p.membership_snapshot_attempted=true;
 if(!melee_web_stadium_text_membership_snapshot_begin(&lease.prior,error,sizeof(error))){
  if(p.error.empty())p.error=error[0]?error:"Exact SIS text membership snapshot refused";
  return;
 }
 p.membership_snapshot_started=true;p.membership_snapshot_active=true;
}
void first_sss_text_tail_close_returned(){
 if(!first_sss_text_tail_probe.opted_in)return;
 first_sss_text_tail_cleanup("native-close-returned");
 first_sss_text_tail_retain();
}
void first_sss_text_tail_setup_failed(){
 if(!first_sss_text_tail_probe.opted_in)return;
 first_sss_text_tail_cleanup("setup-failure");
 first_sss_text_tail_retain();
}
void first_sss_text_tail_unload_refused(const char* error){
 auto& p=first_sss_text_tail_probe;
 if(!p.opted_in)return;
 p.unload_refused=true;
 if(p.unload_error.empty())p.unload_error=error?error:"Native unload refused";
 first_sss_text_tail_cleanup("native-unload-refused");
 first_sss_text_tail_retain();
}
struct FirstSssTextTailCloseGuard {
 bool active=false;
 int uncaught_on_entry=std::uncaught_exceptions();
 ~FirstSssTextTailCloseGuard() noexcept {
  if(!active)return;
  try{
   if(std::uncaught_exceptions()>uncaught_on_entry){
    first_sss_text_tail_cleanup("native-close-threw");
    first_sss_text_tail_retain();
   }else first_sss_text_tail_close_returned();
  }
  catch(...){
   auto& p=first_sss_text_tail_probe;
   if(p.membership_snapshot_active){
    melee_web_stadium_text_membership_snapshot_end();
    p.membership_snapshot_active=false;p.membership_snapshot_ended=true;
   }
   if(p.probe_enabled){HSD_SisLib_C1TextProbeSet(0);p.probe_enabled=false;}
   p.cleanup_done=true;
  }
 }
};
std::string first_sss_vs_setup_json(){
 const auto& s=first_sss_vs_setup;std::ostringstream out;
 out<<"{\"attempted\":"<<(s.attempted?"true":"false")
    <<",\"captured\":"<<(s.captured?"true":"false")
    <<",\"complete\":"<<(s.complete?"true":"false")
    <<",\"failed\":"<<(s.failed?"true":"false")
    <<",\"requested\":"<<(s.requested?"true":"false")
    <<",\"import_committed\":"<<(s.import_committed?"true":"false")
    <<",\"getter_calls\":"<<s.getter_calls
    <<",\"menu_retired\":"<<(s.menu_retired?"true":"false")
    <<",\"parent_seed_before_retirement\":"<<s.parent_seed
    <<",\"parent_seed_after_retirement\":"<<s.parent_seed_after_retirement
    <<",\"parent_seed_owner_value_restored\":"<<(s.parent_seed_restored?"true":"false")
    <<",\"requested_generation\":"<<s.requested_generation
    <<",\"committed_generation\":"<<s.committed_generation
    <<",\"construction_steps\":"<<s.construction_steps
    <<",\"construction_complete\":"<<(s.construction_complete?"true":"false")
    <<",\"source_frames\":"<<s.source_frames
    <<",\"host_tick_calls\":"<<s.host_tick_calls<<",\"host_draw_calls\":"<<s.host_draw_calls
    <<",\"prior_world_generation\":"<<s.prior_world_generation
    <<",\"match_world_generation\":"<<s.match_world_generation
    <<",\"audio_render_calls_before\":"<<s.audio_calls_before
    <<",\"audio_render_calls_after\":"<<s.audio_calls_after
    <<",\"audio_render_frames_before\":"<<s.audio_frames_before
    <<",\"audio_render_frames_after\":"<<s.audio_frames_after
    <<",\"prior_observations\":\"historical_at_setup_begin_before_menu_retirement\""
    <<",\"operation_counters_scope\":\"setup_service_only_no_tick_draw_calls\""
    <<",\"source_frames_scope\":\"actual_match_flow_clock_not_original_scene_frame\""
    <<",\"current_owners\":"<<first_sss_vs_setup_current_json()
    <<",\"actual\":"<<(s.actual.empty()?"null":s.actual)
    <<",\"stadium_setup_state\":"<<(s.stadium_setup_state.empty()?"null":s.stadium_setup_state)
    <<",\"error\":"<<(s.error.empty()?"null":first_css_browser_draw_json_quote(s.error));
 if(matched_setup_cf.opted_in||matched_setup_cf.armed||matched_setup_cf.failed||matched_setup_cf.complete)
  out<<",\"matched_setup_cf\":"<<matched_setup_cf_json();
 if(first_sss_text_tail_probe.opted_in)
  out<<",\"text_tail_probe\":"<<first_sss_text_tail_probe_json();
 out<<'}';
 return out.str();
}
void first_sss_sequence_sync(bool prefix){
 auto& s=prefix?first_sss_prefix_sequence:first_sss_draw_sequence;
 if(!s.armed||!host)return;
 char error[256]{};
 if(!melee_web_menu_host_sss_sequence(host,prefix,&s.native,error,sizeof(error))){
  s.failed=true;if(s.error.empty())s.error=error;
 }else if(s.native.state==3){s.failed=true;if(s.error.empty())s.error=s.native.error;}
}
void first_sss_sequence_fail(bool prefix,const std::string& error){
 auto& s=prefix?first_sss_prefix_sequence:first_sss_draw_sequence;
 s.failed=true;if(s.error.empty())s.error=error;
 running=false;pending=false;menu_clock.reset();first_use_draw_pending=false;
 message=s.error;
}
std::string first_sss_sequence_sample_json(const MeleeWebMenuFirstSssTickSnapshot& note,
 const char* phase,bool prefix){
 uint8_t consumed[44]{};
 for(unsigned i=0;i<4;++i){const auto& p=note.consumed_pad[i];auto* b=consumed+i*11;
  b[0]=p.button>>8;b[1]=p.button;b[2]=p.stickX;b[3]=p.stickY;b[4]=p.substickX;
  b[5]=p.substickY;b[6]=p.triggerLeft;b[7]=p.triggerRight;b[8]=p.analogA;b[9]=p.analogB;b[10]=p.err;}
 std::ostringstream out;
 out<<"{\"phase\":"<<first_css_browser_draw_json_quote(phase)
    <<",\"source_scene\":"<<note.source_scene<<",\"scene_kind\":"<<note.scene_kind
    <<",\"scene_frame\":"<<note.scene_frame<<",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(note.random_seed)
    <<"\",\"pad_state_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex(note.pad_state,sizeof note.pad_state)<<'"';
 if(std::string(phase)=="scheduler_end")out<<",\"consumed_pad_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex(consumed,sizeof consumed)<<'"';
 out<<",\"scene_routing_getters\":{\"current_game_mode\":"<<note.scene_routing_getters[0]
    <<",\"previous_game_mode\":"<<note.scene_routing_getters[1]
    <<",\"current_scene_index\":"<<note.scene_routing_getters[2]
    <<",\"previous_scene_index\":"<<note.scene_routing_getters[3]<<"},\"owners\":{";
 static const char* names[]={"host","session","world","audio","vs_mode","scene_info","payload","seed"};
 for(unsigned i=0;i<8;++i){if(i)out<<',';out<<'"'<<names[i]<<"\":"<<(note.owners[i]?"true":"false");}
 out<<"},\"world_generation\":"<<note.world_generation<<",\"audio_generation\":"<<note.audio_generation;
 if(prefix){const unsigned i=first_sss_prefix_sequence.current_index;
  check(i<124,"SSS prefix snapshot index is outside its input");
  out<<",\"input_index\":"<<i<<",\"input_ordinal\":"<<i+1
     <<",\"consumed_pad_sequence\":"<<first_sss_prefix_sequence.input.records[i].sequences[0];}
 out<<'}';return out.str();
}
int first_sss_sequence_compare(void* data,int phase,
 const MeleeWebMenuFirstSssTickSnapshot* sample,const MeleeWebMenuFirstSssPairNoteSnapshot* exit_note){
 const bool prefix=data!=nullptr;
 first_sss_sequence_sync(prefix);
 std::string actual;const char* name=nullptr;
 try{
  if(phase==MELEE_WEB_SSS_SAMPLE_EXIT){
   check(exit_note&&exit_note->captured,"SSS exit callback lacks its retained note");
   std::ostringstream out;first_sss_pair_write_note(out,*exit_note);actual=out.str();name="sss_exit";
  }else if(phase==MELEE_WEB_SSS_SAMPLE_SELECTED_STAGE){
   const auto& n=first_sss_prefix_sequence.native;
   check(n.selected_stage_captured,"SSS selected-stage callback lacks its retained note");
   actual="{\"index\":"+std::to_string(n.selected_stage_index)+",\"kind\":"+std::to_string(n.selected_stage_kind)+"}";
   name="selected_stage";
  }else{
   check(sample&&sample->captured,"SSS callback lacks its retained source sample");
   name=phase==MELEE_WEB_SSS_SAMPLE_TICK?"scheduler_end":
        phase==MELEE_WEB_SSS_SAMPLE_DRAW_ENTER?"draw_enter":
        phase==MELEE_WEB_SSS_SAMPLE_DRAW_RETURN?"draw_return":nullptr;
   check(name!=nullptr,"SSS callback phase is unknown");
   actual=first_sss_sequence_sample_json(*sample,name,prefix);
  }
  const int approval=EM_ASM_INT({
   const cb=$0?globalThis.__meleeWebStadiumFirstSssPrefixCompare:globalThis.__meleeWebStadiumFirstSssDrawCompare;
   if(typeof cb!=='function')return 0;
   try{const v=cb(UTF8ToString($1),UTF8ToString($2));if(v&&typeof v.then==='function')return -3;return v===true?1:-1;}
   catch(_){return -2;}
  },prefix,name,actual.c_str());
  if(approval!=1){first_sss_sequence_fail(prefix,approval==0?"SSS host comparator is absent":
    approval==-2?"SSS host comparator threw":approval==-3?"SSS host comparator must be synchronous":"SSS host comparator refused actual sample");return 0;}
  return 1;
 }catch(const std::exception& error){first_sss_sequence_fail(prefix,error.what());return 0;}
}
std::string first_sss_sequence_observation_json(bool prefix){
 auto& s=prefix?first_sss_prefix_sequence:first_sss_draw_sequence;first_sss_sequence_sync(prefix);
 const auto& n=s.native;std::ostringstream out;
 out<<"{\"armed\":"<<(s.armed?"true":"false")<<",\"kicked\":"<<(s.kicked?"true":"false")
    <<",\"attempted\":"<<(s.attempted?"true":"false")<<",\"complete\":"<<(s.complete?"true":"false")
    <<",\"failed\":"<<(s.failed?"true":"false")<<",\"error\":"<<(s.error.empty()?"null":first_css_browser_draw_json_quote(s.error))
    <<",\"captured\":"<<(n.draw_enter.captured&&n.draw_return.captured?"true":"false")
    <<",\"compared\":"<<(n.matched_draw_enters&&n.matched_draw_returns&&!s.failed&&n.state!=3?"true":"false")
    <<",\"prerequisite_pair_retention_boundary\":\"before_first_sss_draw_arm\""
    <<",\"prerequisite_pair_is_historical\":"<<(!first_sss_frozen_pair_json.empty()?"true":"false")
    <<",\"input_index\":"<<n.input_index<<",\"consumed_inputs\":"<<n.consumed_inputs
    <<",\"host_tick_calls\":"<<n.host_tick_calls<<",\"host_draw_calls\":"<<n.host_draw_calls
    <<",\"matched_ticks\":"<<n.matched_ticks<<",\"matched_draw_enters\":"<<n.matched_draw_enters
    <<",\"matched_draw_returns\":"<<n.matched_draw_returns
    <<",\"aurora_begin_calls\":"<<s.aurora_begin_calls<<",\"aurora_end_calls\":"<<s.aurora_end_calls
    <<",\"frame_end_returned\":"<<(s.frame_end_returned?"true":"false")
    <<",\"last_tick_result\":"<<n.last_tick_result<<",\"transition_requested\":"<<(n.transition_requested?"true":"false");
 // Observation must remain readable after ordinary leave has retired live owners.
 if(prefix)out<<",\"current_host_entered\":"<<(host_entered?"true":"false")
    <<",\"current_source_scene\":"<<(host?melee_web_menu_host_source_scene(host):0)
    <<",\"current_session_phase\":"<<(host?melee_web_menu_host_phase(host):0)
    <<",\"session_ticks\":"<<(n.exit_captured?n.exit_note.session_ticks:0);
 const auto write_sample=[&](const char* key,const char* phase,const MeleeWebMenuFirstSssTickSnapshot& sample){
  out<<",\""<<key<<"\":";
  if(sample.captured){
   out<<first_sss_sequence_sample_json(sample,phase,prefix);
  }else out<<"null";
 };
 write_sample("scheduler_end","scheduler_end",n.scheduler_end);
 write_sample("draw_enter","draw_enter",n.draw_enter);write_sample("draw_return","draw_return",n.draw_return);
 out<<",\"exit_note\":";if(n.exit_captured)first_sss_pair_write_note(out,n.exit_note);else out<<"null";
 out<<",\"selected_stage_captured\":"<<(n.selected_stage_captured?"true":"false")<<",\"selected_stage_note\":";
 if(n.selected_stage_captured)out<<"{\"index\":"<<n.selected_stage_index<<",\"kind\":"<<n.selected_stage_kind<<'}';else out<<"null";
 out<<",\"audio_render_calls_before\":"<<s.audio_render_calls_before
    <<",\"audio_render_frames_before\":"<<s.audio_render_frames_before
    <<",\"audio_render_calls_after\":"<<s.audio_render_calls_after
    <<",\"audio_render_frames_after\":"<<s.audio_render_frames_after
    <<",\"audio_phase_before\":"<<s.audio_phase_before<<",\"audio_phase_after\":"<<s.audio_phase_after;
 out<<",\"prepared_output_comparison\":\"unpaired\",\"prepared_output_unpaired\":"<<(s.prepared_output_unpaired.empty()?"null":s.prepared_output_unpaired);
 out<<'}';return out.str();
}

std::string first_sss_pair_actual_json(){
 auto& state=first_sss_constructor_pair;
 const auto& native=state.native;
 const bool host_entered_now=host_entered&&host&&
     melee_web_menu_host_phase(host)==MELEE_WEB_MENU_SSS;
 const bool audio_live=native.entry.captured&&native.returned.captured&&
     native.entry.owners[3]&&native.returned.owners[3];
 const uint64_t world_generation=native.entry.captured?
     native.entry.world_generation:native.returned.world_generation;
 const uint64_t audio_generation=native.entry.captured?
     native.entry.audio_generation:native.returned.audio_generation;
 const char* status=state.failed?"failed":state.complete?"complete":
     state.captured?"captured":state.kicked?"kicked":state.armed?"armed":"idle";
 std::ostringstream out;
 out<<"{\"status\":\""<<status<<"\",\"armed\":"<<(state.armed?"true":"false")
    <<",\"kicked\":"<<(state.kicked?"true":"false")
    <<",\"attempted\":"<<(state.attempted?"true":"false")
    <<",\"captured\":"<<(state.captured?"true":"false")
    <<",\"compared\":"<<(state.compared?"true":"false")
    <<",\"complete\":"<<(state.complete?"true":"false")
    <<",\"failed\":"<<(state.failed?"true":"false")
    <<",\"error\":"<<(state.error.empty()?"null":
       first_css_browser_draw_json_quote(state.error))
    <<",\"host_entered\":"<<(host_entered_now?"true":"false")
    <<",\"session_phase\":"<<(host?melee_web_menu_host_phase(host):0)
    <<",\"world_generation\":"<<world_generation
    <<",\"audio_generation\":"<<audio_generation
    <<",\"audio_owner_live\":"<<(audio_live?"true":"false")
    <<",\"audio_render_calls_before_rebuild\":"<<state.audio_render_calls_before_rebuild
    <<",\"audio_render_frames_before_rebuild\":"<<state.audio_render_frames_before_rebuild
    <<",\"audio_render_calls_after_sss_enter\":"<<state.audio_render_calls_after_sss_enter
    <<",\"audio_render_frames_after_sss_enter\":"<<state.audio_render_frames_after_sss_enter
    <<",\"sss_host_tick_calls\":"<<native.host_tick_calls
    <<",\"sss_host_draw_calls\":"<<native.host_draw_calls
    <<",\"entry\":";
 if(native.entry.captured)first_sss_pair_write_note(out,native.entry);else out<<"null";
 out<<",\"returned\":";
 if(native.returned.captured)first_sss_pair_write_note(out,native.returned);else out<<"null";
 out<<'}';
 return out.str();
}
int first_sss_pair_compare(const std::string& actual_json){
 if(actual_json.empty())return 0;
 return EM_ASM_INT({
  const callback=globalThis.__meleeWebStadiumFirstSssPairCompare;
  if(typeof callback!=="function")return 0;
  try{
   const result=callback("sss_pair",UTF8ToString($0));
   if(result&&typeof result.then==="function")return -3;
   return result===true?1:-1;
  }catch(_){return -2;}
 },actual_json.c_str());
}
void first_sss_pair_capture_and_compare(){
 auto& state=first_sss_constructor_pair;
 if(!state.armed||!state.kicked||state.attempted)return;
 state.attempted=true;
 state.audio_render_calls_after_sss_enter=diagnostic_audio_render_calls;
 state.audio_render_frames_after_sss_enter=diagnostic_audio_render_frames;
 first_sss_pair_sync_native();
 if(state.native.state!=2||!state.native.entry.captured||
    !state.native.returned.captured||!host_entered||
    melee_web_menu_host_phase(host)!=MELEE_WEB_MENU_SSS){
  first_sss_pair_fail(state.error.empty()?
      "Native SSS constructor pair did not retain both notes after host entry":state.error);
  state.actual_json=first_sss_pair_actual_json();
  first_css_browser_draw_refresh_observation();
  return;
 }
 state.captured=true;
 state.actual_json=first_sss_pair_actual_json();
 const int approval=first_sss_pair_compare(state.actual_json);
 state.callback_attempted=approval!=0;
 state.compared=approval==1||approval==-1;
 if(approval==1&&state.native.host_tick_calls==0&&
    state.native.host_draw_calls==0){
  state.complete=true;state.frame_terminal=true;
 }else{
  const char* reason=approval==0?"SSS pair host comparator is absent":
      approval==-2?"SSS pair host comparator threw":
      approval==-3?"SSS pair host comparator must be synchronous":
      approval<0?"SSS pair host comparator refused the actual constructor pair":
      "SSS host tick or draw was attempted during the constructor pair";
  first_sss_pair_fail(reason);
 }
 first_css_browser_draw_refresh_observation();
}
void first_css_browser_draw_fail(const std::string& error){
 if(!first_css_browser_draw.failed){
  first_css_browser_draw.failed=true;first_css_browser_draw.error=error;
 }
 running=false;menu_clock.reset();message=first_css_browser_draw.error;
}
void first_css_browser_draw_check_live(const char* boundary){
 auto& state=first_css_browser_draw;
 check(state.armed&&state.entry_captured&&host&&world&&host_entered,
       "First-CSS draw lost its armed live menu owners");
 check(melee_web_gameplay_world_exists()&&melee_web_source_memory_healthy()&&
       melee_web_gameplay_generation()==state.world_generation&&state.world_generation!=0,
       "First-CSS draw world owner/generation is no longer live");
 check(seed_ptr!=nullptr&&seed_ptr==state.seed_owner,
       "First-CSS draw RNG owner changed; refusing to read it");
 const auto* const scene=static_cast<const GameModeState::GameSceneInfo*>(
     melee_web_current_scene_info());
 check(scene!=nullptr&&scene==state.scene_owner,
       "First-CSS draw scene owner changed; refusing to read it");
 check(scene->scene_kind==8&&
       melee_web_menu_host_source_scene(host)==MELEE_WEB_MENU_HOST_SCENE_CSS&&
       melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CSS,
       (std::string("First-CSS draw left its idle CSS owner at ")+boundary).c_str());
}
FirstCssDrawSample first_css_browser_draw_capture(const char* boundary){
 auto& state=first_css_browser_draw;
 first_css_browser_draw_check_live(boundary);
 FirstCssDrawSample sample{};
 sample.world_generation=melee_web_gameplay_generation();
 sample.world_generation_stable=sample.world_generation==state.world_generation;
 sample.scene_owner_stable=true;sample.seed_owner_stable=true;
 sample.source_scene=melee_web_menu_host_source_scene(host);
 sample.menu_phase=melee_web_menu_host_phase(host);
 const auto* const scene=static_cast<const GameModeState::GameSceneInfo*>(
     melee_web_current_scene_info());
 check(scene==state.scene_owner,"First-CSS scene owner changed during snapshot");
 sample.scene_kind=scene->scene_kind;
 check(seed_ptr==state.seed_owner,"First-CSS RNG owner changed during snapshot");
 sample.seed=*state.seed_owner;
 sample.scene_frame=gm_801A4BA8();
 melee_web_pad_state_capture(sample.pad.data());
 sample.current_mode=static_cast<unsigned>(gm_GetCurrentGameMode());
 sample.previous_mode=static_cast<unsigned>(gm_GetPreviousGameMode());
 sample.current_scene_index=static_cast<unsigned>(gm_GetCurrentSceneIndex());
 sample.previous_scene_index=static_cast<unsigned>(gm_GetPreviousSceneIndex());
 return sample;
}
void first_css_browser_draw_write_route(std::ostream& out,const FirstCssDrawSample& sample){
 out<<"{\"current_game_mode\":"<<sample.current_mode
    <<",\"previous_game_mode\":"<<sample.previous_mode
    <<",\"current_scene_index\":"<<sample.current_scene_index
    <<",\"previous_scene_index\":"<<sample.previous_scene_index<<'}';
}
void first_css_browser_draw_write_sample(std::ostream& out,const FirstCssDrawSample& sample){
 out<<"{\"source_scene\":"<<sample.source_scene
    <<",\"menu_phase\":"<<sample.menu_phase
    <<",\"scene_kind\":"<<sample.scene_kind
    <<",\"scene_frame\":"<<sample.scene_frame
    <<",\"world_generation\":"<<sample.world_generation
    <<",\"world_generation_stable\":"<<(sample.world_generation_stable?"true":"false")
    <<",\"scene_owner_stable\":"<<(sample.scene_owner_stable?"true":"false")
    <<",\"seed_owner_stable\":"<<(sample.seed_owner_stable?"true":"false")
    <<",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(sample.seed)
    <<"\",\"pad_state_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex(sample.pad.data(),sample.pad.size())
    <<"\",\"scene_routing_getters\":";
 first_css_browser_draw_write_route(out,sample);
 out<<'}';
}
std::string first_css_browser_postdraw_stream_actual_json(
 const FirstCssDrawSample& sample,unsigned input_index,unsigned source_tick,
 int tick_result,
 bool terminal_transition){
 std::ostringstream out;
 const unsigned ordinal=input_index+1;
 const uint32_t consume_sequence=
     melee_web::stadium_first_css_diagnostic::kPostdrawFirstConsumeSequence+
     input_index*5;
 out<<"{\"source_tick\":"<<source_tick<<",\"draw_ordinal\":"<<ordinal
    <<",\"scene_frame\":"<<sample.scene_frame
    <<",\"tick_result\":"<<tick_result
    <<",\"executed_host_ticks\":"
    <<first_css_browser_postdraw_stream.executed_host_ticks
    <<",\"stream_input_ordinal\":"<<ordinal
    <<",\"consumed_pad_sequence\":"<<consume_sequence
    <<",\"terminal_transition\":"<<(terminal_transition?"true":"false");
 out<<",\"source_scene\":"<<sample.source_scene
    <<",\"menu_phase\":"<<sample.menu_phase
    <<",\"scene_kind\":"<<sample.scene_kind
    <<",\"world_generation\":"<<sample.world_generation
    <<",\"world_generation_stable\":"<<(sample.world_generation_stable?"true":"false")
    <<",\"scene_owner_stable\":"<<(sample.scene_owner_stable?"true":"false")
    <<",\"seed_owner_stable\":"<<(sample.seed_owner_stable?"true":"false")
    <<",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(sample.seed)
    <<"\",\"pad_state_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex(sample.pad.data(),sample.pad.size())
    <<"\",\"scene_routing_getters\":";
 first_css_browser_draw_write_route(out,sample);
 out<<'}';
 return out.str();
}
void first_css_browser_postdraw_stream_fail(const std::string& error,
                                            const char* outcome="mismatch"){
 auto& state=first_css_browser_postdraw_stream;
 if(!state.failed){state.failed=true;state.error=error;state.outcome=outcome;}
 state.terminal=true;running=false;menu_clock.reset();message=state.error;
}
void first_css_final_pending_draw_fail(const std::string& error){
 auto& state=first_css_final_pending_draw;
 if(!state.failed){state.failed=true;state.error=error;}
 running=false;menu_clock.reset();message=state.error;
}
int first_css_final_pending_draw_compare(const std::string& actual_json){
 if(actual_json.empty())return 0;
 return EM_ASM_INT({
  const callback=globalThis.__meleeWebStadiumFirstCssFinalDrawCompare;
  if(typeof callback!=="function")return 0;
  try{return callback("final_draw",UTF8ToString($0))===true?1:-1;}
  catch(_){return -1;}
 },actual_json.c_str());
}
std::string first_css_final_pending_draw_actual_json(
 const FirstCssDrawSample& sample){
 const auto& stream=first_css_browser_postdraw_stream;
 const auto& baseline=first_css_browser_draw;
 const unsigned source_tick=stream.executed_host_ticks;
 const unsigned draw_ordinal=baseline.host_draw_calls+stream.host_draw_calls;
 check(source_tick==149&&draw_ordinal==148&&stream.matched_ticks==147&&
       stream.matched_draws==147&&stream.consumed_inputs==148&&
       baseline.host_draw_calls==1&&stream.host_draw_calls==147&&
       stream.tick_result==3&&stream.transition_captured,
       "Final CSS draw actual coordinates lost the retained stream boundary");
 std::ostringstream out;
 out<<"{\"source_tick\":"<<source_tick<<",\"draw_ordinal\":"<<draw_ordinal
    <<",\"scene_frame\":"<<sample.scene_frame
    <<",\"source_scene\":"<<sample.source_scene
    <<",\"menu_phase\":"<<sample.menu_phase
    <<",\"scene_kind\":"<<sample.scene_kind
    <<",\"world_generation\":"<<sample.world_generation
    <<",\"world_generation_stable\":"<<(sample.world_generation_stable?"true":"false")
    <<",\"scene_owner_stable\":"<<(sample.scene_owner_stable?"true":"false")
    <<",\"seed_owner_stable\":"<<(sample.seed_owner_stable?"true":"false")
    <<",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(sample.seed)
    <<"\",\"pad_state_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex(sample.pad.data(),sample.pad.size())
    <<"\",\"scene_routing_getters\":";
 first_css_browser_draw_write_route(out,sample);
 out<<'}';
 return out.str();
}
void first_css_browser_postdraw_stream_check_live(const char*,uint32_t);
void first_css_final_pending_draw_check_live(const char* boundary){
 auto& state=first_css_final_pending_draw;
 auto& stream=first_css_browser_postdraw_stream;
 auto& baseline=first_css_browser_draw;
 check(state.armed&&!state.failed&&!state.complete&&
       stream.armed&&stream.kicked&&stream.terminal&&!stream.failed&&
       !stream.complete&&stream.outcome=="stop_after_last_input_request"&&
       stream.current_input_index==147&&stream.next_input_index==147&&
       stream.consumed_inputs==148&&stream.executed_host_ticks==149&&
       stream.matched_ticks==147&&stream.matched_draws==147&&
       stream.transition_captured&&stream.tick_result==3&&
       !stream.frame_end_returned&&stream.host_draw_calls==147&&
       stream.aurora_begin_calls==stream.aurora_end_calls&&
       baseline.armed&&baseline.complete&&!baseline.failed&&
       baseline.frame_end_returned&&host&&world&&host_entered&&!pending&&!replay,
       "Final CSS draw lost its stopped 147-pair stream boundary");
 first_css_browser_postdraw_stream_check_live(boundary,stream.transition.scene_frame);
 check(stream.transition.scene_frame==gm_801A4BA8()&&
       stream.transition.world_generation==baseline.world_generation&&
       seed_ptr==baseline.seed_owner&&*baseline.seed_owner==stream.transition.seed&&
       melee_web_current_scene_info()==baseline.scene_owner,
       "Final CSS draw source owner changed after the retained terminal tick");
 std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> pad{};
 melee_web_pad_state_capture(pad.data());
 check(pad==stream.transition.pad,
       "Final CSS draw PAD changed after the retained terminal tick");
}
FirstCssDrawSample first_css_final_pending_draw_capture(const char* boundary){
 auto& baseline=first_css_browser_draw;
 check(host&&world&&host_entered&&
       melee_web_gameplay_world_exists()&&melee_web_source_memory_healthy()&&
       melee_web_gameplay_generation()==baseline.world_generation&&
       baseline.world_generation!=0&&seed_ptr!=nullptr&&
       seed_ptr==baseline.seed_owner,
       (std::string("Final CSS draw lost its live world/RNG owner at ")+boundary).c_str());
 const auto* const scene=static_cast<const GameModeState::GameSceneInfo*>(
     melee_web_current_scene_info());
 check(scene!=nullptr&&scene==baseline.scene_owner,
       (std::string("Final CSS draw lost its retained scene owner at ")+boundary).c_str());
 FirstCssDrawSample sample{};
 sample.world_generation=melee_web_gameplay_generation();
 sample.world_generation_stable=true;
 sample.scene_owner_stable=true;
 sample.seed_owner_stable=true;
 sample.source_scene=melee_web_menu_host_source_scene(host);
 sample.menu_phase=melee_web_menu_host_phase(host);
 sample.scene_kind=scene->scene_kind;
 sample.seed=*seed_ptr;
 sample.scene_frame=gm_801A4BA8();
 melee_web_pad_state_capture(sample.pad.data());
 sample.current_mode=static_cast<unsigned>(gm_GetCurrentGameMode());
 sample.previous_mode=static_cast<unsigned>(gm_GetPreviousGameMode());
 sample.current_scene_index=static_cast<unsigned>(gm_GetCurrentSceneIndex());
 sample.previous_scene_index=static_cast<unsigned>(gm_GetPreviousSceneIndex());
 return sample;
}
void first_css_browser_postdraw_stream_check_live(const char* boundary,
                                                   uint32_t expected_frame){
 auto& stream=first_css_browser_postdraw_stream;
 auto& baseline=first_css_browser_draw;
 check(stream.armed&&baseline.armed&&baseline.complete&&baseline.frame_end_returned&&
       baseline.entry_captured&&baseline.tick_captured&&baseline.draw_captured&&
       host&&world&&host_entered,
       "First-CSS post-draw stream lost its completed one-shot baseline owners");
 first_css_browser_draw_check_live(boundary);
 check(stream.input.source_sha256==baseline.context.source_sha256&&
       stream.input.first_consume_sequence==
           melee_web::stadium_first_css_diagnostic::kPostdrawFirstConsumeSequence&&
       melee_web_gameplay_generation()==baseline.world_generation&&
       gm_801A4BA8()==expected_frame&&
       seed_ptr==baseline.seed_owner&&
       melee_web_current_scene_info()==baseline.scene_owner,
       "First-CSS post-draw stream owner, frame or seed changed after baseline");
}
bool first_css_browser_postdraw_stream_compare(const char* phase,
                                               const std::string& actual_json){
 if(!phase||actual_json.empty())return false;
 return EM_ASM_INT({
  const callback=globalThis.__meleeWebStadiumFirstCssStreamCompare;
  if(typeof callback!=="function")return 0;
  try{
   const result=callback(UTF8ToString($0),UTF8ToString($1));
   return result===true?1:0;
  }catch(_){return 0;}
 },phase,actual_json.c_str())==1;
}
void first_css_browser_draw_refresh_observation(){
 auto& state=first_css_browser_draw;
 if(!first_sss_vs_setup.history.empty()){
  state.observation=first_sss_vs_setup.history.substr(0,first_sss_vs_setup.history.size()-1)+
      ",\"first_sss_vs_setup\":"+first_sss_vs_setup_json()+"}";
  return;
 }
 std::ostringstream out;
 const char* status=state.failed?"failed":state.complete?"complete":
     state.draw_captured?"draw-captured":state.tick_captured?"tick-captured":
     state.kicked?"kicked":state.entry_captured?"entered":state.armed?"armed":"idle";
 out<<"{\"schema\":\"melee-web-stadium-first-css-browser-draw-v1\",\"state\":\""
    <<status<<"\",\"source_stream_sha256\":\""
    <<state.context.source_sha256<<"\",\"error\":"
    <<(state.error.empty()?"null":first_css_browser_draw_json_quote(state.error))
    <<",\"source_callbacks_before_kick\":"<<state.source_callbacks_before_kick
    <<",\"source_steps\":"<<state.source_steps<<",\"source_draws\":"<<state.source_draws
    <<",\"host_tick_calls\":"<<state.host_tick_calls
    <<",\"host_draw_calls\":"<<state.host_draw_calls
    <<",\"aurora_begin_calls\":"<<state.aurora_begin_calls
    <<",\"aurora_end_calls\":"<<state.aurora_end_calls
    <<",\"frame_end_returned\":"<<(state.frame_end_returned?"true":"false")
    <<",\"preparation_source_callbacks\":"<<state.preparation_source_callbacks
    <<",\"full_session_comparison\":false";
 if(state.entry_captured){
  out<<",\"entry\":{\"source_scene\":"<<state.entry.source_scene
     <<",\"scene_kind\":"<<state.entry.source_scene_kind
     <<",\"random_seed_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex32(state.entry.random_seed)
     <<"\",\"pad_state_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex(state.entry.pad_state,sizeof(state.entry.pad_state))
     <<"\",\"ko_counts_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex(state.entry.ko_counts,sizeof(state.entry.ko_counts))
     <<"\",\"css\":";
  melee_web::stadium_first_css_diagnostic::write_css(out,state.entry.css,"source_vs_owned");
  out<<",\"abi_padding_excluded\":[\"pad_x5C\"]}";
 }
 if(state.tick_captured){
  out<<",\"tick\":{\"source_stream_sha256\":\""
     <<state.context.source_sha256<<"\",\"consumed_pad_sequence\":834,\"source_tick_sequence\":835"
     <<",\"source_tick_value\":0,\"source_draw_ordinal\":0,\"original_source_frame\":"
     <<state.frame_before
     <<",\"phase_relation\":\"native host post-tick sample at frame 1 versus original scheduler-end SourceTick 835 before frame increment at frame 0\""
     <<",\"host_tick_calls\":"<<state.host_tick_calls<<",\"host_tick_result\":"<<state.tick_result
     <<",\"source_scene_kind\":"<<state.tick.scene_kind
     <<",\"host_source_scene\":"<<state.tick.source_scene
     <<",\"host_menu_phase\":"<<state.tick.menu_phase
     <<",\"native_post_host_tick_frame\":"<<state.tick.scene_frame
     <<",\"world_generation\":"<<state.tick.world_generation
     <<",\"world_generation_stable\":"<<(state.tick.world_generation_stable?"true":"false")
     <<",\"source_scene_stable\":"<<(state.tick.scene_owner_stable?"true":"false")
     <<",\"seed_owner_stable\":"<<(state.tick.seed_owner_stable?"true":"false")
     <<",\"random_seed_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex32(state.tick.seed)
     <<"\",\"pad_state_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex(state.tick.pad.data(),state.tick.pad.size())
     <<"\",\"scene_routing_getters\":";
  first_css_browser_draw_write_route(out,state.tick);
  out<<",\"routing_raw_fields_excluded\":[\"pending_mode\",\"next_state_id\"]}";
 }
 if(state.draw_captured){
  out<<",\"draw\":{\"boundary\":\"browser_host_draw_return\",\"source_tick\":"
     <<state.draw_source_tick<<",\"draw_ordinal\":"<<state.draw_ordinal
     <<",\"observation_phase\":\"after source host draw, before Aurora end-frame\",\"host_draw_calls\":"
     <<state.host_draw_calls<<",\"aurora_begin_calls\":"<<state.aurora_begin_calls
     <<",\"source_scene\":"<<state.draw.source_scene<<",\"menu_phase\":"<<state.draw.menu_phase
     <<",\"scene_kind\":"<<state.draw.scene_kind<<",\"scene_frame\":"<<state.draw.scene_frame
     <<",\"world_generation\":"<<state.draw.world_generation
     <<",\"world_generation_stable\":"<<(state.draw.world_generation_stable?"true":"false")
     <<",\"scene_owner_stable\":"<<(state.draw.scene_owner_stable?"true":"false")
     <<",\"seed_owner_stable\":"<<(state.draw.seed_owner_stable?"true":"false")
     <<",\"random_seed_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex32(state.draw.seed)
     <<"\",\"pad_state_hex\":\""
     <<melee_web::stadium_first_css_diagnostic::hex(state.draw.pad.data(),state.draw.pad.size())
     <<"\",\"scene_routing_getters\":";
  first_css_browser_draw_write_route(out,state.draw);
  out<<'}';
 }
 auto& stream=first_css_browser_postdraw_stream;
 if(stream.armed){
  out<<",\"postdraw_stream\":{\"status\":\""
     <<(stream.failed?"failed":stream.complete?"bounded-pair-cap":
        stream.terminal?"terminal":stream.kicked?"running":"armed")
     <<"\",\"error\":"
     <<(stream.error.empty()?"null":first_css_browser_draw_json_quote(stream.error))
     <<",\"outcome\":"
     <<(stream.outcome.empty()?"null":first_css_browser_draw_json_quote(stream.outcome))
     <<",\"failed\":"<<(stream.failed?"true":"false")
     <<",\"terminal\":"<<(stream.terminal?"true":"false")
     <<",\"complete\":"<<(stream.complete?"true":"false")
     <<",\"input_index\":"<<stream.next_input_index
     <<",\"current_input_index\":"<<stream.current_input_index
     <<",\"current_input_ordinal\":"<<(stream.current_input_index+1)
     <<",\"current_consumed_pad_sequence\":"
     <<(melee_web::stadium_first_css_diagnostic::kPostdrawFirstConsumeSequence+
        stream.current_input_index*5)
     <<",\"consumed_inputs\":"<<stream.consumed_inputs
     <<",\"host_tick_calls\":"<<stream.host_tick_calls
     <<",\"executed_host_ticks\":"<<stream.executed_host_ticks
     <<",\"matched_ticks\":"<<stream.matched_ticks
     <<",\"matched_draws\":"<<stream.matched_draws
     <<",\"host_draw_calls\":"<<stream.host_draw_calls
     <<",\"aurora_begin_calls\":"<<stream.aurora_begin_calls
     <<",\"aurora_end_calls\":"<<stream.aurora_end_calls
     <<",\"tick_result\":"<<stream.tick_result
     <<",\"frame_end_returned\":"<<(stream.frame_end_returned?"true":"false")
     <<",\"input_source_stream_sha256\":\""<<stream.input.source_sha256<<"\"";
  if(stream.tick_captured){
   out<<",\"tick\":"<<first_css_browser_postdraw_stream_actual_json(
       stream.tick,stream.current_input_index,stream.current_input_index+1,
       stream.tick_result,false);
  }
  if(stream.draw_captured){
   out<<",\"draw\":"<<first_css_browser_postdraw_stream_actual_json(
       stream.draw,stream.current_input_index,stream.current_input_index+2,1,false);
  }
  if(stream.transition_captured){
   out<<",\"transition_snapshot\":"
      <<first_css_browser_postdraw_stream_actual_json(
          stream.transition,stream.current_input_index,stream.current_input_index+1,3,true);
  }
  out<<'}';
 }
 auto& final_draw=first_css_final_pending_draw;
 if(final_draw.armed){
  const char* final_status=final_draw.failed?"failed":
      final_draw.complete?"complete":final_draw.captured?"draw-captured":
      final_draw.attempted?"attempted":final_draw.kicked?"kicked":"armed";
  out<<",\"final_pending_css_draw\":{\"status\":\""<<final_status
     <<"\",\"armed\":"<<(final_draw.armed?"true":"false")
     <<",\"kicked\":"<<(final_draw.kicked?"true":"false")
     <<",\"attempted\":"<<(final_draw.attempted?"true":"false")
     <<",\"captured\":"<<(final_draw.captured?"true":"false")
     <<",\"compared\":"<<(final_draw.compared?"true":"false")
     <<",\"failed\":"<<(final_draw.failed?"true":"false")
     <<",\"complete\":"<<(final_draw.complete?"true":"false")
     <<",\"frame_end_returned\":"<<(final_draw.frame_end_returned?"true":"false")
     <<",\"error\":"<<(final_draw.error.empty()?"null":
          first_css_browser_draw_json_quote(final_draw.error))
     <<",\"input_ordinal\":148,\"consumed_pad_sequence\":1574"
     <<",\"terminal_tick_sequence\":1575,\"draw_enter_sequence\":1576"
     <<",\"draw_return_sequence\":1577,\"source_steps\":0"
     <<",\"source_draws\":0,\"source_pending\":false"
     <<",\"host_draw_calls\":"<<final_draw.host_draw_calls
     <<",\"aurora_begin_calls\":"<<final_draw.aurora_begin_calls
     <<",\"aurora_end_calls\":"<<final_draw.aurora_end_calls;
  if(final_draw.captured)
   out<<",\"actual_draw\":"
      <<first_css_final_pending_draw_actual_json(final_draw.actual);
  out<<'}';
 }
 if(first_sss_constructor_pair.armed){
  first_sss_pair_sync_native();
  out<<",\"first_sss_constructor_pair\":"<<(first_sss_frozen_pair_json.empty()?first_sss_pair_actual_json():first_sss_frozen_pair_json);
 }
 if(first_sss_consumed_tick.armed||first_sss_consumed_tick.failed){
  first_sss_tick_sync_native();
  out<<",\"first_sss_consumed_tick\":"<<first_sss_tick_observation_json();
 }
 if(first_sss_draw_sequence.armed||first_sss_draw_sequence.failed)
  out<<",\"first_sss_draw\":"<<first_sss_sequence_observation_json(false);
 if(first_sss_prefix_sequence.armed||first_sss_prefix_sequence.failed)
  out<<",\"first_sss_prefix\":"<<first_sss_sequence_observation_json(true);
 if(first_sss_vs_entry.attempted||first_sss_vs_entry.failed)
  out<<",\"first_sss_vs_entry\":"<<first_sss_vs_entry_json();
 if(first_sss_vs_setup.attempted||first_sss_vs_setup.failed)
  out<<",\"first_sss_vs_setup\":"<<first_sss_vs_setup_json();
 out<<'}';state.observation=out.str();
}
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
// Pending continuation/PAD outlive scoped reads and deferred construction.
MeleeWebMenuMatchContinuation pending_match_continuation{};
std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)>
    sudden_death_input{nullptr,melee_web_pad_state_free};
const MeleeWebPadState* results_borrowed_input=nullptr;
bool sudden_death_route_active=false;
uint32_t prior_vs_source_frames=0,final_sd_source_frames=0;
MatchExitInfo prior_vs_terminal{};
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
melee_web::DiagnosticPrefixProgress replay_prefix_progress;
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
 if(match)return match->sudden_death()?-1:melee_web::kRetailReplayMatch;
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
constexpr int kStartupPipelineServiceDelayMs=8;
EM_JS_DEPS(startup_pipeline_service_asyncify_deps,
           "$safeSetTimeout,$dynCall,$Asyncify");
EM_JS(void, dispatch_startup_pipeline_service_at_asyncify_idle,
      (uintptr_t callback,int delay_ms), {
 const schedule=()=>{
  if(ABORT||window.menuOwnerStopped?.())return;
  safeSetTimeout(()=>{
   if(ABORT||window.menuOwnerStopped?.())return;
   if(Asyncify.state!==Asyncify.State.Normal||Asyncify.currData!==null){
    schedule();
    return;
   }
   dynCall('vp',callback,[0]);
  },delay_ms);
 };
 schedule();
});
melee_web::MenuPreparationState preparation;
unsigned audio_phase=0,diagnostic_start_ticks=0;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
uint64_t diagnostic_audio_render_calls=0;
uint64_t diagnostic_audio_render_frames=0;
#endif
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
 // Construction targets survive teardown. Read the entered source owner so
 // preparation settling and resume cannot relabel returned CSS as prior SSS.
 if(!host||!host_entered)return "Original menu";
 switch(melee_web_menu_host_source_scene(host)){
 case MELEE_WEB_MENU_HOST_SCENE_CSS:
  return melee_web_menu_host_mode_kind(host)==GM_TRAINING?
      "Original Training character select":"Original character select";
 case MELEE_WEB_MENU_HOST_SCENE_SSS:return "Original stage select";
 case MELEE_WEB_MENU_HOST_SCENE_MAIN:return "Original main menu";
 case MELEE_WEB_MENU_HOST_SCENE_TITLE:return "Original title";
 case MELEE_WEB_MENU_HOST_SCENE_OPENING:return "Original Opening movie";
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
 message=match?"Preparing original match continuation...":prize?"Preparing original character select...":"Preparing original next scene...";
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
void stadium_recreation_match_retired();
#endif
void close(){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 FirstSssTextTailCloseGuard text_tail_close{first_sss_text_tail_probe.opted_in};
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  stadium_recreation_match_retired();
#endif
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
 results_input.reset();results_borrowed_input=nullptr;
 sudden_death_input.reset();pending_match_continuation={};sudden_death_route_active=false;
 prior_vs_source_frames=final_sd_source_frames=0;prior_vs_terminal={};
 if(source_session_owned){
  check(melee_web_gameplay_session_end(error,sizeof(error)),error);
  source_session_owned=false;
 }
 // The end record remains after successful checked owner/session retirement.
 // Diagnostic completion is not a terminal result or whole-session claim.
 if(replay&&replay->diagnostic_entity_prefix&&replay_final_draw&&!faulted)
  std::fprintf(stderr,"ENTITY_PREFIX_END observations=%u first_source_tick=%u last_source_tick=%u diagnostic_prefix_complete=true whole_session_equivalent=false\n",
      replay_prefix_progress.observations,replay_prefix_progress.first_source_tick,
      replay_prefix_progress.last_source_tick);
 if(replay&&replay_trace&&replay_final_draw&&!faulted)
  melee_web::retail_replay_end(replay->frames.size(),replay->whole_session());
 replay.reset();melee_web_net_reset();replay_completion={};replay_prefix_progress={};replay_cursor=0;replay_trace=replay_pending=replay_started=replay_final_draw=false;
 replay_match_complete=false;replay_outcome=0;replay_winner=-1;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 stadium_c1a_armed=false;stadium_c1a_observation.clear();stadium_c1a_raw_stkind=-1;
 first_css_browser_draw=FirstCssBrowserDrawState{};
 first_css_browser_postdraw_stream=FirstCssBrowserPostdrawStreamState{};
 first_css_final_pending_draw=FirstCssFinalPendingDrawState{};
 first_sss_constructor_pair=FirstSssConstructorPairState{};
 first_sss_consumed_tick=FirstSssConsumedTickState{};
 first_sss_draw_sequence=FirstSssSequenceBrowserState{};
 first_sss_prefix_sequence=FirstSssSequenceBrowserState{};first_sss_frozen_pair_json.clear();
 first_sss_vs_entry=FirstSssVsEntryState{};
 first_sss_vs_setup=FirstSssVsSetupState{};
 matched_setup_cf=MatchedSetupCfState{};
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
 case AssetDestination::FirstSssVsSetup:
  check(first_sss_vs_setup.attempted&&!first_sss_vs_setup.failed&&selection,
        "VS Setup asset scope requires its one-use checked selection");
  names=melee_web::stadium_c1a_asset_names(*selection);break;
 case AssetDestination::StadiumC1a:
  check(stadium_c1a_armed&&selection!=nullptr,
        "Stadium C1a preparation requires its armed source selection");
  names=melee_web::stadium_c1a_asset_names(*selection);break;
#endif
 case AssetDestination::SuddenDeath:
  check(sudden_death_route_active&&sudden_death_input,
        "Sudden Death assets require their retained typed continuation and PAD");
  names=melee_web::sudden_death_asset_names(host,pending_match_continuation);break;
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(destination==AssetDestination::FirstSssVsSetup){
  first_sss_vs_setup.requested_generation=asset_generation;first_sss_vs_setup.requested=true;
 }
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 const bool capture_first_css_return=first_css_browser_draw.armed;
#endif
 check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(capture_first_css_return){
  try{
   auto& state=first_css_browser_draw;
   check(melee_web_menu_host_first_css_return(host,&state.entry,error,sizeof(error)),error);
   state.world_generation=melee_web_gameplay_generation();
   state.seed_owner=seed_ptr;
   state.scene_owner=static_cast<const GameModeState::GameSceneInfo*>(
       melee_web_current_scene_info());
   check(state.world_generation!=0&&melee_web_gameplay_world_exists()&&
         melee_web_source_memory_healthy()&&state.seed_owner!=nullptr&&
         state.scene_owner!=nullptr&&*state.seed_owner==state.entry.random_seed&&
         state.entry.source_scene==MELEE_WEB_MENU_HOST_SCENE_CSS&&
         state.entry.source_scene_kind==8&&state.entry.css.ko_counts==nullptr&&
         state.scene_owner->scene_kind==8,
         "Immediate first-CSS return did not retain checked CSS, RNG and world owners");
   state.entry_captured=true;
   first_css_browser_draw_refresh_observation();
  }catch(const std::exception& failure){
   first_css_browser_draw_fail(failure.what());
   first_css_browser_draw_refresh_observation();
  }
 }
#endif
 host_entered=true;world_exposed=true;
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
void enter_typed_results_world(){
 const auto* input=results_input?results_input.get():results_borrowed_input;
 check(input!=nullptr,"Original Results input was not retained across the asset handoff");
 const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 results=std::make_unique<melee_web::GameplayResultsSession>(files,results_info,results_seed,*input);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 results_camera_entry_snapshot=results->camera_entry_snapshot();
#endif
 results_input.reset();results_borrowed_input=nullptr;sudden_death_route_active=false;
 const double constructed=emscripten_get_now();
 report_construction("results-enter",started,constructed,constructed,before,aurora_stats_snapshot());
 first_use_draw_pending=true;pending=false;running=true;audio_phase=0;
 menu_clock.reset();audio_clock.reset();message="Original Results";
}
void enter_typed_sudden_death_world(){
 check(sudden_death_route_active&&sudden_death_input,
       "Sudden Death construction lost its exact continuation/PAD owner");
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 char error[256]{};MeleeWebMenuMatchSelection selection{};
 check(melee_web_menu_host_sudden_death_selection(
           host,&pending_match_continuation,&selection,error,sizeof(error)),error);
 melee_web::pipeline_preparation::match(selection);
#endif
 match=std::make_unique<melee_web::GameplayMatchSession>(files,host,pending_match_continuation,
     *archive_cache,melee_web::GameplayMatchConstruction::Deferred,*sudden_death_input);
 running=false;message="Preparing original Sudden Death...";
}

void begin_typed_results(const MeleeWebMenuMatchContinuation& continuation,
                        const MatchExitInfo& terminal,uint32_t seed,
                        const uint8_t (*final_input)[MELEE_WEB_PAD_STATE_BYTES]){
 check(continuation.kind==MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS,
       "Original match did not supply its typed Results payload");
 char error[256]{};results_info=continuation.payload.results;
 results_route_active=true;results_seed=seed;++completed_matches;
 if(final_input){
  results_input.reset(melee_web_pad_state_decode(*final_input,MELEE_WEB_PAD_STATE_BYTES,error,sizeof(error)));
  check(results_input!=nullptr,error);results_borrowed_input=nullptr;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_entry_packet.capture(completed_matches,terminal,results_info,results_seed,*final_input);
#endif
 }else{
  results_borrowed_input=melee_web_menu_host_input(host);
  check(results_borrowed_input!=nullptr,"Typed SD Results lost its host-owned final PAD bank");
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  results_entry_packet.clear();
#endif
 }
#if defined(MELEE_WEB_PUBLIC_RUNTIME)
 (void)terminal;
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 results_pad_trace_count=0;results_pad_trace_attempts=0;results_pad_trace_overflow=false;
 results_camera_entry_snapshot={};
#endif
 if(scoped_assets){pending=false;request_assets(AssetDestination::Results);return;}
 enter_typed_results_world();
}
void begin_typed_sudden_death(const MeleeWebMenuMatchContinuation& continuation,
                             const uint8_t (&final_input)[MELEE_WEB_PAD_STATE_BYTES]){
 // The original mode callback has already chosen SD. Reject unsupported
 // timelines now, before any asset request, claim, construction or tick.
 sudden_death_route_active=true;pending_match_continuation=continuation;
 check(!replay,"Sudden Death replay timeline is not supported; original continuation was not ticked");
 check(!melee_web_net_active(),"Sudden Death network timeline is not supported; original continuation was not ticked");
 char error[256]{};
 sudden_death_input.reset(melee_web_pad_state_decode(final_input,sizeof(final_input),error,sizeof(error)));
 check(sudden_death_input!=nullptr,error);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
 results_entry_packet.clear();
#endif
 match_message="Original Sudden Death";
 if(scoped_assets){pending=false;request_assets(AssetDestination::SuddenDeath);return;}
 enter_typed_sudden_death_world();
}

void dispatch_vs_continuation(const MatchExitInfo& terminal,uint32_t seed,
                              const uint8_t (&final_input)[MELEE_WEB_PAD_STATE_BYTES]){
 char error[256]{};MeleeWebMenuMatchContinuation continuation{};
 check(melee_web_menu_host_match_continuation_begin(
           host,&terminal,seed,&continuation,error,sizeof(error)),error);
 if(continuation.kind==MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH){
  begin_typed_sudden_death(continuation,final_input);return;
 }
 check(seed_ptr!=nullptr,"Typed VS Results lost its host RNG owner");
 begin_typed_results(continuation,terminal,*seed_ptr,&final_input);
}

void abort_sudden_death_after_failure(const std::string& primary) noexcept {
 if(!sudden_death_route_active)return;
 try{close();}catch(const std::exception& cleanup){
  std::fprintf(stderr,"Sudden Death cleanup after %s: %s\n",primary.c_str(),cleanup.what());
 }
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
  const bool sudden_death=match->sudden_death();
  if(sudden_death)final_sd_source_frames=match->source_frames();
  else prior_vs_source_frames=match->source_frames();
  terminal_match_observation=melee_web_native_menu_match_observe();
  report_owner_lifetime("match-before-teardown");
  const bool checking_stock=stock_check==-1;
  uint32_t seed=0;uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES]{};
  MeleeWebMenuMatchContinuation continuation{};
  {
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   const melee_web::provenance::Scope teardown(pipeline_context(
       MELEE_WEB_PIPELINE_PHASE_TEARDOWN,MELEE_WEB_PIPELINE_SCENE_TEARDOWN));
#endif
   if(sudden_death)match->finish_sudden_death(continuation);
   else match->finish_vs(seed,final_input);
   match.reset();
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
  MatchExitInfo terminal{};
  check(melee_web_match_rules_terminal_data(&terminal),"Original match exit payload is unavailable");
  if(!terminal_match_observation.empty()&&terminal_match_observation.back()=='}'){
   terminal_match_observation.pop_back();
   terminal_match_observation+=",\"sd_source_frames\":"+std::to_string(final_sd_source_frames)+"}";
  }
  if(sudden_death){
   // The public finish captured final SD input internally, after publication.
   // Results receives the observable exact host bank and entry seed instead.
   check(seed_ptr!=nullptr,"Typed SD Results lost its host RNG owner");
   seed=*seed_ptr;
   pending_match_continuation={};sudden_death_input.reset();
   begin_typed_results(continuation,terminal,seed,nullptr);return;
  }
  prior_vs_terminal=terminal;
  dispatch_vs_continuation(terminal,seed,final_input);return;
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
  asset_selection=selection;asset_selection_valid=true;
  prior_vs_source_frames=final_sd_source_frames=0;prior_vs_terminal={};
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

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
std::string first_sss_vs_setup_current_json(){
 std::ostringstream out;out<<"{\"host\":"<<(host?"true":"false")
   <<",\"menu_world\":"<<(world?"true":"false")<<",\"match\":"<<(match?"true":"false")
   <<",\"cache\":"<<(archive_cache?"true":"false")<<",\"host_entered\":"<<(host_entered?"true":"false")
   <<",\"host_phase\":"<<(host?melee_web_menu_host_phase(host):-1)
   <<",\"source_world_exists\":"<<(melee_web_gameplay_world_exists()?"true":"false")
   <<",\"source_world_generation\":"<<melee_web_gameplay_generation()
   <<",\"pending_scope_generation\":"<<asset_scope.pending_generation()
   <<",\"asset_generation\":"<<asset_generation<<",\"active_files\":"<<asset_scope.active_file_count()
   <<",\"running\":"<<(running?"true":"false")<<",\"pending\":"<<(pending?"true":"false")
   <<",\"first_use_draw_pending\":"<<(first_use_draw_pending?"true":"false")<<'}';return out.str();
}
bool first_sss_vs_setup_asset_pending(unsigned generation){
 const auto& s=first_sss_vs_setup;
 return s.attempted&&s.requested&&!s.failed&&!s.complete&&!s.import_committed&&
        s.requested_generation==generation&&generation!=0&&
        asset_destination==AssetDestination::FirstSssVsSetup;
}
void first_sss_vs_setup_fail(const char* error){
 auto& s=first_sss_vs_setup;s.failed=true;s.complete=false;
 if(s.error.empty())s.error=error;
 first_sss_text_tail_setup_failed();
 s.audio_calls_after=diagnostic_audio_render_calls;s.audio_frames_after=diagnostic_audio_render_frames;
 if(match){s.source_frames=match->source_frames();s.construction_complete=match->construction_complete();}
 running=false;pending=false;first_use_draw_pending=false;menu_clock.reset();audio_clock.reset();
 first_css_browser_draw_refresh_observation();
}
bool first_sss_vs_setup_finish_handoff(){
  auto& s=first_sss_vs_setup;
  check(s.attempted&&s.requested&&!s.failed&&!s.complete&&!s.import_committed&&
        asset_generation==s.requested_generation&&asset_selection_valid&&host&&!host_entered&&
        melee_web_menu_host_phase(host)==MELEE_WEB_MENU_READY&&
        s.input&&s.host_input==melee_web_menu_host_input(host),
        "VS Setup handoff lost its exact stopped host/input/generation");
  std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> input_now{};
  check(melee_web_pad_state_encode(s.host_input,input_now.data(),input_now.size())&&
        input_now==s.input_bytes,"VS Setup retained host PAD changed during asset handoff");
  const auto names=melee_web::stadium_c1a_asset_names(asset_selection);
  check(names==requested_assets&&files.size()==names.size()&&
        asset_scope.active_file_count()==names.size(),"VS Setup manifest differs from its exact selection");
  for(const auto& name:names){const auto it=files.find(name);
   check(it!=files.end()&&!it->second.empty(),"VS Setup manifest file is missing");}
  check(!melee_web_gameplay_world_exists()&&seed_ptr==s.parent_seed_owner&&
        *s.parent_seed_owner==s.parent_seed,"VS Setup lost the restored parent RNG owner/value");
  s.import_committed=true;s.committed_generation=asset_generation;
  archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
  match=std::make_unique<melee_web::GameplayMatchSession>(files,asset_selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred,*s.input,
      melee_web::GameplayMatchDiagnostic::StadiumReady);
  s.match_world_generation=melee_web_gameplay_generation();
  check(s.match_world_generation&&s.match_world_generation!=s.prior_world_generation,
        "VS Setup did not acquire a fresh source world");
  running=false;pending=false;first_use_draw_pending=false;return false;
}
#endif
bool finish_asset_handoff(){
 if(asset_destination==AssetDestination::None)return true;
 if(!asset_committed)return false;
 check(!world&&!match&&!archive_cache,"Asset handoff found a live source owner");
 const auto destination=asset_destination;
 asset_destination=AssetDestination::None;asset_committed=false;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(destination==AssetDestination::FirstSssVsSetup)return first_sss_vs_setup_finish_handoff();
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
 if(destination==AssetDestination::Results){enter_typed_results_world();return true;}
 if(destination==AssetDestination::SuddenDeath){
  check(sudden_death_route_active&&sudden_death_input,
        "Sudden Death asset handoff lost its exact continuation/PAD owner");
  enter_typed_sudden_death_world();return false;
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
  if(match->sudden_death()){sudden_death_input.reset();pending_match_continuation={};}
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(first_sss_constructor_pair.armed&&first_sss_constructor_pair.kicked){
  running=false;pending=false;first_use_draw_pending=false;menu_clock.reset();
  first_sss_pair_capture_and_compare();
 }
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 ++diagnostic_audio_render_calls;
 diagnostic_audio_render_frames+=count;
#endif
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
 // Keep the single-owner flag set while the service call is active. If that
 // call yields, another main-loop callback must not queue a duplicate timer.
 if(!startup_pipeline_service_scheduled)return;
 // This independent timer can run before tick consumes the terminal signal.
 // Query owner state without draining commands or changing lifecycle intent.
 if(EM_ASM_INT({return window.menuOwnerStopped?.()?1:0;})||
    !startup_pipeline_service_allowed()){
  startup_pipeline_service_scheduled=false;
  return;
 }
 const AuroraStats* stats=aurora_get_stats();
 if(!stats||stats->queuedPipelines==0){
  startup_pipeline_service_scheduled=false;
  return;
 }
 // One delayed timer owns one bounded Aurora service batch. The flag remains
 // set across the call so a yielded/reentrant main loop cannot queue a peer.
 if(!aurora_pipeline_service_preparation()){
  startup_pipeline_service_failed=true;
  startup_pipeline_service_scheduled=false;
  return;
 }
 startup_pipeline_service_scheduled=false;
 // A successful batch may leave startup work queued. Recheck the JS owner
 // after the service and let the ordinary scheduling guards recheck native
 // ownership and queue state before chaining another independent timer.
 if(EM_ASM_INT({return window.menuOwnerStopped?.()?1:0;}))return;
 schedule_startup_pipeline_service();
}
void schedule_startup_pipeline_service(){
 if(startup_pipeline_service_scheduled||startup_pipeline_service_failed||
    !startup_pipeline_service_allowed())return;
 const AuroraStats* stats=aurora_get_stats();
 if(!stats||stats->queuedPipelines==0)return;
 startup_pipeline_service_scheduled=true;
 dispatch_startup_pipeline_service_at_asyncify_idle(
     reinterpret_cast<uintptr_t>(startup_pipeline_service_callback),
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
// A private callback owns exactly one presentation; the ordinary simulation loop
// stays stopped. It neither applies context nor advances a SourceFrameSequence.
bool first_sss_sequence_service(){
 const bool prefix=first_sss_prefix_sequence.armed;
 auto& s=prefix?first_sss_prefix_sequence:first_sss_draw_sequence;
 if(!s.armed)return false;
 if(!s.kicked||s.complete||s.failed)return true;
 bool began=false;
 char error[256]{};
 try{
  check(!running&&!pending&&!faulted&&world&&host&&host_entered&&
        !match&&!results&&!prize&&!replay&&!menu_scene_rebuild_pending&&
        !preparation.busy(),"SSS private presentation lost its stopped owner");
  s.attempted=true;s.frame_end_returned=false;
  if(prefix){
   first_sss_sequence_sync(true);s.current_index=s.native.input_index;
   check(s.current_index<124&&!s.failed,"SSS prefix callback exceeds its finite input lease");
   melee_web::stadium_first_css_diagnostic::ConsumedPadInput input{};
   input.ports=s.input.records[s.current_index].ports;PADStatus raw[4]{};
   melee_web::stadium_first_css_diagnostic::decode_consumed_pad_statuses(input,raw);
   const int result=melee_web_menu_host_tick_first_sss_prefix(host,raw,s.current_index,error,sizeof error);
   first_sss_sequence_sync(true);
   check(!s.failed&&result==(s.current_index==123?MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED:MELEE_WEB_MENU_RESULT_TICKED),
         error[0]?error:"SSS prefix scheduler phase refused");
   // Diagnostic policy: one existing audio render per approved source tick;
   // preserve audio_phase carry, no PCM/timing equivalence claim.
   render_audio_tick(world->audio(),error,sizeof error);
  }
  for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event)
   check(event->type!=AURORA_EXIT,"SSS diagnostic renderer requested exit");
  ++s.aurora_begin_calls;
  began=aurora_begin_frame();check(began,"SSS diagnostic AuroraBegin refused");
  const int result=prefix?melee_web_menu_host_draw_first_sss_prefix(host,s.current_index,error,sizeof error):
                           melee_web_menu_host_draw_first_sss(host,error,sizeof error);
  first_sss_sequence_sync(prefix);
  // Finish the renderer frame even if a draw observation was refused.
  began=false;aurora_end_frame();++s.aurora_end_calls;s.frame_end_returned=true;
  check(result&&!s.failed,error[0]?error:"SSS diagnostic draw comparator refused");
  if(prefix){
   if(s.native.input_index==124){
    const int left=melee_web_menu_host_leave_first_sss_prefix(host,error,sizeof error);
    first_sss_sequence_sync(true);
    host_entered=melee_web_menu_host_source_scene(host)!=0 /* existing closed-source sentinel */;
    if(const auto* vs=melee_web_menu_host_post_vs_mode(host)){
     std::ostringstream out;melee_web::stadium_first_css_diagnostic::write_vs_mode(out,*vs);
     s.prepared_output_unpaired=out.str();
    }
    check(left&&!s.failed&&s.native.state==2,error[0]?error:"SSS prefix checked leave failed");
    s.complete=true;
   }
  }else{
   check(s.native.state==2&&s.native.host_tick_calls==0&&s.native.host_draw_calls==1&&
         s.native.matched_draw_enters==1&&s.native.matched_draw_returns==1,
         "First SSS draw did not finish its one-draw protocol");s.complete=true;
  }
 }catch(const std::exception& error){
  if(began){try{aurora_end_frame();++s.aurora_end_calls;s.frame_end_returned=true;}catch(...){} }
  first_sss_sequence_fail(prefix,error.what());first_sss_sequence_sync(prefix);
 }
 s.audio_render_calls_after=diagnostic_audio_render_calls;
 s.audio_render_frames_after=diagnostic_audio_render_frames;s.audio_phase_after=audio_phase;
 running=false;pending=false;first_use_draw_pending=false;menu_clock.reset();
 first_css_browser_draw_refresh_observation();return true;
}

bool matched_setup_cf_setup_equal(const MeleeWebStadiumSetupSnapshot& a,
                                 const MeleeWebStadiumSetupSnapshot& b){
 return a.version==b.version&&a.world_generation==b.world_generation&&
  a.world_ticks==b.world_ticks&&a.source_frame==b.source_frame&&a.map_id==b.map_id&&
  a.initial_countdown==b.initial_countdown&&a.current_countdown==b.current_countdown&&
  a.parameter_x0==b.parameter_x0&&a.parameter_x4==b.parameter_x4&&
  a.constructor_randi_call_count==b.constructor_randi_call_count&&
  a.constructor_random_bound_valid==b.constructor_random_bound_valid&&
  a.constructor_random_bound==b.constructor_random_bound&&
  a.constructor_random_result_valid==b.constructor_random_result_valid&&
  a.constructor_random_result==b.constructor_random_result&&
  a.source_seed_before==b.source_seed_before&&a.source_seed_after==b.source_seed_after&&
  a.ground_source_heap_handle==b.ground_source_heap_handle&&
  a.ground_requested_bytes==b.ground_requested_bytes&&
  a.ground_allocation_generation==b.ground_allocation_generation&&
  a.object_source_heap_handle==b.object_source_heap_handle&&
  a.object_requested_bytes==b.object_requested_bytes&&
  a.object_allocation_generation==b.object_allocation_generation&&
  a.constructor_callback_count==b.constructor_callback_count&&
  a.source_parameter_roots_agree==b.source_parameter_roots_agree;
}
bool matched_setup_cf_go_event_equal(const MeleeWebStadiumGoAlignmentEvent& a,
                                     const MeleeWebStadiumGoAlignmentEvent& b){
 return a.generation==b.generation&&a.world_ticks==b.world_ticks&&
  a.source_frame==b.source_frame&&a.sequence==b.sequence&&
  a.object_identity==b.object_identity&&a.proc_identity==b.proc_identity&&
  a.callback_index==b.callback_index&&a.source_branch==b.source_branch&&
  a.map2_gate==b.map2_gate&&a.display_mode==b.display_mode&&
  a.remap_branch==b.remap_branch&&a.callback_identity==b.callback_identity&&
  a.hud_enabled==b.hud_enabled;
}
bool matched_setup_cf_go_equal(const MeleeWebStadiumGoAlignmentSnapshot& a,
                               const MeleeWebStadiumGoAlignmentSnapshot& b){
 return a.generation==b.generation&&a.armed_world_ticks==b.armed_world_ticks&&
  a.armed_source_frame==b.armed_source_frame&&a.armed==b.armed&&
  a.failure==b.failure&&a.expected_branch==b.expected_branch&&
  a.next_sequence==b.next_sequence&&
  matched_setup_cf_go_event_equal(a.stage_before,b.stage_before)&&
  matched_setup_cf_go_event_equal(a.stage_after,b.stage_after)&&
  matched_setup_cf_go_event_equal(a.go_after,b.go_after)&&
  matched_setup_cf_go_event_equal(a.hud_after,b.hud_after);
}
bool matched_setup_cf_idle_equal(const MeleeWebStadiumIdleSnapshot& a,
                                 const MeleeWebStadiumIdleSnapshot& b){
 return a.version==b.version&&a.world_generation==b.world_generation&&
  a.world_ticks==b.world_ticks&&a.source_frame==b.source_frame&&
  a.map2_gate==b.map2_gate&&a.map2_countdown==b.map2_countdown&&
  a.map2_phase==b.map2_phase&&a.map2_form==b.map2_form&&
  a.map2_history_e0==b.map2_history_e0&&a.map2_history_e2==b.map2_history_e2&&
  a.display_mode==b.display_mode&&a.display_e6==b.display_e6&&a.display_e8==b.display_e8&&
  std::equal(std::begin(a.display_timer_native_bytes),std::end(a.display_timer_native_bytes),
             std::begin(b.display_timer_native_bytes))&&
  a.display_timer_initialization_observed==b.display_timer_initialization_observed&&
  a.display_previous_mode==b.display_previous_mode&&
  a.display_selected_player==b.display_selected_player&&a.display_f2==b.display_f2&&
  a.display_flags==b.display_flags&&a.sis_font_slot==b.sis_font_slot&&
  a.sis_static_text_role==b.sis_static_text_role&&a.sis_dynamic_text_role==b.sis_dynamic_text_role&&
  a.sis_context_present==b.sis_context_present&&a.sis_title_entry_seen==b.sis_title_entry_seen&&
  a.sis_title_entry_index==b.sis_title_entry_index&&a.title_bytes_captured==b.title_bytes_captured&&
  a.sis_source_heap_handle==b.sis_source_heap_handle&&
  a.sis_world_generation==b.sis_world_generation&&
  a.sis_allocation_generation==b.sis_allocation_generation&&
  a.sis_source_epoch==b.sis_source_epoch&&a.sis_requested_bytes==b.sis_requested_bytes&&
  a.map2_source_heap_handle==b.map2_source_heap_handle&&
  a.map2_world_generation==b.map2_world_generation&&
  a.map2_allocation_generation==b.map2_allocation_generation&&
  a.map2_requested_bytes==b.map2_requested_bytes&&a.map2_buffer_origin==b.map2_buffer_origin;
}
std::string matched_setup_cf_go_event_json(const MeleeWebStadiumGoAlignmentEvent& event){
 std::ostringstream out;
 out<<"{\"generation\":"<<event.generation<<",\"world_ticks\":"<<event.world_ticks
    <<",\"source_frame\":"<<event.source_frame<<",\"sequence\":"<<event.sequence
    <<",\"callback_index\":"<<event.callback_index
    <<",\"source_branch\":"<<event.source_branch<<",\"map2_gate\":"<<event.map2_gate
    <<",\"display_mode\":"<<event.display_mode<<",\"remap_branch\":"<<event.remap_branch
    <<",\"callback_identity\":"<<event.callback_identity
    <<",\"hud_enabled\":"<<event.hud_enabled<<'}';
 return out.str();
}
std::string matched_setup_cf_go_json(const MeleeWebStadiumGoAlignmentSnapshot& go){
 std::ostringstream out;
 out<<"{\"generation\":"<<go.generation<<",\"armed_world_ticks\":"<<go.armed_world_ticks
    <<",\"armed_source_frame\":"<<go.armed_source_frame<<",\"armed\":"<<go.armed
    <<",\"failure\":"<<go.failure<<",\"expected_branch\":"<<go.expected_branch
    <<",\"next_sequence\":"<<go.next_sequence
    <<",\"stage_before\":"<<matched_setup_cf_go_event_json(go.stage_before)
    <<",\"stage_after\":"<<matched_setup_cf_go_event_json(go.stage_after)
    <<",\"go_after\":"<<matched_setup_cf_go_event_json(go.go_after)
    <<",\"hud_after\":"<<matched_setup_cf_go_event_json(go.hud_after)<<'}';
 return out.str();
}
std::string matched_setup_cf_idle_json(const MeleeWebStadiumIdleSnapshot& idle){
 std::ostringstream out;
 out<<"{\"version\":"<<idle.version<<",\"world_generation\":"<<idle.world_generation
    <<",\"world_ticks\":"<<idle.world_ticks<<",\"source_frame\":"<<idle.source_frame
    <<",\"map2_gate\":"<<idle.map2_gate<<",\"map2_countdown\":"<<idle.map2_countdown
    <<",\"map2_phase\":"<<idle.map2_phase<<",\"map2_form\":"<<idle.map2_form
    <<",\"map2_history_e0\":"<<idle.map2_history_e0
    <<",\"map2_history_e2\":"<<idle.map2_history_e2
    <<",\"display_mode\":"<<idle.display_mode<<",\"display_e6\":"<<idle.display_e6
    <<",\"display_e8\":"<<idle.display_e8<<",\"display_timer_native_bytes\":["
    <<unsigned(idle.display_timer_native_bytes[0])<<','<<unsigned(idle.display_timer_native_bytes[1])<<','
    <<unsigned(idle.display_timer_native_bytes[2])<<','<<unsigned(idle.display_timer_native_bytes[3])<<']'
    <<",\"display_timer_initialization_observed\":"<<idle.display_timer_initialization_observed
    <<",\"display_previous_mode\":"<<idle.display_previous_mode
    <<",\"display_selected_player\":"<<idle.display_selected_player
    <<",\"display_f2\":"<<idle.display_f2<<",\"display_flags\":"<<idle.display_flags
    <<",\"sis_font_slot\":"<<idle.sis_font_slot
    <<",\"sis_static_text_role\":"<<idle.sis_static_text_role
    <<",\"sis_dynamic_text_role\":"<<idle.sis_dynamic_text_role
    <<",\"sis_context_present\":"<<idle.sis_context_present
    <<",\"sis_title_entry_seen\":"<<idle.sis_title_entry_seen
    <<",\"sis_title_entry_index\":"<<idle.sis_title_entry_index
    <<",\"title_bytes_captured\":"<<idle.title_bytes_captured
    <<",\"sis_source_heap_handle\":"<<idle.sis_source_heap_handle
    <<",\"sis_world_generation\":"<<idle.sis_world_generation
    <<",\"sis_allocation_generation\":"<<idle.sis_allocation_generation
    <<",\"sis_source_epoch\":"<<idle.sis_source_epoch
    <<",\"sis_requested_bytes\":"<<idle.sis_requested_bytes
    <<",\"map2_source_heap_handle\":"<<idle.map2_source_heap_handle
    <<",\"map2_world_generation\":"<<idle.map2_world_generation
    <<",\"map2_allocation_generation\":"<<idle.map2_allocation_generation
    <<",\"map2_requested_bytes\":"<<idle.map2_requested_bytes
    <<",\"map2_buffer_origin\":"<<idle.map2_buffer_origin<<'}';
 return out.str();
}
// This compact row reuses the value-copy already captured for the pending
// action. It adds no owner getter, source call, or retained native history.
constexpr size_t MATCHED_SETUP_CF_COMMON_SAMPLE_MAX_BYTES=1508;
std::string matched_setup_cf_common_fields_json(
    const MeleeWebStadiumIdleSnapshot& idle,uint32_t seed,unsigned schedule_index,unsigned source_tick){
 check(idle.map2_gate>=0&&idle.map2_gate<=1&&idle.display_flags<=7,
       "Stadium common-field byte values are outside their declared bit ranges");
 std::ostringstream out;
 out<<"{\"schema\":\"melee-web-stadium-per-tick-common-fields-native-v2\",\"schedule_index\":"
    <<schedule_index<<",\"source_tick\":"<<source_tick
    <<",\"world_ticks\":"<<idle.world_ticks
    <<",\"source_frame\":"<<idle.source_frame
    <<",\"world_generation\":"<<idle.world_generation
    <<",\"map2_countdown\":"<<idle.map2_countdown
    <<",\"map2_phase\":"<<idle.map2_phase
    <<",\"map2_form\":"<<idle.map2_form
    <<",\"map2_history_e0\":"<<idle.map2_history_e0
    <<",\"map2_history_e2\":"<<idle.map2_history_e2
    <<",\"map2_gate\":"<<(idle.map2_gate?"true":"false")
    <<",\"display_mode\":"<<idle.display_mode
    <<",\"display_e6\":"<<idle.display_e6
    <<",\"display_e8\":"<<idle.display_e8
    <<",\"display_previous_mode\":"<<idle.display_previous_mode
    <<",\"display_selected_player\":"<<idle.display_selected_player
    <<",\"display_f2\":"<<idle.display_f2
    <<",\"display_flags\":"<<unsigned(idle.display_flags)
    <<",\"seed_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex32(seed)<<"\"}";
 const std::string sample=out.str();
 check(sample.size()<=MATCHED_SETUP_CF_COMMON_SAMPLE_MAX_BYTES,
       "Stadium per-tick common-field sample exceeded its reviewed byte bound");
 return sample;
}
void matched_setup_cf_fail(const char* error){
 auto& state=matched_setup_cf;
 state.failed=true;state.complete=false;state.queued=false;state.awaiting_approval=false;
 if(state.error.empty())state.error=error?error:"Matched Setup C/F continuation refused";
 running=false;pending=false;first_use_draw_pending=false;menu_clock.reset();audio_clock.reset();
 message=state.error;first_css_browser_draw_refresh_observation();
}
// gm_16AE fn_8016CD98 gates the match clock on HUD/pause state. A source
// traversal therefore advances it zero or one times; a draw never advances it.
// Keep this clock separate from the world's traversal counter through Ready.
bool matched_setup_cf_clock_transition(bool tick_action,uint64_t before,uint64_t after){
 return before<=UINT32_MAX&&after<=UINT32_MAX&&after>=before&&
        (tick_action?after-before<=1:after==before);
}
void matched_setup_cf_check_owner(unsigned expected_ticks,unsigned expected_draws=~0U){
 const auto& state=matched_setup_cf;
 check(state.armed&&!state.failed&&!state.complete&&first_sss_vs_setup.complete&&
       !first_sss_vs_setup.failed&&match&&match.get()==state.owner&&
       archive_cache&&archive_cache.get()==state.cache_owner&&
       host&&host==state.host_owner&&!host_entered&&!running&&!pending&&!faulted&&!first_use_draw_pending&&
       asset_generation==state.asset_generation&&
       melee_web_gameplay_world_exists()&&
       melee_web_gameplay_generation()==state.world_generation&&
       match->construction_complete()&&melee_web_gameplay_stats().ticks==expected_ticks,
       "Matched Setup C/F action lost its bound stopped source owner or tick position");
 if(state.version2&&expected_draws!=~0U)
  check(state.source_draws==expected_draws,
        "Matched Setup C/F v2 independent source draw counter changed");
 if(state.version2)
  check(match->source_frames()==state.last_source_frame_after,
        "Matched Setup C/F v2 match clock changed outside its acknowledged action");
 if(state.version2&&state.queued&&!state.awaiting_approval)
  check(state.source_ticks==state.queued_ticks_before&&
        state.source_draws==state.queued_draws_before&&
        match->source_frames()==state.queued_source_frame_before,
        "Matched Setup C/F v2 source frame/tick/draw advanced while an action awaited service");
 if(state.version2&&state.awaiting_approval)
  check(state.source_ticks==state.last_ticks_after&&
        state.source_draws==state.last_draws_after&&
        match->source_frames()==state.last_source_frame_after,
        "Matched Setup C/F v2 source frame/tick/draw advanced while awaiting host approval");
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 MeleeWebPipelineStatus provenance_status{};char provenance_error[256]{};
 check(melee_web_pipeline_status(melee_web_provenance_recorder(),&provenance_status,
                                provenance_error,sizeof(provenance_error))&&
       provenance_status.capture_active&&!provenance_status.final&&provenance_status.valid&&
       provenance_status.errors==0&&provenance_status.dropped==0,
       provenance_error[0]?provenance_error:"Matched Setup C/F source provenance is not active and valid");
#endif
 MeleeWebStadiumGoAlignmentSnapshot go{};char error[256]{};
 check(match->diagnostic_stadium_go_alignment_snapshot(&go,error,sizeof(error)),
       error[0]?error:"Matched Setup C/F GO owner snapshot refused");
 check(go.armed==1&&go.failure==0&&go.expected_branch==0&&
       go.generation==state.world_generation,
       "Matched Setup C/F GO alignment owner changed");
 if(state.queued)
  check(match->random_seed()==state.seed_at_queue,
        "Matched Setup C/F source RNG changed after its action was queued");
 if(state.awaiting_approval)
  check(match->random_seed()==state.seed_after_action&&
        diagnostic_audio_render_calls==state.audio_calls_after&&
        diagnostic_audio_render_frames==state.audio_frames_after&&audio_phase==state.audio_phase_after,
        "Matched Setup C/F source RNG/audio changed while awaiting host approval");
 if(state.awaiting_approval&&((state.version2&&state.last_ticks_after>state.c_source_tick)||
                              (!state.version2&&state.last_action_kind==1&&state.last_ordinal>=84))){
  MeleeWebStadiumIdleSnapshot idle{};
  check(match->diagnostic_stadium_idle_snapshot(&idle,error,sizeof(error)),
        error[0]?error:"Matched Setup C/F idle owner snapshot refused during host approval");
  check(matched_setup_cf_idle_equal(idle,state.idle)&&matched_setup_cf_go_equal(go,state.go),
        "Matched Setup C/F native owner state changed while awaiting host approval");
 }
}
bool matched_setup_cf_service(){
 auto& state=matched_setup_cf;
 if(!state.armed||state.failed||state.complete)return true;
 bool frame_began=false;
 try{
  unsigned expected_ticks=0,expected_draws=~0U;
  if(state.version2){expected_ticks=state.source_ticks;expected_draws=state.source_draws;}
  else if(state.awaiting_approval)expected_ticks=state.last_ordinal+1;
  else if(state.queued)expected_ticks=state.last_ordinal+(state.last_action_kind==2?1U:0U);
  else if(state.cursor<171){
   const unsigned ordinal=state.cursor<168?state.cursor/2:
       state.cursor==168||state.cursor==169?84U:85U;
   const bool tick_action=state.cursor==168||state.cursor==170||
       (state.cursor<168&&(state.cursor%2)==0);
   expected_ticks=ordinal+(tick_action?0U:1U);
  }else throw std::runtime_error("Matched Setup C/F action cursor exceeded its fixed schedule");
  matched_setup_cf_check_owner(expected_ticks,expected_draws);
  if(!state.queued||state.awaiting_approval){
   first_css_browser_draw_refresh_observation();return true;
  }
  const unsigned index=state.last_schedule_index,ordinal=state.last_ordinal;
  const bool tick_action=state.last_action_kind==1;
  check(index==state.cursor,"Matched Setup C/F pending action is outside its ordered cursor");
  const uint64_t calls_before=diagnostic_audio_render_calls;
  const uint64_t audio_frames_before=diagnostic_audio_render_frames;
  const unsigned phase_before=audio_phase;
  char error[256]{};
  {
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
   const melee_web::provenance::Scope provenance(pipeline_context());
#endif
  if(tick_action){
   melee_web::stadium_first_css_diagnostic::ConsumedPadInput input{};
   input.ports=state.queued_statuses;
   PADStatus raw[4]{};
   melee_web::stadium_first_css_diagnostic::decode_consumed_pad_statuses(input,raw);
   match->tick(raw);
   check(melee_web_gameplay_stats().ticks==ordinal+1&&!match->paused()&&!match->ending()&&
         !match->complete(),"Matched Setup C/F source tick left its exact live sequence");
   if(state.version2)state.source_ticks=melee_web_gameplay_stats().ticks;
   render_audio_tick(match->audio(),error,sizeof(error));
   check(diagnostic_audio_render_calls==calls_before+1,
         "Matched Setup C/F tick did not render exactly one original audio block");
  }else{
   for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event)
    check(event->type!=AURORA_EXIT,"Matched Setup C/F renderer requested exit");
   ++state.aurora_begin_calls;
   frame_began=aurora_begin_frame();
   check(frame_began,"Matched Setup C/F AuroraBegin refused");
   GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
   match->draw();
   if(state.version2)++state.source_draws;
   aurora_end_frame();++state.aurora_end_calls;frame_began=false;
  }
  } // Close source provenance before exporting its ordered records.
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
  melee_web_provenance_frame(0);
#endif
  state.queued=false;
  if(state.version2){
   check(state.source_ticks==state.queued_ticks_before+(tick_action?1U:0U)&&
         state.source_draws==state.queued_draws_before+(tick_action?0U:1U),
         "Matched Setup C/F v2 action changed independent tick/draw counters unexpectedly");
   const uint64_t source_frame_after=match->source_frames();
   check(matched_setup_cf_clock_transition(tick_action,state.queued_source_frame_before,
                                           source_frame_after),
         "Matched Setup C/F v2 match clock violates its source tick/draw transition");
   state.last_ticks_after=state.source_ticks;state.last_draws_after=state.source_draws;
   state.last_source_frame_after=source_frame_after;
   matched_setup_cf_check_owner(state.last_ticks_after,state.last_draws_after);
  }else matched_setup_cf_check_owner(ordinal+1);
  state.seed_after_action=match->random_seed();
  state.last_action_complete=true;
  state.audio_calls_before=calls_before;state.audio_frames_before=audio_frames_before;
  state.audio_calls_after=diagnostic_audio_render_calls;
  state.audio_frames_after=diagnostic_audio_render_frames;
  state.audio_phase_before=phase_before;state.audio_phase_after=audio_phase;
  std::ostringstream actual;
  const unsigned c_tick=state.version2?state.c_source_tick:84U;
  const unsigned f_tick=state.version2?state.f_source_tick:85U;
  if(tick_action&&(ordinal==c_tick||ordinal==f_tick)){
   const char* label=ordinal==c_tick?"C":"F";
   check(state.expected_branch==0,"Matched Setup C/F supports only the approved branch-0 Setup");
   char owner_error[256]{};
   check(match->diagnostic_stadium_idle_snapshot(&state.idle,owner_error,sizeof(owner_error)),
         owner_error[0]?owner_error:"Matched Setup C/F idle snapshot refused");
   check(match->diagnostic_stadium_go_alignment_snapshot(&state.go,owner_error,sizeof(owner_error)),
         owner_error[0]?owner_error:"Matched Setup C/F GO snapshot refused");
   check(state.go.failure==0&&state.go.armed==1&&state.go.expected_branch==0&&
         state.go.generation==state.world_generation&&state.go.armed_world_ticks==0&&
         state.go.armed_source_frame==0&&state.go.next_sequence==3&&
         state.go.stage_before.sequence==1&&state.go.stage_after.sequence==2&&
         state.go.go_after.sequence==3&&state.go.hud_after.sequence==0&&
         state.go.stage_before.world_ticks==c_tick&&state.go.stage_after.world_ticks==c_tick&&
         state.go.go_after.world_ticks==c_tick&&state.go.go_after.source_branch==0&&
         state.go.stage_before.generation==state.world_generation&&
         state.go.stage_after.generation==state.world_generation&&
         state.go.go_after.generation==state.world_generation&&
         state.idle.world_generation==state.world_generation&&
         state.idle.world_ticks==ordinal+1,
         "Matched Setup C/F branch-0 GO events do not match the dynamic C boundary");
   if(state.version2)
    check(state.idle.source_frame==state.last_source_frame_after,
          "Matched Setup C/F v2 snapshot match clock differs from its action receipt");
   actual<<"{\"label\":\""<<label<<"\",\"ordinal\":"<<ordinal
         <<",\"seed_hex\":\""<<melee_web::stadium_first_css_diagnostic::hex32(state.seed_after_action)
         <<"\",\"expected_branch\":"<<state.expected_branch
         <<",\"idle_snapshot\":"<<matched_setup_cf_idle_json(state.idle)
         <<",\"go_trace\":"<<matched_setup_cf_go_json(state.go)<<'}';
  }else actual<<"null";
  if(state.version2&&state.source_ticks>c_tick&&
     !(tick_action&&(ordinal==c_tick||ordinal==f_tick))){
   char owner_error[256]{};
   check(match->diagnostic_stadium_idle_snapshot(&state.idle,owner_error,sizeof(owner_error)),
         owner_error[0]?owner_error:"Matched Setup C/F v2 idle snapshot refused");
   check(match->diagnostic_stadium_go_alignment_snapshot(&state.go,owner_error,sizeof(owner_error)),
         owner_error[0]?owner_error:"Matched Setup C/F v2 GO snapshot refused");
  }
  std::string common_sample_json;
  if(state.common_fields){
   common_sample_json="null";
   if(tick_action&&ordinal>=84&&ordinal<=3584){
    check(state.idle.world_generation==state.world_generation&&
          state.idle.world_ticks==ordinal+1&&
          state.idle.source_frame==state.last_source_frame_after,
          "Stadium per-tick common sample is not the completed post-tick owner snapshot");
    common_sample_json=matched_setup_cf_common_fields_json(
        state.idle,state.seed_after_action,index,ordinal);
   }
  }
  std::ostringstream input_hex;
  if(state.version2&&tick_action)for(uint8_t byte:state.queued_statuses)
   input_hex<<"0123456789abcdef"[byte>>4]<<"0123456789abcdef"[byte&15];
  std::string v2_counter_json;
  if(state.version2){
   v2_counter_json=",\"source_ticks_before\":"+std::to_string(state.queued_ticks_before)+
      ",\"source_ticks_after\":"+std::to_string(state.last_ticks_after)+
      ",\"source_draws_before\":"+std::to_string(state.queued_draws_before)+
      ",\"source_draws_after\":"+std::to_string(state.last_draws_after)+
      ",\"source_frames_before\":"+std::to_string(state.queued_source_frame_before)+
      ",\"source_frames_after\":"+std::to_string(state.last_source_frame_after)+
      ",\"source_tick_input_pad_hex\":"+(tick_action?
          first_css_browser_draw_json_quote(input_hex.str()):"null");
  }
  state.last_action_json="{\"schedule_index\":"+std::to_string(index)+
      ",\"action\":"+first_css_browser_draw_json_quote(tick_action?"tick":"draw")+
      ",\"ordinal\":"+std::to_string(ordinal)+",\"bundle\":"+
      (tick_action?std::to_string(state.last_bundle):"null")+
      ",\"complete\":true,\"source_frames\":"+std::to_string(match->source_frames())
      +",\"world_ticks\":"+std::to_string(melee_web_gameplay_stats().ticks)
      +v2_counter_json
      +",\"audio_render_calls_before\":"+std::to_string(state.audio_calls_before)
      +",\"audio_render_calls_after\":"+std::to_string(state.audio_calls_after)
      +",\"audio_render_frames_before\":"+std::to_string(state.audio_frames_before)
      +",\"audio_render_frames_after\":"+std::to_string(state.audio_frames_after)
      +",\"audio_phase_before\":"+std::to_string(state.audio_phase_before)
      +",\"audio_phase_after\":"+std::to_string(state.audio_phase_after)
      +",\"aurora_begin_calls\":"+std::to_string(state.aurora_begin_calls)
      +",\"aurora_end_calls\":"+std::to_string(state.aurora_end_calls)
      +",\"observation\":"+actual.str()+
      (state.common_fields?",\"common_fields_sample\":"+common_sample_json:"")+"}";
  state.awaiting_approval=true;
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
  drain_pipeline_provenance();
  MeleeWebPipelineStatus provenance_status{};
  check(melee_web_pipeline_status(melee_web_provenance_recorder(),&provenance_status,error,sizeof(error))&&
        provenance_status.capture_active&&!provenance_status.final&&provenance_status.valid&&
        provenance_status.errors==0&&provenance_status.dropped==0,
        error[0]?error:"Matched Setup C/F source provenance became invalid");
#endif
  matched_setup_cf_check_owner(state.version2?state.last_ticks_after:ordinal+1,
                               state.version2?state.last_draws_after:~0U);
  first_css_browser_draw_refresh_observation();return true;
 }catch(const std::exception& error){
  if(frame_began){try{aurora_end_frame();++state.aurora_end_calls;}catch(...){} }
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
  melee_web_provenance_frame(0);
  try{drain_pipeline_provenance();}catch(...){}
#endif
  matched_setup_cf_fail(error.what());return true;
 }
}
std::string matched_setup_cf_json(){
 const auto& state=matched_setup_cf;
 const char* phase=state.failed?"failed":state.complete?"complete":
     state.awaiting_approval?"awaiting-approval":state.queued?"queued":
     state.armed?"armed":"idle";
 std::ostringstream out;
 if(state.common_fields)out<<"{\"schema\":\"melee-web-stadium-source-action-native-v5\",\"scope\":\"original-full7167-common-fields-seed-observation-only\",\"common_field_count\":15,\"seed_value_count\":1,\"action_count\":"
    <<state.action_count<<",\"source_ticks\":"<<state.source_ticks
    <<",\"source_draws\":"<<state.source_draws<<",\"state\":\"";
 else if(state.full_schedule)out<<"{\"schema\":\"melee-web-stadium-source-action-native-v3\",\"scope\":\"original-full7167-schedule-observation-only\",\"action_count\":"
    <<state.action_count<<",\"source_ticks\":"<<state.source_ticks
    <<",\"source_draws\":"<<state.source_draws<<",\"state\":\"";
 else if(state.version2)out<<"{\"schema\":\"melee-web-stadium-source-action-native-v2\",\"scope\":\"proper-prefix-next-draw-tick-draw\",\"action_count\":"
    <<state.action_count<<",\"source_ticks\":"<<state.source_ticks
    <<",\"source_draws\":"<<state.source_draws<<",\"state\":\"";
 else out<<"{\"schema\":\"melee-web-stadium-matched-setup-cf-native-v1\",\"state\":\"";
 out<<phase<<"\",\"opted_in\":"<<(state.opted_in?"true":"false")
    <<",\"armed\":"<<(state.armed?"true":"false")
    <<",\"cursor\":"<<state.cursor<<",\"queued\":"<<(state.queued?"true":"false")
    <<",\"awaiting_approval\":"<<(state.awaiting_approval?"true":"false")
    <<",\"failed\":"<<(state.failed?"true":"false")
    <<",\"complete\":"<<(state.complete?"true":"false")
    <<",\"error\":"<<(state.error.empty()?"null":first_css_browser_draw_json_quote(state.error))
    <<",\"last_action\":"<<(state.last_action_json.empty()?"null":state.last_action_json)<<'}';
 return out.str();
}
std::string matched_setup_cf_retirement_json(){
 std::ostringstream out;
 out<<"{\"schema\":\"melee-web-stadium-matched-setup-cf-retirement-v1\""
    <<",\"native_retired\":"<<(matched_setup_cf_retirement.native_retired?"true":"false")
    <<",\"terminal\":"<<(matched_setup_cf_retirement.terminal?"true":"false")
    <<",\"phase_after_close\":"<<matched_setup_cf_retirement.phase_after_close
    <<",\"running_after_close\":"<<matched_setup_cf_retirement.running_after_close
    <<",\"pipeline_cache_status_before_shutdown\":"<<matched_setup_cf_retirement.pipeline_cache_before
    <<",\"pipeline_cache_status_after_shutdown\":"<<matched_setup_cf_retirement.pipeline_cache_after
    <<",\"error\":"<<(matched_setup_cf_retirement.error.empty()?"null":
        first_css_browser_draw_json_quote(matched_setup_cf_retirement.error))
    <<",\"matched_setup_cf\":"<<(matched_setup_cf_retirement.receipt.empty()?"null":
        matched_setup_cf_retirement.receipt);
 const auto recreation_b=stadium_recreation_b_json();
 if(!recreation_b.empty())out<<",\"recreation_b_rng\":"<<recreation_b;
 out<<'}';
 return out.str();
}
#endif

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
bool first_sss_vs_setup_service(){
 if(!first_sss_vs_setup.attempted)return false;
  auto& s=first_sss_vs_setup;
  if(s.complete&&matched_setup_cf.opted_in){
   matched_setup_cf_service();return true;
  }
  try{
   check(!running&&!pending&&!first_use_draw_pending,"VS Setup continuation became runnable");
   if(!s.failed&&!s.complete){
    if(asset_destination==AssetDestination::FirstSssVsSetup){
     if(asset_committed)(void)finish_asset_handoff();
     else check(asset_scope.pending_generation()==s.requested_generation,
                "VS Setup pending asset generation was aborted or replaced");
    }else{
     check(s.import_committed&&match&&archive_cache,"VS Setup lost deferred construction owners");
     ++s.construction_steps;
     first_sss_text_tail_begin_construction();
     const bool complete=match->advance_construction();
     s.source_frames=match->source_frames();s.construction_complete=match->construction_complete();
     check(s.source_frames==0,"VS Setup construction advanced gameplay");
     if(complete){
      check(s.construction_complete,"VS Setup phase completion disagrees with session");
      std::ostringstream out;out<<"{\"phase\":\"vs_setup\",\"random_seed_hex\":\""
       <<melee_web::stadium_first_css_diagnostic::hex32(match->random_seed())<<"\",\"start\":";
      melee_web::stadium_first_css_diagnostic::write_start(out,match->start_data());out<<'}';
      MeleeWebStadiumSetupSnapshot setup{};char setup_error[256]{};
      const bool setup_captured=match->diagnostic_stadium_setup_snapshot(
          &setup,setup_error,sizeof(setup_error));
      check(setup_captured,setup_error[0]?setup_error:
            "Stadium Setup state receipt refused its exact owner boundary");
      s.setup_snapshot=setup;
      std::ostringstream setup_json;
      setup_json<<"{\"schema\":\"melee-web-stadium-zero-tick-setup-state-v1\""
        <<",\"world_generation\":"<<setup.world_generation
        <<",\"world_ticks\":"<<setup.world_ticks
        <<",\"source_frame\":"<<setup.source_frame
        <<",\"map_id\":"<<setup.map_id
        <<",\"initial_countdown\":"<<setup.initial_countdown
        <<",\"current_countdown\":"<<setup.current_countdown
        <<",\"parameter_x0\":"<<setup.parameter_x0
        <<",\"parameter_x4\":"<<setup.parameter_x4
        <<",\"constructor_callback_count\":"
        <<setup.constructor_callback_count
        <<",\"constructor_randi_call_count\":"
        <<setup.constructor_randi_call_count
        <<",\"constructor_randi_scope\":\"existing_inline_grStadium_randi_call\""
        <<",\"constructor_randi_bound\":"
        <<(setup.constructor_random_bound_valid?std::to_string(setup.constructor_random_bound):"null")
        <<",\"constructor_randi_result\":"
        <<(setup.constructor_random_result_valid?std::to_string(setup.constructor_random_result):"null")
        <<",\"source_seed_before_hex\":\""
        <<melee_web::stadium_first_css_diagnostic::hex32(setup.source_seed_before)
        <<"\",\"source_seed_after_hex\":\""
        <<melee_web::stadium_first_css_diagnostic::hex32(setup.source_seed_after)
        <<"\",\"ground_source_heap_handle\":"<<setup.ground_source_heap_handle
        <<",\"ground_requested_bytes\":"<<setup.ground_requested_bytes
        <<",\"ground_allocation_generation\":"<<setup.ground_allocation_generation
        <<",\"object_source_heap_handle\":"<<setup.object_source_heap_handle
        <<",\"object_requested_bytes\":"<<setup.object_requested_bytes
        <<",\"object_allocation_generation\":"<<setup.object_allocation_generation
        <<",\"source_parameter_roots_agree\":"
        <<(setup.source_parameter_roots_agree?"true":"false")<<'}';
      s.stadium_setup_state=setup_json.str();
      s.actual=out.str();s.captured=true;s.complete=true;
      first_sss_text_tail_capture_membership();
      if(first_sss_text_tail_probe.opted_in){
       const auto& probe=first_sss_text_tail_probe;
       check(probe.error.empty()&&probe.sis_lease_captured&&
             probe.membership_snapshot_started&&probe.membership_snapshot_active,
             probe.error.empty()?"Setup text-tail SIS lease/membership acquisition incomplete":
                                 probe.error.c_str());
      }
     }
    }
   }
   s.audio_calls_after=diagnostic_audio_render_calls;s.audio_frames_after=diagnostic_audio_render_frames;
   first_css_browser_draw_refresh_observation();
  }catch(const std::exception& error){first_sss_vs_setup_fail(error.what());}
  return true;
}
#endif

void tick(){
#if defined(MELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC)
 if(match_draw_discard_terminal)return;
#endif
 if(graphics_startup_started_ms>=0&&graphics_startup_first_callback_ms<0)
  graphics_startup_first_callback_ms=emscripten_get_now()-graphics_startup_started_ms;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 // The opt-in source-action driver owns these stopped diagnostic callbacks.
 // Its async export is the only path allowed to consume a queued action.
 if(matched_setup_cf.explicit_step_mode)return;
#endif
 // Consume owner failure and lifecycle suspension before source or clock work.
 // A hidden/frozen interval may contain no callback at all; its handoff leaves
 // running/manual pause intent unchanged and excludes only inactive wall time.
 const int owner_action=EM_ASM_INT({return window.menuServiceCommands?.() || 0;});
 if(owner_action<0){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(first_sss_vs_setup.attempted&&!first_sss_vs_setup.complete)
   first_sss_vs_setup_fail("VS Setup owner stopped during asynchronous preparation");
  if(matched_setup_cf.armed&&!matched_setup_cf.complete&&!matched_setup_cf.failed)
   matched_setup_cf_fail("Matched Setup C/F owner stopped before its acknowledged boundary");
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(first_sss_vs_setup_service())return;
 if(first_sss_sequence_service())return;
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(first_css_browser_draw.armed&&!first_css_browser_draw.kicked&&
    !first_css_browser_draw.failed&&!first_css_browser_draw.complete)
  ++first_css_browser_draw.source_callbacks_before_kick;
#endif
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
 bool first_css_draw_terminal=false;
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  bool final_pending_css_draw_active=false;
  auto& final_pending_draw=first_css_final_pending_draw;
  if(final_pending_draw.armed){
   check(final_pending_draw.kicked&&!final_pending_draw.attempted&&
         !final_pending_draw.failed&&!final_pending_draw.complete&&
         !suppress_draw&&!faulted,
         "Final CSS draw presenter was called outside its one-use kick");
   first_css_final_pending_draw_check_live("before final pending CSS draw");
   final_pending_draw.attempted=true;
   ++final_pending_draw.aurora_begin_calls;
   final_pending_css_draw_active=true;
  }else if(first_css_browser_postdraw_stream.armed){
   auto& stream=first_css_browser_postdraw_stream;
   check(stream.kicked&&!stream.terminal&&!stream.failed&&
         stream.tick_captured&&stream.tick_approved&&!stream.draw_captured&&
         stream.current_input_index==stream.next_input_index&&
         stream.matched_ticks==stream.current_input_index+1&&
         stream.matched_draws==stream.current_input_index&&
         stream.host_draw_calls==stream.matched_draws&&
         stream.aurora_begin_calls==stream.aurora_end_calls,
         "First-CSS stream draw is not paired with one approved current tick");
   first_css_browser_postdraw_stream_check_live(
       "before streamed browser source draw",
       first_css_browser_draw.draw.scene_frame+stream.matched_ticks);
   ++stream.aurora_begin_calls;
  }else if(first_css_browser_draw.armed){
   auto& state=first_css_browser_draw;
   if(preparation.busy()&&preparation.preparation_draws_source())
    ++state.preparation_source_callbacks;
   check(state.kicked&&!state.failed&&!state.complete&&state.entry_captured&&
         state.source_steps==1&&state.host_tick_calls==1&&state.tick_captured&&
         state.tick_result==1&&!state.draw_captured&&state.host_draw_calls==0&&
         state.aurora_begin_calls==0&&state.aurora_end_calls==0,
         "First-CSS diagnostic attempted a draw outside its one tick/one draw boundary");
   first_css_browser_draw_check_live("before browser source draw");
   ++state.aurora_begin_calls;
  }
#endif
  const double render_started=emscripten_get_now();
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  const unsigned boundary_draw_index=first_replay_callback_probe
      ? replay_boundary_draw_index++ : 0;
  replay_boundary_mark("aurora_begin_frame_begin",static_cast<int>(boundary_draw_index),
                       static_cast<int>(source_frames.steps()),static_cast<int>(replay_cursor));
#endif
  const bool frame_began=aurora_begin_frame();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(final_pending_css_draw_active&&!frame_began){
   first_css_final_pending_draw_fail(
       "Aurora did not begin the one admitted final CSS draw");
   first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }else if(first_css_browser_postdraw_stream.armed&&!frame_began){
   first_css_browser_postdraw_stream_fail(
       "Aurora did not begin the admitted first-CSS stream draw");
   first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }else if(first_css_browser_draw.armed&&
           !first_css_browser_postdraw_stream.armed&&!final_pending_css_draw_active&&!frame_began){
   first_css_browser_draw_fail("Aurora did not begin the one admitted first-CSS source draw");
   first_css_browser_draw_refresh_observation();
  }
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
      if(final_pending_css_draw_active){
       auto& final_draw=first_css_final_pending_draw;
       bool owners_valid=false;
       try{
        first_css_final_pending_draw_check_live("before final host draw");
        check(source_frames.steps()==0&&source_frames.draws()==0&&
              !source_frames.pending()&&
              preparation.phase()==melee_web::MenuPreparationState::Phase::Idle,
              "Final CSS draw cannot run with source-frame work pending");
        owners_valid=true;
       }catch(const std::exception& error){
        first_css_final_pending_draw_fail(error.what());
       }
       if(owners_valid){
        ++final_draw.host_draw_calls;
        drawn=melee_web_menu_host_draw_final_pending_css(host,error,sizeof(error));
        if(drawn==1){
         try{
          /* After renderer return, retain changed compared values as the
           * actual result.  Check only safe live owners before reading them;
           * the host-side final comparator reports PAD/RNG/frame mismatches. */
          final_draw.actual=first_css_final_pending_draw_capture(
              "final pending CSS browser draw return");
          final_draw.captured=true;
          const int approval=first_css_final_pending_draw_compare(
              first_css_final_pending_draw_actual_json(final_draw.actual));
          final_draw.compared=approval!=0;
          final_draw.approved=approval==1;
          if(approval==0)final_draw.error=
              "Final CSS draw host comparator is absent";
          else if(approval<0)final_draw.error=
              "Final CSS draw host comparator refused the actual draw";
         }catch(const std::exception& error){
          final_draw.error=error.what();
         }
        }else{
         final_draw.error=error[0]?error:
             "Native host refused the one final pending CSS draw";
        }
       }else{
        drawn=0;
       }
      }else{
      if(first_css_browser_postdraw_stream.armed){
       auto& stream=first_css_browser_postdraw_stream;
       ++stream.host_draw_calls;
       check(stream.kicked&&!stream.terminal&&!stream.failed&&
             stream.tick_result==1&&stream.tick_approved&&!pending&&
             stream.current_input_index==stream.next_input_index&&
             stream.matched_ticks==stream.current_input_index+1&&
             stream.matched_draws==stream.current_input_index&&
             source_frames.steps()==1&&source_frames.draws()==0&&
             preparation.phase()==melee_web::MenuPreparationState::Phase::Idle,
             "First-CSS stream draw no longer follows one approved CSS tick");
       first_css_browser_postdraw_stream_check_live(
           "before streamed host draw",
           first_css_browser_draw.draw.scene_frame+stream.matched_ticks);
      }else if(first_css_browser_draw.armed){
       ++first_css_browser_draw.host_draw_calls;
       check(first_css_browser_draw.tick_result==1&&!pending&&
             source_frames.steps()==1&&source_frames.draws()==0&&
             preparation.phase()==melee_web::MenuPreparationState::Phase::Idle,
             "First-CSS draw no longer follows one live non-transition CSS tick");
       first_css_browser_draw.draw_source_tick=source_frames.steps();
       first_css_browser_draw.draw_ordinal=source_frames.draws();
       first_css_browser_draw_check_live("before host draw");
      }
      drawn=melee_web_menu_host_draw(host,error,sizeof(error));
      }
#else
      drawn=melee_web_menu_host_draw(host,error,sizeof(error));
#endif
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
      if(final_pending_css_draw_active){
       /* The actual final row and comparison are retained before Aurora end. */
      }else if(first_css_browser_postdraw_stream.armed&&drawn==1){
       auto& stream=first_css_browser_postdraw_stream;
       first_css_browser_postdraw_stream_check_live(
           "after streamed host draw",
           first_css_browser_draw.draw.scene_frame+stream.matched_ticks);
       stream.draw=first_css_browser_draw_capture("postdraw stream browser draw return");
       stream.draw_captured=true;
       const std::string actual=first_css_browser_postdraw_stream_actual_json(
           stream.draw,stream.current_input_index,stream.current_input_index+2,1,false);
       stream.draw_approved=first_css_browser_postdraw_stream_compare("draw",actual);
       first_css_browser_draw_refresh_observation();
      }else if(first_css_browser_draw.armed&&
               !first_css_browser_postdraw_stream.armed&&!final_pending_css_draw_active){
       check(drawn==1,error[0]?error:"Original first-CSS host draw did not execute");
       first_css_browser_draw_check_live("after host draw");
       auto& state=first_css_browser_draw;
       state.draw=first_css_browser_draw_capture("first browser source draw return");
       check(state.draw.source_scene==MELEE_WEB_MENU_HOST_SCENE_CSS&&
             state.draw.menu_phase==MELEE_WEB_MENU_CSS&&state.draw.scene_kind==8&&
             state.draw.scene_frame==1&&state.draw.world_generation_stable&&
             state.draw.scene_owner_stable&&state.draw.seed_owner_stable,
             "First browser draw left the original idle CSS source boundary");
       state.draw_captured=true;
       ++state.source_draws;
       first_css_browser_draw_refresh_observation();
      }
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(final_pending_css_draw_active){
    auto& final_draw=first_css_final_pending_draw;
    ++final_draw.aurora_end_calls;
    final_draw.frame_end_returned=true;
    if(final_draw.captured&&final_draw.compared&&final_draw.approved&&
       final_draw.host_draw_calls==1&&final_draw.aurora_begin_calls==1&&
       final_draw.aurora_end_calls==1){
     final_draw.complete=true;
    }else{
     first_css_final_pending_draw_fail(final_draw.error.empty()?
         "Final CSS draw did not complete one compared Aurora frame":final_draw.error);
    }
    first_css_draw_terminal=true;
    running=false;menu_clock.reset();
    first_css_browser_draw_refresh_observation();
   }else if(first_css_browser_postdraw_stream.armed){
    auto& stream=first_css_browser_postdraw_stream;
    ++stream.aurora_end_calls;
    stream.frame_end_returned=true;
    if(!stream.draw_captured||!stream.draw_approved){
     first_css_browser_postdraw_stream_fail(
         "First-CSS stream DrawReturn comparator refused the completed browser draw");
     first_css_draw_terminal=true;
    }else{
     ++stream.matched_draws;
     ++stream.next_input_index;
     if(stream.next_input_index==
        melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount){
      stream.complete=true;stream.terminal=true;stream.outcome="bounded_pair_cap";
      running=false;menu_clock.reset();
     }
    }
    first_css_browser_draw_refresh_observation();
   }else if(first_css_browser_draw.armed){
    ++first_css_browser_draw.aurora_end_calls;
    first_css_browser_draw.frame_end_returned=true;
    first_css_browser_draw_refresh_observation();
   }
#endif
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
    if(replay->diagnostic_entity_prefix){
     check(replay_prefix_progress.ready(replay->diagnostic_source_observations(),true,true,match!=nullptr,pending,
           replay_match_complete,replay_outcome,replay->diagnostic_active_entity_prefix),
           "Diagnostic entity prefix did not consume its bound live source interval and final draw");
     replay_final_draw=true;replay_completed_now=true;running=false;menu_clock.reset();
     message="Diagnostic entity prefix complete; whole-session comparison remains incomplete.";
    }else if(replay->whole_session()){
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
  }else if(audio_before_construction
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
           &&!(first_css_browser_postdraw_stream.armed&&
               first_css_browser_postdraw_stream.terminal&&
               !(first_sss_constructor_pair.armed&&first_sss_constructor_pair.kicked&&
                 !first_sss_constructor_pair.frame_terminal))
#endif
           ){
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_sss_constructor_pair.armed&&first_sss_constructor_pair.frame_terminal){
    preparation.reset();running=false;pending=false;first_use_draw_pending=false;
    suppress_draw=1;
   }else
#endif
   if(construction_complete){
    preparation_profile.construction_finished(emscripten_get_now());
    preparation.finish_construction(true,preparation_uses_source_draws());running=false;
   }
   preparation_ms=emscripten_get_now()-preparation_started;preparation_started=0;
   suppress_draw=preparation.suppress_source_draw();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_sss_constructor_pair.armed&&first_sss_constructor_pair.frame_terminal)
    suppress_draw=1;
#endif
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
  bool simulation_clock_running=running&&(world||match||results||prize)&&input->visible;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(first_css_browser_postdraw_stream.armed)
   simulation_clock_running=simulation_clock_running&&
       first_css_browser_postdraw_stream.kicked&&
       !first_css_browser_postdraw_stream.failed&&
       !first_css_browser_postdraw_stream.terminal&&
       !first_css_browser_postdraw_stream.complete;
  else if(first_css_browser_draw.armed)
   simulation_clock_running=simulation_clock_running&&first_css_browser_draw.kicked&&
       !first_css_browser_draw.failed&&!first_css_browser_draw.complete;
#endif
  const auto observe_simulation_clock_stall=[](const auto& event) noexcept {
   diagnostic_clock_stall(event,1);
  };
  const auto elapsed=(local_capture_clock
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
      ||(first_css_browser_postdraw_stream.armed&&
         first_css_browser_postdraw_stream.kicked)||
        (first_css_browser_draw.armed&&first_css_browser_draw.kicked)
#endif
      )?menu_clock.tick_with_budget(simulation_clock_now,simulation_clock_running,1,
                                    observe_simulation_clock_stall):
       menu_clock.tick(simulation_clock_now,simulation_clock_running,
                       observe_simulation_clock_stall);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(first_css_browser_postdraw_stream.armed&&elapsed.steps>1)
   first_css_browser_postdraw_stream_fail(
       "First-CSS post-draw stream clock exceeded one source step");
  if(first_css_browser_postdraw_stream.armed&&elapsed.stalled)
   first_css_browser_postdraw_stream_fail(
       "First-CSS post-draw stream clock stalled during its bounded run");
  if(!first_css_browser_postdraw_stream.armed&&first_css_browser_draw.armed&&
     first_css_browser_draw.kicked&&elapsed.steps>1)
   first_css_browser_draw_fail("First-CSS one-shot clock exceeded one source step");
#endif
  if(elapsed.stalled){running=false;message="Paused after a timing disruption. Resume to continue.";}
  if(elapsed.steps&&transition_audio_continues){
   transition_audio_continues=false;
  }
  for(unsigned step=0;step<elapsed.steps;step++){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_css_browser_postdraw_stream.armed&&
      (first_css_browser_postdraw_stream.failed||
       first_css_browser_postdraw_stream.terminal))break;
#endif
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_css_browser_postdraw_stream.armed)
    check(first_css_browser_postdraw_stream.kicked&&
          !first_css_browser_postdraw_stream.failed&&
          !first_css_browser_postdraw_stream.terminal&&
          !first_css_browser_postdraw_stream.complete&&
          !source_frames.pending()&&source_frames.steps()==0&&source_frames.draws()==0,
          "First-CSS stream refused a callback with pending prior source work");
#endif
   source_frames.before_step(present_source);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_css_browser_postdraw_stream.armed){
    auto& stream=first_css_browser_postdraw_stream;
    check(stream.kicked&&!stream.failed&&!stream.terminal&&!stream.complete&&
          stream.next_input_index<melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount&&
          stream.host_tick_calls==stream.matched_ticks&&
          stream.executed_host_ticks==stream.matched_ticks+1&&
          stream.consumed_inputs==stream.matched_ticks&&
          stream.host_draw_calls==stream.matched_draws&&
          stream.matched_ticks==stream.matched_draws&&
          stream.aurora_begin_calls==stream.aurora_end_calls&&
          !source_frames.pending()&&source_frames.steps()==0&&source_frames.draws()==0&&
          !pending&&!faulted&&!replay&&!melee_web_net_active()&&
          diagnostic_start_ticks==0&&diagnostic_pad_remaining==0&&
          preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
          !preparation.busy(),
          "First-CSS stream reached a source tick outside its exact idle draw-pair boundary");
    stream.current_input_index=stream.next_input_index;
    stream.tick_captured=false;stream.tick_approved=false;
    stream.draw_captured=false;stream.draw_approved=false;
    stream.transition_captured=false;stream.frame_end_returned=false;
    stream.tick_result=0;
    melee_web::stadium_first_css_diagnostic::decode_postdraw_pad_statuses(
        stream.input,stream.current_input_index,stream.input_status);
    check(stream.input_status[0].err==PAD_ERR_NONE,
          "Retained post-draw stream PAD is not connected on P1");
    first_css_browser_postdraw_stream_check_live(
        "before streamed source tick",
        first_css_browser_draw.draw.scene_frame+stream.matched_ticks);
   }else if(first_css_browser_draw.armed){
    check(first_css_browser_draw.kicked&&!first_css_browser_draw.failed&&
          !first_css_browser_draw.complete&&first_css_browser_draw.source_steps==0&&
          first_css_browser_draw.host_tick_calls==0&&first_css_browser_draw.host_draw_calls==0&&
          source_frames.steps()==0&&source_frames.draws()==0,
          "First-CSS diagnostic reached a second source step or draw boundary");
    first_css_browser_draw_check_live("before one source tick");
   }else
#endif
   if(prepare_deferred_pipelines())break;
   PADStatus checked_input[4];const PADStatus* sample=input->raw;bool copied_input=false;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
   if(first_css_browser_postdraw_stream.armed){
    sample=first_css_browser_postdraw_stream.input_status;
   }else if(first_css_browser_draw.armed){
    check(first_css_browser_draw.consumed_status[0].err==PAD_ERR_NONE,
          "Retained first-CSS consumed PAD is not connected on P1");
    sample=first_css_browser_draw.consumed_status;
   }
#endif
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
   if(match&&match->sudden_death())
    check(!replay&&!melee_web_net_active(),"Sudden Death has no admitted replay/network timeline");
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
    // gm_801A4BA8 is the original scene traversal cursor (MWRO80479d58),
    // not the gameplay timer returned by source_frames()/gm_GetFrameCount.
    const auto prefix_tick_before=replay&&replay->diagnostic_entity_prefix?gm_801A4BA8():0;
    match->tick(sample);
    if(replay&&replay->diagnostic_entity_prefix)
     check(replay_prefix_progress.observe(prefix_tick_before,gm_801A4BA8(),replay->diagnostic_active_entity_prefix) &&
           !match->paused()&&!match->complete()&&!match->ending(),
           "Diagnostic entity prefix lost its original live source boundary");
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(first_css_browser_postdraw_stream.armed){
     auto& stream=first_css_browser_postdraw_stream;
     check(stream.current_input_index==stream.next_input_index&&
           stream.current_input_index<melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount&&
           stream.consumed_inputs==stream.current_input_index&&
           stream.host_tick_calls==stream.matched_ticks&&
           stream.host_draw_calls==stream.matched_draws&&
           stream.matched_ticks==stream.matched_draws&&
           !stream.tick_captured&&!stream.transition_captured&&
           source_frames.steps()==0&&source_frames.draws()==0&&!source_frames.pending(),
           "First-CSS stream tick no longer owns the next exact retained input");
     first_css_browser_postdraw_stream_check_live(
         "before streamed host tick",
         first_css_browser_draw.draw.scene_frame+stream.matched_ticks);
     ++stream.host_tick_calls;
     ++stream.consumed_inputs;
     result=melee_web_menu_host_tick(host,sample,error,sizeof(error));
     ++stream.executed_host_ticks;
     stream.tick_result=result;
     check(result==1||result==3,error[0]?error:
           "First-CSS stream host tick returned an unsupported result");
     if(result==3){
      stream.transition=first_css_browser_draw_capture(
          "first-CSS post-draw transition tick return");
      stream.transition_captured=true;
      const bool approved=first_css_browser_postdraw_stream_compare(
          "transition",first_css_browser_postdraw_stream_actual_json(
              stream.transition,stream.current_input_index,
              stream.current_input_index+1,result,true));
      check(!approved,
            "First-CSS stream transition comparator unexpectedly approved a draw boundary");
      stream.terminal=true;
      if(stream.current_input_index==
         melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount-1){
       stream.outcome="stop_after_last_input_request";
       message="First-CSS stream stopped at its retained final transition input; final draw is unpaired.";
      }else{
       first_css_browser_postdraw_stream_fail(
           "Original CSS requested transition before the retained input cap",
           "early_source_transition_before_raw_pair_cap");
      }
      running=false;menu_clock.reset();
      first_css_draw_terminal=true;
      first_css_browser_draw_refresh_observation();
      break;
     }
     const uint32_t expected_frame=first_css_browser_draw.draw.scene_frame+
         stream.matched_ticks+1;
     first_css_browser_postdraw_stream_check_live(
         "after streamed host tick",expected_frame);
     stream.tick=first_css_browser_draw_capture(
         "first-CSS post-draw source tick return");
     check(stream.tick.scene_frame==expected_frame,
           "First-CSS stream source tick did not advance exactly one scene frame");
     stream.tick_captured=true;
     const bool approved=first_css_browser_postdraw_stream_compare(
         "tick",first_css_browser_postdraw_stream_actual_json(
             stream.tick,stream.current_input_index,
             stream.current_input_index+1,result,false));
     if(!approved){
      first_css_browser_postdraw_stream_fail(
          "First-CSS stream SourceTick comparator refused the actual source state");
      first_css_draw_terminal=true;
      first_css_browser_draw_refresh_observation();
      break;
     }
     stream.tick_approved=true;
     ++stream.matched_ticks;
     first_css_browser_draw_refresh_observation();
     source_frames.did_step();
    }else{
     if(first_css_browser_draw.armed){
      auto& state=first_css_browser_draw;
      ++state.host_tick_calls;
      state.frame_before=gm_801A4BA8();
      check(state.frame_before==0&&state.host_tick_calls==1&&
            state.source_steps==0&&state.host_draw_calls==0,
            "First-CSS source tick did not begin at exact frame-zero owner");
      first_css_browser_draw_check_live("before original source tick");
     }
     result=melee_web_menu_host_tick(host,sample,error,sizeof(error));
     if(first_css_browser_draw.armed){
      auto& state=first_css_browser_draw;
      state.tick_result=result;
      check(result==1,"First-CSS retained source tick requested a transition or failed");
      first_css_browser_draw_check_live("after original source tick");
      state.frame_after_tick=gm_801A4BA8();
      state.tick=first_css_browser_draw_capture("original source tick return");
      check(state.frame_after_tick==1&&state.tick.scene_frame==1&&
            state.tick.source_scene==MELEE_WEB_MENU_HOST_SCENE_CSS&&
            state.tick.menu_phase==MELEE_WEB_MENU_CSS&&state.tick.scene_kind==8,
            "First-CSS tick did not advance exactly one live CSS source frame");
      state.tick_captured=true;
      first_css_browser_draw_refresh_observation();
     }
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
     if(probe_css_tick)
      replay_boundary_mark("css_tick_returned",static_cast<int>(source_frames.steps()),
                           static_cast<int>(replay_cursor),result);
#endif
     check(result==1||result==3,error);
     source_frames.did_step();
     if(first_css_browser_draw.armed){
      check(result==1,"First-CSS source tick returned a transition result");
      ++first_css_browser_draw.source_steps;
     }
    }
#else
    result=melee_web_menu_host_tick(host,sample,error,sizeof(error));
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
    if(probe_css_tick)
     replay_boundary_mark("css_tick_returned",static_cast<int>(source_frames.steps()),
                          static_cast<int>(replay_cursor),result);
#endif
    check(result==1||result==3,error);source_frames.did_step();
#endif
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
  if(!audio_before_construction&&!audio_elapsed.stalled
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
     &&!(first_css_browser_postdraw_stream.armed&&
         first_css_browser_postdraw_stream.terminal&&
         !(first_sss_constructor_pair.armed&&first_sss_constructor_pair.kicked&&
           !first_sss_constructor_pair.frame_terminal))
#endif
     )
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(!first_sss_constructor_pair.armed&&
     first_css_final_pending_draw.armed&&first_css_final_pending_draw.kicked&&
     !first_css_final_pending_draw.attempted&&!first_css_final_pending_draw.failed&&
     !first_css_final_pending_draw.complete){
   check(source_frames.steps()==0&&source_frames.draws()==0&&
         !source_frames.pending()&&!pending&&
         first_css_browser_postdraw_stream.terminal&&
         first_css_browser_postdraw_stream.outcome=="stop_after_last_input_request",
         "Final CSS draw cannot run while source-frame/input work remains");
   (void)present_source();
  }
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("source_frames_finish_begin",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
#endif
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  auto& postdraw_stream=first_css_browser_postdraw_stream;
  if(postdraw_stream.armed&&postdraw_stream.terminal){
   check(source_frames.steps()==0&&source_frames.draws()==0&&!source_frames.pending(),
         "Terminal first-CSS stream advanced or finished an unapproved source draw");
   first_css_draw_terminal=true;
  }else{
   source_frames.finish(present_source);
   if(postdraw_stream.armed){
    if(postdraw_stream.terminal){
     if(!postdraw_stream.failed){
      check(source_frames.steps()==1&&source_frames.draws()==1&&
            !source_frames.pending()&&postdraw_stream.tick_captured&&
            postdraw_stream.tick_approved&&postdraw_stream.draw_captured&&
             postdraw_stream.host_draw_calls==postdraw_stream.matched_draws&&
            postdraw_stream.aurora_begin_calls==postdraw_stream.aurora_end_calls&&
            postdraw_stream.frame_end_returned&&postdraw_stream.complete&&
            postdraw_stream.outcome=="bounded_pair_cap"&&
            postdraw_stream.next_input_index==
                melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount&&
            postdraw_stream.matched_ticks==
                melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount&&
            postdraw_stream.matched_draws==
                melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount,
            "Terminal CSS stream cap did not retire all 148 actual Aurora frames exactly once");
     }
     first_css_draw_terminal=true;
     running=false;menu_clock.reset();
     first_css_browser_draw_refresh_observation();
    }else if(postdraw_stream.kicked&&source_frames.steps()!=0&&
             postdraw_stream.current_input_index<
             melee_web::stadium_first_css_diagnostic::kPostdrawBatchCount){
     check(source_frames.steps()==1&&source_frames.draws()==1&&
           !source_frames.pending()&&postdraw_stream.tick_captured&&
           postdraw_stream.tick_approved&&postdraw_stream.draw_captured&&
           postdraw_stream.draw_approved&&postdraw_stream.host_tick_calls==
               postdraw_stream.matched_ticks&&postdraw_stream.host_draw_calls==
               postdraw_stream.matched_draws&&postdraw_stream.matched_ticks==
               postdraw_stream.current_input_index+1&&postdraw_stream.matched_draws==
               postdraw_stream.current_input_index+1&&postdraw_stream.consumed_inputs==
               postdraw_stream.current_input_index+1&&postdraw_stream.next_input_index==
               postdraw_stream.current_input_index+1&&postdraw_stream.aurora_begin_calls==
               postdraw_stream.aurora_end_calls&&postdraw_stream.frame_end_returned,
           "First-CSS stream pair did not finish as one approved tick and one presented draw");
    }else if(postdraw_stream.armed){
     check(source_frames.steps()==0&&source_frames.draws()==0&&
           !source_frames.pending()&&postdraw_stream.host_tick_calls==
               postdraw_stream.matched_ticks&&postdraw_stream.host_draw_calls==
               postdraw_stream.matched_draws&&postdraw_stream.aurora_begin_calls==
               postdraw_stream.aurora_end_calls,
           "Idle first-CSS stream callback changed source or presentation ownership");
    }
   }else if(first_css_browser_draw.armed&&
            (first_css_browser_draw.complete||first_css_browser_draw.failed)){
    check(source_frames.steps()==0&&source_frames.draws()==0,
          "Terminal first-CSS diagnostic advanced source work in a later callback");
    first_css_draw_terminal=true;
   }else if(first_css_browser_draw.armed&&first_css_browser_draw.kicked&&
            first_css_browser_draw.draw_captured){
    auto& state=first_css_browser_draw;
    check(source_frames.steps()==1&&source_frames.draws()==1&&state.source_steps==1&&
          state.source_draws==1&&state.host_tick_calls==1&&state.host_draw_calls==1&&
          state.aurora_begin_calls==1&&state.aurora_end_calls==1&&
          state.frame_end_returned&&state.tick_result==1,
          "First-CSS browser draw did not finish as exactly one tick and one presented draw");
    state.complete=true;
    first_css_draw_terminal=true;
    running=false;menu_clock.reset();
    first_css_browser_draw_refresh_observation();
   }
  }
#else
  source_frames.finish(present_source);
#endif
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  replay_boundary_mark("source_frames_finish_returned",static_cast<int>(source_frames.steps()),
                       static_cast<int>(source_frames.draws()),static_cast<int>(replay_cursor));
#endif
  if(!first_css_draw_terminal)(void)prepare_deferred_pipelines();
  // Camera callbacks mutate source state (including magnifier damage flags).
  // A callback without a source tick retains the last match image when the
  // renderer guarantees complete draws. Menu priming retains its existing path.
  if(!first_css_draw_terminal&&source_frames.steps()==0&&
     (preparation.preparation_draws_source()||(!world&&!match&&!results&&!prize)))present_source();
  if(!first_css_draw_terminal&&preparation.warming()&&!preparation.preparation_draws_source()){
   const double service_started=emscripten_get_now();
   check(aurora_pipeline_service_preparation(),"Renderer preparation overlapped an active frame");
   preparation_ms+=emscripten_get_now()-service_started;
  }
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  (void)melee_web::pipeline_preparation::status();
#endif
  check(!replay||source_frames.draws()==replay_draw_boundaries,
        "Reference replay source draws disagree with clock batch boundaries");
  if(replay&&replay->whole_session()&&!replay->diagnostic_entity_prefix&&replay_completion.final_input_drawn){
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
 }catch(const std::exception& e){
  const std::string primary=e.what();
  abort_sudden_death_after_failure(primary);
  diagnostic_incident(4);running=false;faulted=true;preparation.reset();render_only_preparation=false;pending=false;clear_diagnostic_pad();clear_scheduled_results_pad();clear_scheduled_results_pauses();menu_clock.reset();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(first_sss_constructor_pair.armed&&first_sss_constructor_pair.kicked){
   first_sss_pair_fail(e.what());first_sss_pair_sync_native();
   first_sss_constructor_pair.actual_json=first_sss_pair_actual_json();
   first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }else if(first_css_final_pending_draw.armed&&first_css_final_pending_draw.kicked){
   if(!first_css_final_pending_draw.complete)
    first_css_final_pending_draw_fail(e.what());
   first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }else if(first_css_browser_postdraw_stream.armed){
   first_css_browser_postdraw_stream_fail(e.what());first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }else if(first_css_browser_draw.armed){
   first_css_browser_draw_fail(e.what());first_css_draw_terminal=true;
   first_css_browser_draw_refresh_observation();
  }
#endif
  message=e.what();if(preparation_started)preparation_ms=emscripten_get_now()-preparation_started;preparation_failed(e.what());timing_valid=0;std::fprintf(stderr,"Native menu: %s\n",e.what());
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
 if(!first_css_draw_terminal&&
    preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&running&&actual_source_draw&&
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
 if(replay_completed_now)EM_ASM({window.menuReplayCompleted?.($0,!!$1,$2,$3,$4,$5,$6,$7,$8,$9);},
                                replay_cursor,replay_match_complete?1:0,
                                replay_outcome,replay_winner,observed_replay_scene(),
                                replay&&replay->diagnostic_entity_prefix?(replay->diagnostic_active_entity_prefix?2:1):0,
                                replay_prefix_progress.observations,
                                replay_prefix_progress.first_source_tick,
                                replay_prefix_progress.last_source_tick,
                                replay?replay->diagnostic_source_observations():0);
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

#if defined(MELEE_WEB_MATCH_DRAW_DISCARD_DIAGNOSTIC)
static bool match_draw_discard_ready(){
 if(!match||faulted||pending||preparation.busy()||replay||melee_web_net_active()||
    results||prize||stock_check==-1||diagnostic_start_ticks||diagnostic_pad_remaining||
    !match->ready()||match->paused()||match->ending()||match->complete()||
    match->opening_demo()||match->sudden_death()||melee_web_source_file_drive_busy())return false;
 const auto* stats=aurora_get_stats();if(!stats||stats->queuedPipelines)return false;
 const auto& start=match->start_data();
 if(start.rules.stkind!=St_Kind_Last)return false;
 for(unsigned i=0;i<2;++i)
  if(start.players[i].ckind!=CKIND_MARIO||match->player_stats(i).stocks!=4)return false;
 for(unsigned i=2;i<6;++i)if(start.players[i].slot_type!=Gm_PKind_NA)return false;
 return true;
}
int melee_web_native_menu_match_draw_discard_arm(unsigned expected_frame){try{
 check(!match_draw_discard_terminal&&running&&match_draw_discard_ready(),
       "Draw/discard arm requires a local active Ready four-stock Mario/FD match");
 check(match->source_frames()==expected_frame,"Draw/discard source frame changed before arm");
 check(EM_ASM_INT({return globalThis.__matchDrawDiscard?.arm()===true?1:0;}),
       "Draw/discard observer absent, incomplete or already used");
 match_draw_discard_terminal=true;match_draw_discard_frame=expected_frame;
 running=false;emscripten_cancel_main_loop();
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_match_draw_discard_run(){
 bool began=false,ended=false;unsigned ticks=0,draws=0;
 AuroraBrowserMatchDrawDiscardSnapshot renderer{};
 PADStatus raw[4]{};
 const double started=emscripten_get_now();
 double tick_ms=0,begin_ms=0,draw_ms=0,end_ms=0;
 std::string error;
 try{
  check(match_draw_discard_terminal&&match_draw_discard_ready()&&
        match->source_frames()==match_draw_discard_frame,"Draw/discard owner changed after arm");
  check(EM_ASM_INT({return globalThis.__matchDrawDiscard?.phase==='ready'?1:0;}),
        "Draw/discard needs a completed real prior queue fence and no pending maps/async creates");
  check(aurora_browser_match_draw_discard_begin(),"Draw/discard renderer is busy, used or profiling-enabled");
  EM_ASM({globalThis.__matchDrawDiscard.begin();});
  const auto* input=melee_web_input_poll();
  std::copy_n(input->raw,4,raw);
  auto t=emscripten_get_now();match->tick(raw);tick_ms=emscripten_get_now()-t;++ticks;
  t=emscripten_get_now();began=aurora_begin_frame();begin_ms=emscripten_get_now()-t;
  check(began,"Draw/discard BeginFrame failed");
  GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
  t=emscripten_get_now();match->draw();draw_ms=emscripten_get_now()-t;++draws;
  t=emscripten_get_now();
  ended=aurora_browser_match_draw_discard_end_frame(&renderer)!=0;
  end_ms=emscripten_get_now()-t;check(ended,"Draw/discard finalization failed");
  check(match->source_frames()==match_draw_discard_frame+1,"Draw/discard source cursor did not advance exactly one");
 }catch(const std::exception& e){error=e.what();
  // Retire CPU recording even after a real source error; never report a success.
  if(began&&!ended)ended=aurora_browser_match_draw_discard_end_frame(&renderer)!=0;
 }
 const double finished=emscripten_get_now();
 EM_ASM({if(globalThis.__matchDrawDiscard.phase==='measuring')globalThis.__matchDrawDiscard.finish();});
 char numbers[1600];
 std::snprintf(numbers,sizeof(numbers),
  "{\"source_frame_before\":%u,\"source_frame_after\":%u,\"tick_calls_returned\":%u,\"draw_and_flow_present_calls_returned\":%u,"
  "\"tick_ms\":%.9g,\"begin_ms\":%.9g,\"draw_ms\":%.9g,\"end_ms\":%.9g,\"total_native_ms\":%.9g,"
  "\"ended\":%u,\"uncommitted_after_submit_callbacks\":%u,\"uncommitted_depth_copies\":%u,\"pending_depth_maps\":%u,"
  "\"depth_latest_reads\":%u,\"depth_requests\":%u,\"depth_identity_before\":\"%llu\",\"depth_identity_after\":\"%llu\","
  "\"raw_pad_bytes\":[",
  match_draw_discard_frame,match?match->source_frames():0,ticks,draws,tick_ms,begin_ms,draw_ms,end_ms,finished-started,
  renderer.ended,renderer.uncommittedAfterSubmitCallbacks,renderer.uncommittedDepthCopies,renderer.pendingDepthMaps,
  renderer.depthLatestReads,renderer.depthSnapshotRequests,
  static_cast<unsigned long long>(renderer.latestDepthIdentityBefore),
  static_cast<unsigned long long>(renderer.latestDepthIdentityAfter));
 match_draw_discard_receipt=numbers;
 const auto* bytes=reinterpret_cast<const unsigned char*>(raw);
 for(size_t i=0;i<sizeof(raw);++i){if(i)match_draw_discard_receipt+=',';match_draw_discard_receipt+=std::to_string(bytes[i]);}
 match_draw_discard_receipt+="],\"source_failed\":"+std::string(error.empty()?"false":"true")+
  ",\"browser_pcm_evolution\":\"UNMEASURED\",\"resumable\":false}";
 if(!error.empty())message=error;
 return error.empty()&&ticks==1&&draws==1&&ended?1:0;
}
const char* melee_web_native_menu_match_draw_discard_status(){
 static std::string status;
 status=match_draw_discard_receipt.empty()?"{}":match_draw_discard_receipt;
 status.pop_back();
 if(status.size()>1)status+=',';
 status+="\"native_retired\":"+std::string(match_draw_discard_native_retired?"true":"false")+"}";
 return status.c_str();
}
int melee_web_native_menu_match_draw_discard_retire(){try{
 check(!match_draw_discard_native_retired,"Draw/discard native owner already retired; close must not run twice");
 check(EM_ASM_INT({return typeof window.menuMatchDrawDiscardRetired==='function'?1:0;}),
       "Draw/discard runtime owner completion callback is unavailable");
 check(match_draw_discard_terminal&&EM_ASM_INT({return globalThis.__matchDrawDiscard?.phase==='fenced'?1:0;}),
       "Draw/discard retirement requires the real observed queue cleanup fence");
 check(!melee_web_source_file_drive_busy(),
       "Draw/discard retirement refuses source file requests created by the measured tick/draw");
 close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();
 match_draw_discard_native_retired=true;
 EM_ASM({globalThis.__matchDrawDiscard.retired();});
 check(EM_ASM_INT({return window.menuMatchDrawDiscardRetired()===true?1:0;}),
       "Draw/discard native retirement completed but runtime owner notification was refused");
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
#endif
int melee_web_native_menu_unload(){try{close();message="Native menus unloaded.";return 1;}catch(const std::exception& e){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 first_sss_text_tail_unload_refused(e.what());first_css_browser_draw_refresh_observation();
#endif
 message=e.what();return 0;}}
void melee_web_native_menu_pause(int paused){
 if(faulted||replay_final_draw||preparation.busy()||pending||(!host_entered&&!match&&!results&&!prize))return;
 diagnostic_incident(paused?5:6);
 running=(world||match||results||prize)&&!paused;menu_clock.reset();
 message=running?(match?match_message:results?"Original Results":prize?"Original unlock notification":source_menu_message()):"Paused.";
}
const char* melee_web_native_menu_message(){return message.c_str();}
int melee_web_native_menu_running(){return running;}
int melee_web_native_menu_cache_idle(){
 if(!render_cache_can_flush()){publish_graphics_startup(0,true);return 0;}
 const auto state=aurora_pipeline_cache_status();
 const int idle=state==AURORA_PIPELINE_CACHE_READY?1:state==AURORA_PIPELINE_CACHE_ERROR?-1:0;
 publish_graphics_startup(idle,true);
 return idle;
}
int melee_web_native_menu_phase(){
 if(match)return match->sudden_death()?14:7;
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
int melee_web_native_menu_stadium_first_css_draw_arm(
    const uint8_t* context_bytes,unsigned context_size,
    const uint8_t* consumed_pad_bytes,unsigned consumed_pad_size){try{
 auto& state=first_css_browser_draw;
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 check(false,"First-CSS browser draw requires the reviewed complete-draw pipeline build");
#endif
 check(!state.armed&&!stadium_c1a_armed&&scoped_assets&&scoped_disc_import&&
       asset_destination==AssetDestination::None&&!asset_committed&&
       asset_scope.pending_generation()==0&&world&&host&&!host_entered&&
       !world_exposed&&!match&&!results&&!prize&&!pending&&!running&&
       !replay&&!melee_web_net_active()&&diagnostic_start_ticks==0&&
       diagnostic_pad_remaining==0&&source_session_owned&&!preparation.busy()&&
       melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED&&
       melee_web_menu_host_mode_kind(host)==GM_VS,
       "First-CSS draw arm requires one prepared, unentered imported-disc VS host");
 auto context=melee_web::stadium_first_css_diagnostic::decode_context(
     context_bytes,context_size);
 auto consumed=melee_web::stadium_first_css_diagnostic::decode_consumed_pad(
     consumed_pad_bytes,consumed_pad_size,context.source_sha256);
 char error[256]{};
 check(melee_web_menu_host_apply_replay_context(
       host,context.seed,context.pad.data(),context.css.data(),context.ko.data(),
       context.rules.data(),context.save.data(),error,sizeof(error)),error);
 check(melee_web_menu_host_enable_stadium_c1a(host,error,sizeof(error)),error);
 check(melee_web_menu_host_arm_first_css_return(host,error,sizeof(error)),error);
 state.context=std::move(context);
 state.consumed=std::move(consumed);
 melee_web::stadium_first_css_diagnostic::decode_consumed_pad_statuses(
     state.consumed,state.consumed_status);
 state.armed=true;
 first_css_browser_draw_refresh_observation();
 message="Private first-CSS browser draw diagnostic armed.";
 return 1;
}catch(const std::exception& error){message=error.what();return 0;}}
int melee_web_native_menu_stadium_first_css_draw_kick(){try{
 auto& state=first_css_browser_draw;
 check(state.armed&&state.entry_captured&&!state.kicked&&!state.complete&&!state.failed&&
       host&&world&&host_entered&&state.source_steps==0&&state.host_tick_calls==0&&
       state.host_draw_calls==0&&state.aurora_begin_calls==0&&state.aurora_end_calls==0,
       "First-CSS draw kick requires its captured return before any source tick or draw");
 first_css_browser_draw_check_live("pre-kick");
 check(gm_801A4BA8()==0&&seed_ptr==state.seed_owner&&
       *state.seed_owner==state.entry.random_seed,
       "First-CSS draw kick lost the exact frame-zero RNG owner");
 state.kicked=true;
 menu_clock.reset();audio_clock.reset();running=true;
 first_css_browser_draw_refresh_observation();
 return 1;
}catch(const std::exception& error){first_css_browser_draw_fail(error.what());
 first_css_browser_draw_refresh_observation();return 0;}}
const char* melee_web_native_menu_stadium_first_css_draw_observe(){
 if(!first_css_browser_draw.armed)
  return first_sss_text_tail_retained_json.empty()
      ?nullptr:first_sss_text_tail_retained_json.c_str();
 first_css_browser_draw_refresh_observation();
 return first_css_browser_draw.observation.c_str();
}
int melee_web_native_menu_stadium_first_css_postdraw_stream_arm(
    const uint8_t* input_bytes,unsigned input_size){try{
 auto& baseline=first_css_browser_draw;
 auto& stream=first_css_browser_postdraw_stream;
 check(!stream.armed&&baseline.armed&&baseline.complete&&!baseline.failed&&
       baseline.entry_captured&&baseline.tick_captured&&baseline.draw_captured&&
       baseline.frame_end_returned&&baseline.source_steps==1&&baseline.source_draws==1&&
       baseline.host_tick_calls==1&&baseline.host_draw_calls==1&&
       baseline.aurora_begin_calls==1&&baseline.aurora_end_calls==1&&
       baseline.tick_result==1&&!running&&!faulted&&!pending&&!replay&&
       !melee_web_net_active()&&preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       !preparation.busy()&&asset_destination==AssetDestination::None&&
       world&&host&&host_entered&&!match&&!results&&!prize,
       "First-CSS post-draw stream arm requires the completed one-shot baseline");
 first_css_browser_draw_check_live("postdraw stream arm");
 check(gm_801A4BA8()==baseline.draw.scene_frame&&seed_ptr==baseline.seed_owner&&
       *baseline.seed_owner==baseline.draw.seed&&
       melee_web_current_scene_info()==baseline.scene_owner,
       "First-CSS post-draw stream cannot continue after an intervening source change");
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstCssStreamCompare===
                    'function'?1:0;})==1,
       "First-CSS post-draw stream host comparator is absent");
 auto input=melee_web::stadium_first_css_diagnostic::decode_postdraw_input(
     input_bytes,input_size,baseline.context.source_sha256);
 stream=FirstCssBrowserPostdrawStreamState{};
 stream.input=std::move(input);
 stream.armed=true;
 stream.executed_host_ticks=1;
 first_css_browser_draw_refresh_observation();
 message="Private first-CSS post-draw stream armed after the matched one-shot baseline.";
 return 1;
}catch(const std::exception& error){message=error.what();return 0;}}
int melee_web_native_menu_stadium_first_css_postdraw_stream_kick(){try{
 auto& baseline=first_css_browser_draw;
 auto& stream=first_css_browser_postdraw_stream;
 check(stream.armed&&!stream.kicked&&!stream.terminal&&!stream.failed&&
       baseline.armed&&baseline.complete&&!baseline.failed&&
       baseline.frame_end_returned&&!running&&host&&world&&host_entered&&
       stream.next_input_index==0&&stream.consumed_inputs==0&&
       stream.host_tick_calls==0&&stream.executed_host_ticks==1&&
       stream.matched_ticks==0&&stream.matched_draws==0,
       "First-CSS post-draw stream kick requires its unchanged completed baseline");
 first_css_browser_postdraw_stream_check_live(
     "postdraw stream pre-kick",baseline.draw.scene_frame);
 check(*baseline.seed_owner==baseline.draw.seed&&
       gm_801A4BA8()==baseline.draw.scene_frame,
       "First-CSS post-draw stream lost the baseline seed or scene frame");
 stream.kicked=true;
 menu_clock.reset();audio_clock.reset();running=true;
 first_css_browser_draw_refresh_observation();
 return 1;
}catch(const std::exception& error){
 first_css_browser_postdraw_stream_fail(error.what(),"kick-refused");
 first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_css_final_draw_arm(){try{
 auto& final_draw=first_css_final_pending_draw;
 check(!final_draw.armed&&!final_draw.failed&&!final_draw.complete&&
       !running&&!pending&&!faulted&&!replay&&!melee_web_net_active()&&
       host&&world&&host_entered&&!match&&!results&&!prize&&
       preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       !preparation.busy(),
       "Final CSS draw arm requires the stopped live CSS stream");
 final_draw.armed=true;
 first_css_final_pending_draw_check_live("final pending CSS draw arm");
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstCssFinalDrawCompare===
                    'function'?1:0;})==1,
       "Final CSS draw host comparator is absent");
 auto& stream=first_css_browser_postdraw_stream;
 char error[256]{};
 check(melee_web_menu_host_arm_final_pending_css_draw(
       host,148,1574,stream.input_status,error,sizeof(error)),error);
 first_css_browser_draw_refresh_observation();
 message="One final pending CSS draw armed after the retained terminal stream.";
 return 1;
}catch(const std::exception& error){
 first_css_final_pending_draw_fail(error.what());
 first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_css_final_draw_kick(){try{
 auto& final_draw=first_css_final_pending_draw;
 check(final_draw.armed&&!final_draw.kicked&&!final_draw.attempted&&
       !final_draw.failed&&!final_draw.complete&&!running&&!pending&&
       first_css_browser_postdraw_stream.terminal&&
       first_css_browser_postdraw_stream.outcome=="stop_after_last_input_request",
       "Final CSS draw kick requires its unchanged stopped terminal witness");
 first_css_final_pending_draw_check_live("final pending CSS draw pre-kick");
 final_draw.kicked=true;
 menu_clock.reset();audio_clock.reset();
 first_css_browser_draw_refresh_observation();
 return 1;
}catch(const std::exception& error){
 first_css_final_pending_draw_fail(error.what());
 first_css_browser_draw_refresh_observation();return 0;}}

int melee_web_native_menu_stadium_first_sss_draw_arm(){try{
 auto& s=first_sss_draw_sequence;
 check(!s.armed&&!s.failed&&!s.attempted&&!running&&!pending&&!faulted&&host&&world&&host_entered&&
       !match&&!results&&!prize&&!replay&&!melee_web_net_active()&&!menu_scene_rebuild_pending&&
       !preparation.busy()&&preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       asset_destination==AssetDestination::None&&first_sss_constructor_pair.complete&&
       first_sss_constructor_pair.compared&&!first_sss_constructor_pair.failed&&
       first_sss_constructor_pair.native.state==2&&first_sss_consumed_tick.complete&&
       first_sss_consumed_tick.compared&&!first_sss_consumed_tick.failed,
       "First SSS draw arm requires its unchanged approved first tick");
 s.armed=true;first_sss_frozen_pair_json=first_sss_pair_actual_json();s.audio_render_calls_before=s.audio_render_calls_after=diagnostic_audio_render_calls;
 s.audio_render_frames_before=s.audio_render_frames_after=diagnostic_audio_render_frames;
 s.audio_phase_before=s.audio_phase_after=audio_phase;
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstSssDrawCompare==='function'?1:0;})==1,"SSS first-draw comparator absent");
 char error[256]{};
 check(melee_web_menu_host_arm_first_sss_draw(host,first_sss_sequence_compare,nullptr,error,sizeof error),error);
 first_sss_sequence_sync(false);first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_sequence_fail(false,error.what());first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_draw_kick(){try{
 auto& s=first_sss_draw_sequence;
 check(s.armed&&!s.kicked&&!s.attempted&&!s.failed&&!s.complete&&!running&&!pending&&host&&world&&host_entered,
       "First SSS draw kick requires its stopped one-use arm");
 s.kicked=true;menu_clock.reset();first_use_draw_pending=false;
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_sequence_fail(false,error.what());first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe){try{
 auto& s=first_sss_vs_setup;
 check(!s.attempted&&!s.failed&&!s.complete,"VS Setup continuation is one-use");
 matched_setup_cf=MatchedSetupCfState{};
 check(text_tail_probe==0||text_tail_probe==1,"VS Setup text probe flag must be 0 or 1");
 first_sss_text_tail_probe=FirstSssTextTailProbeState{};
 first_sss_text_tail_retained_json.clear();
 first_sss_text_tail_probe.opted_in=text_tail_probe==1;
 s.attempted=true;
 s.audio_calls_before=s.audio_calls_after=diagnostic_audio_render_calls;
 s.audio_frames_before=s.audio_frames_after=diagnostic_audio_render_frames;
 const auto eligible=[&](){return !s.failed&&!running&&!pending&&!faulted&&host&&!host_entered&&
   world&&!match&&!results&&!prize&&!replay&&!melee_web_net_active()&&!stadium_c1a_armed&&
   scoped_assets&&scoped_disc_import&&!menu_scene_rebuild_pending&&!preparation.busy()&&
   preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
   asset_destination==AssetDestination::None&&first_sss_vs_entry.complete&&!first_sss_vs_entry.failed&&
   first_sss_prefix_sequence.complete&&!first_sss_prefix_sequence.failed&&
   melee_web_menu_host_source_scene(host)==0&&melee_web_menu_host_phase(host)==MELEE_WEB_MENU_READY;};
 check(eligible(),"VS Setup requires its stopped approved Entry host");
 check(EM_ASM_INT({try{const f=globalThis.__meleeWebStadiumFirstSssVsEntryApproved;
   return typeof f==='function'&&f()===true?1:0;}catch(_){return 0;}})==1,
   "VS Setup requires strict host Entry approval");
 check(eligible(),"VS Setup eligibility changed during host approval");
 MeleeWebMenuMatchSelection selection{};char error[256]{};
 ++s.getter_calls;
 check(melee_web_menu_host_stadium_c1a_selection(host,&selection,error,sizeof error),error);
 std::ostringstream selected;selected<<"{\"phase\":\"vs_entry\",\"random_seed_hex\":\""
   <<melee_web::stadium_first_css_diagnostic::hex32(selection.random_seed)<<"\",\"start\":";
 melee_web::stadium_first_css_diagnostic::write_start(selected,selection.start);selected<<'}';
 check(selected.str()==first_sss_vs_entry.actual,"VS Setup selection changed from approved actual Entry");
 s.host_input=melee_web_menu_host_input(host);
 check(melee_web_pad_state_encode(s.host_input,s.input_bytes.data(),s.input_bytes.size()),
       "VS Setup could not serialize retained source PAD history");
 s.owned_input.reset(melee_web_pad_state_decode(s.input_bytes.data(),s.input_bytes.size(),error,sizeof error));
 check(s.owned_input!=nullptr,error);s.input=s.owned_input.get();
 first_css_browser_draw_refresh_observation();s.history=first_css_browser_draw.observation;
 const auto setup_key=s.history.rfind(",\"first_sss_vs_setup\":");
 check(setup_key!=std::string::npos,"VS Setup prerequisite snapshot is missing");
 s.history.resize(setup_key);s.history+='}';
 s.prior_world_generation=melee_web_gameplay_generation();
 s.parent_seed_owner=seed_ptr;check(s.parent_seed_owner!=nullptr,"VS Setup parent RNG owner is missing");
 s.parent_seed=*s.parent_seed_owner;
 world->close();world.reset();world_exposed=false;s.menu_retired=true;
 s.parent_seed_after_retirement=seed_ptr?*seed_ptr:0;
 s.parent_seed_restored=seed_ptr==s.parent_seed_owner&&s.parent_seed_after_retirement==s.parent_seed;
 check(!melee_web_gameplay_world_exists(),"VS Setup menu world retirement did not complete");
 check(s.parent_seed_restored,"VS Setup menu retirement changed parent RNG owner/value");
 request_assets(AssetDestination::FirstSssVsSetup,&selection);
 s.requested_generation=asset_generation;s.requested=true;
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_vs_setup_fail(error.what());return 0;}}

int matched_setup_cf_arm_internal(bool version2,unsigned action_count,
    unsigned c_action_index,unsigned f_action_index,unsigned c_source_tick,
    unsigned f_source_tick,bool full_schedule=false,bool common_fields=false){try{
 auto& state=matched_setup_cf;auto& setup=first_sss_vs_setup;
 stadium_recreation_b_require_arm(version2,full_schedule,common_fields,action_count);
 check(!state.opted_in&&!state.armed&&!state.failed&&!state.complete,
       "Matched Setup C/F continuation is one-use");
 state.opted_in=true;
 state.version2=version2;
 state.full_schedule=full_schedule;
 state.common_fields=common_fields;
 check(!common_fields||(version2&&full_schedule),
       "Per-tick common fields require the explicit full-schedule v2 owner");
 state.action_count=version2?action_count:171U;
 state.c_action_index=version2?c_action_index:168U;
 state.f_action_index=version2?f_action_index:170U;
 state.c_source_tick=version2?c_source_tick:84U;
 state.f_source_tick=version2?f_source_tick:85U;
 state.source_ticks=state.source_draws=0;
 state.queued_source_frame_before=state.last_source_frame_after=0;
 check(!version2||(action_count==state.version2_actions.size()&&action_count>=5&&
       action_count<=7167&&c_action_index<f_action_index&&
       (full_schedule?(action_count==7167&&c_action_index==168&&f_action_index==170&&
         c_source_tick==84&&f_source_tick==85):f_action_index+3==action_count-1)&&
       c_source_tick+1==f_source_tick),
       "Matched Setup C/F v2 immutable prefix dimensions are invalid");
 check(setup.attempted&&setup.complete&&!setup.failed&&setup.construction_complete&&
       setup.source_frames==0&&!running&&!pending&&!faulted&&!host_entered&&
       host&&match&&archive_cache&&match->construction_complete()&&
       match->source_frames()==0&&asset_generation==setup.committed_generation&&
       !first_use_draw_pending&&!replay&&!melee_web_net_active(),
       "Matched Setup C/F arm requires the completed stopped same-run Setup owner");
 const auto* const expected_owner=match.get();
 const auto* const expected_cache=archive_cache.get();
 const auto* const expected_host=host;
 const auto expected_asset_generation=asset_generation;
 const auto expected_world=melee_web_gameplay_stats();
 const auto expected_setup=setup.setup_snapshot;
 const uint32_t expected_seed=match->random_seed();
 check(expected_world.ticks==0,"Matched Setup C/F arm lost its zero-tick world");
 check(EM_ASM_INT({try{const f=globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved;
   return typeof f==='function'&&f()===true?1:0;}catch(_){return 0;}})==1,
   "Matched Setup C/F arm requires strict same-run Setup approval");
 check(state.opted_in&&!state.armed&&!state.failed&&!state.complete&&
       setup.complete&&!setup.failed&&match&&match.get()==expected_owner&&
       archive_cache&&archive_cache.get()==expected_cache&&host==expected_host&&
       !host_entered&&!running&&!pending&&!faulted&&!first_use_draw_pending&&
       asset_generation==expected_asset_generation&&melee_web_gameplay_world_exists()&&
       melee_web_gameplay_generation()==expected_world.generation&&
       melee_web_gameplay_stats().ticks==0,
       "Matched Setup C/F host approval changed the stopped source owner");
 check(match->random_seed()==expected_seed,
       "Matched Setup C/F host approval changed the source RNG");
 MeleeWebStadiumSetupSnapshot current{};char error[256]{};
 check(match->diagnostic_stadium_setup_snapshot(&current,error,sizeof(error)),
       error[0]?error:"Matched Setup C/F zero-tick owner snapshot refused");
 check(matched_setup_cf_setup_equal(current,expected_setup)&&
       matched_setup_cf_setup_equal(setup.setup_snapshot,expected_setup)&&
       current.world_ticks==0&&current.source_frame==0&&current.world_generation!=0,
       "Matched Setup C/F zero-tick owner differs from the strict Setup receipt");
 state.owner=match.get();state.cache_owner=archive_cache.get();state.host_owner=host;
 state.world_generation=current.world_generation;
 state.asset_generation=asset_generation;
 state.expected_branch=match->start_data().rules.x1_3?1U:0U;
 check(state.expected_branch==0,
       "Matched Setup C/F bounded continuation supports only the approved branch-0 profile");
 match->diagnostic_stadium_go_alignment_arm(0,error,sizeof(error));
 state.audio_calls_before=state.audio_calls_after=diagnostic_audio_render_calls;
 state.audio_frames_before=state.audio_frames_after=diagnostic_audio_render_frames;
 state.audio_phase_before=state.audio_phase_after=audio_phase;
 state.armed=true;
 matched_setup_cf_check_owner(0,version2?0U:~0U);
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm(){
 return matched_setup_cf_arm_internal(false,171U,168U,170U,84U,85U);
}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm(
    const uint8_t* manifest,size_t manifest_bytes){try{
 auto& state=matched_setup_cf;
 check(manifest&&manifest_bytes>=24,"Matched Setup C/F v2 manifest header is absent");
 check(manifest[0]=='M'&&manifest[1]=='W'&&manifest[2]=='P'&&manifest[3]=='2',
       "Matched Setup C/F v2 manifest magic differs");
 const auto read32=[&](size_t offset){
  check(offset+4<=manifest_bytes,"Matched Setup C/F v2 manifest integer is truncated");
  return static_cast<uint32_t>(manifest[offset])|
      (static_cast<uint32_t>(manifest[offset+1])<<8)|
      (static_cast<uint32_t>(manifest[offset+2])<<16)|
      (static_cast<uint32_t>(manifest[offset+3])<<24);
 };
 const unsigned action_count=read32(4),c_index=read32(8),f_index=read32(12),
                c_tick=read32(16),f_tick=read32(20);
 constexpr size_t header_bytes=24,action_bytes=57;
 check(action_count>=5&&action_count<=7167&&
       manifest_bytes==header_bytes+static_cast<size_t>(action_count)*action_bytes,
       "Matched Setup C/F v2 manifest count or length is invalid");
 check(c_index<f_index&&f_index+3==action_count-1&&c_tick+1==f_tick,
       "Matched Setup C/F v2 C/F/terminal coordinates are inconsistent");
 std::vector<MatchedSetupCfState::ManifestAction> actions;actions.reserve(action_count);
 unsigned ticks=0,draws=0;
 for(unsigned index=0;index<action_count;++index){
  const size_t offset=header_bytes+static_cast<size_t>(index)*action_bytes;
  MatchedSetupCfState::ManifestAction action{};
  action.kind=manifest[offset];action.ordinal=read32(offset+1);
  action.bundle=read32(offset+5);action.draws_before=read32(offset+9);
  std::copy(manifest+offset+13,manifest+offset+action_bytes,action.pad.begin());
  if(action.kind==1){
   check(action.ordinal==ticks&&action.bundle==ticks&&action.draws_before==draws,
         "Matched Setup C/F v2 tick descriptor is discontinuous");
   ++ticks;
  }else if(action.kind==2){
   check(action.ordinal==draws&&action.bundle==UINT32_MAX&&action.draws_before==draws&&
         std::all_of(action.pad.begin(),action.pad.end(),[](uint8_t byte){return byte==0;}),
         "Matched Setup C/F v2 draw descriptor is malformed");
   ++draws;
  }else throw std::runtime_error("Matched Setup C/F v2 action kind is unknown");
  actions.push_back(action);
 }
 check(actions[c_index].kind==1&&actions[c_index].ordinal==c_tick&&
       actions[f_index].kind==1&&actions[f_index].ordinal==f_tick,
       "Matched Setup C/F v2 C/F descriptors do not match their declared source ticks");
 check(std::none_of(actions.begin()+c_index+1,actions.begin()+f_index,
       [](const auto& action){return action.kind==1;}),
       "Matched Setup C/F v2 inserted another tick between C and F");
 check(actions[f_index+1].kind==2&&actions[f_index+2].kind==1&&
       actions[f_index+3].kind==2,
       "Matched Setup C/F v2 terminal is not the required next draw/tick/draw");
 check(!state.opted_in&&!state.armed&&!state.failed&&!state.complete,
       "Matched Setup C/F v2 continuation is one-use");
 state.version2_actions=std::move(actions);
 return matched_setup_cf_arm_internal(true,action_count,c_index,f_index,c_tick,f_tick);
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

// Explicit full run2 scope; the existing v2 prefix decoder above is unchanged.
int matched_setup_cf_full_manifest_arm(
    const uint8_t* manifest,size_t manifest_bytes){try{
 auto& state=matched_setup_cf;
 check(manifest&&manifest_bytes>=24,"Matched Setup full schedule manifest header is absent");
 const bool common_fields=manifest[0]=='M'&&manifest[1]=='W'&&
       manifest[2]=='C'&&manifest[3]=='5';
 check((manifest[0]=='M'&&manifest[1]=='W'&&manifest[2]=='F'&&manifest[3]=='3')||
       common_fields,
       "Matched Setup full schedule manifest magic differs");
 const auto read32=[&](size_t offset){
  check(offset+4<=manifest_bytes,"Matched Setup full schedule manifest integer is truncated");
  return static_cast<uint32_t>(manifest[offset])|
      (static_cast<uint32_t>(manifest[offset+1])<<8)|
      (static_cast<uint32_t>(manifest[offset+2])<<16)|
      (static_cast<uint32_t>(manifest[offset+3])<<24);
 };
 const unsigned action_count=read32(4),c_index=read32(8),f_index=read32(12),
                c_tick=read32(16),f_tick=read32(20);
 constexpr size_t header_bytes=24,action_bytes=57;
 check(action_count==7167&&
       manifest_bytes==header_bytes+static_cast<size_t>(action_count)*action_bytes,
       "Matched Setup full schedule manifest count or length is invalid");
 check(c_index==168&&f_index==170&&c_tick==84&&f_tick==85,
       "Matched Setup full schedule C/F/terminal coordinates are inconsistent");
 std::vector<MatchedSetupCfState::ManifestAction> actions;actions.reserve(action_count);
 unsigned ticks=0,draws=0;
 for(unsigned index=0;index<action_count;++index){
  const size_t offset=header_bytes+static_cast<size_t>(index)*action_bytes;
  MatchedSetupCfState::ManifestAction action{};
  action.kind=manifest[offset];action.ordinal=read32(offset+1);
  action.bundle=read32(offset+5);action.draws_before=read32(offset+9);
  std::copy(manifest+offset+13,manifest+offset+action_bytes,action.pad.begin());
  if(action.kind==1){
   check(action.ordinal==ticks&&action.bundle==ticks&&action.draws_before==draws,
         "Matched Setup full schedule tick descriptor is discontinuous");
   ++ticks;
  }else if(action.kind==2){
   check(action.ordinal==draws&&action.bundle==UINT32_MAX&&action.draws_before==draws&&
         std::all_of(action.pad.begin(),action.pad.end(),[](uint8_t byte){return byte==0;}),
         "Matched Setup full schedule draw descriptor is malformed");
   ++draws;
  }else throw std::runtime_error("Matched Setup full schedule action kind is unknown");
  actions.push_back(action);
 }
 check(ticks==3585&&draws==3582&&actions.back().kind==2&&actions.back().ordinal==3581,
       "Full schedule tick/draw terminal dimensions differ");
 check(actions[c_index].kind==1&&actions[c_index].ordinal==c_tick&&
       actions[f_index].kind==1&&actions[f_index].ordinal==f_tick,
       "Matched Setup full schedule C/F descriptors do not match their declared source ticks");
 check(std::none_of(actions.begin()+c_index+1,actions.begin()+f_index,
       [](const auto& action){return action.kind==1;}),
       "Matched Setup full schedule inserted another tick between C and F");
 check(actions[f_index+1].kind==2&&actions[f_index+2].kind==1&&
       actions[f_index+3].kind==2,
       "Matched Setup full schedule terminal is not the required next draw/tick/draw");
 check(!state.opted_in&&!state.armed&&!state.failed&&!state.complete,
       "Matched Setup full schedule continuation is one-use");
 state.version2_actions=std::move(actions);
 return matched_setup_cf_arm_internal(true,action_count,c_index,f_index,c_tick,f_tick,true,
                                      common_fields);
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_explicit_arm(
    const uint8_t* manifest,size_t manifest_bytes){try{
 auto& state=matched_setup_cf;
 check(!state.explicit_step_mode&&!state.explicit_step_inflight&&
       !matched_setup_cf_retirement.native_retired&&
       matched_setup_cf_retirement.receipt.empty(),
       "Matched Setup C/F explicit-step owner is one-use");
 matched_setup_cf_retirement=MatchedSetupCfRetirement{};
 check(melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm(
       manifest,manifest_bytes)==1,
       "Matched Setup C/F v2 arm refused the explicit-step manifest");
 check(state.version2&&state.armed&&!state.failed&&!state.complete&&
       !state.queued&&!state.awaiting_approval,
       "Matched Setup C/F explicit-step arm did not retain a fresh v2 owner");
 state.explicit_step_mode=true;
 // Pause and clear MainLoop.func. Asyncify resumes an exported call through
 // MainLoop.resume when a function remains installed, which would re-enable
 // RAF admission alongside the explicit host-owned step sequence.
 emscripten_cancel_main_loop();
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm(
    const uint8_t* manifest,size_t manifest_bytes){try{
 auto& state=matched_setup_cf;
 check(!state.explicit_step_mode&&!state.explicit_step_inflight&&
       !matched_setup_cf_retirement.native_retired&&
       matched_setup_cf_retirement.receipt.empty(),
       "Matched Setup C/F explicit-step owner is one-use");
 matched_setup_cf_retirement=MatchedSetupCfRetirement{};
 check(matched_setup_cf_full_manifest_arm(
       manifest,manifest_bytes)==1,
       "Matched Setup C/F v2 arm refused the explicit-step manifest");
 check(state.version2&&state.full_schedule&&state.armed&&!state.failed&&!state.complete&&
       !state.queued&&!state.awaiting_approval,
       "Matched Setup C/F explicit-step arm did not retain a fresh v2 owner");
 state.explicit_step_mode=true;
 // Pause and clear MainLoop.func. Asyncify resumes an exported call through
 // MainLoop.resume when a function remains installed, which would re-enable
 // RAF admission alongside the explicit host-owned step sequence.
 emscripten_cancel_main_loop();
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once(){try{
 auto& state=matched_setup_cf;
 check(state.explicit_step_mode&&state.version2&&state.armed&&!state.failed&&
       !state.complete&&!state.explicit_step_inflight&&state.queued&&
       !state.awaiting_approval&&state.cursor==state.last_schedule_index,
       "Matched Setup C/F explicit step is outside its one-use queued cursor");
 state.explicit_step_inflight=true;
 // Match tick()'s owner/lifecycle handoff before allowing any source work.
 const int owner_action=EM_ASM_INT({return window.menuServiceCommands?.() || 0;});
 if(owner_action<0){
  matched_setup_cf_fail("Matched Setup C/F owner stopped before its explicit action");
  running=false;faulted=true;menu_clock.reset();audio_clock.reset();
  emscripten_cancel_main_loop();state.explicit_step_inflight=false;return 0;
 }
 if(owner_action==1){menu_clock.reset();audio_clock.reset();}
 // This is the existing single-row service. It owns the original PAD/tick or
 // Aurora draw path, source checks, and complete receipt; no tick is batched.
 (void)matched_setup_cf_service();
 state.explicit_step_inflight=false;
 check(!state.failed&&!state.queued&&state.awaiting_approval&&
       state.last_action_complete&&state.cursor==state.last_schedule_index,
       "Matched Setup C/F explicit service returned without one completed pending receipt");
 return 1;
}catch(const std::exception& error){
 matched_setup_cf.explicit_step_inflight=false;
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready(){
 auto& state=matched_setup_cf;
 if(EM_ASM_INT({return !ABORT&&typeof Module.stadiumMatchedSetupNativeIdle==='function'&&
                        Module.stadiumMatchedSetupNativeIdle()===true?1:0;})!=1) return -1;
 const bool terminal=state.complete||state.failed||
                     (state.awaiting_approval&&state.last_action_complete);
 if(!state.explicit_step_mode||!state.version2||!state.armed||state.explicit_step_inflight||
    !terminal||state.queued||!match||match.get()!=state.owner||!archive_cache||
    archive_cache.get()!=state.cache_owner||!host||host!=state.host_owner||host_entered||
    running||pending||(faulted&&!state.failed)||first_use_draw_pending||preparation.busy()||
    preparation.phase()!=melee_web::MenuPreparationState::Phase::Idle||
    asset_destination!=AssetDestination::None||menu_scene_rebuild_pending||replay||
    melee_web_net_active()||melee_web_source_file_drive_busy()||
    !melee_web_gameplay_world_exists()||melee_web_gameplay_generation()!=state.world_generation||
    asset_generation!=state.asset_generation||!match->construction_complete()||
    match->source_frames()!=state.last_source_frame_after) return -1;
 const auto submission=aurora_browser_submission_status();
 const auto* stats=aurora_get_stats();
 if(!stats)return -1;
 return stats->queuedPipelines==0&&submission.pendingFramePackets==0&&
        submission.pendingStagingBuffers==0&&submission.workerBusy==0?1:0;
}
const char* melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status(){
 static std::string status;status=matched_setup_cf_retirement_json();return status.c_str();
}
int melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire(){try{
 check(!matched_setup_cf_retirement.native_retired&&
       matched_setup_cf_retirement.receipt.empty(),
       "Matched Setup C/F retirement is one-use");
 check(melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready()==1,
       "Matched Setup C/F retirement requires a terminal owner and settled renderer");
 check(EM_ASM_INT({return typeof window.menuMatchedSetupCfRetired==='function'?1:0;}),
       "Matched Setup C/F runtime retirement callback is unavailable");
 auto& state=matched_setup_cf;
 matched_setup_cf_retirement.terminal=state.complete||state.failed||
     (state.awaiting_approval&&state.last_action_complete);
 matched_setup_cf_retirement.receipt=matched_setup_cf_json();
 stadium_recreation_b_before_retire();
 matched_setup_cf_retirement.pipeline_cache_before=
     static_cast<int>(aurora_pipeline_cache_status());
 // This diagnostic never resumes the regular loop or calls another source step.
 // The exact terminal receipt above survives close(), which resets the live C/F owner.
 emscripten_cancel_main_loop();
 close();
 matched_setup_cf_retirement.phase_after_close=melee_web_native_menu_phase();
 matched_setup_cf_retirement.running_after_close=melee_web_native_menu_running();
 check(!match&&!world&&!results&&!prize&&!host&&
       matched_setup_cf_retirement.phase_after_close==0&&
       matched_setup_cf_retirement.running_after_close==0,
       "Matched Setup C/F native close left a live owner");
 stadium_recreation_b_after_close();
 melee_web_input_shutdown();
 // Aurora shutdown retains its existing opportunistic writer behavior. JS IDBFS
 // persistence and the top-level render-cache flush boundary remain unrun here.
 aurora_shutdown();
 matched_setup_cf_retirement.pipeline_cache_after=
     static_cast<int>(aurora_pipeline_cache_status());
 matched_setup_cf_retirement.native_retired=true;
 matched_setup_cf_retirement.error.clear();
 const std::string status=matched_setup_cf_retirement_json();
 check(EM_ASM_INT({return window.menuMatchedSetupCfRetired(
       JSON.parse(UTF8ToString($0)))===true?1:0;},status.c_str()),
       "Matched Setup C/F native retirement completed but runtime notification was refused");
 return 1;
}catch(const std::exception& error){
 if(matched_setup_cf_retirement.error.empty())matched_setup_cf_retirement.error=error.what();
 message=error.what();return 0;
}}
} // extern "C"
namespace melee_web_menu_browser {
// Private recreation prerequisite. This is separate from terminal retirement:
// its receipt survives close(), and only an acknowledged returned boundary can
// restart the ordinary ownerless callback needed by the existing import route.
struct StadiumRecreationCanary {
 bool attempted=false,closed=false,restarted=false;
 const uint32_t* expected_host_owner=nullptr;
 const uint32_t* expected_parent_owner=nullptr;
 uint64_t prior_generation=0;
 uint32_t match_seed=0,host_seed=0,parent_seed=0;
 bool match_to_host_restored=false,host_to_parent_restored=false;
 bool b_admitted=false,b_retiring=false,b_match_restored=false,b_parent_restored=false,b_terminal_checked=false;
 bool b_full_common_fields_seed=false;
 uint64_t b_generation=0;
 uint32_t b_asset_generation=0,b_match_seed_before=0,b_match_seed_at_retirement=0,b_host_seed=0,b_parent_seed=0;
 const melee_web::GameplayMatchSession* b_match_owner=nullptr;
 const melee_web::RuntimeArchiveCache* b_cache_owner=nullptr;
 const MeleeWebMenuHost* b_host_owner=nullptr;
 const uint32_t* b_expected_host_owner=nullptr;
 const uint32_t* b_expected_parent_owner=nullptr;
 std::string action_receipt,error;
};
StadiumRecreationCanary stadium_recreation_canary;
void stadium_recreation_b_require_arm(bool version2,bool full_schedule,bool common_fields,unsigned action_count){
 const auto& s=stadium_recreation_canary;if(!s.b_admitted)return;
 check(version2&&(s.b_full_common_fields_seed
       ? (full_schedule&&common_fields&&action_count==7167)
       : (!full_schedule&&!common_fields&&action_count==174)),
       "Stadium B arm differs from its explicit admitted lifetime scope");
}

void stadium_recreation_match_retired(){
 auto& s=stadium_recreation_canary;
 if(s.b_admitted){
  check(s.b_retiring&&!s.b_terminal_checked,"Stadium B match close is outside checked terminal retirement");
  s.b_match_restored=seed_ptr==s.b_expected_host_owner&&*seed_ptr==s.b_host_seed;
  check(s.b_match_restored,"Stadium B match retirement changed parent RNG owner/value");
  return;
 }
 if(!s.attempted||s.closed||s.restarted||!s.error.empty())return;
 s.match_to_host_restored=seed_ptr==s.expected_host_owner&&*seed_ptr==s.host_seed;
 check(s.match_to_host_restored,"Stadium recreation match retirement changed its parent RNG owner/value");
}
std::string stadium_recreation_canary_json(){
 const auto& s=stadium_recreation_canary;std::ostringstream out;
 out<<"{\"schema\":\"melee-web-stadium-recreation-boundary-v1\",\"attempted\":"
    <<(s.attempted?"true":"false")<<",\"closed\":"<<(s.closed?"true":"false")
    <<",\"restarted\":"<<(s.restarted?"true":"false")
    <<",\"prior_world_generation\":"<<s.prior_generation
    <<",\"match_seed\":"<<s.match_seed<<",\"host_seed\":"<<s.host_seed
    <<",\"parent_seed\":"<<s.parent_seed
    <<",\"match_to_host_restored\":"<<(s.match_to_host_restored?"true":"false")
    <<",\"host_to_parent_restored\":"<<(s.host_to_parent_restored?"true":"false")
    <<",\"matched_setup_cf\":"<<(s.action_receipt.empty()?"null":s.action_receipt)
    <<",\"error\":";
 out<<(s.error.empty()?"null":first_css_browser_draw_json_quote(s.error));
 const auto b=stadium_recreation_b_json();if(!b.empty())out<<",\"lifetime_b\":"<<b;
 out<<'}';return out.str();
}
std::string stadium_recreation_b_json(){
 const auto& s=stadium_recreation_canary;if(!s.b_admitted)return {};
 std::ostringstream out;out<<"{\"schema\":\"melee-web-stadium-recreation-b-rng-v1\",\"admitted\":true"
   <<",\"world_generation\":"<<s.b_generation<<",\"prior_world_generation\":"<<s.prior_generation
   <<",\"match_seed_before\":"<<s.b_match_seed_before<<",\"match_seed_at_retirement\":"<<s.b_match_seed_at_retirement
   <<",\"host_seed\":"<<s.b_host_seed<<",\"parent_seed\":"<<s.b_parent_seed
   <<",\"match_to_host_restored\":"<<(s.b_match_restored?"true":"false")
   <<",\"host_to_parent_restored\":"<<(s.b_parent_restored?"true":"false")
   <<",\"terminal_checked\":"<<(s.b_terminal_checked?"true":"false");
 if(s.b_full_common_fields_seed)out<<",\"full_common_fields_seed\":true";
 out<<'}';return out.str();
}
} // namespace melee_web_menu_browser
extern "C" {
int melee_web_native_menu_stadium_recreation_admit_b(){try{
 auto& s=stadium_recreation_canary;const auto& setup=first_sss_vs_setup;
 check(s.closed&&s.restarted&&s.error.empty()&&!s.b_admitted&&!s.b_terminal_checked&&
       setup.complete&&!setup.failed&&match&&host&&!host_entered&&archive_cache&&
       !running&&!pending&&!faulted&&!preparation.busy()&&!menu_scene_rebuild_pending&&
       !melee_web_source_file_drive_busy()&&match->construction_complete()&&match->source_frames()==0&&
       !matched_setup_cf.opted_in&&!matched_setup_cf.armed&&
       setup.match_world_generation>s.prior_generation&&
       melee_web_gameplay_generation()==setup.match_world_generation&&
       asset_generation==setup.committed_generation,
       "Stadium B admission requires the fresh compared Setup owner/generation");
 MeleeWebMenuHostRngBoundary rng{};uint32_t seed=0;
 check(melee_web_menu_host_rng_boundary(host,&rng)==1&&rng.host_owner==setup.parent_seed_owner&&
       rng.host_value==setup.parent_seed&&rng.parent_owner==s.expected_parent_owner&&
       rng.parent_value==s.parent_seed&&melee_web_match_rng_seed_owner_read(&seed)==1,
       "Stadium B admission lost match/host/outer-parent RNG identity or value");
 s.b_generation=setup.match_world_generation;s.b_asset_generation=asset_generation;
 s.b_match_owner=match.get();s.b_cache_owner=archive_cache.get();s.b_host_owner=host;
 s.b_expected_host_owner=rng.host_owner;s.b_expected_parent_owner=rng.parent_owner;
 s.b_host_seed=rng.host_value;s.b_parent_seed=rng.parent_value;s.b_match_seed_before=seed;s.b_admitted=true;
 return 1;
}catch(const std::exception& error){
 if(stadium_recreation_canary.error.empty())stadium_recreation_canary.error=error.what();message=error.what();return 0;
}}
int melee_web_native_menu_stadium_recreation_admit_full_b(){
 // Reuse every original fresh-owner/RNG admission check; bind only the explicit
 // full common-fields+seed intent before any source action is armed.
 if(melee_web_native_menu_stadium_recreation_admit_b()!=1)return 0;
 stadium_recreation_canary.b_full_common_fields_seed=true;
 return 1;
}
} // extern "C"
namespace melee_web_menu_browser {
void stadium_recreation_b_before_retire(){
 auto& s=stadium_recreation_canary;if(!s.b_admitted)return;
 const auto& cf=matched_setup_cf;MeleeWebMenuHostRngBoundary rng{};
 check(s.closed&&s.restarted&&s.error.empty()&&!s.b_retiring&&!s.b_terminal_checked&&
       cf.complete&&!cf.failed&&!cf.awaiting_approval&&cf.cursor==cf.action_count&&
       (s.b_full_common_fields_seed
        ? (cf.version2&&cf.full_schedule&&cf.common_fields&&cf.action_count==7167&&
           cf.source_ticks==3585&&cf.source_draws==3582)
        : (cf.cursor==174&&cf.action_count==174&&!cf.full_schedule))&&
       match.get()==s.b_match_owner&&archive_cache.get()==s.b_cache_owner&&host==s.b_host_owner&&
       cf.world_generation==s.b_generation&&melee_web_gameplay_generation()==s.b_generation&&
       cf.asset_generation==s.b_asset_generation&&asset_generation==s.b_asset_generation&&
       melee_web_menu_host_rng_boundary(host,&rng)==1&&rng.host_owner==s.b_expected_host_owner&&
       rng.host_value==s.b_host_seed&&rng.parent_owner==s.b_expected_parent_owner&&rng.parent_value==s.b_parent_seed&&
       melee_web_match_rng_seed_owner_read(&s.b_match_seed_at_retirement)==1,
       "Stadium B terminal retirement lost its admitted owner or RNG boundary");
 s.b_retiring=true;
}
void stadium_recreation_b_after_close(){
 auto& s=stadium_recreation_canary;if(!s.b_admitted)return;
 check(s.b_retiring&&s.b_match_restored&&!s.b_terminal_checked&&
       seed_ptr==s.b_expected_parent_owner&&*seed_ptr==s.b_parent_seed,
       "Stadium B terminal close changed outer-parent RNG owner/value");
 s.b_parent_restored=true;s.b_terminal_checked=true;
}
} // namespace melee_web_menu_browser
extern "C" {
const char* melee_web_native_menu_stadium_recreation_status(){
 static std::string json;json=stadium_recreation_canary_json();return json.c_str();
}
int melee_web_native_menu_stadium_recreation_close(){try{
 auto& s=stadium_recreation_canary;auto& cf=matched_setup_cf;
 check(!s.attempted&&!s.closed&&!s.restarted&&s.error.empty(),"Stadium recreation canary is one-use");
 check(melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready()==1&&
       cf.complete&&!cf.failed&&!cf.awaiting_approval&&cf.cursor==cf.action_count&&
       cf.version2&&!cf.full_schedule&&cf.action_count==174&&
       !matched_setup_cf_retirement.native_retired&&matched_setup_cf_retirement.receipt.empty(),
       "Stadium recreation requires an approved complete174-action original prefix");
 const auto* expected_match=match.get();const auto* expected_host=host;
 const auto* expected_cache=archive_cache.get();
 const auto expected_world_generation=cf.world_generation;
 const auto expected_asset_generation=cf.asset_generation;
 const auto expected_source_frame=cf.last_source_frame_after;
 check(EM_ASM_INT({try{return globalThis.__meleeWebStadiumRecreationCanary?.beforeClose()===true?1:0;}catch(_){return 0;}})==1,
       "Stadium recreation requires persisted immutable lifetime A approval");
 // Host callback must not advance or replace any native owner.
 check(melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready()==1&&
       match.get()==expected_match&&host==expected_host&&archive_cache.get()==expected_cache&&
       cf.world_generation==expected_world_generation&&melee_web_gameplay_generation()==expected_world_generation&&
       cf.asset_generation==expected_asset_generation&&asset_generation==expected_asset_generation&&
       cf.last_source_frame_after==expected_source_frame&&
       cf.complete&&!cf.failed&&!cf.awaiting_approval&&cf.cursor==cf.action_count,
       "Stadium recreation owner changed during host approval");
 MeleeWebMenuHostRngBoundary rng{};
 check(melee_web_menu_host_rng_boundary(host,&rng)==1&&
       rng.host_owner==first_sss_vs_setup.parent_seed_owner&&
       rng.host_value==first_sss_vs_setup.parent_seed&&
       melee_web_match_rng_seed_owner_read(&s.match_seed)==1,
       "Stadium recreation lost the match/host/parent RNG ownership chain");
 const void* arena=nullptr;size_t arena_bytes=0;
 check(melee_web_gameplay_session_arena(&arena,&arena_bytes)==1,
       "Stadium recreation requires its live source session arena");
 const auto parent=reinterpret_cast<uintptr_t>(rng.parent_owner),base=reinterpret_cast<uintptr_t>(arena);
 check(parent<base||parent-base>=arena_bytes,
       "Stadium recreation parent RNG owner is inside the retiring session arena");
 s.attempted=true;s.prior_generation=cf.world_generation;s.host_seed=rng.host_value;
 s.parent_seed=rng.parent_value;s.expected_host_owner=rng.host_owner;
 s.expected_parent_owner=rng.parent_owner;
 s.action_receipt=matched_setup_cf_json();
 // Preserve close's original report/match/host/session ordering. The private
 // hook checks the restored host immediately after its existing match close.
 close();
 s.host_to_parent_restored=seed_ptr==rng.parent_owner&&*seed_ptr==s.parent_seed;
 check(s.host_to_parent_restored&&!match&&!world&&!host&&!results&&!prize&&
       !source_session_owned&&!archive_cache&&!scoped_assets&&
       !melee_web_gameplay_world_exists()&&melee_web_native_menu_phase()==0&&
       melee_web_native_menu_running()==0,
       "Stadium recreation close left live owners or changed parent RNG");
 s.closed=true;return 1;
}catch(const std::exception& error){
 if(stadium_recreation_canary.error.empty())stadium_recreation_canary.error=error.what();
 message=error.what();return 0;
}}
int melee_web_native_menu_stadium_recreation_restart(){try{
 auto& s=stadium_recreation_canary;
 check(s.attempted&&s.closed&&!s.restarted&&s.error.empty()&&
       !match&&!world&&!host&&!results&&!prize&&!source_session_owned&&
       !archive_cache&&!scoped_assets&&!running&&!pending&&!faulted&&
       !melee_web_source_file_drive_busy()&&s.expected_parent_owner&&
       seed_ptr==s.expected_parent_owner&&*seed_ptr==s.parent_seed,
       "Stadium recreation restart requires its unchanged closed owner and parent RNG boundary");
 check(EM_ASM_INT({try{return !ABORT&&Module.stadiumMatchedSetupNativeIdle?.()===true&&
       window.menuOwnerStopped?.()===false&&
       globalThis.__meleeWebStadiumRecreationCanary?.afterClose()===true?1:0;}catch(_){return 0;}})==1,
       "Stadium recreation restart requires returned idle and persisted close approval");
 check(!match&&!world&&!host&&!results&&!prize&&!source_session_owned&&
       !archive_cache&&!scoped_assets&&!running&&!pending&&!faulted&&
       !melee_web_source_file_drive_busy()&&seed_ptr==s.expected_parent_owner&&
       *seed_ptr==s.parent_seed,
       "Stadium recreation closed owner or parent RNG changed during host approval");
 const auto submission=aurora_browser_submission_status();const auto* stats=aurora_get_stats();
 check(stats&&stats->queuedPipelines==0&&submission.pendingFramePackets==0&&
       submission.pendingStagingBuffers==0&&submission.workerBusy==0,
       "Stadium recreation restart requires settled retained renderer");
 // No native source owner exists. This restores ordinary command/import service;
 // B still must pass the existing fresh import and original CSS/SSS/Setup route.
 s.restarted=true;emscripten_set_main_loop(tick,0,0);return 1;
}catch(const std::exception& error){
 if(stadium_recreation_canary.error.empty())stadium_recreation_canary.error=error.what();
 message=error.what();return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_queue(
    unsigned schedule_index,int action_kind,unsigned ordinal,int bundle,
    const uint8_t* bytes,size_t byte_count){try{
 auto& state=matched_setup_cf;
 check(!state.version2&&state.armed&&!state.failed&&!state.complete&&!state.queued&&
       !state.awaiting_approval&&schedule_index==state.cursor&&schedule_index<171,
       "Matched Setup C/F action queue is not at its one-use cursor");
 const bool tick_action=schedule_index==168||schedule_index==170||
       (schedule_index<168&&(schedule_index%2)==0);
 const unsigned expected_ordinal=schedule_index<168?schedule_index/2:
       schedule_index==168||schedule_index==169?84U:85U;
 check(action_kind==(tick_action?1:2)&&ordinal==expected_ordinal&&
       (tick_action?bundle==static_cast<int>(ordinal)&&bytes&&byte_count==44:
                    bundle==-1&&bytes==nullptr&&byte_count==0),
       "Matched Setup C/F action differs from the fixed typed input schedule");
 const unsigned expected_ticks=ordinal+(tick_action?0U:1U);
 matched_setup_cf_check_owner(expected_ticks);
 state.seed_at_queue=match->random_seed();
 state.last_schedule_index=schedule_index;state.last_action_kind=action_kind;
 state.last_ordinal=ordinal;state.last_bundle=tick_action?ordinal:UINT32_MAX;
 state.last_action_complete=false;state.last_action_json.clear();
 state.queued_statuses.fill(0);
 if(tick_action)std::copy(bytes,bytes+44,state.queued_statuses.begin());
 state.queued=true;first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue(
    unsigned schedule_index,int action_kind,unsigned ordinal,int bundle){try{
 auto& state=matched_setup_cf;
 check(state.version2&&state.armed&&!state.failed&&!state.complete&&!state.queued&&
       !state.awaiting_approval&&schedule_index==state.cursor&&
       schedule_index<state.version2_actions.size(),
       "Matched Setup C/F v2 action queue is not at its one-use cursor");
 const auto& action=state.version2_actions[schedule_index];
 check(action_kind==action.kind&&ordinal==action.ordinal&&
       bundle==(action.kind==1?static_cast<int>(action.bundle):-1)&&
       action.draws_before==state.source_draws&&action.ordinal==
         (action.kind==1?state.source_ticks:state.source_draws),
       "Matched Setup C/F v2 queued action differs from its immutable manifest row");
 matched_setup_cf_check_owner(state.source_ticks,state.source_draws);
 state.seed_at_queue=match->random_seed();
 state.last_schedule_index=schedule_index;state.last_action_kind=action.kind;
 state.last_ordinal=action.ordinal;state.last_bundle=action.kind==1?action.bundle:UINT32_MAX;
 state.last_action_complete=false;state.last_action_json.clear();
 state.queued_statuses=action.pad;
 state.queued_ticks_before=state.source_ticks;
 state.queued_draws_before=state.source_draws;
 state.queued_source_frame_before=match->source_frames();
 state.queued=true;first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve(
    unsigned schedule_index,int accepted){try{
 auto& state=matched_setup_cf;
 check((accepted==0||accepted==1)&&state.armed&&!state.failed&&!state.complete&&
       state.awaiting_approval&&state.last_action_complete&&
       schedule_index==state.cursor&&schedule_index==state.last_schedule_index,
       "Matched Setup C/F approval is duplicate, stale, or out of order");
 if(state.version2)matched_setup_cf_check_owner(state.last_ticks_after,state.last_draws_after);
 else matched_setup_cf_check_owner(state.last_ordinal+1);
 if(!accepted){matched_setup_cf_fail("Host refused the pending matched Setup C/F boundary");return 1;}
 state.awaiting_approval=false;state.last_action_complete=false;++state.cursor;
 if(state.cursor==state.action_count)state.complete=true;
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 matched_setup_cf_fail(error.what());return 0;
}}

int melee_web_native_menu_stadium_first_sss_vs_entry_kick(){try{
 auto& s=first_sss_vs_entry;const auto& prefix=first_sss_prefix_sequence;
 check(!s.attempted&&!s.failed&&!s.complete,"VS Entry endpoint is one-use");
 s.attempted=true; // Retain every subsequent gate/getter refusal in this sibling.
 s.audio_calls_before=s.audio_calls_after=diagnostic_audio_render_calls;
 s.audio_frames_before=s.audio_frames_after=diagnostic_audio_render_frames;
 s.audio_phase_before=s.audio_phase_after=audio_phase;
 const auto eligible=[&](){return !running&&!pending&&!faulted&&host&&!host_entered&&
   !match&&!results&&!prize&&!replay&&!melee_web_net_active()&&!menu_scene_rebuild_pending&&
   !preparation.busy()&&preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
   asset_destination==AssetDestination::None&&prefix.complete&&!prefix.failed&&
   prefix.native.state==2&&prefix.native.input_index==124&&prefix.native.consumed_inputs==124&&
   prefix.native.host_tick_calls==124&&prefix.native.host_draw_calls==124&&
   prefix.native.matched_ticks==124&&prefix.native.matched_draw_enters==124&&
   prefix.native.matched_draw_returns==124&&prefix.native.last_tick_result==3&&
   prefix.native.transition_requested&&prefix.frame_end_returned&&
   melee_web_menu_host_source_scene(host)==0&&melee_web_menu_host_phase(host)==MELEE_WEB_MENU_READY;};
 check(eligible(),"VS Entry endpoint requires the stopped completed SSS prefix");
 check(EM_ASM_INT({try{
   const f=globalThis.__meleeWebStadiumFirstSssPrefixApproved;
   return typeof f==='function'&&f()===true?1:0;
  }catch(_){return 0;}})==1,"VS Entry endpoint requires strict host prefix approval");
 check(!s.failed&&eligible(),"VS Entry endpoint prefix changed during approval");
 MeleeWebMenuMatchSelection selection{};char error[256]{};
 ++s.getter_calls;
 check(melee_web_menu_host_stadium_c1a_selection(host,&selection,error,sizeof error),error);
 std::ostringstream out;
 out<<"{\"phase\":\"vs_entry\",\"random_seed_hex\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(selection.random_seed)
    <<"\",\"start\":";
 melee_web::stadium_first_css_diagnostic::write_start(out,selection.start);
 out<<'}';s.actual=out.str();s.captured=true;
 s.audio_calls_after=diagnostic_audio_render_calls;s.audio_frames_after=diagnostic_audio_render_frames;
 s.audio_phase_after=audio_phase;
 check(s.audio_calls_after==s.audio_calls_before&&s.audio_frames_after==s.audio_frames_before&&
       s.audio_phase_after==s.audio_phase_before,"VS Entry endpoint performed unexpected audio work");
 check(!s.failed&&eligible(),"VS Entry endpoint changed after checked selection read");
 s.complete=true;first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 auto& s=first_sss_vs_entry;s.failed=true;s.complete=false;
 s.audio_calls_after=diagnostic_audio_render_calls;s.audio_frames_after=diagnostic_audio_render_frames;
 s.audio_phase_after=audio_phase;
 if(s.error.empty())s.error=error.what();
 first_css_browser_draw_refresh_observation();return 0;
}}

int melee_web_native_menu_stadium_first_sss_prefix_arm(const uint8_t* bytes,unsigned size){try{
 auto& s=first_sss_prefix_sequence;
 check(!s.armed&&!s.failed&&!s.attempted&&!running&&!pending&&!faulted&&host&&world&&host_entered&&
       !match&&!results&&!prize&&!replay&&!melee_web_net_active()&&!menu_scene_rebuild_pending&&
       !preparation.busy()&&preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       asset_destination==AssetDestination::None&&first_sss_draw_sequence.complete&&
       !first_sss_draw_sequence.failed&&first_sss_draw_sequence.frame_end_returned,
       "SSS prefix arm requires its unchanged same-run approved first draw");
 s.armed=true;s.audio_render_calls_before=s.audio_render_calls_after=diagnostic_audio_render_calls;
 s.audio_render_frames_before=s.audio_render_frames_after=diagnostic_audio_render_frames;
 s.audio_phase_before=s.audio_phase_after=audio_phase;
 s.input=melee_web::stadium_first_css_diagnostic::decode_first_sss_prefix_input(bytes,size,first_css_browser_draw.context.source_sha256);
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstSssPrefixCompare==='function'?1:0;})==1,"SSS prefix comparator absent");
 char error[256]{};
 check(melee_web_menu_host_arm_first_sss_prefix(host,first_sss_sequence_compare,&first_sss_prefix_sequence,error,sizeof error),error);
 first_sss_sequence_sync(true);first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_sequence_fail(true,error.what());first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_prefix_kick(){try{
 auto& s=first_sss_prefix_sequence;
 check(s.armed&&!s.kicked&&!s.attempted&&!s.failed&&!s.complete&&!running&&!pending&&host&&world&&host_entered&&
       first_sss_draw_sequence.complete&&!first_sss_draw_sequence.failed,
       "SSS prefix kick requires its stopped one-use finite arm");
 s.kicked=true;menu_clock.reset();first_use_draw_pending=false;
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_sequence_fail(true,error.what());first_css_browser_draw_refresh_observation();return 0;}}

int melee_web_native_menu_stadium_first_sss_tick_arm(const uint8_t* bytes,unsigned size){try{
 auto& state=first_sss_consumed_tick;const auto& pair=first_sss_constructor_pair;
 check(!state.armed&&!state.failed&&!state.attempted&&!running&&!pending&&!faulted&&
       !replay&&!melee_web_net_active()&&host&&world&&host_entered&&
       !match&&!results&&!prize&&!menu_scene_rebuild_pending&&
       !preparation.busy()&&preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       asset_destination==AssetDestination::None&&pair.complete&&pair.compared&&
       pair.frame_terminal&&!pair.failed&&pair.native.state==2&&
       pair.native.host_tick_calls==0&&pair.native.host_draw_calls==0,
       "First SSS tick arm requires its stopped approved constructor pair");
 state.armed=true; // Retain all subsequent decode/owner refusals in this sibling.
 state.input=melee_web::stadium_first_css_diagnostic::decode_first_sss_tick_input(bytes,size,first_css_browser_draw.context.source_sha256);
 melee_web::stadium_first_css_diagnostic::ConsumedPadInput consumed{};
 consumed.ports=state.input.ports;
 melee_web::stadium_first_css_diagnostic::decode_consumed_pad_statuses(consumed,state.raw);
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstSssTickCompare==='function'?1:0;})==1,
       "First SSS tick host comparator is absent");
 char error[256]{};
 check(melee_web_menu_host_arm_first_sss_tick(host,state.raw,error,sizeof(error)),error);
 first_sss_tick_sync_native();first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_tick_browser_fail(error.what());first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_tick_kick(){try{
 auto& state=first_sss_consumed_tick;
 check(state.armed&&!state.kicked&&!state.attempted&&!state.failed&&!running&&!pending&&
       !faulted&&host&&world&&host_entered&&!preparation.busy()&&
       !menu_scene_rebuild_pending&&first_sss_constructor_pair.complete&&
       !first_sss_constructor_pair.failed,
       "First SSS tick kick requires its unchanged stopped one-use arm");
 state.kicked=true;state.attempted=true;
 char error[256]{};
 const int result=melee_web_menu_host_tick(host,state.raw,error,sizeof(error));
 first_sss_tick_sync_native();
 check(result==MELEE_WEB_MENU_RESULT_TICKED&&!state.failed&&state.captured&&
       state.native.host_tick_calls==1&&state.native.host_draw_calls==0&&
       state.native.clock_post_succeeded&&!state.native.transition_requested&&
       state.native.post_host_frame_captured&&state.native.post_host_frame==1,
       error[0]?error:"First SSS tick did not finish its strict no-draw protocol");
 const std::string actual=first_sss_tick_sample_json();
 const int approval=EM_ASM_INT({
  const callback=globalThis.__meleeWebStadiumFirstSssTickCompare;
  if(typeof callback!=='function')return 0;
  try{const value=callback('scheduler_end',UTF8ToString($0));
   if(value&&typeof value.then==='function')return -3;
   return value===true?1:-1;
  }catch(_){return -2;}
 },actual.c_str());
 state.compared=approval==1||approval==-1;
 check(approval==1,approval==0?"First SSS tick host comparator is absent":
       approval==-2?"First SSS tick host comparator threw":
       approval==-3?"First SSS tick host comparator must be synchronous":
       "First SSS tick host comparator refused its actual scheduler sample");
 state.complete=true;running=false;pending=false;menu_clock.reset();
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){first_sss_tick_browser_fail(error.what());first_sss_tick_sync_native();first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_pair_arm(){try{
 auto& pair=first_sss_constructor_pair;
 check(!pair.armed&&!pair.failed&&!pair.attempted&&!running&&!pending&&!faulted&&
       !replay&&!melee_web_net_active()&&host&&world&&host_entered&&
       !match&&!results&&!prize&&!menu_scene_rebuild_pending&&
       asset_destination==AssetDestination::None&&!preparation.busy()&&
       preparation.phase()==melee_web::MenuPreparationState::Phase::Idle&&
       first_css_final_pending_draw.complete&&first_css_final_pending_draw.compared&&
       first_css_final_pending_draw.approved&&first_css_final_pending_draw.frame_end_returned&&
       !first_css_final_pending_draw.failed&&first_css_final_pending_draw.error.empty()&&
       first_css_browser_postdraw_stream.terminal&&!first_css_browser_postdraw_stream.failed&&
       first_css_browser_postdraw_stream.outcome=="stop_after_last_input_request"&&
       first_css_browser_postdraw_stream.matched_ticks==147&&
       first_css_browser_postdraw_stream.matched_draws==147&&
       first_css_browser_postdraw_stream.consumed_inputs==148,
       "First SSS pair arm requires the unchanged completed final CSS draw");
 check(EM_ASM_INT({return typeof globalThis.__meleeWebStadiumFirstSssPairCompare===
                    'function'?1:0;})==1,"First SSS pair host comparator is absent");
 char error[256]{};
 check(melee_web_menu_host_arm_first_sss_pair(host,error,sizeof(error)),error);
 pair.armed=true;
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 first_sss_pair_fail(error.what());first_css_browser_draw_refresh_observation();return 0;}}
int melee_web_native_menu_stadium_first_sss_pair_kick(){try{
 auto& pair=first_sss_constructor_pair;
 check(pair.armed&&!pair.kicked&&!pair.attempted&&!pair.failed&&!running&&!pending&&
       host&&world&&host_entered&&!preparation.busy()&&!menu_scene_rebuild_pending&&
       first_css_final_pending_draw.complete&&first_css_final_pending_draw.frame_end_returned&&
       !first_css_final_pending_draw.failed,
       "First SSS pair kick requires its unchanged armed CSS boundary");
 pair.kicked=true;
 pair.audio_render_calls_before_rebuild=diagnostic_audio_render_calls;
 pair.audio_render_frames_before_rebuild=diagnostic_audio_render_frames;
 pair.audio_before_rebuild_captured=true;
 // Reuse ordinary CSS leave, audio acknowledgement and world reconstruction.
 // The old stream stays terminal; no source tick/input is scheduled here.
 pending=true;running=false;menu_clock.reset();audio_clock.reset();
 first_css_browser_draw_refresh_observation();return 1;
}catch(const std::exception& error){
 first_sss_pair_fail(error.what());first_css_browser_draw_refresh_observation();return 0;}}
#endif
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
 graphics_startup_started_ms=emscripten_get_now();
 publish_graphics_startup(0,false);
 aurora_initialize(argc,argv,&config);
 graphics_startup_renderer_init_ms=emscripten_get_now()-graphics_startup_started_ms;
 publish_graphics_startup(0,true);
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 // The private host runner checks this closure before every matched Setup
 // native getter/export. It must remain false while Asyncify is unwinding,
 // rewinding, retaining currData, or after a runtime abort.
 if(!EM_ASM_INT({
   if(typeof Module!=='object'||typeof Asyncify!=='object')return 0;
   Module.stadiumMatchedSetupNativeIdle=()=>
     !ABORT&&Asyncify.state===Asyncify.State.Normal&&Asyncify.currData===null;
   return 1;
 }))return 1;
#endif
 emscripten_set_main_loop(tick,0,1);return 0;
}
