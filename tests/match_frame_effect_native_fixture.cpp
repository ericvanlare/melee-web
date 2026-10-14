#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <bit>
#include <cstdio>
#include <dolphin/pad.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "source_frame_sequence.hpp"
// ACTUAL_AURORA_API
constexpr int St_Kind_Last=32, CKIND_MARIO=0, Gm_PKind_NA=3;
struct Stats { unsigned queuedPipelines=0; } stats;
struct Submission { unsigned pendingFramePackets=0,pendingStagingBuffers=0,workerBusy=0; } submission;
struct Preparation { bool busy_value=false; bool busy() const{return busy_value;} };
struct Start { struct {int stkind=St_Kind_Last;} rules; struct {int ckind=CKIND_MARIO,slot_type=Gm_PKind_NA;} players[6]; };
namespace melee_web {
struct GameplayMatchSession {
 unsigned frame=200; Start start; bool ready_value=true;
 bool ready()const{return ready_value;} bool paused()const{return false;} bool ending()const{return false;}
 bool complete()const{return false;} bool opening_demo()const{return false;} bool sudden_death()const{return false;}
 const Start& start_data()const{return start;}
 struct Player{unsigned stocks=4;}; Player player_stats(unsigned)const{return {};}
 unsigned source_frames()const{return frame;}
};
}
namespace melee_web_menu_browser {
std::unique_ptr<melee_web::GameplayMatchSession> match;
Preparation preparation;
bool faulted=false,pending=false,replay=false,results=false,prize=false,running=true,transition_audio_continues=false;
int stock_check=0; unsigned diagnostic_start_ticks=0,diagnostic_pad_remaining=0,audio_phase=0,asset_generation=1;
std::string message;
}
bool net=false,source_busy=false,arm_allowed=true,em_answer=true,throw_close=false;
unsigned gpu_arms=0,cancel_calls=0,returned_tasks=0;
std::vector<std::string> retired_calls;
AuroraBrowserFrameEffectSnapshot gpu{};
AuroraBrowserFrameEffectDepthOwners depth_owners{};
int melee_web_net_active(){return net;}
int melee_web_source_file_drive_busy(){return source_busy;}
const Stats* aurora_get_stats(){return &stats;}
Submission aurora_browser_submission_status(){return submission;}
int aurora_browser_frame_effect_observer_arm(){++gpu_arms;return arm_allowed;}
int aurora_browser_frame_effect_observer_arm_first_draw(){++gpu_arms;return arm_allowed;}
AuroraBrowserFrameEffectDepthOwners aurora_browser_frame_effect_observer_depth_owners(){return depth_owners;}
int aurora_browser_frame_effect_observer_finalize_first_draw(){
 if(depth_owners.pendingCount)return 0;depth_owners.finalized=1;return 1;
}
AuroraBrowserFrameEffectSnapshot aurora_browser_frame_effect_observer_snapshot(){return gpu;}
std::vector<AuroraBrowserFrameEffectEvent> event_rows;
std::vector<AuroraBrowserFrameEffectHandle> handle_rows;
std::vector<AuroraBrowserFrameEffectResource> resource_rows;
std::vector<AuroraBrowserFrameEffectBindingEntry> binding_rows;
std::vector<AuroraBrowserFrameEffectDraw> draw_rows;
std::vector<AuroraBrowserFrameEffectLayoutEntry> layout_rows;
std::vector<AuroraBrowserFrameEffectProvenance> provenance_rows;
unsigned refused_category=0;
template<typename T> int fixture_row(unsigned category,uint32_t index,T* out,const std::vector<T>& rows){
 if(refused_category==category||index>=rows.size())return 0;*out=rows[index];return 1;
}
int aurora_browser_frame_effect_observer_event(uint32_t i,AuroraBrowserFrameEffectEvent* out){return fixture_row(1,i,out,event_rows);}
int aurora_browser_frame_effect_observer_handle(uint32_t i,AuroraBrowserFrameEffectHandle* out){return fixture_row(2,i,out,handle_rows);}
int aurora_browser_frame_effect_observer_resource(uint32_t i,AuroraBrowserFrameEffectResource* out){return fixture_row(3,i,out,resource_rows);}
int aurora_browser_frame_effect_observer_binding_entry(uint32_t i,AuroraBrowserFrameEffectBindingEntry* out){return fixture_row(4,i,out,binding_rows);}
int aurora_browser_frame_effect_observer_layout_entry(uint32_t i,AuroraBrowserFrameEffectLayoutEntry* out){return fixture_row(6,i,out,layout_rows);}
int aurora_browser_frame_effect_observer_provenance(uint32_t i,AuroraBrowserFrameEffectProvenance* out){return fixture_row(7,i,out,provenance_rows);}
int aurora_browser_frame_effect_observer_draw(uint32_t i,AuroraBrowserFrameEffectDraw* out){return fixture_row(5,i,out,draw_rows);}
void emscripten_cancel_main_loop(){++cancel_calls;}
void close(){retired_calls.push_back("close");if(throw_close)throw std::runtime_error("close failed");melee_web_menu_browser::match.reset();}
void melee_web_input_shutdown(){retired_calls.push_back("input");}
void aurora_shutdown(){retired_calls.push_back("gpu");}
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
#define EM_ASM(...) (++returned_tasks)
#define EM_ASM_INT(...) (em_answer?1:0)
// ACTUAL_OBSERVER_SOURCE

