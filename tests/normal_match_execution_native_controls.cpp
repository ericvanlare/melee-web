// Actual extracted production helpers with explicit source/audio endpoints.
// Mechanism/order checks only; these endpoints do not establish gameplay parity.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <optional>
#include <type_traits>
#include <bit>
#include <dolphin/pad.h>
#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"
#include "gameplay_replay_completion_policy.hpp"
#define CHECK(v) do { if(!(v)){std::fprintf(stderr,"CHECK %d: %s\n",__LINE__,#v);std::exit(1);} } while(0)
static std::vector<std::string> calls;
static void note(const char* name){calls.emplace_back(name);}
#define EM_ASM(...) note("replay-start-dispatch")
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
static std::array<PADStatus,4> observed_pads{};
struct MeleeWebAudio{};static MeleeWebAudio owned_audio;
static const PADStatus* expected_sample=nullptr;
static unsigned cursor=0,tick_calls=0,audio_calls=0;static bool throw_tick=false,throw_outcome=false,throw_audio=false;
static unsigned gm_801A4BA8(){note("cursor");return cursor;}
struct Player {int stocks=4,ground_or_air=0;float position[3]{};int motion_id=0;};
struct Match {
 bool paused_value=false,complete_value=false,ending_value=false,opening_value=false;
 int outcome_value=0;
 bool paused(){return paused_value;}bool complete(){return complete_value;}
 bool ending(){return ending_value;}bool opening_demo(){return opening_value;}
 void tick(const PADStatus* p){note("tick");CHECK(p==expected_sample);std::memcpy(observed_pads.data(),p,sizeof(observed_pads));++tick_calls;if(throw_tick)throw std::runtime_error("tick failure");++cursor;}
 int outcome(int& winner){note("outcome");if(throw_outcome)throw std::runtime_error("outcome failure");winner=1;return outcome_value;}
 Player player_stats(int){note("player");return {};}
};static std::unique_ptr<Match> match=std::make_unique<Match>();
namespace melee_web {using GameplayMatchSession=::Match;}
struct ReplayFrame{std::array<PADStatus,4> pads{};};
struct Replay{
 std::vector<ReplayFrame> frames{2};bool diagnostic_entity_prefix=false,diagnostic_active_entity_prefix=false;
 bool closes_draw_batch(size_t)const{return true;}
};static std::unique_ptr<Replay> replay;
static bool replay_started=false,replay_trace=false,replay_match_complete=false,pending=false;
static size_t replay_cursor=0;
static int replay_outcome=0,replay_winner=-1,stock_check=0,stock_count=4,stock_respawns=0;
static unsigned stock_tick=0;static bool stock_lost=false,stock_jump=false;
static constexpr int OUTCOME_ELIMINATION=1;
static melee_web::DiagnosticPrefixProgress replay_prefix_progress;
static void melee_web_cpu_observation_set_event_cursor(size_t){note("cpu-cursor");}
static void melee_web_cpu_observation_scheduler_return(){note("cpu-return");}
namespace melee_web {static void retail_replay_frame(Replay&,size_t){note("replay-frame");}}
static void frame_effect_begin_tick(const PADStatus* p,size_t,unsigned,unsigned,bool,bool){note("observer-begin");CHECK(p==expected_sample);}
static void frame_effect_tick_returned(){note("observer-return");}
struct LocalMarker {
 unsigned& count;const int& identity;const int* expected_identity;const LocalMarker* expected_self;
 void operator()(const char* name,int,int,int)const {CHECK(this==expected_self && &identity==expected_identity);++count;note(name);}
};
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
#define LOCAL_MARKER_ARG ,replay_boundary_mark
#else
#define LOCAL_MARKER_ARG
#endif
static void render_audio_tick(MeleeWebAudio* audio,char* error,size_t size){note("audio");CHECK(audio==&owned_audio);CHECK(size==256);CHECK(std::strcmp(error,"actual-error-owner")==0);++audio_calls;if(throw_audio)throw std::runtime_error("audio failure");}
// ACTUAL_NORMAL_MATCH_IMPLEMENTATION
static void reset(){calls.clear();cursor=tick_calls=audio_calls=0;throw_tick=throw_outcome=throw_audio=false;match=std::make_unique<Match>();replay.reset();replay_started=replay_trace=replay_match_complete=pending=false;replay_cursor=0;replay_outcome=0;replay_winner=-1;stock_check=0;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;replay_prefix_progress={};}
static auto index(const char* name){return std::find(calls.begin(),calls.end(),name)-calls.begin();}
static bool same_declared_pads(const PADStatus* a,const PADStatus* b){
 for(unsigned i=0;i<4;++i)
  if(a[i].button!=b[i].button||a[i].stickX!=b[i].stickX||a[i].stickY!=b[i].stickY||
     a[i].substickX!=b[i].substickX||a[i].substickY!=b[i].substickY||
     a[i].triggerLeft!=b[i].triggerLeft||a[i].triggerRight!=b[i].triggerRight||
     a[i].analogA!=b[i].analogA||a[i].analogB!=b[i].analogB||a[i].err!=b[i].err
#ifdef TARGET_PC
     ||a[i].extButton!=b[i].extButton
#endif
    )return false;
 return true;
}
static bool same_tick(const melee_web::FixedTickClock::Tick& a,const melee_web::FixedTickClock::Tick& b){
 return a.steps==b.steps&&a.stalled==b.stalled&&a.pending_steps==b.pending_steps&&a.reason==b.reason&&
  std::bit_cast<uint64_t>(a.triggering_value)==std::bit_cast<uint64_t>(b.triggering_value)&&
  std::bit_cast<uint64_t>(a.threshold)==std::bit_cast<uint64_t>(b.threshold)&&
  std::bit_cast<uint64_t>(a.interval_ms)==std::bit_cast<uint64_t>(b.interval_ms);
}
struct PathResult {std::vector<std::string> events;std::string first_error;unsigned ticks,audio;size_t steps,draws;int result;};
static PathResult run_owned_plan_path(bool use_owned,int failure,unsigned scheduled_audio_steps){
 reset();melee_web::FixedTickClock source_clock,audio_clock;
 (void)source_clock.tick(0,true);if(scheduled_audio_steps)(void)audio_clock.tick(0,true);
 const double callback_now=1000.0/60.0;
 auto elapsed=source_clock.tick(callback_now,true),audio_elapsed=audio_clock.tick(callback_now,true);
 CHECK(elapsed.steps==1&&audio_elapsed.steps==scheduled_audio_steps);
 const auto expected_elapsed=elapsed,expected_audio=audio_elapsed;
 CHECK(owned_normal_match_plan_eligible(elapsed,audio_elapsed,false,false,false,false,true,false,0,true));
 std::array<PADStatus,4> input;
 // Controlled object storage includes the actual TARGET_PC extension and any
 // ABI padding; snapshot it with memcpy, never infer padding from assignment.
 std::memset(input.data(),0xa5,sizeof(input));
 for(unsigned i=0;i<4;++i){auto& v=input[i];v.button=uint16_t(0x100+i);v.stickX=int8_t(-80+int(i));v.stickY=int8_t(70-int(i));v.substickX=int8_t(-60+int(i));v.substickY=int8_t(50-int(i));v.triggerLeft=uint8_t(10+i);v.triggerRight=uint8_t(20+i);v.analogA=uint8_t(30+i);v.analogB=uint8_t(40+i);v.err=int8_t(-int(i));
#ifdef TARGET_PC
  v.extButton=0x12340000u+i;
#endif
 }
 std::array<PADStatus,4> expected;std::memcpy(expected.data(),input.data(),sizeof(input));
 const PADStatus* selected=input.data();std::optional<OwnedNormalMatchPlan> owned;
 if(use_owned){
  owned.emplace(selected,elapsed,audio_elapsed,match.get(),&owned_audio);
  CHECK(std::memcmp(owned->pads(),input.data(),sizeof(input))==0);
  std::memset(input.data(),0x5a,sizeof(input));elapsed={};audio_elapsed={};
  CHECK(same_declared_pads(owned->pads(),expected.data()));
  CHECK(same_tick(owned->source_elapsed(),expected_elapsed)&&same_tick(owned->audio_elapsed(),expected_audio));
  expected_sample=owned->pads();
 }else expected_sample=selected;
 if(failure==1)throw_tick=true;if(failure==2)throw_outcome=true;if(failure==3)throw_audio=true;
 melee_web::SourceFrameSequence frames;unsigned replay_steps=0,replay_draw_boundaries=0,marker_calls=0;
 const int identity=314;const LocalMarker replay_boundary_mark{marker_calls,identity,&identity,&replay_boundary_mark};
 int result=1;char error[256]="actual-error-owner";std::string first_error;
 const auto draw=[&](){note("draw");return failure!=4;};
 try{
  if(owned)owned->consume(frames,result,replay_steps,replay_draw_boundaries);
  else execute_match_source_step(selected,frames,elapsed,audio_elapsed,false,false,result,replay_steps,replay_draw_boundaries);
  render_normal_callback_audio(owned?owned->borrowed_audio():&owned_audio,owned?owned->audio_elapsed():audio_elapsed,false,error LOCAL_MARKER_ARG);
  finish_source_callback_draw(frames,draw LOCAL_MARKER_ARG);
 }catch(const std::runtime_error& e){first_error=e.what();}
 CHECK(tick_calls==1);CHECK(same_declared_pads(observed_pads.data(),expected.data()));
 if(owned){const auto count=calls.size();bool refused=false;try{owned->consume(frames,result,replay_steps,replay_draw_boundaries);}catch(const std::runtime_error& e){refused=std::string(e.what())=="Owned normal match plan was already consumed";}CHECK(refused&&calls.size()==count&&tick_calls==1);}
 CHECK(replay_steps==0&&replay_draw_boundaries==0);
 return {calls,first_error,tick_calls,audio_calls,frames.steps(),frames.draws(),result};
}
static void control_owned_normal_match_plan(){
 static_assert(!std::is_copy_constructible_v<OwnedNormalMatchPlan>);
 static_assert(!std::is_move_constructible_v<OwnedNormalMatchPlan>);
 static_assert(!std::is_copy_assignable_v<OwnedNormalMatchPlan>);
 static_assert(!std::is_move_assignable_v<OwnedNormalMatchPlan>);
 static_assert(PAD_MAX_CONTROLLERS==4);
 for(unsigned audio_steps=0;audio_steps<=1;++audio_steps)for(int failure=0;failure<=4;++failure){
  const auto baseline=run_owned_plan_path(false,failure,audio_steps);const auto owned=run_owned_plan_path(true,failure,audio_steps);
  CHECK(baseline.events==owned.events&&baseline.first_error==owned.first_error);
  CHECK(baseline.ticks==owned.ticks&&baseline.audio==owned.audio&&baseline.steps==owned.steps&&baseline.draws==owned.draws&&baseline.result==owned.result);
  if(audio_steps==0)CHECK(owned.audio==0&&std::find(owned.events.begin(),owned.events.end(),"audio")==owned.events.end());
 }
 reset();melee_web::FixedTickClock clock;(void)clock.tick(0,true);const auto one=clock.tick(1000.0/60.0,true);
 melee_web::FixedTickClock two_clock;(void)two_clock.tick(0,true);const auto two=two_clock.tick(2000.0/60.0,true);
 melee_web::FixedTickClock zero_clock;const auto zero=zero_clock.tick(0,true);const auto stalled=zero_clock.tick(10000,true);
 const auto eligible=[](const auto& a,const auto& b){return owned_normal_match_plan_eligible(a,b,false,false,false,false,true,false,0,true);};
 CHECK(eligible(one,one)&&eligible(one,zero)&&!eligible(zero,one)&&!eligible(two,one)&&!eligible(one,two)&&!eligible(stalled,one));
 CHECK(!owned_normal_match_plan_eligible(one,one,true,false,false,false,true,false,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,true,false,false,true,false,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,true,false,true,false,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,true,true,false,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,false,false,false,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,false,true,true,0,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,false,true,false,-1,true));
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,false,true,false,0,false));
 // Global owner authentication, not a caller-supplied pointer assertion.
 for(bool make_null:{false,true}){
  reset();std::array<PADStatus,4> pads{};OwnedNormalMatchPlan bad_owner(pads.data(),one,one,match.get(),&owned_audio);
  auto authentic=std::move(match);if(!make_null)match=std::make_unique<Match>();
  melee_web::SourceFrameSequence frames;int result=1;unsigned steps=0,draws=0;bool refused=false;
  try{bad_owner.consume(frames,result,steps,draws);}catch(const std::runtime_error& e){refused=std::string(e.what())=="Owned normal match plan current callback owner changed";}
  CHECK(refused&&tick_calls==0&&calls.empty());
  match=std::move(authentic);refused=false;
  try{bad_owner.consume(frames,result,steps,draws);}catch(const std::runtime_error& e){refused=std::string(e.what())=="Owned normal match plan was already consumed";}
  CHECK(refused&&tick_calls==0&&calls.empty());
 }
 // ACTUAL last queued sample selector: counter1->0 does not set copied_input.
 reset();struct Input {PADStatus raw[4]{};} storage;const auto* input=&storage;
 PADStatus checked_input[4];const PADStatus* sample=input->raw;bool copied_input=false,diagnostic_start_pulse=false;
 unsigned diagnostic_pad_remaining=1,diagnostic_pad_port=2;PADStatus diagnostic_pad{};
 diagnostic_pad.button=PAD_BUTTON_A;diagnostic_pad.err=PAD_ERR_TRANSFER;diagnostic_pad.extButton=0x44556677u;
 // ACTUAL_QUEUED_DIAGNOSTIC_PAD_SELECTION
 CHECK(diagnostic_pad_remaining==0&&!copied_input&&sample==checked_input&&sample!=input->raw);
 CHECK(sample[2].button==diagnostic_pad.button&&sample[2].err==PAD_ERR_TRANSFER&&sample[2].extButton==0x44556677u);
 CHECK(!owned_normal_match_plan_eligible(one,one,false,false,false,copied_input||sample!=input->raw,true,false,0,true));

}
int main(){
 control_owned_normal_match_plan();
 // Plans come from the maintained real clock, not fabricated helper plans.
 for(unsigned n=0;n<=2;++n){reset();
  unsigned replay_steps=0,replay_draw_boundaries=0,marker_calls=0;
  const int marker_identity=int(n);const LocalMarker replay_boundary_mark{marker_calls,marker_identity,&marker_identity,&replay_boundary_mark};melee_web::FixedTickClock source_clock,audio_clock;
  (void)source_clock.tick(0,true);(void)audio_clock.tick(0,true);
  const auto elapsed=source_clock.tick(double(n)*1000.0/60.0,true);
  const auto audio_elapsed=audio_clock.tick(double(n)*1000.0/60.0,true);
  CHECK(elapsed.steps==n&&audio_elapsed.steps==n);
  std::array<PADStatus,4> pads{};pads[0].button=0x140;const auto original=pads;
  const PADStatus* sample=pads.data();expected_sample=sample;
  melee_web::SourceFrameSequence frames;int result=1;char error[256]="actual-error-owner";
  const auto present=[&](){note("draw");CHECK(frames.pending());return true;};
  for(unsigned step=0;step<elapsed.steps;++step){frames.before_step(present);execute_match_source_step(sample,frames,elapsed,audio_elapsed,false,false,result,replay_steps,replay_draw_boundaries);}
  render_normal_callback_audio(&owned_audio,audio_elapsed,false,error LOCAL_MARKER_ARG);
  finish_source_callback_draw(frames,present LOCAL_MARKER_ARG);
  CHECK(tick_calls==n&&audio_calls==n&&frames.steps()==n&&frames.draws()==n);
  CHECK(replay_steps==0&&replay_draw_boundaries==0);
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  CHECK(marker_calls==4+2*n);
#else
  CHECK(marker_calls==0);
#endif
  CHECK(same_declared_pads(pads.data(),original.data())&&sample==pads.data()&&result==1);
  if(n){CHECK(index("tick")<index("outcome"));CHECK(index("audio")<index("source_frames_finish_begin")||index("source_frames_finish_begin")==calls.size());CHECK(calls.back()==
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
   "source_frames_finish_returned"
#else
   "draw"
#endif
  );}
  if(n==2){CHECK(std::count(calls.begin(),calls.end(),"draw")==2);CHECK(index("draw")<index("audio"));}
 }
 // Actual helper failures must propagate before downstream audio/draw.
 for(int mode=0;mode<8;++mode){reset();
  unsigned replay_steps=0,replay_draw_boundaries=0,marker_calls=0;
  const int marker_identity=mode+10;const LocalMarker replay_boundary_mark{marker_calls,marker_identity,&marker_identity,&replay_boundary_mark};melee_web::FixedTickClock clock;(void)clock.tick(0,true);const auto elapsed=clock.tick(1000.0/60,true);
  std::array<PADStatus,4> pads{};const PADStatus* sample=pads.data();expected_sample=sample;
  melee_web::SourceFrameSequence frames;int result=1;char error[256]="actual-error-owner";
  bool failed=false;const auto draw=[](){note("draw");return true;};
  if(mode==0)throw_tick=true;
  if(mode==1)throw_outcome=true;
  if(mode==2){replay=std::make_unique<Replay>();match->paused_value=true;}
  if(mode==3){match->complete_value=true;match->outcome_value=0;}
  if(mode==4){match->complete_value=true;match->outcome_value=1;}
  if(mode==5)throw_audio=true;
  if(mode==6){replay=std::make_unique<Replay>();replay_trace=true;expected_sample=replay->frames[0].pads.data();}
  if(mode==7){match->complete_value=true;match->opening_value=true;}
  try{execute_match_source_step(sample,frames,elapsed,elapsed,false,false,result,replay_steps,replay_draw_boundaries);render_normal_callback_audio(&owned_audio,elapsed,false,error LOCAL_MARKER_ARG);finish_source_callback_draw(frames,draw LOCAL_MARKER_ARG);}catch(const std::runtime_error&){failed=true;}
  CHECK(failed==(mode<=3||mode==5));
  if(mode<=3){CHECK(audio_calls==0);CHECK(index("draw")==calls.size());}
  if(mode==0||mode==2)CHECK(frames.steps()==0);
  if(mode==2)CHECK(tick_calls==0);
  if(mode==4||mode==7)CHECK(pending&&result==3);
  if(mode==5)CHECK(index("draw")==calls.size());
  if(mode==6){CHECK(replay_started&&replay_cursor==1&&replay_steps==1&&replay_draw_boundaries==1);CHECK(sample==replay->frames[0].pads.data());CHECK(index("replay-start-dispatch")<index("tick"));CHECK(index("outcome")<index("replay-frame"));}
 }
 // Transition-audio/stall exclusion and whole-replay scheduler markers are retained.
 unsigned replay_steps=0,replay_draw_boundaries=0,marker_calls=0;
 const int marker_identity=100;const LocalMarker replay_boundary_mark{marker_calls,marker_identity,&marker_identity,&replay_boundary_mark};
 reset();melee_web::FixedTickClock clock;(void)clock.tick(0,true);const auto elapsed=clock.tick(1000.0/60,true);char error[256]="actual-error-owner";
 render_normal_callback_audio(&owned_audio,elapsed,true,error LOCAL_MARKER_ARG);CHECK(audio_calls==0);
 const auto stalled=clock.tick(10000,true);CHECK(stalled.stalled);render_normal_callback_audio(&owned_audio,stalled,false,error LOCAL_MARKER_ARG);CHECK(audio_calls==0);
 reset();replay=std::make_unique<Replay>();std::array<PADStatus,4> pads{};const PADStatus* sample=pads.data();expected_sample=sample;melee_web::SourceFrameSequence frames;int result=1;
 execute_match_source_step(sample,frames,elapsed,elapsed,false,true,result,replay_steps,replay_draw_boundaries);
 CHECK(index("cpu-cursor")<index("tick")&&index("tick")<index("cpu-return"));CHECK(replay_cursor==0&&replay_steps==0&&replay_draw_boundaries==0);
 std::puts("actual shared match/audio/finish endpoint-order controls passed");
}
