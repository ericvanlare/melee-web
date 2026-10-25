"""Portable actual-source typed22 serialization/admission controls; no game proof."""
import json,os,shutil,subprocess
from pathlib import Path
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from test_first_css_final_pending_draw_host import _function
ROOT=Path(os.environ.get('MELEE_STADIUM_FULL_TEST_ROOT',Path(__file__).resolve().parents[1]))
class Typed22PairTests(OwnedWorkspaceTests):
 def test_host_fields_context_and_refusals(self):
  subprocess.run([str(node_runtime()),'--test',str(ROOT/'tests/stadium_fresh132_typed22_pair.test.mjs')],check=True)
 def test_retained_source_full_driver_with_explicit_synthetic_native(self):
  run_root=os.environ.get('MELEE_STADIUM_FRESH132_CONTROL_RUN_ROOT')
  if not run_root:self.skipTest('retained source projection needs explicit run root')
  env=dict(os.environ,MELEE_STADIUM_TYPED22_DRIVER_CONTROL='1')
  subprocess.run([str(node_runtime()),str(ROOT/'tests/stadium_fresh132_full_a_controls.test.mjs'),run_root],env=env,check=True)
 def test_actual_native_serializer_and_B_scope(self):
  source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
  serialize=_function(source,'std::string matched_setup_cf_common_fields_json(')
  equal=_function(source,'bool matched_setup_cf_idle_equal(')
  arm=_function(source,'void stadium_recreation_b_require_arm(bool version2,bool full_schedule,bool common_fields,unsigned action_count,bool typed22){')
  hex32=_function((ROOT/'src/stadium_first_css_diagnostic_snapshot_json.hpp').read_text(),'inline std::string hex32(')
  # Explicit leaf fixture supplies no match/source execution: compile the exact serializers and scope predicate.
  text=r'''#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include "gameplay_stadium_display_owner.h"
#include <cassert>
#include <limits>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <iterator>
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
namespace melee_web {namespace stadium_first_css_diagnostic {HEX32}}
constexpr size_t MATCHED_SETUP_CF_COMMON_SAMPLE_MAX_BYTES=1508;
enum class StadiumRecreationScope {Prefix174,LegacyFull7167,Fresh132Typed22};
struct { bool b_admitted=true,b_full_common_fields_seed=true; StadiumRecreationScope scope=StadiumRecreationScope::Fresh132Typed22; } stadium_recreation_canary;
SERIALIZE
EQUAL
ARM
int main(){
 MeleeWebStadiumIdleSnapshot a{};a.version=MELEE_WEB_STADIUM_IDLE_SNAPSHOT_VERSION;
 a.typed_relations_version=1;a.world_generation=UINT64_MAX;a.world_ticks=UINT64_MAX;a.source_frame=UINT32_MAX;
 a.map2_countdown=INT32_MIN;a.map2_phase=a.map2_form=a.map2_history_e0=a.map2_history_e2=INT16_MIN;
 a.display_mode=a.display_e6=a.display_e8=a.display_previous_mode=a.display_selected_player=a.display_f2=INT16_MIN;a.display_flags=7;
 const auto plain=matched_setup_cf_common_fields_json(a,UINT32_MAX,UINT32_MAX,UINT32_MAX);
 const auto typed=matched_setup_cf_common_fields_json(a,UINT32_MAX,UINT32_MAX,UINT32_MAX,true);
 assert(plain.find("native-v2")!=std::string::npos);assert(plain.find("map2_gate_mask_40")==std::string::npos);
 assert(typed.find("native-v3")!=std::string::npos);assert(typed.size()<=1508);
 auto b=a;assert(matched_setup_cf_idle_equal(a,b));b.map2_gate_mask_40=1;assert(!matched_setup_cf_idle_equal(a,b));
 b=a;b.title_static_present=2;bool refused=false;try{matched_setup_cf_common_fields_json(b,0,0,0,true);}catch(...){refused=true;}assert(refused);
 a.typed_relations_version=0;refused=false;try{matched_setup_cf_common_fields_json(a,0,0,0,true);}catch(...){refused=true;}assert(refused);
 stadium_recreation_b_require_arm(true,true,true,7168,true);
 for(unsigned count:{174U,7167U,7169U}){refused=false;try{stadium_recreation_b_require_arm(true,true,true,count,true);}catch(...){refused=true;}assert(refused);}
 refused=false;try{stadium_recreation_b_require_arm(true,true,true,7168,false);}catch(...){refused=true;}assert(refused);
 stadium_recreation_canary.scope=StadiumRecreationScope::LegacyFull7167;stadium_recreation_b_require_arm(true,true,true,7167,false);
 refused=false;try{stadium_recreation_b_require_arm(true,true,true,7168,true);}catch(...){refused=true;}assert(refused);
 stadium_recreation_canary.scope=StadiumRecreationScope::Prefix174;stadium_recreation_canary.b_full_common_fields_seed=false;stadium_recreation_b_require_arm(true,false,false,174,false);
 std::cout<<"{\"native_sample_max_bytes\":"<<typed.size()<<",\"legacy_sample_max_bytes\":"<<plain.size()<<",\"bound\":1508}"<<std::endl;
}
'''.replace('HEX32',hex32).replace('SERIALIZE',serialize).replace('EQUAL',equal).replace('ARM',arm)
  workspace=self.new_workspace(ROOT,'stadium-typed22-');cpp=workspace/'typed22.cpp';binary=workspace/'typed22';cpp.write_text(text)
  compiler=shutil.which('clang++');self.assertIsNotNone(compiler)
  subprocess.run([compiler,'-std=c++17','-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
  result=subprocess.run([str(binary)],check=True,text=True,capture_output=True);bounds=json.loads(result.stdout);self.assertLessEqual(bounds['native_sample_max_bytes'],1508);print(result.stdout)