void reset(){
 using namespace melee_web_menu_browser;
 observation={};match=std::make_unique<melee_web::GameplayMatchSession>();
 preparation={};faulted=pending=replay=results=prize=transition_audio_continues=false;running=true;
 stock_check=0;diagnostic_start_ticks=diagnostic_pad_remaining=audio_phase=0;asset_generation=1;
 depth_owners={};net=source_busy=throw_close=false;arm_allowed=em_answer=true;stats={};submission={};gpu={};
 gpu_arms=cancel_calls=returned_tasks=0;retired_calls.clear();message.clear();
 event_rows.clear();handle_rows.clear();resource_rows.clear();binding_rows.clear();draw_rows.clear();layout_rows.clear();provenance_rows.clear();refused_category=0;
}
void request(){assert(melee_web_native_menu_frame_effect_arm(200)==1);frame_effect_callback_started();}
void begin(unsigned planned_steps=1,bool clock_stalled=false){PADStatus pads[4]{};pads[1].button=0x100;pads[1].extButton=0x12345678;
 reinterpret_cast<unsigned char*>(pads)[11]=0xa5;
 frame_effect_begin_tick(pads,0,planned_steps,1,false,clock_stalled);
 if(observation.admitted)assert(std::memcmp(observation.pads,pads,sizeof(pads))==0);
}
void tick_and_draw(){++match->frame;frame_effect_tick_returned();frame_effect_audio_returned(533);audio_phase=(audio_phase+20)%60;
 frame_effect_begin_draw();frame_effect_draw_returned();frame_effect_callback_finished(1,1);}

