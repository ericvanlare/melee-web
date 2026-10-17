#include "gameplay_menu_browser_state.hpp"
#include "gameplay_menu_frame_effect.hpp"
#include <cstddef>
#include <cstring>
#include <bit>
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
#include "lib/gfx/frame_pending_facts.hpp"
#include "lib/gfx/frame_effect_observer.hpp"
#include "lib/gx/selected_texture_readonly_diagnostic.hpp"
#include "natural_one_tick_enrollment.hpp"
#endif
static_assert(sizeof(PADStatus)==16&&offsetof(PADStatus,err)==10&&offsetof(PADStatus,extButton)==12,
              "Frame observation serializes four native PADStatus records, including Aurora extButton and padding");

namespace melee_web_menu_browser {
namespace {
struct FrameEffectObservation {
 bool requested=false, active=false, terminal=false, admitted=false, renderer_armed=false;
 bool native_retired=false, first_draw=false;
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 bool selected_texture_issue_attempted=false,selected_texture_issued=false;
#endif
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
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
melee_web::detail::NaturalOneTickEnrollment natural_one;
#endif
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
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.pending())natural_one.begin_callback(match?match->source_frames():0,audio_phase);
#endif
 // A request remains pending through source-free callbacks. The first call to
 // frame_effect_begin_tick below marks the naturally due source callback.
}
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
void frame_effect_native_plan(unsigned steps,unsigned audio_steps,bool stalled,bool construction){
 natural_one.plan(steps,audio_steps,stalled,construction);
}
#endif
void frame_effect_begin_tick(const PADStatus pads[4],unsigned prior_steps,
                             unsigned planned_steps,unsigned planned_audio_steps,
                             bool audio_before_construction,bool clock_stalled){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.pending()&&natural_one.before_tick(reinterpret_cast<uintptr_t>(match.get()),asset_generation,match?match->source_frames():0,
                           prior_steps,planned_steps,planned_audio_steps,running&&ready()&&!clock_stalled&&!audio_before_construction)){
  // The original plan is already known. No clock/budget/input/source call is changed.
  observation.owner=match.get();observation.owner_generation=asset_generation;
  observation.requested=true;observation.first_draw=true;observation.requested_frame=match->source_frames();
 }
#endif
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
void frame_effect_tick_returned(){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 natural_one.tick_returned();
#endif
 if(observation.active)++observation.tick_returns;
}
void frame_effect_begin_draw(){
 if(!observation.active||!observation.admitted)return;
 if(observation.first_draw){
  if(observation.tick_returns!=observation.draw_returns+1||!same_owner()||!running||!ready()){
   refuse("first_draw_callback_draw_order_changed");return;
  }
  if(observation.draw_returns!=0)return; // Later original draws are unobserved.
  observation.renderer_armed=aurora_browser_frame_effect_observer_arm_first_draw()!=0;
  if(!observation.renderer_armed)refuse("renderer_observer_arm_refused");
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
  else aurora::gx::request_selected_sampled_texture();
#endif
  return;
 }
 if(observation.tick_returns!=1||observation.draw_returns||observation.renderer_armed){
  refuse("ordinary_callback_draw_order_changed");return;
 }
 observation.renderer_armed=aurora_browser_frame_effect_observer_arm()!=0;
 if(!observation.renderer_armed)refuse("renderer_observer_arm_refused");
}
void frame_effect_draw_returned(){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 natural_one.draw_returned();
#endif
 if(observation.active)++observation.draw_returns;
}
void frame_effect_audio_returned(unsigned pcm_frames){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 natural_one.audio_returned(pcm_frames);
#endif
 if(!observation.requested||observation.terminal)return;
 if(!observation.active){
  ++observation.pre_tick_audio_returns;observation.pre_tick_pcm_frames+=pcm_frames;return;
 }
 ++observation.audio_returns;observation.pcm_frames+=pcm_frames;
}
void frame_effect_callback_aborted(const char* reason){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.requested()){natural_one.aborted();observation.first_draw=true;refuse(reason);stopped();return;}
#endif
 if(!observation.requested||observation.terminal)return;
 refuse(reason);observation.source_after=match?match->source_frames():0;
 observation.audio_phase_after=audio_phase;stopped();
}
void frame_effect_callback_finished(unsigned steps,unsigned draws){
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.requested())natural_one.end_callback(reinterpret_cast<uintptr_t>(match.get()),asset_generation,match?match->source_frames():0,
                          audio_phase,steps,draws,running&&ready());
 if(natural_one.facts.state==melee_web::detail::NaturalOneTickEnrollment::Failed){
  observation.first_draw=true;refuse(natural_one.facts.error);stopped();return;
 }
