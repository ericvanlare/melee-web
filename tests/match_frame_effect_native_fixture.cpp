#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <dolphin/pad.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
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
int melee_web_net_active(){return net;}
int melee_web_source_file_drive_busy(){return source_busy;}
const Stats* aurora_get_stats(){return &stats;}
Submission aurora_browser_submission_status(){return submission;}
int aurora_browser_frame_effect_observer_arm(){++gpu_arms;return arm_allowed;}
AuroraBrowserFrameEffectSnapshot aurora_browser_frame_effect_observer_snapshot(){return gpu;}
int aurora_browser_frame_effect_observer_event(uint32_t,AuroraBrowserFrameEffectEvent*){return 0;}
int aurora_browser_frame_effect_observer_handle(uint32_t,AuroraBrowserFrameEffectHandle*){return 0;}
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
 net=source_busy=throw_close=false;arm_allowed=em_answer=true;stats={};submission={};gpu={};
 gpu_arms=cancel_calls=returned_tasks=0;retired_calls.clear();message.clear();
}
void request(){assert(melee_web_native_menu_frame_effect_arm(200)==1);frame_effect_callback_started();}
void begin(unsigned planned_steps=1,bool clock_stalled=false){PADStatus pads[4]{};pads[1].button=0x100;pads[1].extButton=0x12345678;
 reinterpret_cast<unsigned char*>(pads)[11]=0xa5;
 frame_effect_begin_tick(pads,0,planned_steps,1,false,clock_stalled);
 if(observation.admitted)assert(std::memcmp(observation.pads,pads,sizeof(pads))==0);
}
void tick_and_draw(){++match->frame;frame_effect_tick_returned();frame_effect_audio_returned(533);audio_phase=(audio_phase+20)%60;
 frame_effect_begin_draw();frame_effect_draw_returned();frame_effect_callback_finished(1,1);}
int main(){
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
}