void schema2_serialization_control(){
 reset();request();begin();tick_and_draw();
 gpu.schemaVersion=4;gpu.state=4;gpu.targetFrameIndex=gpu.frameIndex=3;gpu.frameId=9007199254740993ULL;
 gpu.submitCount=gpu.fenceCount=gpu.fenceSuccess=1;gpu.firstBlocker=7;gpu.findingMask=128|(uint64_t{1}<<27);
 gpu.commandRecordCount=gpu.actualDrawCount=1;gpu.opCount=gpu.renderPassCount=gpu.drawCommandCount=1;
 gpu.resourceCount=4;gpu.bindingEntryCount=1;gpu.handleCount=5;gpu.eventCount=8;gpu.layoutEntryCount=2;gpu.provenanceCount=2;
 gpu.queueWriteBytes=UINT64_MAX;
 handle_rows={{1,7,0},{2,6,0},{3,3,0},{4,5,0},{5,9,0}};
 resource_rows.resize(4);
 resource_rows[0].captureId=2;resource_rows[0].ownerId=1;resource_rows[0].kind=6;resource_rows[0].wordCount=2;
 resource_rows[0].words[0]=resource_rows[0].words[1]=UINT32_MAX;
 resource_rows[1].captureId=3;resource_rows[1].ownerId=1;resource_rows[1].kind=3;resource_rows[1].wordCount=5;resource_rows[1].words[0]=128;resource_rows[1].words[2]=4;
 resource_rows[2].captureId=4;resource_rows[2].ownerId=1;resource_rows[2].kind=5;resource_rows[2].wordCount=5;resource_rows[2].words[2]=1;
 resource_rows[3].captureId=5;resource_rows[3].ownerId=1;resource_rows[3].kind=9;resource_rows[3].wordCount=1;resource_rows[3].words[0]=2;
 layout_rows.resize(2);layout_rows[0].layoutId=layout_rows[1].layoutId=5;layout_rows[1].entryIndex=1;layout_rows[1].words[0]=1;
 provenance_rows={{5,1,9007199254740993ULL,UINT64_MAX,6,0},{3,1,9007199254740994ULL,UINT64_MAX,3,0}};
 binding_rows={{4,3,9007199254740993ULL,UINT64_MAX,2,3}};
 draw_rows.resize(1);auto& d=draw_rows[0];d.frameIndex=3;d.rendererKind=2;d.pipelineKey=UINT64_MAX;d.pipelineId=2;
 d.bindGroupIds[0]=4;d.dynamicOffsetCount[0]=4;d.dynamicOffsets[0][0]=1;d.dynamicOffsets[0][1]=2;d.dynamicOffsets[0][2]=3;d.dynamicOffsets[0][3]=UINT32_MAX;
 d.indexBufferId=3;d.indexOffset=9007199254740993ULL;d.indexSize=UINT64_MAX;d.indexFormat=1;
 d.vertexCount=3;d.indexCount=6;d.instanceCount=1;d.dstAlpha=255;d.hasClearColor=1;
 d.viewport[0]=std::bit_cast<float>(uint32_t{0x80000000});d.viewport[1]=std::bit_cast<float>(uint32_t{0x7fc00001});
 d.viewport[2]=d.viewport[3]=d.viewport[5]=1;d.scissor[2]=640;d.scissor[3]=480;
 d.clearColor[0]=std::bit_cast<double>(uint64_t{0x8000000000000000});d.clearColor[1]=std::bit_cast<double>(uint64_t{0x7ff8000000000001});d.clearColor[3]=1;
 event_rows.resize(8);for(auto& e:event_rows)e.frameIndex=3;
 event_rows[0].kind=19;event_rows[0].flags=2;event_rows[0].value0=1;
 auto& detail=event_rows[1];detail.kind=20;detail.flags=2;detail.value0=3;detail.value1=6;detail.value2=1;detail.value3=255;detail.handleA=2;detail.handleB=3;
 for(unsigned i=0;i<4;++i){auto& e=event_rows[i+2];e.kind=21;e.flags=i;if(i==0){e.value0=4;e.value1=1;e.value2=2;e.value3=3;e.handleA=4;}}
 event_rows[6].kind=17;event_rows[7].kind=18;event_rows[7].flags=1;
 AuroraBrowserFrameEffectResource attachmentTexture{};attachmentTexture.captureId=6;attachmentTexture.ownerId=1;attachmentTexture.kind=2;attachmentTexture.wordCount=10;
 attachmentTexture.words[0]=640;attachmentTexture.words[1]=480;attachmentTexture.words[2]=1;attachmentTexture.words[3]=1;attachmentTexture.words[4]=2;attachmentTexture.words[5]=attachmentTexture.words[6]=1;
 AuroraBrowserFrameEffectResource attachmentView{};attachmentView.captureId=7;attachmentView.ownerId=6;attachmentView.kind=1;attachmentView.wordCount=10;attachmentView.words[0]=6;
 resource_rows.push_back(attachmentTexture);resource_rows.push_back(attachmentView);handle_rows.push_back({6,2,0});handle_rows.push_back({7,1,0});
 provenance_rows.push_back({6,1,9007199254740995ULL,UINT64_MAX,15,2});provenance_rows.push_back({7,1,9007199254740996ULL,UINT64_MAX,16,2});
 gpu.resourceCount+=2;gpu.handleCount+=2;gpu.provenanceCount+=2;
 const std::string complete=melee_web_native_menu_frame_effect_status();
 assert(complete.find("\"schema_version\":4")!=std::string::npos);
 assert(complete.find("\"rows_complete\":true")!=std::string::npos);
 assert(complete.find("\"viewport_bits\":[2147483648,2143289345,1065353216,1065353216,0,1065353216]")!=std::string::npos);
 assert(complete.find("\"clear_color_bits\":[\"9223372036854775808\",\"9221120237041090561\",\"0\",\"4607182418800017408\"]")!=std::string::npos);
 assert(complete.find("\"layout_entries\":[{\"layout_id\":\"5\"")!=std::string::npos);
 assert(complete.find("\"creation_serial\":\"9007199254740993\"")!=std::string::npos);
 std::printf("SCHEMA2_STATUS %s\n",complete.c_str());
 for(unsigned category=1;category<=7;++category){
  refused_category=category;const std::string failed=melee_web_native_menu_frame_effect_status();
  assert(failed.find("\"rows_complete\":false")!=std::string::npos);
 }
 refused_category=0;
}