#endif
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
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
 if(natural_one.requested())return 0;
#endif
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
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
int melee_web_native_menu_frame_effect_request_natural_one(unsigned expected_frame){
 if(observation.requested||observation.terminal||!running||!ready()||match->source_frames()!=expected_frame)return 0;
 return natural_one.request(reinterpret_cast<uintptr_t>(match.get()),asset_generation,expected_frame,audio_phase)?1:0;
}
const char* melee_web_native_menu_frame_effect_natural_one_enrollment(){
 if(natural_one.facts.callback_open||observation.active||!observation.terminal)return "{\"busy\":true}";
 static std::string out;const auto& f=natural_one.facts;
 out="{\"schema\":\"melee-web-natural-one-tick-enrollment-v1\",\"copied\":true";
 const auto number=[&](const char* name,auto v){out+=",\"";out+=name;out+="\":";out+=std::to_string(v);};
 const auto wide=[&](const char* name,uint64_t v){out+=",\"";out+=name;out+="\":\"";out+=std::to_string(v);out+='\"';};
 number("state",f.state);number("minimum_frame",natural_one.MinimumFrame);number("maximum_frame",natural_one.MaximumFrame);
 number("origin_frame",f.origin_frame);number("selected_frame",f.selected_frame);number("owner_generation",f.owner_generation);number("origin_audio_phase",f.origin_audio_phase);
 number("row_capacity",natural_one.RowCapacity);number("row_count",f.row_count);
 wide("callback_sequence",f.callback_sequence);wide("selection_callback",f.selection_callback);wide("skipped_callbacks",f.skipped_callbacks);
 wide("skipped_ticks",f.skipped_ticks);wide("skipped_draws",f.skipped_draws);wide("skipped_audio",f.skipped_audio);wide("skipped_pcm_frames",f.skipped_pcm_frames);
 wide("source_free_callbacks",f.source_free_callbacks);wide("source_free_audio",f.source_free_audio);wide("source_free_pcm_frames",f.source_free_pcm_frames);
 out+=",\"callback_open\":";out+=f.callback_open?"true":"false";out+=",\"error\":\"";out+=f.error;out+="\",\"rows\":[";
 for(uint32_t i=0;i<f.row_count;++i){const auto& row=f.rows[i];if(i)out+=',';out+='{';
  out+="\"callback_sequence\":\""+std::to_string(row.callback_sequence)+"\"";
  number("source_before",row.source_before);number("source_after",row.source_after);number("planned_steps",row.planned_steps);number("planned_audio_steps",row.planned_audio_steps);
  number("tick_returns",row.tick_returns);number("draw_returns",row.draw_returns);number("audio_returns",row.audio_returns);number("pcm_frames",row.pcm_frames);
  number("audio_phase_before",row.audio_phase_before);number("audio_phase_after",row.audio_phase_after);out+=",\"selected\":";out+=row.selected?"true":"false";out+='}';
 }
 out+="]}";return out.c_str();
}
#endif
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
namespace {
const char* copy_ready_first_draw_component_facts(aurora::gfx::detail::NativeSubmissionPendingFacts& facts){
 if(!observation.terminal||running||observation.native_retired||!observation.first_draw||!observation.admitted||!same_owner()||*observation.error)
  return "original_boundary_missing";
 if(observation.tick_returns!=1||observation.draw_returns!=1||observation.planned_steps!=1||observation.callback_steps!=1||observation.callback_draws!=1)
  return "exact_one_tick_draw_required";
 if(!EM_ASM_INT({return Module.matchFrameEffectCallbackReturned===true&&!ABORT&&
      Asyncify.state===Asyncify.State.Normal&&Asyncify.currData===null?1:0;}))return "callback_not_returned_idle";
 const auto renderer=aurora_browser_frame_effect_observer_snapshot();
 if(!observation.renderer_armed||renderer.submitCount!=1||renderer.fenceCount!=1||renderer.fenceSuccess!=1)return "normal_fence_missing";
 if(melee_web_source_file_drive_busy())return "source_reads_pending";
 if(!aurora::gfx::capture_native_submission_pending(facts))return "native_owner_book_unavailable";
 if(observation.selected_texture_issued&&!aurora::gx::selected_sampled_texture_readonly_guard_current())return "selected_texture_current_owner_mismatch";
 return nullptr;
}
}
// Separate read-only component receipt. No submission_status/EventManager pump.
const char* melee_web_native_menu_frame_effect_components(){
 static std::string out;
 auto refused=[&](const char* reason){out="{\"schema\":\"melee-web-native-first-draw-components-v1\",\"copied\":false,\"component_ready\":false,\"refusal\":\"";out+=reason;out+="\",\"composite_eligible\":false}";return out.c_str();};
 aurora::gfx::detail::NativeSubmissionPendingFacts facts{};
 if(const auto* reason=copy_ready_first_draw_component_facts(facts))return refused(reason);
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
// Explicit consume-once mutating service; never folded into the read-only getter.
int melee_web_native_menu_frame_effect_issue_selected_texture(){
 if(observation.selected_texture_issue_attempted){refuse("selected_texture_duplicate_issue");return 0;}
 observation.selected_texture_issue_attempted=true;
 aurora::gfx::detail::NativeSubmissionPendingFacts facts{};
 if(const auto* reason=copy_ready_first_draw_component_facts(facts)){refuse(reason);return 0;}
 if(!aurora::gfx::detail::native_submission_component_ready(facts)){refuse("selected_texture_native_not_ready");return 0;}
 aurora::gfx::detail::BrowserFrameEffectObserver::SelectedSampledTextureWitness witness{};
 const auto& observer=aurora::gfx::detail::browser_frame_effect_observer();
 if(!observer.submitted_selected_sampled_texture_witness(witness)||
    witness.frameId!=facts.before.lastEndFrameId||witness.queue!=facts.before.queueHandle){
  refuse("selected_texture_submitted_owner_mismatch");return 0;
 }
 aurora::gx::SelectedTextureIssueRefusal issue_refusal=aurora::gx::SelectedTextureIssueRefusal::None;
 const int issued=aurora::gx::issue_selected_sampled_texture_readonly_guard(issue_refusal);
 if(issued!=1){refuse(aurora::gx::selected_texture_issue_refusal_literal(issue_refusal));return 0;}
 observation.selected_texture_issued=true;return 1;
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
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
// Separate additive getter: the historical frame-effect receipt is unchanged.
const char* melee_web_native_menu_frame_effect_color_conversions(){
 static std::string out;const auto& owner=aurora::gfx::detail::browser_frame_effect_observer();
 const auto snapshot=owner.snapshot();
 out=R"({"schema":"melee-web-color-conversion-facts-v1","packet_frame_id":")"+
   std::to_string(snapshot.frameId)+R"(","internal_gpu_creation_ownership_qualified":false,"packet_content_qualified":false,"restore_eligible":false,"composite_eligible":false,"overflow":)"+
   std::string(owner.color_conversion_overflow()?"true":"false")+R"(,"facts":[)";
 for(uint32_t i=0;i<owner.color_conversion_count();++i){
  aurora::gfx::detail::BrowserFrameEffectObserver::ColorConversionFact f{};
  if(!owner.color_conversion_fact(i,f))break;
  if(i)out+=',';out+='{';
  const auto u=[&](const char* key,uint64_t v){out+='"';out+=key;out+='"';out+=':';out+=std::to_string(v);out+=',';};
  const auto id=[&](const char* key,uint64_t v){out+='"';out+=key;out+='"';out+=':';out+='"';out+=std::to_string(v);out+='"';out+=',';};
  u("event_index",f.eventIndex);u("refusal",f.refusal);u("returned",f.returned);u("gx_format",f.gxFormat);u("filter",f.filter);u("draw_count",f.drawCount);u("load_op",f.loadOp);u("store_op",f.storeOp);
  id("uniform_offset",f.uniformOffset);id("uniform_size",f.uniformSize);
  id("source_texture",f.sourceTexture);id("source_view",f.sourceView);id("destination_texture",f.destinationTexture);id("destination_view",f.destinationView);id("uniform_buffer",f.uniformBuffer);
  id("local_call_pipeline",f.localPipeline);id("local_call_group",f.localGroup);id("local_call_sampler",f.localSampler);
  out+=R"("rect":[)";for(unsigned j=0;j<4;++j){if(j)out+=',';out+=std::to_string(f.rect[j]);}
  out+=R"(],"uv_bits":[)";for(unsigned j=0;j<4;++j){if(j)out+=',';out+=std::to_string(f.uvBits[j]);}
  out+=R"(],"clear_bits":[)";for(unsigned j=0;j<4;++j){if(j)out+=',';out+='"';out+=std::to_string(f.clearBits[j]);out+='"';}
  out+="]}";
 }
 out+="]}";return out.c_str();
}

#endif

}

#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC) && defined(MELEE_WEB_FIRST_PAIR_AUDIO_FILE_DIAGNOSTIC)
namespace melee_web_menu_browser {
bool frame_effect_host_read_boundary_ready(){
 aurora::gfx::detail::NativeSubmissionPendingFacts facts{};
 return copy_ready_first_draw_component_facts(facts)==nullptr&&
        aurora::gfx::detail::native_submission_component_ready(facts);
}
}
#endif
