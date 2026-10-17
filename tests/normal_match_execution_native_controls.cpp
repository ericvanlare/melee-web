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
#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"
#include "gameplay_replay_completion_policy.hpp"
#define CHECK(v) do { if(!(v)){std::fprintf(stderr,"CHECK %d: %s\n",__LINE__,#v);std::exit(1);} } while(0)
static std::vector<std::string> calls;
static void note(const char* name){calls.emplace_back(name);}
#define EM_ASM(...) note("replay-start-dispatch")
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct PADStatus{uint16_t button=0;int8_t stickX=0,stickY=0;};
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
 void tick(const PADStatus* p){note("tick");CHECK(p==expected_sample);++tick_calls;if(throw_tick)throw std::runtime_error("tick failure");++cursor;}
 int outcome(int& winner){note("outcome");if(throw_outcome)throw std::runtime_error("outcome failure");winner=1;return outcome_value;}
 Player player_stats(int){note("player");return {};}
};static Match owned_match;static Match* match=&owned_match;
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
static void reset(){calls.clear();cursor=tick_calls=audio_calls=0;throw_tick=throw_outcome=throw_audio=false;owned_match={};match=&owned_match;replay.reset();replay_started=replay_trace=replay_match_complete=pending=false;replay_cursor=0;replay_outcome=0;replay_winner=-1;stock_check=0;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;replay_prefix_progress={};}
static auto index(const char* name){return std::find(calls.begin(),calls.end(),name)-calls.begin();}
int main(){
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
  CHECK(std::memcmp(pads.data(),original.data(),sizeof(pads))==0&&sample==pads.data()&&result==1);
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
  if(mode==2){replay=std::make_unique<Replay>();owned_match.paused_value=true;}
  if(mode==3){owned_match.complete_value=true;owned_match.outcome_value=0;}
  if(mode==4){owned_match.complete_value=true;owned_match.outcome_value=1;}
  if(mode==5)throw_audio=true;
  if(mode==6){replay=std::make_unique<Replay>();replay_trace=true;expected_sample=replay->frames[0].pads.data();}
  if(mode==7){owned_match.complete_value=true;owned_match.opening_value=true;}
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