void first_draw_actual_tail_control(unsigned plan){
 reset();assert(melee_web_native_menu_frame_effect_arm_first_draw(200)==1);
 frame_effect_callback_started();melee_web::SourceFrameSequence sequence;
 std::vector<std::string> calls;
 const auto present=[&](){frame_effect_begin_draw();calls.push_back("draw");frame_effect_draw_returned();return true;};
 PADStatus pads[4]{};
 for(unsigned i=0;i<plan;++i){
  sequence.before_step(present);pads[0].button=static_cast<uint16_t>(0x100+i);
  frame_effect_begin_tick(pads,sequence.steps(),plan,1,false,false);
  calls.push_back("tick");++match->frame;frame_effect_tick_returned();sequence.did_step();
 }
 calls.push_back("audio");frame_effect_audio_returned(533);audio_phase=20;
 sequence.finish(present);frame_effect_callback_finished(sequence.steps(),sequence.draws());
 assert(sequence.steps()==plan&&sequence.draws()==plan&&match->frame==200+plan);
 if(plan==0){
  assert(!observation.active&&!observation.terminal&&running&&gpu_arms==0);
  assert(observation.pre_tick_callbacks==1&&observation.pre_tick_audio_returns==1);
  assert((calls==std::vector<std::string>{"audio"}));
  return;
 }
 assert(observation.admitted&&observation.terminal&&!*observation.error&&!running);
 assert(observation.tick_returns==plan&&observation.draw_returns==plan&&observation.audio_returns==1);
 assert(observation.pads[0].button==0x100&&observation.source_before==200&&observation.source_after==200+plan);
 assert(gpu_arms==1&&cancel_calls==1&&returned_tasks==1);
 if(plan==2)assert((calls==std::vector<std::string>{"tick","draw","tick","audio","draw"}));
 gpu.fenceCount=gpu.fenceSuccess=1;
 depth_owners.registeredCount=1;depth_owners.pendingCount=1;
 assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.empty());
 depth_owners.completedCount=1;depth_owners.pendingCount=0;
 assert(melee_web_native_menu_frame_effect_retire()==1&&depth_owners.finalized==1);
 assert((retired_calls==std::vector<std::string>{"close","input","gpu"}));
 assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.size()==3);
}

