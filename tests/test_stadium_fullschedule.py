"""Asset-free actual full-manifest decoder/host controls; no game lifecycle proof."""
import os,shutil,subprocess,sys
from pathlib import Path
SOURCE_ROOT=Path(__file__).resolve().parents[1]
ROOT=Path(os.environ.get('MELEE_STADIUM_FULL_TEST_ROOT',SOURCE_ROOT)).resolve()
sys.path.insert(0,str(ROOT/'scripts'));sys.path.insert(0,str(ROOT/'tests'))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from test_first_css_final_pending_draw_host import _function

class StadiumFullScheduleTests(OwnedWorkspaceTests):
 def test_full_host_dimensions_pad_and_prefix_refusals(self):
  result=subprocess.run([str(node_runtime(ROOT)),'--test',str(SOURCE_ROOT/'tests/stadium_fullschedule_controls.test.mjs')],cwd=ROOT,capture_output=True,text=True,timeout=45)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_full_common_fields_and_seed_controls(self):
  result=subprocess.run([str(node_runtime(ROOT)),'--test',str(SOURCE_ROOT/'tests/stadium_per_tick_common_fields_controls.test.mjs')],cwd=ROOT,capture_output=True,text=True,timeout=45)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_fresh132_full_a_projection_consumer_driver_controls(self):
  run_root=os.environ.get('MELEE_STADIUM_FRESH132_CONTROL_RUN_ROOT')
  if not run_root:self.skipTest('retained Fresh132 source controls need an explicit external run root')
  result=subprocess.run([str(node_runtime(ROOT)),str(SOURCE_ROOT/'tests/stadium_fresh132_full_a_controls.test.mjs'),run_root],cwd=ROOT,capture_output=True,text=True,timeout=60)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_actual_native_full_decoder_structure_and_pad_limit(self):
  compiler=shutil.which('c++')
  if not compiler:self.skipTest('native C++ compiler unavailable')
  scratch=self.new_workspace(ROOT,'stadium-full-manifest-')
  source=(SOURCE_ROOT/'src/gameplay_menu_browser.cpp').read_text()
  decoder=(_function(source,'int melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm(')+
           _function(source,'int matched_setup_cf_full_manifest_arm(')+
           _function(source,'void stadium_recreation_b_require_arm(bool version2,bool full_schedule,bool common_fields,unsigned action_count){'))
  fixture=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>
#include <iostream>
struct MatchedSetupCfState { struct ManifestAction { uint8_t kind=0; uint32_t ordinal=0,bundle=0,draws_before=0; std::array<uint8_t,44> pad{}; }; bool opted_in=false,armed=false,failed=false,complete=false; std::vector<ManifestAction> version2_actions; };
MatchedSetupCfState matched_setup_cf;
struct StadiumRecreationCanary { bool b_admitted=false,b_full_common_fields_seed=false; };
StadiumRecreationCanary stadium_recreation_canary;
std::string error;
unsigned arms=0;
void check(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
void matched_setup_cf_fail(const char* message){matched_setup_cf.failed=true;error=message;}
void stadium_recreation_b_require_arm(bool version2,bool full_schedule,bool common_fields,unsigned action_count);
bool expected_common_fields=false;
std::vector<bool> common_fields_flags;
int matched_setup_cf_arm_internal(bool v2,unsigned count,unsigned c,unsigned f,unsigned ct,unsigned ft,bool full=false,bool common_fields=false,bool fresh132_full_a=false){stadium_recreation_b_require_arm(v2,full,common_fields,count);assert(v2&&count==(fresh132_full_a?7168U:full?7167U:174U)&&c==168&&f==170&&ct==84&&ft==85);assert(common_fields==expected_common_fields);assert(!fresh132_full_a|| (full&&common_fields));common_fields_flags.push_back(common_fields);++arms;return 1;}
'''+decoder+r'''
void put(std::vector<uint8_t>& b,size_t off,uint32_t value){for(unsigned i=0;i<4;++i)b[off+i]=static_cast<uint8_t>(value>>(8*i));}
std::vector<uint8_t> manifest(unsigned kind=0){const bool common=kind!=0,fresh=kind==2;const unsigned count=fresh?7168:7167;std::vector<uint8_t>b(24+count*57);b[0]='M';b[1]='W';b[2]=fresh?'C':common?'C':'F';b[3]=fresh?'6':common?'5':'3';put(b,4,count);put(b,8,168);put(b,12,170);put(b,16,84);put(b,20,85);unsigned index=0,draws=0;for(unsigned tick=0;tick<3585;++tick){size_t off=24+(index++)*57;b[off]=1;put(b,off+1,tick);put(b,off+5,tick);put(b,off+9,draws);if(fresh?(tick!=2000&&tick!=2500):(tick!=2000&&tick!=2500&&tick!=3000)){off=24+(index++)*57;b[off]=2;put(b,off+1,draws);put(b,off+5,UINT32_MAX);put(b,off+9,draws);++draws;}}assert(index==count&&draws==(fresh?3583U:3582U));return b;}
int run(std::vector<uint8_t>b){matched_setup_cf={};error.clear();expected_common_fields=b.size()>=4&&b[0]=='M'&&b[1]=='W'&&b[2]=='C'&&(b[3]=='5'||b[3]=='6');return matched_setup_cf_full_manifest_arm(b.data(),b.size());}
int main(){const auto good=manifest();matched_setup_cf={};assert(melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm(good.data(),good.size())==0);auto prefix=good;prefix.resize(24+174*57);prefix[2]='P';prefix[3]='2';put(prefix,4,174);matched_setup_cf={};expected_common_fields=false;assert(melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_arm(prefix.data(),prefix.size())==1&&arms==1&&common_fields_flags.back()==false);assert(run(prefix)==0);assert(run(good)==1&&arms==2&&common_fields_flags.back()==false);const auto common=manifest(1);assert(run(common)==1&&arms==3&&common_fields_flags.back()==true);const auto fresh=manifest(2);assert(run(fresh)==1&&arms==4&&common_fields_flags.back()==true);stadium_recreation_canary={true,true};assert(run(fresh)==0&&arms==4);stadium_recreation_canary={};auto bad=good;bad[2]='P';bad[3]='2';assert(run(bad)==0);bad=fresh;bad.pop_back();assert(run(bad)==0);bad=fresh;bad.push_back(0);assert(run(bad)==0);bad=fresh;put(bad,4,7167);assert(run(bad)==0);bad=fresh;put(bad,12,172);assert(run(bad)==0);bad=fresh;put(bad,24+1,1);assert(run(bad)==0);bad=fresh;bad[24+13]=1;assert(run(bad)==1&&arms==5&&common_fields_flags.back()==true);bad=common;bad[24+13]=1;assert(run(bad)==1&&arms==6&&common_fields_flags.back()==true);std::cout<<"PASS actual native decoder: MWF3/MWC5/MWC6 routing, exact dimensions/order/refusals, and MWC6 rejection under admitted B; PAD mutation remains structurally accepted and requires separate host/source comparison\n";}
'''
  file=scratch/'control.cpp';file.write_text(fixture);binary=scratch/'control'
  compiled=subprocess.run([compiler,'-std=c++20','-O0','-Wall','-Wextra','-Werror',str(file),'-o',str(binary)],capture_output=True,text=True,timeout=45)
  self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
  result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
  self.assertEqual(result.returncode,0,result.stdout+result.stderr)
