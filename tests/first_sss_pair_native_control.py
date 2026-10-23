from pathlib import Path
import shutil, subprocess, sys
root=Path(__file__).resolve().parents[1]
source=(root/"src/gameplay_menu_browser.cpp").read_text()
enum=source[source.index("enum class FirstCssBrowserMode : unsigned {"):source.index("struct FirstCssDrawSample {")]
start=source.index("void first_sss_pair_check_css_boundary(")
end=source.index("\n#endif\n}",start)
actual=source[start:end]
harness=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace control {
// No source_frames global: production's callback-local ledger is unavailable
// to exported arm/kick. Accidental access must fail this extracted compilation.
''' + enum + r'''
#define EM_ASM_INT(...) (1)
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
enum class AssetDestination { None, Other };
AssetDestination asset_destination=AssetDestination::None;
namespace melee_web {
struct MenuPreparationState {
 enum class Phase { Idle, Busy };
};
}
struct Row {
 uint32_t consume_sequence=0,tick_sequence=0,draw_enter_sequence=0,draw_return_sequence=0,poll_sequence=0;
 uint32_t source_tick=0,draw_ordinal=0,draw_source_tick=0,poll_source_tick=0,poll_draw_ordinal=0;
};
struct Context {std::string source_sha256;};
struct FiniteInput {std::string source_sha256;std::vector<Row> rows;uint32_t phase_end_sequence=0;};
struct Sample {uint32_t scene_frame=0,seed=0;uint64_t world_generation=0;};
struct DrawState {
 FirstCssBrowserMode mode=FirstCssBrowserMode::None;
 bool armed=false,complete=false,failed=false,entry_captured=false,tick_captured=false,
   draw_captured=false,frame_end_returned=false;
 unsigned source_steps=0,source_draws=0,host_tick_calls=0,host_draw_calls=0,
   aurora_begin_calls=0,aurora_end_calls=0;
 int tick_result=0;
 uint64_t world_generation=0;
 Context context;
 FiniteInput finite_css;
 Sample draw;
 const uint32_t* seed_owner=nullptr;
 const void* scene_owner=nullptr;
} first_css_browser_draw;
struct Stream {
 bool terminal=false,failed=false,complete=false,transition_captured=false,
   tick_captured=false,frame_end_returned=false;
 std::string outcome;
 struct {std::string source_sha256;} input;
 unsigned current_input_index=0,next_input_index=0,consumed_inputs=0,host_tick_calls=0,
   executed_host_ticks=0,matched_ticks=0,matched_draws=0,host_draw_calls=0,
   aurora_begin_calls=0,aurora_end_calls=0;
 int tick_result=0;
 Sample transition;
} first_css_browser_postdraw_stream;
struct FinalDraw {
 bool complete=false,compared=false,approved=false,frame_end_returned=false,failed=false;
 std::string error;
 Sample actual;
} first_css_final_pending_draw;
struct Pair {
 FirstCssBrowserMode profile=FirstCssBrowserMode::None;
 bool armed=false,kicked=false,attempted=false,failed=false,audio_before_rebuild_captured=false;
 uint64_t audio_render_calls_before_rebuild=0,audio_render_frames_before_rebuild=0;
} first_sss_constructor_pair;
struct FixedTickClock {void reset(){}} menu_clock,audio_clock;
struct Preparation {bool busy()const{return false;} melee_web::MenuPreparationState::Phase phase()const{return melee_web::MenuPreparationState::Phase::Idle;}} preparation;
struct Host{} host_storage; Host* host=&host_storage; int world_storage=1; int* world=&world_storage;
int scene_storage=1; const void* current_scene=&scene_storage;
const void* melee_web_current_scene_info(){return current_scene;}
int source_frame=142; uint32_t seed=0xe2624ac3; uint32_t* seed_ptr=&seed;
bool live_owner=true,running=false,pending=false,faulted=false,replay=false,host_entered=true,
  match=false,results=false,prize=false,menu_scene_rebuild_pending=false,net_active=false;
uint64_t diagnostic_audio_render_calls=21,diagnostic_audio_render_frames=32000;
unsigned host_arm_calls=0;
bool host_arm_success=true;
int gm_801A4BA8(){return source_frame;}
bool melee_web_net_active(){return net_active;}
void first_css_browser_draw_check_live(const char*){check(live_owner,"live owner refusal");}
void first_css_browser_draw_refresh_observation(){}
void first_sss_pair_fail(const char* reason){first_sss_constructor_pair.failed=true;}
int melee_web_menu_host_arm_first_sss_pair(Host*,char* error,size_t size){
 ++host_arm_calls;
 if(!host_arm_success){if(size>1){error[0]='x';error[1]=0;}return 0;}
 return 1;
}
unsigned first_css_postdraw_count(){
 return first_css_browser_draw.mode==FirstCssBrowserMode::FreshFiniteCssPrefixV4
  ? static_cast<unsigned>(first_css_browser_draw.finite_css.rows.size()-1):148;
}
const Row& first_css_finite_row(unsigned index){
 check(index<first_css_browser_draw.finite_css.rows.size(),"bad finite row");
 return first_css_browser_draw.finite_css.rows[index];
}
'''+actual+r'''
void reset_common(){
 first_sss_constructor_pair=Pair{};first_css_browser_postdraw_stream=Stream{};
 first_css_final_pending_draw=FinalDraw{};
 running=pending=faulted=replay=match=results=prize=menu_scene_rebuild_pending=net_active=false;
 host_entered=true;asset_destination=AssetDestination::None;preparation=Preparation{};
 host_arm_calls=0;host_arm_success=true;live_owner=true;source_frame=142;
 seed=0xe2624ac3;seed_ptr=&seed;world=&world_storage;host=&host_storage;
 current_scene=&scene_storage;diagnostic_audio_render_calls=21;diagnostic_audio_render_frames=32000;
}
void setup_fresh(){
 reset_common();first_css_browser_draw=DrawState{};
 auto& b=first_css_browser_draw;b.mode=FirstCssBrowserMode::FreshFiniteCssPrefixV4;
 b.armed=b.complete=b.entry_captured=b.tick_captured=b.draw_captured=b.frame_end_returned=true;
 b.source_steps=b.source_draws=b.host_tick_calls=b.host_draw_calls=b.aurora_begin_calls=b.aurora_end_calls=1;
 b.tick_result=1;b.world_generation=7;b.context.source_sha256=std::string(64,'4');
 b.finite_css.source_sha256=b.context.source_sha256;b.finite_css.phase_end_sequence=1570;
 b.finite_css.rows.resize(142);b.seed_owner=&seed;b.scene_owner=current_scene;b.draw={1,0x312151c3,7};
 auto& r=b.finite_css.rows[141];r.consume_sequence=1565;r.tick_sequence=1566;
 r.draw_enter_sequence=1567;r.draw_return_sequence=1568;r.poll_sequence=1569;
 r.source_tick=141;r.draw_ordinal=141;r.draw_source_tick=142;r.poll_source_tick=142;r.poll_draw_ordinal=142;
 auto& s=first_css_browser_postdraw_stream;s.terminal=true;s.outcome="stop_after_last_input_request";
 s.input.source_sha256=b.context.source_sha256;s.current_input_index=140;s.next_input_index=140;
 s.consumed_inputs=141;s.host_tick_calls=141;s.executed_host_ticks=142;s.matched_ticks=140;
 s.matched_draws=140;s.host_draw_calls=140;s.aurora_begin_calls=s.aurora_end_calls=140;
 s.tick_result=3;s.transition_captured=true;s.transition={142,seed,7};
 auto& f=first_css_final_pending_draw;f.complete=f.compared=f.approved=f.frame_end_returned=true;
 f.actual={142,seed,7};
}
void setup_legacy(){
 reset_common();first_css_browser_draw=DrawState{};auto& b=first_css_browser_draw;
 b.mode=FirstCssBrowserMode::LegacyTickDrawV1;b.armed=b.complete=b.entry_captured=
  b.tick_captured=b.draw_captured=b.frame_end_returned=true;
 b.source_steps=b.source_draws=b.host_tick_calls=b.host_draw_calls=b.aurora_begin_calls=b.aurora_end_calls=1;
 b.tick_result=1;b.world_generation=7;b.seed_owner=&seed;b.scene_owner=current_scene;b.draw={149,seed,7};
 auto& s=first_css_browser_postdraw_stream;s.terminal=true;s.outcome="stop_after_last_input_request";
 s.matched_ticks=s.matched_draws=147;s.consumed_inputs=148;
 auto& f=first_css_final_pending_draw;f.complete=f.compared=f.approved=f.frame_end_returned=true;
 f.actual={149,seed,7};
}
int run_control(){
 setup_fresh();assert(first_css_browser_draw.draw.seed==0x312151c3);
 assert(first_css_browser_postdraw_stream.transition.seed==0xe2624ac3);
 assert(first_css_final_pending_draw.actual.seed==0xe2624ac3);
 assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_arm()==1);
 assert(first_sss_constructor_pair.armed&&first_sss_constructor_pair.profile==FirstCssBrowserMode::FreshFiniteCssPrefixV4&&host_arm_calls==1);
 assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_kick()==1);
 assert(first_sss_constructor_pair.kicked&&pending&&!running&&first_sss_constructor_pair.audio_before_rebuild_captured);

 setup_legacy();assert(melee_web_native_menu_stadium_first_sss_pair_arm()==1);
 assert(first_sss_constructor_pair.profile==FirstCssBrowserMode::LegacyTickDrawV1);
 assert(melee_web_native_menu_stadium_first_sss_pair_kick()==1&&pending);
 setup_fresh();assert(melee_web_native_menu_stadium_first_sss_pair_arm()==0&&host_arm_calls==0);
 setup_legacy();assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_arm()==0&&host_arm_calls==0);

 for(int bad=0;bad<22;bad++){
  setup_fresh();auto& s=first_css_browser_postdraw_stream;auto& b=first_css_browser_draw;
  uint32_t foreign_seed=0x12345678;
  switch(bad){
   case 0:s.matched_ticks=139;break;
   case 1:s.consumed_inputs=140;break;
   case 2:s.executed_host_ticks=141;break;
   case 3:b.finite_css.rows[141].tick_sequence=1567;break;
   case 4:b.finite_css.phase_end_sequence=1571;break;
   case 5:first_css_final_pending_draw.actual.scene_frame=141;break;
   case 6:s.complete=true;break;
   case 7:live_owner=false;break;
   case 8:pending=true;break;
   case 9:seed_ptr=&foreign_seed;break;
   case 10:seed=0x312151c3;break;
   case 11:first_css_final_pending_draw.actual.seed=0x312151c3;break;
   case 12:running=true;break;
   case 13:faulted=true;break;
   case 14:replay=true;break;
   case 15:host_entered=false;break;
   case 16:world=nullptr;break;
   case 17:host=nullptr;break;
   case 18:first_css_final_pending_draw.frame_end_returned=false;break;
   case 19:first_css_final_pending_draw.approved=false;break;
   case 20:b.frame_end_returned=false;break;
   case 21:b.source_draws=2;break;
  }
  assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_arm()==0);
  assert(first_sss_constructor_pair.failed&&host_arm_calls==0);
 }
 setup_legacy();first_css_browser_postdraw_stream.matched_ticks=146;
 assert(melee_web_native_menu_stadium_first_sss_pair_arm()==0&&host_arm_calls==0);
 for(int bad=0;bad<4;bad++){
  setup_fresh();assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_arm()==1);
  switch(bad){
   case 0:running=true;break;
   case 1:pending=true;break;
   case 2:first_css_final_pending_draw.frame_end_returned=false;break;
   case 3:live_owner=false;break;
  }
  assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_kick()==0);
  assert(!first_sss_constructor_pair.kicked&&!first_sss_constructor_pair.audio_before_rebuild_captured);
 }
 setup_fresh();assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_arm()==1);
 first_css_browser_postdraw_stream.matched_ticks=139;
 assert(melee_web_native_menu_stadium_first_sss_pair_fresh_v4_kick()==0);
 assert(!first_sss_constructor_pair.kicked&&!pending);
 std::cout<<"PASS extracted shared native SSS pair arm/kick; legacy 147/148 retained; fresh 140/141/142 exact with distinct first/terminal seeds; cross-profile, seed-owner/current/final-seed and chain refusals before mutation\n";
 return 0;
}
}
int main(){return control::run_control();}
'''
if len(sys.argv)!=2:
    print("expected the OwnedWorkspaceTests scratch directory",file=sys.stderr);sys.exit(2)
work=Path(sys.argv[1])
work_root=(root/"work").resolve()
if (not work.is_absolute() or work.is_symlink() or not work.is_dir() or
        work.resolve().parent!=work_root):
    print("refusing non-owned native-control scratch directory",file=sys.stderr);sys.exit(2)
cpp=work/"fresh_pair_native_control.cpp"
cpp.write_text(harness)
compiler=shutil.which("clang++")
if not compiler:
    print("clang++ unavailable",file=sys.stderr);sys.exit(77)
binary=work/"fresh_pair_native_control.bin"
result=subprocess.run([compiler,"-std=c++20","-O0",str(cpp),"-o",str(binary)],
                      text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
(work/"compile.log").write_text(result.stdout)
if result.returncode:
    print(result.stdout);sys.exit(result.returncode)
run=subprocess.run([str(binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=15)
(work/"run.log").write_text(run.stdout)
print(run.stdout,end="")
sys.exit(run.returncode)