void first_draw_guard_controls(){
 reset();assert(melee_web_native_menu_frame_effect_arm_first_draw(200)==1);
 PADStatus pads[4]{};
 frame_effect_begin_tick(pads,0,2,1,false,true);
 assert(!observation.admitted&&gpu_arms==0);
 frame_effect_callback_aborted("later_abort");
 assert(std::string(observation.error)=="first_draw_callback_tick_guard_changed");
 reset();assert(melee_web_native_menu_frame_effect_arm_first_draw(200)==1);
 frame_effect_begin_tick(pads,0,2,1,false,false);++match->frame;frame_effect_tick_returned();
 frame_effect_begin_draw();frame_effect_draw_returned();++asset_generation;
 frame_effect_begin_tick(pads,1,2,1,false,false);
 assert(!observation.admitted&&gpu_arms==1);
 frame_effect_callback_finished(1,1);
 assert(observation.terminal&&std::string(observation.error)=="first_draw_callback_tick_guard_changed");
 reset();assert(melee_web_native_menu_frame_effect_arm_first_draw(200)==1);
 frame_effect_begin_tick(pads,0,1,1,false,false);++match->frame;frame_effect_tick_returned();
 arm_allowed=false;frame_effect_begin_draw();frame_effect_draw_returned();
 frame_effect_audio_returned(533);audio_phase=20;frame_effect_callback_finished(1,1);
 assert(observation.terminal&&std::string(observation.error)=="renderer_observer_arm_refused");
}

void first_draw_serialization_control(){
 // Reuse the exact schema2 renderer rows including high uint64 and IEEE values.
 schema2_serialization_control();
 observation.first_draw=true;observation.planned_steps=2;observation.callback_steps=2;
 observation.callback_draws=2;observation.tick_returns=2;observation.draw_returns=2;
 observation.source_after=202;
 const std::string raw=melee_web_native_menu_frame_effect_status();
 assert(raw.find("\"schema_version\":5")!=std::string::npos);
 assert(raw.find("\"selected_source_frame\":201,\"unobserved_draws\":1")!=std::string::npos);
 std::printf("SCHEMA3_STATUS %s\n",raw.c_str());
}

