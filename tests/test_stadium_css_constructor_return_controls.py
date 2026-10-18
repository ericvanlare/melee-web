"""Actual constructor-return owner predicates with explicit state doubles.

These controls cover private launch/refusal predicates, observation read gates,
and preparation admission/cleanup fragments with explicit state doubles. They
do not execute a browser, native menu host, source callback, or original service.
"""
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/gameplay_menu_browser.cpp"


def source_slice(start, end):
    source = SOURCE.read_text(encoding="utf-8")
    if source.count(start) != 1 or source.count(end) != 1:
        raise AssertionError(f"Expected one source boundary for {start!r} and {end!r}")
    return source[source.index(start):source.index(end)]


class StadiumCssConstructorReturnControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-css-constructor-return-")

    def test_actual_prepare_admission_and_observation_pointer_read_gates(self):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("clang++ required for extracted owner read controls")
        pointer_helpers = source_slice(
            "bool first_css_constructor_return_seed_pointer_stable(bool owner_live){",
            "void first_css_browser_draw_refresh_observation(){")
        source = SOURCE.read_text(encoding="utf-8")
        gate_start = source.index("  const bool pointer_owner_live=")
        gate = source[gate_start:source.index(";", gate_start) + 1]
        menu = (ROOT / "src/gameplay_menu_browser_menu.cpp").read_text(encoding="utf-8")
        prepare = menu[menu.index("int melee_web_native_menu_prepare(){"):
                       menu.index("int melee_web_native_menu_launch(){")]
        # Compile the actual admission prefix and unchanged destructive catch.
        # The middle preparation body is an explicit throwing double; no source
        # host construction, enter_world, tick or browser execution is covered.
        admission = prepare[:prepare.index("#if defined(MELEE_WEB_PIPELINE_PROVENANCE)")]
        cleanup = prepare[prepare.index("}catch(const std::exception& e){"):]
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
unsigned source_reads=0;
bool trap_source_reads=true;
struct ReadWord { unsigned value;
 friend bool operator==(const ReadWord& word,unsigned expected){
  ++source_reads;
  if(trap_source_reads)throw std::runtime_error("unexpected source pointer read");
  return word.value==expected;
 }
};
struct SeedPointer { const ReadWord* value=nullptr;
 explicit operator bool() const{return value!=nullptr;}
 const ReadWord& operator*() const{return *value;}
 friend bool operator==(SeedPointer a,SeedPointer b){return a.value==b.value;}
};
struct GameModeState { struct GameSceneInfo { ReadWord scene_kind; }; };
struct State {
 bool entry_captured=false;
 SeedPointer seed_owner;
 const GameModeState::GameSceneInfo* scene_owner=nullptr;
 struct { unsigned random_seed=0; } entry;
} first_css_browser_draw;
SeedPointer seed_ptr;
bool host_entered=false,world_exposed=false;
std::string message;
bool constructor_active=false;
bool stadium_first_css_constructor_return_active(){return constructor_active;}
unsigned close_calls=0,world_destroys=0,host_destroys=0,session_ends=0;
unsigned incidents=0,normal_body_calls=0;
struct World {
 void close_prepared(){++close_calls;}
 ~World(){++world_destroys;}
};
std::unique_ptr<World> world;
struct Host {} host_storage;
Host* host=nullptr;
bool source_session_owned=false;
void diagnostic_incident(int){++incidents;}
int melee_web_menu_host_destroy(Host*,char*,size_t){++host_destroys;return 1;}
int melee_web_gameplay_session_end(char*,size_t){++session_ends;return 1;}
'''
        live_gate = ("\nbool actual_observation_gate(bool generation_stable,bool armed_owner_stable){\n"
                     "const auto& state=first_css_browser_draw;\n" + gate +
                     "\nreturn pointer_owner_live;\n}\n")
        prepare_control = admission + '\n++normal_body_calls;throw std::runtime_error("normal preparation failure");\n' + cleanup
        main = r'''
