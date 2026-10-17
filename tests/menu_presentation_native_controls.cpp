// Actual extracted production presenter. GPU/source services below are endpoint
// doubles: these controls prove branch/order/reference propagation, not graphics.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <aurora/gfx.h>
#include "source_frame_sequence.hpp"
#include "gameplay_replay_completion_policy.hpp"
#define CHECK(v) do { if(!(v)) {std::fprintf(stderr,"CHECK %d: %s\n",__LINE__,#v);std::exit(1);} } while(0)
static std::vector<std::string> calls;
static void note(const char* s){calls.emplace_back(s);}
#define EM_ASM(...) note("emasm")
struct GXColor {unsigned char r,g,b,a;};
constexpr unsigned GX_MAX_Z24=0xffffff;
static bool allow_begin=true,throw_end=false,staging=false,hashing=false;
static double now=0;
static double emscripten_get_now(){return ++now;}
static bool aurora_begin_frame(){note("begin");return allow_begin;}
static void GXSetCopyClear(GXColor,unsigned){note("clear");}
static void aurora_end_frame(){note("end");if(throw_end)throw std::runtime_error("end failure");}
static bool aurora_browser_staging_byte_hash_enabled(){return hashing;}
static bool aurora_browser_staging_diagnostics_enabled(){return staging;}
static void aurora_browser_staging_set_diagnostic_source_frame(int v){note(v<0?"staging-reset":"staging-set");}
static int diagnostic_source_frame(){return 181;}
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
struct Owner {const char* label;void draw(){note(label);}unsigned source_frames(){return 181;}};
static Owner m{"match"},r{"results"},p{"prize"},w{"world"};
static Owner *match=nullptr,*results=nullptr,*prize=nullptr,*world=nullptr;
static bool host_entered=false,faulted=false,running=true,pending=false;
static int host=1,world_draw_result=1;
static int melee_web_menu_host_draw(int,char*,size_t){note("host-draw");return world_draw_result;}
static int melee_web_menu_host_phase(int){return 1;}
struct Replay {std::vector<int> frames{1};int version=4;bool diagnostic_entity_prefix=false,diagnostic_active_entity_prefix=false,whole=false;bool whole_session(){return whole;}unsigned diagnostic_source_observations(){return 60;}};
static std::unique_ptr<Replay> replay;
static bool replay_trace=false,replay_final_draw=false,replay_match_complete=false;
static int replay_outcome=0,replay_winner=-1;
static size_t replay_cursor=0;
static melee_web::ReplayCompletionState replay_completion;
static melee_web::DiagnosticPrefixProgress replay_prefix_progress;
static int observed_replay_scene(){return 3;}
static void melee_web_match_rules_publish_result(){note("publish-result");}
static int melee_web_match_rules_outcome(int*){note("result");return 1;}
struct Clock {void reset(){note("clock-reset");}};static Clock menu_clock;
static std::string message;
static bool first_use_draw_pending=false,provenance_return_draw=true;
static int pipeline_context(){return 0;}
namespace melee_web {namespace provenance {struct Scope {explicit Scope(int){note("scope-enter");}~Scope(){note("scope-exit");}};}
static void retail_replay_draw(Replay&,size_t){note("replay-draw");}
static void retail_replay_preparation_draw(Replay&){note("replay-preparation");}}
static void melee_web_provenance_frame(int){note("provenance-return");}
static void frame_effect_begin_draw(){note("observer-begin");}
static void frame_effect_draw_returned(){note("observer-return");}
static AuroraStats returned_stats{};
static AuroraStats aurora_stats_snapshot(){note("stats");return returned_stats;}
// ACTUAL_PRESENTATION_IMPLEMENTATION
static void reset(){calls.clear();match=results=prize=world=nullptr;replay.reset();host_entered=faulted=pending=replay_trace=replay_final_draw=replay_match_complete=first_use_draw_pending=false;running=true;allow_begin=true;throw_end=staging=hashing=false;world_draw_result=1;now=0;replay_cursor=0;replay_completion={};replay_prefix_progress={};returned_stats={};returned_stats.drawCallCount=7;returned_stats.lastTextureUploadSize=11;returned_stats.lastVertSize=2;returned_stats.lastUniformSize=3;returned_stats.lastIndexSize=5;returned_stats.lastStorageSize=13;returned_stats.lastBeginFrameFrameSlotMs=17;returned_stats.lastEndFrameQueueSubmitMs=19;}
static auto at(const char* name){return std::find(calls.begin(),calls.end(),name)-calls.begin();}
int main(){
 for(int mode=0;mode<11;++mode){reset();
  melee_web::SourceFrameSequence frames;char error[256]="source draw failure";
  int began=0,drawn=1,first_use=0,suppress_draw=0;bool actual_source_draw=false,replay_completed_now=false;
  double begin_done=0,draw_done=0,end_done=0,render_begin_ms=0,render_draw_ms=0,render_end_ms=0,render_total_ms=0;
  uint32_t callback_draw_calls=0,callback_texture_upload=0,callback_staging_used=0;
  AuroraStats callback_begin_stats{},callback_end_stats{};
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
  bool first_replay_callback_probe=true;unsigned replay_boundary_draw_index=0;
  const auto replay_boundary_mark=[](const char* name,int,int,int){note(name);};
#endif
  CallbackPresentationContext context{frames,error,began,drawn,first_use,suppress_draw,actual_source_draw,replay_completed_now,begin_done,draw_done,end_done,render_begin_ms,render_draw_ms,render_end_ms,render_total_ms,callback_draw_calls,callback_texture_upload,callback_staging_used,callback_begin_stats,callback_end_stats
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
   ,first_replay_callback_probe,replay_boundary_draw_index
#endif
  };
  if(mode==1)results=&r;else if(mode==2)prize=&p;else if(mode==3){world=&w;host_entered=true;}else match=&m;
  if(mode==4)suppress_draw=1;if(mode==5)faulted=true;if(mode==6)allow_begin=false;
  if(mode==7){match=nullptr;world=&w;host_entered=true;world_draw_result=0;}
  if(mode==8)throw_end=true;
  if(mode==9){replay=std::make_unique<Replay>();replay_trace=true;replay_cursor=1;staging=hashing=true;}
  if(mode==10)first_use_draw_pending=true;
  frames.did_step();bool failed=false;
  try{frames.finish([&]{return present_source_frame(context
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
   ,replay_boundary_mark
#endif
  );});}catch(const std::runtime_error&){failed=true;}
  CHECK(failed==(mode>=4&&mode<=8));CHECK(!calls.empty());
  if(mode==6){CHECK(!began&&callback_draw_calls==0);CHECK(at("clear")==calls.size());}
  else {CHECK(began==1);CHECK(at("begin")<at("clear"));CHECK(at("clear")<at("end"));}
  if(mode<=3||mode==9||mode==10){CHECK(frames.draws()==1&&!frames.pending());CHECK(actual_source_draw);CHECK(callback_draw_calls==7&&callback_texture_upload==11&&callback_staging_used==34);CHECK(callback_begin_stats.lastBeginFrameFrameSlotMs==17&&callback_end_stats.lastEndFrameQueueSubmitMs==19);}
  if(mode==0||mode==9||mode==10)CHECK(at("clear")<at("match")&&at("match")<at("end"));
  if(mode==1)CHECK(at("results")<at("end"));if(mode==2)CHECK(at("prize")<at("end"));if(mode==3)CHECK(at("host-draw")<at("end"));
  if(mode==4||mode==5)CHECK(at("match")==calls.size());
  if(mode==7||mode==8)CHECK(at("stats")==calls.size());
  if(mode==9){CHECK(replay_completion.final_input_drawn&&replay_completed_now&&replay_final_draw&&!running);CHECK(at("replay-draw")<at("end"));CHECK(at("end")<at("publish-result"));CHECK(at("publish-result")<at("clock-reset"));CHECK(at("staging-set")<at("end")&&at("end")<at("staging-reset"));}
  if(mode==10)CHECK(first_use==1&&!first_use_draw_pending);
 }
 std::puts("actual shared presentation 11 endpoint-order/refusal cases passed");
}