int main(){
 first_draw_actual_tail_control(0);first_draw_actual_tail_control(1);first_draw_actual_tail_control(2);first_draw_actual_tail_control(8);first_draw_guard_controls();
 reset();assert(melee_web_native_menu_frame_effect_arm(199)==0);assert(!observation.requested);
 request();assert(melee_web_native_menu_frame_effect_arm(200)==0);
 const std::string pending_status=melee_web_native_menu_frame_effect_status();
 assert(pending_status.find("\"requested\":true")!=std::string::npos);
 assert(pending_status!="{\"busy\":true}");
 begin();assert(std::string(melee_web_native_menu_frame_effect_status())=="{\"busy\":true}");
 tick_and_draw();assert(observation.admitted&&observation.terminal&&!*observation.error);
 assert(gpu_arms==1&&cancel_calls==1&&returned_tasks==1&&!running);
 assert(observation.audio_returns==1&&observation.pcm_frames==533&&observation.pads[1].button==0x100);
 assert(std::string(melee_web_native_menu_frame_effect_status()).find("\"source_frame_after\":201")!=std::string::npos);
 assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.empty()); // no fence
 gpu.fenceCount=gpu.fenceSuccess=1;submission.pendingStagingBuffers=1;
 assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.empty());
 submission={};source_busy=true;assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.empty());
 source_busy=false;assert(melee_web_native_menu_frame_effect_retire()==1);
 assert((retired_calls==std::vector<std::string>{"close","input","gpu"}));
 assert(melee_web_native_menu_frame_effect_retire()==0&&retired_calls.size()==3);

 // A source-free callback may render audio; it remains outside the due-frame counts.
 reset();request();frame_effect_audio_returned(533);audio_phase=(audio_phase+20)%60;
 frame_effect_callback_finished(0,0);
 assert(!observation.terminal&&!observation.active&&observation.requested);
 assert(observation.requested_frame==200&&match->source_frames()==200&&running&&gpu_arms==0);
 assert(observation.pre_tick_callbacks==1&&observation.pre_tick_audio_returns==1&&observation.pre_tick_pcm_frames==533);
 const std::string waiting=melee_web_native_menu_frame_effect_status();
 assert(waiting.find("\"requested\":true")!=std::string::npos);
 assert(waiting.find("\"pre_tick_audio_returns\":1")!=std::string::npos);
 frame_effect_callback_started();begin();
 assert(observation.active&&observation.admitted&&observation.source_before==200);
 tick_and_draw();
 assert(observation.terminal&&!*observation.error&&observation.source_after==201);
 assert(observation.callback_steps==1&&observation.callback_draws==1&&observation.audio_returns==1);
 assert(observation.pcm_frames==533&&observation.pre_tick_pcm_frames==533);
 assert(observation.audio_phase_before==20&&observation.audio_phase_after==40);
 assert(gpu_arms==1&&cancel_calls==1&&returned_tasks==1&&!running);
 gpu.fenceCount=gpu.fenceSuccess=1;
 assert(melee_web_native_menu_frame_effect_retire()==1);
 assert((retired_calls==std::vector<std::string>{"close","input","gpu"}));

 // Pending requests fail terminally on owner/cursor/readiness or source-work changes.
 reset();request();++asset_generation;frame_effect_callback_finished(0,0);
 assert(observation.terminal&&std::string(observation.error)=="owner_changed_before_due_tick");
 assert(!running&&cancel_calls==1&&returned_tasks==1&&gpu_arms==0);
 reset();request();++match->frame;frame_effect_callback_finished(0,0);
 assert(observation.terminal&&std::string(observation.error)=="source_cursor_changed_before_due_tick");
 assert(observation.source_after==201&&!running&&gpu_arms==0);
 reset();request();running=false;frame_effect_callback_finished(0,0);
 assert(observation.terminal&&std::string(observation.error)=="callback_stopped_before_due_tick");
 assert(cancel_calls==1&&returned_tasks==1&&gpu_arms==0);
 reset();request();net=true;frame_effect_callback_finished(0,0);
 assert(observation.terminal&&std::string(observation.error)=="match_not_ready_before_due_tick");
 assert(!running&&cancel_calls==1&&gpu_arms==0);
 reset();request();frame_effect_callback_finished(1,1);
 assert(observation.terminal&&std::string(observation.error)=="source_work_before_due_tick");
 assert(!running&&cancel_calls==1&&gpu_arms==0);
 // A due callback marked stalled is refused; its first error survives callback finish.
 reset();request();begin(1,true);assert(!observation.admitted&&gpu_arms==0);
 frame_effect_callback_finished(1,1);
 assert(observation.terminal&&std::string(observation.error)=="ordinary_callback_not_one_ready_tick");
 assert(observation.source_after==200&&!running&&cancel_calls==1&&gpu_arms==0);

 reset();request();++match->frame;begin();assert(!observation.admitted);frame_effect_callback_finished(0,0);
 assert(observation.terminal&&std::string(observation.error)=="ordinary_callback_not_one_ready_tick");
 reset();request();++asset_generation;begin();assert(!observation.admitted);
 reset();request();auto previous=std::move(match);match=std::make_unique<melee_web::GameplayMatchSession>();begin();assert(!observation.admitted);
 reset();request();frame_effect_callback_aborted("runtime_owner_stopped");
 assert(observation.terminal&&std::string(observation.error)=="runtime_owner_stopped"&&returned_tasks==1);
 frame_effect_callback_aborted("later");assert(std::string(observation.error)=="runtime_owner_stopped");
 reset();request();frame_effect_callback_aborted("aurora_exit");
 assert(observation.terminal&&std::string(observation.error)=="aurora_exit"&&!running&&gpu_arms==0);
 reset();request();begin();arm_allowed=false;tick_and_draw();
 assert(std::string(observation.error)=="renderer_observer_arm_refused");
 reset();request();PADStatus pads[4]{};frame_effect_begin_tick(pads,0,2,1,false,false);tick_and_draw();
 assert(!observation.admitted&&gpu_arms==0);
 reset();request();begin();faulted=true;tick_and_draw();
 assert(std::string(observation.error)=="ordinary_callback_guard_or_owner_changed");
 first_draw_serialization_control();
}