int main(){
 ReadWord seed{17};GameModeState::GameSceneInfo scene{{8}};
 auto& state=first_css_browser_draw;
 state.seed_owner={&seed};seed_ptr={&seed};state.scene_owner=&scene;
 state.entry.random_seed=17;state.entry_captured=true;
 host_entered=true;world_exposed=true;
 auto observe=[&](bool generation,bool owner){
  const bool live=actual_observation_gate(generation,owner);
  return first_css_constructor_return_seed_pointer_stable(live)||
         first_css_constructor_return_scene_pointer_stable(live,&scene);
 };
 assert(!observe(false,true)&&source_reads==0);
 assert(!observe(true,false)&&source_reads==0);
 state.entry_captured=false;
 assert(!observe(true,true)&&source_reads==0);
 // Also exercise the helper's own entry gate with a true caller flag.
 assert(!first_css_constructor_return_seed_pointer_stable(true)&&source_reads==0);
 assert(!first_css_constructor_return_scene_pointer_stable(true,&scene)&&source_reads==0);
 state.entry_captured=true;host_entered=false;
 assert(!observe(true,true)&&source_reads==0);
 host_entered=true;world_exposed=false;
 assert(!observe(true,true)&&source_reads==0);
 world_exposed=true;trap_source_reads=false;
 assert(first_css_constructor_return_seed_pointer_stable(actual_observation_gate(true,true)));
 assert(first_css_constructor_return_scene_pointer_stable(actual_observation_gate(true,true),&scene));
 assert(source_reads==2);
 trap_source_reads=true;
 bool trap_fired=false;
 try{(void)first_css_constructor_return_seed_pointer_stable(true);}
 catch(const std::runtime_error&){trap_fired=true;}
 assert(trap_fired&&source_reads==3);

 // Protected prepared and entered owners refuse before the catch/body.
 constructor_active=true;world=std::make_unique<World>();host=&host_storage;
 source_session_owned=true;
 auto* const retained_world=world.get();
 for(bool entered:{false,true}){
  host_entered=entered;
  assert(melee_web_native_menu_prepare()==0);
  assert(world.get()==retained_world&&host==&host_storage&&source_session_owned);
  assert(close_calls==0&&world_destroys==0&&host_destroys==0&&session_ends==0);
  assert(normal_body_calls==0&&incidents==0&&constructor_active);
 }
 // With admission inactive, the original catch still retires a failed
 // unentered preparation through its ordinary close/destroy/end calls.
 constructor_active=false;host_entered=false;
 assert(melee_web_native_menu_prepare()==0);
 assert(normal_body_calls==1&&incidents==1&&close_calls==1&&world_destroys==1);
 assert(host_destroys==1&&session_ends==1&&!world&&!host&&!source_session_owned);
}
'''
        cpp = self.scratch / "pointer-and-prepare-controls.cpp"
        binary = self.scratch / "pointer-and-prepare-controls"
        cpp.write_text(fixture + pointer_helpers + live_gate + prepare_control + main,
                       encoding="utf-8")
        compile_log = self.scratch / "pointer-and-prepare-compile.log"
        with compile_log.open("w", encoding="utf-8") as log:
            result = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                                     str(cpp), "-o", str(binary)],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(result.returncode, 0, compile_log.read_text(encoding="utf-8"))
        run_log = self.scratch / "pointer-and-prepare-run.log"
        with run_log.open("w", encoding="utf-8") as log:
            result = subprocess.run([str(binary)], stdout=log,
                                    stderr=subprocess.STDOUT, timeout=20)
        self.assertEqual(result.returncode, 0, run_log.read_text(encoding="utf-8"))

    def test_actual_private_launch_predicates_are_one_use_and_fail_closed(self):
        compiler = shutil.which("clang++")
        if not compiler:
            self.skipTest("clang++ required for extracted owner-predicate controls")
        enum = source_slice("enum class FirstCssBrowserMode : unsigned {",
                            "struct FirstCssDrawSample {")
        failure = source_slice("void first_css_browser_draw_fail(const std::string& error){",
                               "void first_css_browser_draw_check_live(")
        helpers = source_slice("bool stadium_first_css_constructor_return_active(){",
                               "uint32_t asset_generation=0;")
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <stdexcept>
struct MeleeWebMenuHost {};
namespace melee_web { struct GameplayMenuWorld {}; }
MeleeWebMenuHost host_storage;
constexpr int GM_VS=1,MELEE_WEB_MENU_CREATED=0,MELEE_WEB_MENU_CSS=1;
constexpr int MELEE_WEB_MENU_HOST_SCENE_CSS=1;
'''
        state = r'''
struct FirstCssBrowserDrawState {
 FirstCssBrowserMode mode=FirstCssBrowserMode::None;
 bool armed=false,failed=false,constructor_arm_ready=false;
 bool constructor_launch_consumed=false,constructor_entry_attempted=false;
 bool entry_captured=false,constructor_return_terminal=false;
 bool arm_source_session_owned=false;
 MeleeWebMenuHost* armed_host_owner=nullptr;
 melee_web::GameplayMenuWorld* armed_world_owner=nullptr;
 uint64_t armed_world_generation=0,world_generation=0;
 std::string error;
};
FirstCssBrowserDrawState first_css_browser_draw;
MeleeWebMenuHost* host=nullptr;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
bool host_entered=false,running=false,pending=false,first_use_draw_pending=false;
bool source_session_owned=false,faulted=false,world_exposed=false;
uint64_t current_world_generation=0;
uint64_t melee_web_gameplay_generation(){return current_world_generation;}
int current_host_phase=MELEE_WEB_MENU_CREATED,current_host_kind=GM_VS;
int current_host_scene=0;
int melee_web_menu_host_phase(MeleeWebMenuHost*){return current_host_phase;}
int melee_web_menu_host_mode_kind(MeleeWebMenuHost*){return current_host_kind;}
int melee_web_menu_host_source_scene(MeleeWebMenuHost*){return current_host_scene;}
unsigned audio_phase=0;
std::string message;
struct Clock { void reset(){} };
Clock menu_clock,audio_clock;
unsigned refreshes=0;
void first_css_browser_draw_refresh_observation(){++refreshes;}
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
'''
        main = r'''
static void reset(){first_css_browser_draw={};world.reset();host=nullptr;
 host_entered=false;running=false;pending=false;first_use_draw_pending=false;
 source_session_owned=false;faulted=false;world_exposed=false;current_world_generation=0;
 current_host_phase=MELEE_WEB_MENU_CREATED;current_host_kind=GM_VS;current_host_scene=0;
 audio_phase=0;message.clear();refreshes=0;}
static void admitted(){auto& s=first_css_browser_draw;s.mode=FirstCssBrowserMode::ConstructorReturnOnlyV2;
 host=&host_storage;world=std::make_unique<melee_web::GameplayMenuWorld>();
 current_world_generation=7;source_session_owned=true;
 current_host_phase=MELEE_WEB_MENU_CREATED;current_host_kind=GM_VS;current_host_scene=0;
 s.armed=true;s.constructor_arm_ready=true;s.arm_source_session_owned=true;
 s.armed_host_owner=host;s.armed_world_owner=world.get();s.armed_world_generation=7;}
int main(){
 reset();assert(!stadium_first_css_constructor_return_active());
 assert(stadium_first_css_constructor_return_consume_launch());
 reset();first_css_browser_draw.mode=FirstCssBrowserMode::LegacyTickDrawV1;
 assert(stadium_first_css_constructor_return_consume_launch());
 reset();admitted();assert(stadium_first_css_constructor_return_active());
 assert(stadium_first_css_constructor_return_consume_launch());
 assert(first_css_browser_draw.constructor_launch_consumed);
 assert(!stadium_first_css_constructor_return_consume_launch());
 assert(stadium_first_css_constructor_return_active());
 reset();admitted();first_css_browser_draw.constructor_entry_attempted=true;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();first_css_browser_draw.failed=true;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();first_css_browser_draw.armed=false;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();first_css_browser_draw.constructor_arm_ready=false;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();host_entered=true;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();++current_world_generation;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();source_session_owned=false;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();host=nullptr;
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();world.reset();
 assert(!stadium_first_css_constructor_return_consume_launch());
 reset();admitted();first_css_browser_draw.entry_captured=true;
 first_css_browser_draw.constructor_return_terminal=true;host_entered=true;world_exposed=true;
 first_css_browser_draw.world_generation=7;current_host_phase=MELEE_WEB_MENU_CSS;
 current_host_scene=MELEE_WEB_MENU_HOST_SCENE_CSS;
 assert(stadium_first_css_constructor_return_entry_succeeded());
 ++current_world_generation;assert(!stadium_first_css_constructor_return_entry_succeeded());
 current_world_generation=7;
 running=true;assert(!stadium_first_css_constructor_return_entry_succeeded());
 running=false;first_css_browser_draw.failed=true;
 assert(!stadium_first_css_constructor_return_entry_succeeded());
 first_css_browser_draw.failed=false;first_css_browser_draw.entry_captured=false;
 assert(!stadium_first_css_constructor_return_entry_succeeded());
 first_css_browser_draw.entry_captured=true;first_css_browser_draw.constructor_return_terminal=false;
 assert(!stadium_first_css_constructor_return_entry_succeeded());
 reset();admitted();first_css_browser_draw.constructor_arm_ready=false;
 stadium_first_css_constructor_return_fail("partial arm refusal");
 assert(first_css_browser_draw.failed&&first_css_browser_draw.constructor_return_terminal&&
        !first_css_browser_draw.constructor_arm_ready&&
        first_css_browser_draw.error=="partial arm refusal"&&
        stadium_first_css_constructor_return_active()&&
        !stadium_first_css_constructor_return_consume_launch()&&
        !stadium_first_css_constructor_return_entry_succeeded()&&
        !running&&!pending&&!first_use_draw_pending&&audio_phase==0&&
        message=="partial arm refusal"&&
        refreshes==1);
}
'''
        cpp = self.scratch / "controls.cpp"
        binary = self.scratch / "controls"
        cpp.write_text(fixture + enum + state + failure + helpers + main, encoding="utf-8")
        compile_log = self.scratch / "compile.log"
        with compile_log.open("w", encoding="utf-8") as log:
            compiled = subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                                       str(cpp), "-o", str(binary)],
                                      stdout=log, stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(compiled.returncode, 0, compile_log.read_text(encoding="utf-8"))
        run_log = self.scratch / "run.log"
        with run_log.open("w", encoding="utf-8") as log:
            result = subprocess.run([str(binary)], stdout=log,
                                    stderr=subprocess.STDOUT, timeout=20)
        self.assertEqual(result.returncode, 0, run_log.read_text(encoding="utf-8"))
        self.assertEqual(run_log.read_text(encoding="utf-8"), "")


if __name__ == "__main__":
    unittest.main()
