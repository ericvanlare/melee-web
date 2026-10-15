#include "gameplay_menu_browser_state.hpp"
#include "gameplay_menu_frame_effect.hpp"
#include <cstddef>
#include <cstring>
#include <bit>
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
#include "lib/gfx/frame_pending_facts.hpp"
#endif
static_assert(sizeof(PADStatus)==16&&offsetof(PADStatus,err)==10&&offsetof(PADStatus,extButton)==12,
              "Frame observation serializes four native PADStatus records, including Aurora extButton and padding");

namespace melee_web_menu_browser {
namespace {
struct FrameEffectObservation {
 bool requested=false, active=false, terminal=false, admitted=false, renderer_armed=false;
 bool native_retired=false, first_draw=false;
 const melee_web::GameplayMatchSession* owner=nullptr;
 uint32_t owner_generation=0;
 unsigned requested_frame=0, source_before=0, source_after=0;
 unsigned tick_returns=0, draw_returns=0, audio_returns=0, pcm_frames=0;
 unsigned planned_steps=0, planned_audio_steps=0, callback_steps=0, callback_draws=0;
 unsigned audio_phase_before=0, audio_phase_after=0;
 unsigned pre_tick_callbacks=0, pre_tick_audio_returns=0, pre_tick_pcm_frames=0;
 PADStatus pads[4]{};
 // Fixed diagnostic messages only; no arbitrary text is inserted into JSON.
 const char* error="";
} observation;
void refuse(const char* reason){if(!*observation.error)observation.error=reason;}
bool same_owner(){return match.get()==observation.owner&&asset_generation==observation.owner_generation;}
void stopped(){
 observation.active=false;observation.terminal=true;running=false;
 emscripten_cancel_main_loop();
 // Each caller is at a terminal tail with no later native yield.
 EM_ASM({queueMicrotask(()=>{Module.matchFrameEffectCallbackReturned=true;});});
}
bool ready(){
 if(!match||faulted||pending||preparation.busy()||replay||melee_web_net_active()||
    results||prize||stock_check==-1||diagnostic_start_ticks||diagnostic_pad_remaining||
    transition_audio_continues||!match->ready()||match->paused()||match->ending()||
    match->complete()||match->opening_demo()||match->sudden_death()||
    melee_web_source_file_drive_busy())return false;
 const auto* stats=aurora_get_stats();if(!stats||stats->queuedPipelines)return false;
 const auto& start=match->start_data();
 if(start.rules.stkind!=St_Kind_Last)return false;
 for(unsigned i=0;i<2;++i)
  if(start.players[i].ckind!=CKIND_MARIO||match->player_stats(i).stocks!=4)return false;
 for(unsigned i=2;i<6;++i)if(start.players[i].slot_type!=Gm_PKind_NA)return false;
 return true;
}
}
bool frame_effect_terminal(){return observation.terminal;}
void frame_effect_callback_started(){
 // A request remains pending through source-free callbacks. The first call to
 // frame_effect_begin_tick below marks the naturally due source callback.
}
void frame_effect_begin_tick(const PADStatus pads[4],unsigned prior_steps,
                             unsigned planned_steps,unsigned planned_audio_steps,
                             bool audio_before_construction,bool clock_stalled){
 if(!observation.requested||observation.terminal)return;
 if(observation.first_draw){
  if(!observation.active)observation.active=true;
  // FixedTickClock::tick_with_budget selects at most eight source ticks.
  const bool first=observation.tick_returns==0;
  if(planned_steps==0||planned_steps>8||clock_stalled||audio_before_construction||
     !running||!ready()||!same_owner()||prior_steps!=observation.tick_returns||
     match->source_frames()!=observation.requested_frame+observation.tick_returns||
     (!first&&planned_steps!=observation.planned_steps)){
   refuse("first_draw_callback_tick_guard_changed");observation.admitted=false;return;
  }
  if(first){
   observation.planned_steps=planned_steps;observation.planned_audio_steps=planned_audio_steps;
   observation.source_before=match->source_frames();observation.audio_phase_before=audio_phase;
   std::memcpy(observation.pads,pads,sizeof(observation.pads));observation.admitted=true;
  }else if(planned_audio_steps!=observation.planned_audio_steps){
   refuse("first_draw_callback_audio_plan_changed");observation.admitted=false;
  }
  return;
 }
 // Activation is tied to the first source tick selected by the existing clock.
 if(!observation.active)observation.active=true;
 if(observation.tick_returns||prior_steps||planned_steps!=1||clock_stalled||
    audio_before_construction||!running||!ready()||!same_owner()||
    match->source_frames()!=observation.requested_frame){
  refuse("ordinary_callback_not_one_ready_tick");observation.admitted=false;return;
 }
 observation.planned_steps=planned_steps;observation.planned_audio_steps=planned_audio_steps;
 observation.source_before=match->source_frames();observation.audio_phase_before=audio_phase;
 std::memcpy(observation.pads,pads,sizeof(observation.pads));observation.admitted=true;
}
void frame_effect_tick_returned(){if(observation.active)++observation.tick_returns;}
void frame_effect_begin_draw(){
 if(!observation.active||!observation.admitted)return;
 if(observation.first_draw){
  if(observation.tick_returns!=observation.draw_returns+1||!same_owner()||!running||!ready()){
   refuse("first_draw_callback_draw_order_changed");return;
  }
  if(observation.draw_returns!=0)return; // Later original draws are unobserved.
  observation.renderer_armed=aurora_browser_frame_effect_observer_arm_first_draw()!=0;
  if(!observation.renderer_armed)refuse("renderer_observer_arm_refused");
  return;
 }
 if(observation.tick_returns!=1||observation.draw_returns||observation.renderer_armed){
  refuse("ordinary_callback_draw_order_changed");return;
 }
 observation.renderer_armed=aurora_browser_frame_effect_observer_arm()!=0;
 if(!observation.renderer_armed)refuse("renderer_observer_arm_refused");
}
void frame_effect_draw_returned(){if(observation.active)++observation.draw_returns;}
void frame_effect_audio_returned(unsigned pcm_frames){
 if(!observation.requested||observation.terminal)return;
 if(!observation.active){
  ++observation.pre_tick_audio_returns;observation.pre_tick_pcm_frames+=pcm_frames;return;
 }
 ++observation.audio_returns;observation.pcm_frames+=pcm_frames;
}
void frame_effect_callback_aborted(const char* reason){
 if(!observation.requested||observation.terminal)return;
 refuse(reason);observation.source_after=match?match->source_frames():0;
 observation.audio_phase_after=audio_phase;stopped();
}
void frame_effect_callback_finished(unsigned steps,unsigned draws){
 if(!observation.requested||observation.terminal)return;
 if(!observation.active){
  observation.source_after=match?match->source_frames():0;
  observation.audio_phase_after=audio_phase;
  if(steps!=0||draws!=0)refuse("source_work_before_due_tick");
  else if(!same_owner())refuse("owner_changed_before_due_tick");
  else if(!match||observation.source_after!=observation.requested_frame)
   refuse("source_cursor_changed_before_due_tick");
  else if(!running)refuse("callback_stopped_before_due_tick");
  else if(!ready())refuse("match_not_ready_before_due_tick");
  if(*observation.error){stopped();return;}
  ++observation.pre_tick_callbacks;return;
 }
 observation.callback_steps=steps;observation.callback_draws=draws;
 observation.source_after=match?match->source_frames():0;observation.audio_phase_after=audio_phase;
 if(observation.first_draw){
  if(!observation.admitted||steps!=observation.planned_steps||draws!=steps||
     observation.tick_returns!=steps||observation.draw_returns!=draws||
     observation.source_after!=observation.source_before+steps||
     observation.audio_returns!=observation.planned_audio_steps||!observation.renderer_armed)
   refuse("first_draw_callback_count_mismatch");
 }else if(!observation.admitted||steps!=1||draws!=1||observation.tick_returns!=1||
    observation.draw_returns!=1||observation.source_after!=observation.source_before+1||
    observation.audio_returns!=observation.planned_audio_steps||!observation.renderer_armed)
  refuse("ordinary_callback_count_mismatch");
 if(!running||!ready()||!same_owner())refuse("ordinary_callback_guard_or_owner_changed");
 // This happens after the unchanged live timing/H1 checks and all source work.
 // No Resume is supported, even when admission or the frame failed.
 stopped();
}
}
using namespace melee_web_menu_browser;
extern "C" {
int melee_web_native_menu_frame_effect_arm(unsigned expected_frame){
 if(observation.requested||observation.terminal||!running||!ready()||
    match->source_frames()!=expected_frame)return 0;
 observation.owner=match.get();observation.owner_generation=asset_generation;
 observation.requested=true;observation.requested_frame=expected_frame;
 return 1;
}
int melee_web_native_menu_frame_effect_arm_first_draw(unsigned expected_frame){
 if(!melee_web_native_menu_frame_effect_arm(expected_frame))return 0;
 observation.first_draw=true;return 1;
}
const char* melee_web_native_menu_frame_effect_status(){
 static std::string out;
 if(observation.active)return "{\"busy\":true}";
 const auto renderer=aurora_browser_frame_effect_observer_snapshot();
 bool rows_complete=true;
 out=observation.first_draw?
     "{\"schema_version\":6,\"scope\":\"first_draw_in_normal_callback_observation_only\"":
     "{\"schema_version\":5,\"scope\":\"one_normal_callback_observation_only\"";
 const auto number=[&](const char* name,auto value){out+=",\"";out+=name;out+="\":";out+=std::to_string(value);};
 const auto boolean=[&](const char* name,bool value){out+=",\"";out+=name;out+=value?"\":true":"\":false";};
 boolean("requested",observation.requested);boolean("terminal",observation.terminal);
 boolean("native_retired",observation.native_retired);boolean("admitted",observation.admitted);
 boolean("renderer_armed",observation.renderer_armed);boolean("resumable",false);
 number("requested_source_frame",observation.requested_frame);
 number("owner_generation",observation.owner_generation);
 number("source_frame_before",observation.source_before);number("source_frame_after",observation.source_after);
 number("tick_returns",observation.tick_returns);number("draw_returns",observation.draw_returns);
 number("audio_returns",observation.audio_returns);number("pcm_frames",observation.pcm_frames);
 number("pre_tick_callbacks",observation.pre_tick_callbacks);
 number("pre_tick_audio_returns",observation.pre_tick_audio_returns);
 number("pre_tick_pcm_frames",observation.pre_tick_pcm_frames);
 number("planned_steps",observation.planned_steps);number("planned_audio_steps",observation.planned_audio_steps);
 number("callback_steps",observation.callback_steps);number("callback_draws",observation.callback_draws);
 number("audio_phase_before",observation.audio_phase_before);number("audio_phase_after",observation.audio_phase_after);
 if(observation.first_draw){
  number("selected_source_frame",observation.source_before?observation.source_before+1:0);
  number("unobserved_draws",observation.draw_returns?observation.draw_returns-1:0);
  const auto owners=aurora_browser_frame_effect_observer_depth_owners();
  out+=",\"selected_depth_owners\":{\"registered\":"+std::to_string(owners.registeredCount);
  number("completed",owners.completedCount);number("pending",owners.pendingCount);
  number("ignored_other_issuer",owners.ignoredOtherIssuerCount);
  boolean("finalized",owners.finalized!=0);out+='}';
 }
 out+=",\"error\":\"";out+=observation.error;out+="\",\"raw_pad_bytes\":[";
 const auto* bytes=reinterpret_cast<const unsigned char*>(observation.pads);
 for(size_t i=0;i<sizeof(observation.pads);++i){if(i)out+=',';out+=std::to_string(bytes[i]);}
 out+="],\"renderer\":{\"schema_version\":"+std::to_string(renderer.schemaVersion);
 number("state",renderer.state);number("first_blocker",renderer.firstBlocker);
 number("target_frame_index",renderer.targetFrameIndex);number("frame_index",renderer.frameIndex);
 // frameId and IDs are decimal strings: JS must not round uint64 values.
 out+=",\"frame_id\":\""+std::to_string(renderer.frameId)+"\"";
 number("event_count",renderer.eventCount);number("handle_count",renderer.handleCount);
 number("op_count",renderer.opCount);number("render_pass_count",renderer.renderPassCount);
 number("draw_command_count",renderer.drawCommandCount);number("custom_draw_count",renderer.customDrawCount);
 number("encoder_task_count",renderer.encoderTaskCount);number("texture_copy_count",renderer.textureCopyCount);
 number("texture_upload_count",renderer.textureUploadCount);number("palette_conversion_count",renderer.paletteConversionCount);
 number("resolve_count",renderer.resolveCount);number("snapshot_count",renderer.snapshotCount);
 number("staging_copy_count",renderer.stagingCopyCount);number("queue_write_count",renderer.queueWriteCount);
 out+=",\"queue_write_bytes\":\""+std::to_string(renderer.queueWriteBytes)+"\"";
 number("after_submit_callback_count",renderer.afterSubmitCallbackCount);
 number("submit_count",renderer.submitCount);number("fence_count",renderer.fenceCount);
 number("fence_success",renderer.fenceSuccess);
 out+=",\"finding_mask\":\""+std::to_string(renderer.findingMask)+"\"";
 number("command_record_count",renderer.commandRecordCount);number("actual_draw_count",renderer.actualDrawCount);
 number("resource_count",renderer.resourceCount);number("binding_entry_count",renderer.bindingEntryCount);
 number("layout_entry_count",renderer.layoutEntryCount);number("provenance_count",renderer.provenanceCount);
 number("attachment_fact_count",renderer.attachmentFactCount);
 number("depth_trace_count",renderer.depthTraceCount);number("depth_request_count",renderer.depthRequestCount);
 out+="},\"events\":[";
 for(uint32_t i=0;i<renderer.eventCount;++i){
  AuroraBrowserFrameEffectEvent e{};if(!aurora_browser_frame_effect_observer_event(i,&e)){rows_complete=false;break;}
  if(i)out+=',';out+="{\"kind\":"+std::to_string(e.kind);
  number("frame_index",e.frameIndex);number("op_index",e.opIndex);number("item_index",e.itemIndex);
  number("flags",e.flags);number("value0",e.value0);number("value1",e.value1);
  number("value2",e.value2);number("value3",e.value3);
  out+=",\"handle_a\":\""+std::to_string(e.handleA)+"\",\"handle_b\":\""+std::to_string(e.handleB)+"\"}";
 }
 out+="],\"handles\":[";
 for(uint32_t i=0;i<renderer.handleCount;++i){
  AuroraBrowserFrameEffectHandle h{};if(!aurora_browser_frame_effect_observer_handle(i,&h)){rows_complete=false;break;}
  if(i)out+=',';out+="{\"capture_id\":\""+std::to_string(h.captureId)+"\"";
  number("kind",h.kind);number("reserved",h.reserved);out+='}';
 }
 out+="],\"resources\":[";
 for(uint32_t i=0;i<renderer.resourceCount;++i){
  AuroraBrowserFrameEffectResource r{};
  if(!aurora_browser_frame_effect_observer_resource(i,&r)){rows_complete=false;break;}
  if(i)out+=',';
  out+="{\"capture_id\":\""+std::to_string(r.captureId)+"\",\"owner_id\":\""+std::to_string(r.ownerId)+"\"";
  number("kind",r.kind);number("word_count",r.wordCount);out+=",\"words\":[";
  for(unsigned j=0;j<16;++j){if(j)out+=',';out+=std::to_string(r.words[j]);}out+="]}";
 }
 out+="],\"binding_entries\":[";
 for(uint32_t i=0;i<renderer.bindingEntryCount;++i){
  AuroraBrowserFrameEffectBindingEntry b{};
  if(!aurora_browser_frame_effect_observer_binding_entry(i,&b)){rows_complete=false;break;}
  if(i)out+=',';
  out+="{\"bind_group_id\":\""+std::to_string(b.bindGroupId)+"\",\"resource_id\":\""+std::to_string(b.resourceId)+"\"";
  out+=",\"offset\":\""+std::to_string(b.offset)+"\",\"size\":\""+std::to_string(b.size)+"\"";
  number("binding",b.binding);number("kind",b.kind);out+='}';
 }
 out+="],\"layout_entries\":[";
 for(uint32_t i=0;i<renderer.layoutEntryCount;++i){
  AuroraBrowserFrameEffectLayoutEntry row{};
  if(!aurora_browser_frame_effect_observer_layout_entry(i,&row)){rows_complete=false;break;}
  if(i)out+=',';
  out+="{\"layout_id\":\""+std::to_string(row.layoutId)+"\"";
  number("entry_index",row.entryIndex);out+=",\"words\":[";
  for(unsigned j=0;j<15;++j){if(j)out+=',';out+=std::to_string(row.words[j]);}out+="]}";
 }
 out+="],\"provenance\":[";
 for(uint32_t i=0;i<renderer.provenanceCount;++i){
  AuroraBrowserFrameEffectProvenance row{};
  if(!aurora_browser_frame_effect_observer_provenance(i,&row)){rows_complete=false;break;}
  if(i)out+=',';
  out+="{\"capture_id\":\""+std::to_string(row.captureId)+"\",\"device_id\":\""+std::to_string(row.deviceId);
  out+="\",\"creation_serial\":\""+std::to_string(row.creationSerial)+"\",\"renderer_epoch\":\""+std::to_string(row.rendererEpoch)+"\"";
  number("role",row.role);number("slot",row.slot);out+='}';
 }
 out+="],\"attachment_facts\":[";
 for(uint32_t i=0;i<renderer.attachmentFactCount;++i){
  AuroraBrowserFrameEffectAttachmentFact row{};
  if(!aurora_browser_frame_effect_observer_attachment_fact(i,&row)){rows_complete=false;break;}
  if(i)out+=',';out+="{\"event_index\":"+std::to_string(row.eventIndex);
  number("kind",row.kind);number("aspects",row.aspects);number("reserved",row.reserved);
  out+=",\"view_id\":\""+std::to_string(row.viewId)+"\",\"resolve_view_id\":\""+std::to_string(row.resolveViewId)+"\",\"color_clear_bits\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+='\"';out+=std::to_string(row.colorClearBits[j]);out+='\"';}out+=']';
  number("depth_clear_bits",row.depthClearBits);number("stencil_clear_value",row.stencilClearValue);out+='}';
 }
 out+="],\"draws\":[";
 for(uint32_t i=0;i<renderer.actualDrawCount;++i){
  AuroraBrowserFrameEffectDraw d{};
  if(!aurora_browser_frame_effect_observer_draw(i,&d)){rows_complete=false;break;}
  if(i)out+=',';out+="{\"frame_index\":"+std::to_string(d.frameIndex);
  number("op_index",d.opIndex);number("command_index",d.commandIndex);number("renderer_kind",d.rendererKind);
  out+=",\"pipeline_key\":\""+std::to_string(d.pipelineKey)+"\",\"pipeline_id\":\""+std::to_string(d.pipelineId)+"\"";
  out+=",\"bind_group_ids\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+='\"';out+=std::to_string(d.bindGroupIds[j]);out+='\"';}
  out+="],\"dynamic_offset_count\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+=std::to_string(d.dynamicOffsetCount[j]);}
  out+="],\"dynamic_offsets\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+='[';for(unsigned k=0;k<4;++k){if(k)out+=',';out+=std::to_string(d.dynamicOffsets[j][k]);}out+=']';}
  out+="]";
  out+=",\"index_buffer_id\":\""+std::to_string(d.indexBufferId)+"\",\"index_offset\":\""+std::to_string(d.indexOffset)+"\",\"index_size\":\""+std::to_string(d.indexSize)+"\"";
  number("index_format",d.indexFormat);number("vertex_count",d.vertexCount);number("index_count",d.indexCount);
  number("instance_count",d.instanceCount);number("dst_alpha",d.dstAlpha);number("has_clear_color",d.hasClearColor);number("reserved",d.reserved);
  // Exact IEEE bits retain negative zero and NaN payloads; no decimal rounding.
  out+=",\"viewport_bits\":[";
  for(unsigned j=0;j<6;++j){if(j)out+=',';out+=std::to_string(std::bit_cast<uint32_t>(d.viewport[j]));}
  out+="],\"scissor\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+=std::to_string(d.scissor[j]);}
  out+="],\"clear_color_bits\":[";
  for(unsigned j=0;j<4;++j){if(j)out+=',';out+='\"';out+=std::to_string(std::bit_cast<uint64_t>(d.clearColor[j]));out+='\"';}out+="]}";
 }
 out+="]";boolean("rows_complete",rows_complete);out+="}";return out.c_str();
}
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
// Separate read-only component receipt. No submission_status/EventManager pump.
const char* melee_web_native_menu_frame_effect_components(){
 static std::string out;
 auto refused=[&](const char* reason){out="{\"schema\":\"melee-web-native-first-draw-components-v1\",\"copied\":false,\"component_ready\":false,\"refusal\":\"";out+=reason;out+="\",\"composite_eligible\":false}";return out.c_str();};
 if(!observation.terminal||observation.native_retired||!observation.first_draw||!observation.admitted||!same_owner()||*observation.error)
  return refused("original_boundary_missing");
 if(observation.tick_returns!=1||observation.draw_returns!=1||observation.planned_steps!=1||observation.callback_steps!=1||observation.callback_draws!=1)
  return refused("exact_one_tick_draw_required");
 if(!EM_ASM_INT({return Module.matchFrameEffectCallbackReturned===true&&!ABORT&&
      Asyncify.state===Asyncify.State.Normal&&Asyncify.currData===null?1:0;}))return refused("callback_not_returned_idle");
 const auto renderer=aurora_browser_frame_effect_observer_snapshot();
 if(!observation.renderer_armed||renderer.fenceCount!=1||renderer.fenceSuccess!=1)return refused("normal_fence_missing");
 if(melee_web_source_file_drive_busy())return refused("source_reads_pending");
 aurora::gfx::detail::NativeSubmissionPendingFacts facts{};
 if(!aurora::gfx::capture_native_submission_pending(facts))return refused("native_owner_book_unavailable");
 const bool ready=aurora::gfx::detail::native_submission_component_ready(facts);
 out="{\"schema\":\"melee-web-native-first-draw-components-v1\",\"copied\":true,\"component_ready\":";
 out+=ready?"true":"false";out+=",\"refusal\":\"\",\"composite_eligible\":false,\"frame_slot_capacity\":";
 out+=std::to_string(aurora::gfx::detail::FrameSlotCount);out+=",\"staging_slot_capacity\":";
 out+=std::to_string(aurora::gfx::detail::StagingBufferCount);
 auto u32=[&](const char* key,uint32_t value){out+=",\"";out+=key;out+="\":";out+=std::to_string(value);};
 auto u64=[&](const char* key,uint64_t value){out+=",\"";out+=key;out+="\":\"";out+=std::to_string(value);out+='"';};
 out+=",\"facts\":{\"schema_version\":"+std::to_string(facts.schemaVersion);u32("owner_book_present",facts.ownerBookPresent);
 auto stamp=[&](const char* key,const aurora::gfx::detail::NativeSubmissionObservationStamp& v){out+=",\"";out+=key;out+="\":{\"generation\":\""+std::to_string(v.generation)+'"';
  u64("registration_sequence",v.registrationSequence);u64("callback_sequence",v.callbackSequence);u64("stale_callback_count",v.staleCallbackCount);
  u64("queue_handle",v.queueHandle);u64("last_begin_frame_id",v.lastBeginFrameId);u64("last_end_frame_id",v.lastEndFrameId);
  u32("frame_index",v.frameIndex);u32("active_frame_count",v.activeFrameCount);u32("active_staging_count",v.activeStagingCount);
  u32("free_frame_count",v.freeFrameCount);u32("free_staging_count",v.freeStagingCount);u32("worker_idle",v.workerIdle);u32("process_events_queued",v.processEventsQueued);out+='}';};
 stamp("before",facts.before);stamp("after",facts.after);
 const auto& p=facts.packets;out+=",\"packets\":{\"schema_version\":"+std::to_string(p.schemaVersion);u32("active_slot_count",p.activeSlotCount);
 out+=",\"recorder\":{\"active\":"+std::to_string(p.recorder.active);u32("in_offscreen",p.recorder.inOffscreen);u32("suspended_efb_pass",p.recorder.hasSuspendedEfbPass);out+="},\"slots\":[";
 for(size_t i=0;i<p.slots.size();++i){if(i)out+=',';const auto& v=p.slots[i];out+="{\"slot\":"+std::to_string(v.slot);
  u32("frame_index",v.frameIndex);u32("encoder_present",v.encoderPresent);u64("frame_id",v.frameId);
  u64("render_pass_count",v.renderPassCount);u64("texture_copy_count",v.textureCopyCount);u64("encoder_task_count",v.encoderTaskCount);u64("op_count",v.opCount);
  u64("texture_upload_count",v.textureUploadCount);u64("after_submit_callback_count",v.afterSubmitCallbackCount);
  u64("verts_bytes",v.vertsBytes);u64("uniforms_bytes",v.uniformsBytes);u64("indices_bytes",v.indicesBytes);u64("storage_bytes",v.storageBytes);u64("texture_upload_bytes",v.textureUploadBytes);out+='}';}
 out+=" ]},\"owners\":[";
 for(size_t i=0;i<facts.owners.size();++i){if(i)out+=',';const auto& v=facts.owners[i];out+="{\"state\":"+std::to_string(v.state);
  u64("future_id",v.futureId);u64("registration_sequence",v.registrationSequence);u64("callback_sequence",v.callbackSequence);u64("captured_generation",v.capturedGeneration);u64("queue_handle",v.queueHandle);u64("encoder_handle",v.encoderHandle);out+='}';}
 out+="]}}";return out.c_str();
}
#endif
int melee_web_native_menu_frame_effect_retire(){try{
 check(observation.terminal&&!observation.native_retired,
       "Frame effect retirement requires a stopped, unretired callback");
 check(EM_ASM_INT({return Module.matchFrameEffectCallbackReturned===true&&
                         !ABORT&&Asyncify.state===Asyncify.State.Normal&&Asyncify.currData===null&&
                         typeof window.menuMatchFrameEffectRetired==='function'?1:0;}),
       "Frame effect callback has not returned or runtime retirement hook is absent");
 const auto renderer=aurora_browser_frame_effect_observer_snapshot();
 // A failed observer may have stopped collecting before Submit. Such a run
 // remains a failure and is retired by its external browser owner if this
 // checked native path refuses; no missing normal fence is fabricated.
 const auto submission=aurora_browser_submission_status();
 check(submission.pendingFramePackets==0&&submission.pendingStagingBuffers==0&&submission.workerBusy==0,
       "Frame effect retirement refuses outstanding GPU work");
 check(!observation.renderer_armed||(renderer.fenceCount==1&&renderer.fenceSuccess==1),
       "Frame effect retirement requires its successful normal queue fence");
 check(!melee_web_source_file_drive_busy(),"Frame effect retirement refuses pending source reads");
 if(observation.first_draw&&observation.renderer_armed)
  check(aurora_browser_frame_effect_observer_finalize_first_draw()!=0,
        "First-draw observation refuses pending selected map callbacks");
 close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();
 observation.native_retired=true;
 check(EM_ASM_INT({return window.menuMatchFrameEffectRetired()===true?1:0;}),
       "Frame effect native retirement completed but runtime notification was refused");
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
}
