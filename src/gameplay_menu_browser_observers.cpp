// JSON observers and serializers. They read shared state and never mutate a
// source owner. Write one field per FixedFormatWriter::add() call (or one
// std::string append per field) so that adding a field is a one-line change.
#include "gameplay_menu_browser_state.hpp"
#include "fixed_format_writer.hpp"
using namespace melee_web_menu_browser;
namespace {
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
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
#endif
}
namespace melee_web_menu_browser {
void PreparationProfile::report(double settled_at) const {
  const double audio_wait=audio_ready_at?audio_ready_at-requested_at:0;
  const double construction=constructed_at?constructed_at-audio_ready_at:0;
  const double render_wait=constructed_at?settled_at-constructed_at:settled_at-requested_at;
  char profile[1280];
  melee_web::FixedFormatWriter out(profile,sizeof(profile));
  out.add("{");
  out.add("\"source_transition\":%s",source_transition?"true":"false");
  out.add(",\"total_ms\":%.3f",settled_at-requested_at);
  out.add(",\"audio_wait_ms\":%.3f",audio_wait);
  out.add(",\"construction_ms\":%.3f",construction);
  out.add(",\"render_wait_ms\":%.3f",render_wait);
  out.add(",\"render_cpu_ms\":%.3f",render_cpu_ms);
  out.add(",\"callbacks\":%u",callbacks);
  out.add(",\"source_draws\":%u",source_draws);
  out.add(",\"max_callback_ms\":%.3f",max_callback_ms);
  out.add(",\"max_draw_ms\":%.3f",max_draw_ms);
  out.add(",\"max_end_ms\":%.3f",max_end_ms);
  out.add(",\"texture_upload_bytes\":%llu",static_cast<unsigned long long>(texture_upload_bytes));
  out.add(",\"max_draw_calls\":%u",max_draw_calls);
  out.add(",\"max_queued\":%u",max_queued);
  out.add(",\"queued_delta\":%d",queued_delta);
  out.add(",\"created_delta\":%d",created_delta);
  out.add(",\"gpu_completion_wait_ms\":%.3f",submission_wait_started?submission_ready_at-submission_wait_started:0);
  out.add(",\"gpu_completion_wait_callbacks\":%u",submission_wait_callbacks);
  out.add(",\"pending_staging_at_settle\":%u",pending_staging_at_settle);
  out.add(",\"pending_staging_at_first_arm\":%u",pending_staging_at_first_arm);
  out.add(",\"gpu_completion_ready\":%s",submission_ready_at?"true":"false");
  out.add("}");
  EM_ASM({window.menuPreparationProfile?.(JSON.parse(UTF8ToString($0)));},profile);
}
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
void publish_runtime_timing(RuntimeTimingSample s){
 char timing[4096];
 const uint32_t staging_used_bytes=s.callback_staging_used;
 melee_web::FixedFormatWriter out(timing,sizeof(timing));
 out.add("{");
 out.add("\"frame\":%u",++render_frame);
 out.add(",\"started\":%.3f",s.started);
 out.add(",\"valid\":%d",s.timing_valid);
 out.add(",\"first_use\":%d",s.first_use);
 out.add(",\"input_ms\":%.3f",s.input_done-s.started);
 out.add(",\"simulation_audio_ms\":%.3f",s.simulation_cpu_ms);
 out.add(",\"preparation_ms\":%.3f",s.preparation_ms);
 out.add(",\"begin_phases\":{");
 out.add("\"frame_slot_ms\":%.3f",s.callback_begin_stats.lastBeginFrameFrameSlotMs);
 out.add(",\"frame_slot_wait_ms\":%.3f",s.callback_begin_stats.lastBeginFrameFrameSlotWaitMs);
 out.add(",\"frame_slot_wait_count\":%u",s.callback_begin_stats.lastBeginFrameFrameSlotWaitCount);
 out.add(",\"staging_slot_ms\":%.3f",s.callback_begin_stats.lastBeginFrameStagingSlotMs);
 out.add(",\"staging_slot_wait_ms\":%.3f",s.callback_begin_stats.lastBeginFrameStagingSlotWaitMs);
 out.add(",\"staging_slot_wait_count\":%u",s.callback_begin_stats.lastBeginFrameStagingSlotWaitCount);
 out.add(",\"packet_ms\":%.3f",s.callback_begin_stats.lastBeginFramePacketMs);
 out.add(",\"record_ms\":%.3f",s.callback_begin_stats.lastBeginFrameRecordMs);
 out.add(",\"pipeline_ms\":%.3f",s.callback_begin_stats.lastBeginFramePipelineMs);
 out.add(",\"worker_ms\":%.3f",s.callback_begin_stats.lastBeginFrameWorkerMs);
 out.add(",\"encoder_ms\":%.3f",s.callback_begin_stats.lastBeginFrameEncoderMs);
 out.add(",\"total_ms\":%.3f",s.callback_begin_stats.lastBeginFrameTotalMs);
 out.add(",\"residual_ms\":%.3f",s.callback_begin_stats.lastBeginFrameResidualMs);
 out.add(",\"outer_surface_ms\":%.3f",s.callback_begin_stats.lastBeginFrameOuterSurfaceMs);
 out.add(",\"outer_imgui_ms\":%.3f",s.callback_begin_stats.lastBeginFrameOuterImguiMs);
 out.add(",\"outer_fifo_ms\":%.3f",s.callback_begin_stats.lastBeginFrameOuterFifoMs);
 out.add(",\"outer_total_ms\":%.3f",s.callback_begin_stats.lastBeginFrameOuterTotalMs);
 out.add(",\"outer_residual_ms\":%.3f",s.callback_begin_stats.lastBeginFrameOuterResidualMs);
 out.add(",\"start_ms\":%.3f",s.callback_begin_stats.lastBeginFrameStartMs);
 out.add(",\"end_ms\":%.3f",s.callback_begin_stats.lastBeginFrameEndMs);
 out.add(",\"max_wait_ms\":%.3f",s.callback_begin_stats.lastBeginFrameMaxWaitMs);
 out.add(",\"max_wait_start_ms\":%.3f",s.callback_begin_stats.lastBeginFrameMaxWaitStartMs);
 out.add(",\"max_wait_end_ms\":%.3f",s.callback_begin_stats.lastBeginFrameMaxWaitEndMs);
 out.add(",\"max_wait_kind\":%u",s.callback_begin_stats.lastBeginFrameMaxWaitKind);
 out.add(",\"last_frame_id\":%llu",static_cast<unsigned long long>(s.callback_begin_stats.lastBeginFrameId));
 out.add("}");
 out.add(",\"begin_ms\":%.3f",s.render_begin_ms);
 out.add(",\"draw_ms\":%.3f",s.render_draw_ms);
 out.add(",\"end_ms\":%.3f",s.render_end_ms);
 out.add(",\"total_ms\":%.3f",s.finished-s.started);
 out.add(",\"end_phases\":{");
 out.add("\"last_frame\":%llu",static_cast<unsigned long long>(s.callback_end_stats.lastEndFrameId));
 out.add(",\"fifo_texture_ms\":%.3f",s.callback_end_stats.lastEndFrameFifoTextureMs);
 out.add(",\"gfx_finish_ms\":%.3f",s.callback_end_stats.lastEndFrameGfxFinishMs);
 out.add(",\"staging_writes_ms\":%.3f",s.callback_end_stats.lastEndFrameStagingWritesMs);
 out.add(",\"surface_encode_ms\":%.3f",s.callback_end_stats.lastEndFrameSurfaceEncodeMs);
 out.add(",\"encoder_finish_ms\":%.3f",s.callback_end_stats.lastEndFrameEncoderFinishMs);
 out.add(",\"queue_submit_ms\":%.3f",s.callback_end_stats.lastEndFrameQueueSubmitMs);
 out.add(",\"cleanup_ms\":%.3f",s.callback_end_stats.lastEndFrameCleanupMs);
 out.add(",\"outer_prep_ms\":%.3f",s.callback_end_stats.lastEndFrameOuterPrepMs);
 out.add(",\"record_ms\":%.3f",s.callback_end_stats.lastEndFrameRecordMs);
 out.add(",\"packet_ms\":%.3f",s.callback_end_stats.lastEndFramePacketMs);
 out.add(",\"callback_ms\":%.3f",s.callback_end_stats.lastEndFrameCallbackMs);
 out.add(",\"callback_post_submit_ms\":%.3f",s.callback_end_stats.lastEndFrameCallbackPostSubmitMs);
 out.add(",\"observer_ms\":%.3f",s.callback_end_stats.lastEndFrameObserverMs);
 out.add(",\"callback_residual_ms\":%.3f",s.callback_end_stats.lastEndFrameCallbackResidualMs);
 out.add(",\"tail_ms\":%.3f",s.callback_end_stats.lastEndFrameTailMs);
 out.add(",\"worker_ms\":%.3f",s.callback_end_stats.lastEndFrameWorkerMs);
 out.add(",\"worker_residual_ms\":%.3f",s.callback_end_stats.lastEndFrameWorkerResidualMs);
 out.add(",\"total_ms\":%.3f",s.callback_end_stats.lastEndFrameTotalMs);
 out.add(",\"residual_ms\":%.3f",s.callback_end_stats.lastEndFrameResidualMs);
 out.add("}");
 out.add(",\"began\":%d",s.began);
 out.add(",\"drawn\":%d",s.drawn);
 out.add(",\"queued_delta\":%d",stat_delta(s.stats_after.queuedPipelines,s.stats_before.queuedPipelines));
 out.add(",\"created_delta\":%d",stat_delta(s.stats_after.createdPipelines,s.stats_before.createdPipelines));
 out.add(",\"queued_total\":%u",s.stats_after.queuedPipelines);
 out.add(",\"created_total\":%u",s.stats_after.createdPipelines);
 out.add(",\"draw_calls\":%u",s.callback_draw_calls);
 out.add(",\"texture_upload_bytes\":%u",s.callback_texture_upload);
 out.add(",\"staging_used_bytes\":%u",staging_used_bytes);
 out.add(",\"wasm_heap_bytes\":%zu",emscripten_get_heap_size());
 out.add(",\"draw_suppressed\":%d",s.suppress_draw);
 out.add(",\"source_steps\":%zu",s.source_frames.steps());
 out.add(",\"source_draws\":%zu",s.source_frames.draws());
 out.add("}");
 const int timing_written=out.result();
 if(timing_written<0||static_cast<size_t>(timing_written)>=sizeof(timing)){
  s.timing_valid=0;
  std::snprintf(timing,sizeof(timing),"{\"frame\":%u,\"valid\":0,\"timing_truncated\":true}",render_frame);
  EM_ASM({
   const text=UTF8ToString($0);
   if(window.menuRuntimeTimingError)window.menuRuntimeTimingError(text);
   else console.error(text);
 },timing_written<0?"Native menu timing JSON formatting failed":"Native menu timing JSON exceeded 4096 bytes");
 }
 EM_ASM({if(window.menuRuntimeTiming)window.menuRuntimeTiming(JSON.parse(UTF8ToString($0)));},timing);
}
#endif
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
 json+="{";
 json+="\"schema\":\"melee-web-results-pad-trace-v1\"";
 json+=",\"source_frame_semantics\":\"zero-based Results source frame immediately before this tick attempt\"";
 json+=",\"attempts\":";json+=std::to_string(results_pad_trace_attempts);
 json+=",\"retained\":";json+=std::to_string(results_pad_trace_count);
 json+=",\"capacity\":";json+=std::to_string(kResultsPadTraceCapacity);
 json+=",\"overflow\":";json+=results_pad_trace_overflow?"true":"false";
 append_camera_entry_json(json);
 json+=",\"samples\":[";
 for(size_t index=0;index<results_pad_trace_count;++index){
  if(index)json+=',';
  const auto& row=results_pad_trace[index];
  char sample[512];
  melee_web::FixedFormatWriter row_text(sample,sizeof(sample));
  row_text.add("{");
  row_text.add("\"source_frame\":%u",row.source_frame);
  row_text.add(",\"tick_returned\":%s",row.tick_returned?"true":"false");
  row_text.add(",\"pads\":[");
  int length=row_text.result();
  if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
  json.append(sample,static_cast<size_t>(length));
  for(size_t port=0;port<4;++port){
   if(port)json+=',';
   const PADStatus& pad=row.pads[port];
   melee_web::FixedFormatWriter pad_text(sample,sizeof(sample));
   pad_text.add("{");
   pad_text.add("\"button\":%u",static_cast<unsigned>(pad.button));
   pad_text.add(",\"stick_x\":%d",static_cast<int>(pad.stickX));
   pad_text.add(",\"stick_y\":%d",static_cast<int>(pad.stickY));
   pad_text.add(",\"substick_x\":%d",static_cast<int>(pad.substickX));
   pad_text.add(",\"substick_y\":%d",static_cast<int>(pad.substickY));
   pad_text.add(",\"trigger_left\":%u",static_cast<unsigned>(pad.triggerLeft));
   pad_text.add(",\"trigger_right\":%u",static_cast<unsigned>(pad.triggerRight));
   pad_text.add(",\"analog_a\":%u",static_cast<unsigned>(pad.analogA));
   pad_text.add(",\"analog_b\":%u",static_cast<unsigned>(pad.analogB));
   pad_text.add(",\"err\":%d",static_cast<int>(pad.err));
#if defined(TARGET_PC)
   pad_text.add(",\"ext_button\":%u",static_cast<unsigned>(pad.extButton));
#endif
   pad_text.add("}");
   length=pad_text.result();
   if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
   json.append(sample,static_cast<size_t>(length));
  }
  json+="]";
  json+=",\"source_consumed_pads\":";
  if(!row.results_state_sampled)json+="null";
  else{
   json+='[';
   for(size_t port=0;port<4;++port){
    if(port)json+=',';
    const HSD_PadStatus& consumed=row.source_consumed_pads[port];
    melee_web::FixedFormatWriter consumed_text(sample,sizeof(sample));
    consumed_text.add("{");
    consumed_text.add("\"button\":%u",static_cast<unsigned>(consumed.button));
    consumed_text.add(",\"trigger\":%u",static_cast<unsigned>(consumed.trigger));
    consumed_text.add(",\"release\":%u",static_cast<unsigned>(consumed.release));
    consumed_text.add(",\"err\":%d",static_cast<int>(consumed.err));
    consumed_text.add("}");
    length=consumed_text.result();
    if(length<0||static_cast<size_t>(length)>=sizeof(sample))continue;
    json.append(sample,static_cast<size_t>(length));
   }
   json+=']';
  }
  json+=",\"results_state_after_tick\":";
  if(!row.results_state_sampled)json+="null";
  else{
   char state_sample[256];
   melee_web::FixedFormatWriter state_text(state_sample,sizeof(state_sample));
   state_text.add("{");
   state_text.add("\"source_frame\":%u",row.results_state_frame);
   state_text.add(",\"phase\":%u",static_cast<unsigned>(row.results_phase));
   state_text.add(",\"stats_phase\":%u",static_cast<unsigned>(row.results_stats_phase));
   state_text.add(",\"num_pages\":%u",static_cast<unsigned>(row.results_num_pages));
   state_text.add(",\"players\":[");
   int state_length=state_text.result();
   if(state_length>=0&&static_cast<size_t>(state_length)<sizeof(state_sample))
    json.append(state_sample,static_cast<size_t>(state_length));
   for(size_t slot=0;slot<4;++slot){
    if(slot)json+=',';
    melee_web::FixedFormatWriter player_text(state_sample,sizeof(state_sample));
    player_text.add("{");
    player_text.add("\"page\":%u",static_cast<unsigned>(row.results_player_pages[slot]));
    player_text.add(",\"confirmed\":%u",static_cast<unsigned>(row.results_player_confirmed[slot]));
    player_text.add("}");
    state_length=player_text.result();
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
const char* melee_web_native_menu_match_observe(){
 static char text[2048];
 if(!match)return terminal_match_observation.empty()?"{}":terminal_match_observation.c_str();
 if(!match->construction_complete())return "{}";
 try{
 match_observer_error.clear();
 const auto p0=match->player_stats(0),p1=match->player_stats(1);
 // The context's indices are compact; its stats preserve original source slots.
 const unsigned slots[2]{p0.player_slot,p1.player_slot};
 const StartMeleeData& start=match->start_data();
 int winner=-1;const int outcome=match->outcome(winner);
 melee_web::FixedFormatWriter out(text,sizeof(text));
 out.add("{");
 out.add("\"leg\":\"%s\"",match->sudden_death()?"sudden_death":"vs");
 out.add(",\"prior_vs_source_frames\":%u",prior_vs_source_frames);
 out.add(",\"observed_player_source_slots\":[%u,%u]",slots[0],slots[1]);
 if(match->sudden_death()){
  const auto& prior=prior_vs_terminal.match_end;
  out.add(",\"prior_vs_terminal\":{\"outcome\":%d,\"winners\":[",(int)prior.outcome);
  for(int i=0;i<prior.n_winners&&i<GM_MAX_PLAYERS;++i){
   if(i)out.add(",");out.add("%d",(int)prior.winners[i]);
  }
  out.add("]}");
 }
 out.add(",\"ready\":%s",match->ready()?"true":"false");
 out.add(",\"paused\":%s",match->paused()?"true":"false");
 out.add(",\"ending\":%s",match->ending()?"true":"false");
 out.add(",\"complete\":%s",match->complete()?"true":"false");
 out.add(",\"frame\":%u",match->source_frames());
 out.add(",\"rng\":%u",match->random_seed());
 out.add(",\"outcome\":%d",outcome);
 out.add(",\"winner\":%d",winner);
 out.add(",\"rules\":{");
 out.add("\"match_kind\":%d",(int) start.rules.match_kind);
 out.add(",\"stage\":%u",(unsigned) start.rules.stkind);
 out.add(",\"timer_enabled\":%u",(unsigned) start.rules.timer_enabled);
 out.add(",\"time_limit\":%u",(unsigned) start.rules.time_limit);
 out.add(",\"is_stock\":%u",(unsigned)start.rules.is_stock);
 out.add(",\"is_vs\":%u",(unsigned)start.rules.is_vs);
 out.add(",\"source_sudden_death_flag\":%u",(unsigned)start.rules.x6);
 out.add(",\"item_frequency\":%d",(int) (int8_t) start.rules.xB);
 out.add(",\"item_mask_hex\":\"%016llx\"",(unsigned long long) start.rules.x20);
 out.add(",\"is_teams\":%u",(unsigned) start.rules.is_teams);
 out.add(",\"player_teams\":[%d,%d]",(int) start.players[slots[0]].team,(int) start.players[slots[1]].team);
 out.add(",\"player_stocks\":[%d,%d]",(int) start.players[slots[0]].stocks,(int) start.players[slots[1]].stocks);
 out.add(",\"door_teams\":[%d,%d,%d,%d]",(int) start.players[0].team,(int) start.players[1].team,(int) start.players[2].team,(int) start.players[3].team);
 out.add(",\"friendly_fire\":%u",(unsigned) start.rules.friendly_fire);
 out.add("}");
 out.add(",\"players\":[");
 out.add("{");
 out.add("\"fighter\":%d",p0.fighter_kind);
 out.add(",\"human\":%s",start.players[slots[0]].slot_type==Gm_PKind_Human?"true":"false");
 out.add(",\"damage_percent\":%.9g",p0.damage_percent);
 out.add(",\"source_player_index\":%u",slots[0]);
 out.add(",\"source_slot\":%u",(unsigned)start.players[slots[0]].slot);
 out.add(",\"source_port\":%u",(unsigned)(start.players[slots[0]].slot?
         start.players[slots[0]].slot-1u:slots[0]));
 out.add(",\"source_character\":%d",(int)start.players[slots[0]].ckind);
 out.add(",\"source_color\":%u",(unsigned)start.players[slots[0]].color);
 out.add(",\"source_initial_damage\":%u",(unsigned)start.players[slots[0]].x12);
 out.add(",\"slot_type\":%u",(unsigned)start.players[slots[0]].slot_type);
 out.add(",\"source_stocks\":%d",(int)start.players[slots[0]].stocks);
 out.add(",\"stocks\":%d",p0.stocks);
 out.add(",\"motion\":%d",p0.motion_id);
 out.add(",\"groundAir\":%d",p0.ground_or_air);
 out.add(",\"x\":%.9g",p0.position[0]);
 out.add(",\"y\":%.9g",p0.position[1]);
 out.add("}");
 out.add(",{");
 out.add("\"fighter\":%d",p1.fighter_kind);
 out.add(",\"human\":%s",start.players[slots[1]].slot_type==Gm_PKind_Human?"true":"false");
 out.add(",\"damage_percent\":%.9g",p1.damage_percent);
 out.add(",\"source_player_index\":%u",slots[1]);
 out.add(",\"source_slot\":%u",(unsigned)start.players[slots[1]].slot);
 out.add(",\"source_port\":%u",(unsigned)(start.players[slots[1]].slot?
         start.players[slots[1]].slot-1u:slots[1]));
 out.add(",\"source_character\":%d",(int)start.players[slots[1]].ckind);
 out.add(",\"source_color\":%u",(unsigned)start.players[slots[1]].color);
 out.add(",\"source_initial_damage\":%u",(unsigned)start.players[slots[1]].x12);
 out.add(",\"slot_type\":%u",(unsigned)start.players[slots[1]].slot_type);
 out.add(",\"source_stocks\":%d",(int)start.players[slots[1]].stocks);
 out.add(",\"stocks\":%d",p1.stocks);
 out.add(",\"motion\":%d",p1.motion_id);
 out.add(",\"groundAir\":%d",p1.ground_or_air);
 out.add(",\"x\":%.9g",p1.position[0]);
 out.add(",\"y\":%.9g",p1.position[1]);
 out.add("}");
 out.add("]");
 out.add("}");
 if(out.result()<0||out.result()>=static_cast<int>(sizeof(text))){
  match_observer_error="Match observation overflow";
  std::snprintf(text,sizeof(text),"{\"ready\":false,\"observer_error\":true,\"observer_error_reason\":\"match observation overflow\"}");
 }
 return text;
 }catch(const std::exception& e){
  match_observer_error=e.what();
  std::snprintf(text,sizeof(text),"{\"ready\":false,\"observer_error\":true}");
  return text;
 }
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
   melee_web::FixedFormatWriter entry(start_players+used,sizeof(start_players)-used);
   entry.add("%s",i?",":"");
   entry.add("{");
   entry.add("\"character_kind\":%d",(int)player.ckind);
   entry.add(",\"slot_type\":%d",(int)player.slot_type);
   entry.add(",\"stocks\":%d",(int)player.stocks);
   entry.add(",\"color\":%d",(int)player.color);
   entry.add(",\"team\":%d",(int)player.team);
   entry.add(",\"rumble_enabled\":%d",(int)player.rumble_enabled);
   entry.add(",\"cpu_kind\":%d",(int)player.cpu_kind);
   entry.add(",\"cpu_level\":%d",(int)player.cpu_level);
   entry.add("}");
   const int written=entry.result();
   if(written<0||(size_t)written>=sizeof(start_players)-used)
    return "{\"observer_error\":true,\"observer_error_reason\":\"StartMeleeData roster buffer overflow\"}";
   used+=(size_t)written;
  }
  if(used+2>sizeof(start_players))
   return "{\"observer_error\":true,\"observer_error_reason\":\"StartMeleeData roster terminator overflow\"}";
  start_players[used++]=']';start_players[used]='\0';
 }
 melee_web::FixedFormatWriter out(text,sizeof(text));
 out.add("{");
 out.add("\"source\":{");
 out.add("\"valid\":%s",source_valid?"true":"false");
 out.add(",\"scene\":%d",observed.source_scene);
 out.add(",\"menu_kind\":%d",observed.menu_kind);
 out.add(",\"previous_menu_kind\":%d",observed.previous_menu_kind);
 out.add(",\"hovered_selection\":%d",observed.hovered_selection);
 out.add(",\"confirmed_selection\":%d",observed.confirmed_selection);
 out.add(",\"buttons\":%llu",(unsigned long long) observed.menu_buttons);
 out.add(",\"item_input_locked\":%d",observed.item_input_locked);
 out.add(",\"rules\":{");
 out.add("\"mode\":%d",observed.rule_mode);
 out.add(",\"stock_count\":%d",observed.stock_count);
 out.add(",\"time_limit\":%d",observed.time_limit);
 out.add(",\"stock_time_limit\":%d",observed.stock_time_limit);
 out.add(",\"handicap\":%d",observed.handicap);
 out.add(",\"damage_ratio\":%d",observed.damage_ratio);
 out.add(",\"friendly_fire\":%d",observed.friendly_fire);
 out.add("}");
 out.add(",\"items\":{");
 out.add("\"frequency\":%d",observed.item_frequency);
 out.add(",\"mask_hex\":\"%016llx\"",(unsigned long long) observed.item_mask);
 out.add("}");
 out.add(",\"css_setup\":{");
 out.add("\"valid\":%s",observed.css_setup_valid ? "true" : "false");
 out.add(",\"is_teams\":%d",observed.css_is_teams);
 out.add(",\"player_teams\":[%d,%d]",observed.css_player_teams[0],observed.css_player_teams[1]);
 out.add(",\"door_teams\":[%d,%d,%d,%d]",observed.css_player_teams[0],observed.css_player_teams[1],observed.css_player_teams[2],observed.css_player_teams[3]);
 out.add("}");
 out.add("}");
 out.add(",\"items_menu\":{");
 out.add("\"valid\":%s",live_items_valid?"true":"false");
 out.add(",\"cursor\":%d",live_item_cursor);
 out.add(",\"selected_item_enabled\":%d",live_item_enabled);
 out.add(",\"frequency_selector\":%d",live_item_frequency);
 out.add("}");
 out.add(",\"start\":{");
 out.add("\"valid\":%s",start_valid?"true":"false");
 out.add(",\"match_kind\":%d",(int) start.rules.match_kind);
 out.add(",\"stage\":%u",(unsigned) start.rules.stkind);
 out.add(",\"item_frequency\":%d",(int) (int8_t) start.rules.xB);
 out.add(",\"item_mask_hex\":\"%016llx\"",(unsigned long long) start.rules.x20);
 out.add(",\"player_stocks\":[%d,%d]",(int) start.players[0].stocks,(int) start.players[1].stocks);
 out.add(",\"players\":%s",start_players);
 out.add("}");
 out.add("}");
 return text;
}
const char* melee_web_native_menu_memory(){
 // Lifecycle diagnostics only: mallinfo walks the allocator's free lists.
 // Reserved linear memory is not the same as live allocations and cannot shrink.
 const auto info=mallinfo();
const auto allocation=melee_web_gameplay_allocation();
 const auto source=melee_web_gameplay_stats();
 static char text[2048];
 melee_web::FixedFormatWriter out(text,sizeof(text));
 out.add("{");
 out.add("\"wasm_heap_bytes\":%zu",emscripten_get_heap_size());
 out.add(",\"allocator_arena_bytes\":%zu",info.arena);
 out.add(",\"allocator_live_bytes\":%zu",info.uordblks);
 out.add(",\"allocator_free_bytes\":%zu",info.fordblks);
 out.add(",\"allocator_top_free_bytes\":%zu",info.keepcost);
 out.add(",\"source_allocation_identity\":%llu",(unsigned long long)allocation.identity);
 out.add(",\"source_allocation_generation\":%llu",(unsigned long long)allocation.generation);
 out.add(",\"source_allocation_bytes\":%llu",(unsigned long long)allocation.bytes);
 out.add(",\"source_session_owned\":%s",source_session_owned?"true":"false");
 out.add(",\"source_world_generation\":%llu",(unsigned long long)source.generation);
 out.add(",\"source_objects\":%u",source.objects);
 out.add(",\"source_processes\":%u",source.processes);
 out.add(",\"source_heap_free_bytes\":%d",source.heap_free_bytes);
 out.add(",\"cached_archives\":%zu",archive_cache?archive_cache->archive_count():0);
 out.add(",\"cached_audio_banks\":%zu",archive_cache?archive_cache->audio_bank_count():0);
 out.add(",\"match_present\":%s",match?"true":"false");
 out.add(",\"menu_present\":%s",world?"true":"false");
 out.add(",\"results_present\":%s",results?"true":"false");
 out.add(",\"prize_present\":%s",prize?"true":"false");
 out.add(",\"menu_host_entered\":%s",host_entered?"true":"false");
 out.add(",\"menu_source_scene\":%d",host?melee_web_menu_host_source_scene(host):0);
 out.add(",\"menu_mode_kind\":%d",host?melee_web_menu_host_mode_kind(host):-1);
 out.add(",\"menu_route_target\":%d",host?melee_web_menu_host_route_target_mode(host):-1);
 out.add(",\"menu_phase\":%d",host?melee_web_menu_host_phase(host):0);
 out.add(",\"menu_scene_rebuild_pending\":%s",menu_scene_rebuild_pending?"true":"false");
 out.add(",\"css_input\":{");
 out.add("\"valid\":%s",last_css_fighter_observation_valid?"true":"false");
 out.add(",\"target\":%d",last_css_fighter_target);
 out.add(",\"state\":%d",last_css_fighter_drive_state);
 out.add(",\"cursor_port\":%d",last_css_fighter_observation.cursor_port);
 out.add(",\"held_door\":%d",last_css_fighter_observation.held_door);
 out.add(",\"selected_character\":%d",last_css_fighter_observation.selected_character_kind);
 out.add(",\"source_active_port\":%d",last_css_fighter_observation.source_active_port);
 out.add(",\"source_start_cooldown\":%d",last_css_fighter_observation.source_start_cooldown);
 out.add(",\"source_active_cursor_count\":%d",last_css_fighter_observation.source_active_cursor_count);
 out.add(",\"source_pending_scene\":%d",last_css_fighter_observation.source_pending_scene);
 out.add(",\"source_start_ready\":%d",last_css_fighter_observation.source_start_ready);
 out.add(",\"source_selected_model_state\":%d",last_css_fighter_observation.source_selected_model_state);
 out.add(",\"source_confirm_callback_count\":%d",last_css_fighter_observation.source_confirm_callback_count);
 out.add(",\"source_last_start_trigger\":%d",last_css_fighter_observation.source_last_start_trigger);
 out.add(",\"source_last_start_ready\":%d",last_css_fighter_observation.source_last_start_ready);
 out.add(",\"source_last_start_pending\":%d",last_css_fighter_observation.source_last_start_pending);
 out.add(",\"cursor\":[%.3f,%.3f]",last_css_fighter_observation.cursor_x,last_css_fighter_observation.cursor_y);
 out.add(",\"model\":[%.3f,%.3f]",last_css_fighter_observation.model_x,last_css_fighter_observation.model_y);
 out.add(",\"target_bounds\":[%.3f,%.3f,%.3f,%.3f]",last_css_fighter_observation.target_left,last_css_fighter_observation.target_right,last_css_fighter_observation.target_top,last_css_fighter_observation.target_bottom);
 out.add("}");
 out.add(",\"source_pad0\":{");
 out.add("\"err\":%d",HSD_PadCopyStatus[0].err);
 out.add(",\"button\":%u",static_cast<unsigned>(HSD_PadCopyStatus[0].button));
 out.add(",\"trigger\":%u",static_cast<unsigned>(HSD_PadCopyStatus[0].trigger));
 out.add(",\"last_button\":%u",static_cast<unsigned>(HSD_PadCopyStatus[0].last_button));
 out.add("}");
 out.add(",\"scoped_assets\":%s",scoped_assets?"true":"false");
 out.add(",\"asset_files\":%zu",asset_scope.active_file_count());
 out.add(",\"asset_bytes\":%zu",asset_scope.active_byte_count());
 out.add(",\"asset_source_bytes\":%zu",asset_scope.active_source_bytes());
 out.add(",\"staged_asset_files\":%zu",asset_scope.staged_file_count());
 out.add(",\"staged_asset_bytes\":%zu",asset_scope.staged_byte_count());
 out.add(",\"asset_generation\":%u",asset_scope.pending_generation());
 out.add("}");
 return text;
}
}
