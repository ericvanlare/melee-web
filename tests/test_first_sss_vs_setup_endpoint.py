"""Actual Setup functions with controlled services; not runtime equivalence."""
from pathlib import Path
import os, shlex, subprocess, unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(__file__).resolve().parents[1]
PRELUDE=r"""
#include <cassert>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include <cstdint>
#include <memory>
#include <array>
#include <vector>
#include <map>
#include <sstream>
#include <string>
#include <stdexcept>
#include <exception>
#include <cstdio>
#include <cstring>
#define MELEE_WEB_PAD_STATE_BYTES 822
struct MeleeWebPadState {std::array<uint8_t,822> bytes{};};
// Storage declarations for the adjacent opt-in state; this fixture executes only Setup.
struct MeleeWebMenuHost;
struct MeleeWebStadiumIdleSnapshot {};
struct MeleeWebStadiumGoAlignmentSnapshot {};
static unsigned continuation_service_calls=0;
static bool matched_setup_cf_service(){++continuation_service_calls;throw std::runtime_error("default Setup unexpectedly invoked C/F continuation");}

struct MeleeWebDiagnosticSisOwner {void* heap=nullptr;uint64_t world_generation=0,allocation_generation=0;int source_heap_handle=0;uint64_t source_epoch=0;};
struct MeleeWebRetiredSisLease {MeleeWebDiagnosticSisOwner prior;size_t requested_bytes=0;int retirement_verified=0;};
static int text_probe_value=0,text_probe_sets=0,sis_capture_calls=0,membership_begin_calls=0,membership_end_calls=0;
static bool sis_capture_ok=true,membership_begin_ok=true,membership_snapshot_live=false;
static void HSD_SisLib_C1TextProbeSet(int enabled){text_probe_value=enabled;++text_probe_sets;}
static int melee_web_diagnostic_sis_capture(MeleeWebRetiredSisLease* lease,char* error,size_t size){++sis_capture_calls;if(!sis_capture_ok){snprintf(error,size,"capture refused");return 0;}lease->prior.heap=(void*)1;lease->prior.world_generation=9;lease->prior.source_heap_handle=3;lease->prior.allocation_generation=11;lease->prior.source_epoch=13;lease->requested_bytes=0x4800;return 1;}
static int melee_web_stadium_text_membership_snapshot_begin(const MeleeWebDiagnosticSisOwner*,char* error,size_t size){++membership_begin_calls;if(!membership_begin_ok){snprintf(error,size,"membership refused");return 0;}membership_snapshot_live=true;return 1;}
static void melee_web_stadium_text_membership_snapshot_end(){++membership_end_calls;membership_snapshot_live=false;}
static void melee_web_pad_state_free(MeleeWebPadState* p){delete p;}
static int melee_web_pad_state_encode(const MeleeWebPadState* p,uint8_t* out,size_t n){if(!p||!out||n!=822)return 0;memcpy(out,p->bytes.data(),n);return 1;}
static MeleeWebPadState* melee_web_pad_state_decode(const uint8_t* b,size_t n,char*,unsigned){if(n!=822)return nullptr;auto* p=new MeleeWebPadState;memcpy(p->bytes.data(),b,n);return p;}
static MeleeWebPadState host_pad;
static uint32_t seed=17,foreign_seed=19,*seed_ptr=&seed;
static uint64_t generation=7;static bool source_world=true;
static uint64_t melee_web_gameplay_generation(){return source_world?generation:0;}
static bool melee_web_gameplay_world_exists(){return source_world;}
static bool running=false,pending=false,faulted=false,host_entered=false,menu_scene_rebuild_pending=false;
static bool first_use_draw_pending=false,world_exposed=true,stadium_c1a_armed=false,scoped_assets=true,scoped_disc_import=true;
static void* host=(void*)1;static void *results=nullptr,*prize=nullptr,*replay=nullptr;
static bool melee_web_net_active(){return false;}
static int melee_web_menu_host_source_scene(void*){return 0;}
static int melee_web_menu_host_phase(void*){return 5;}
static const MeleeWebPadState* melee_web_menu_host_input(void*){return &host_pad;}
#define MELEE_WEB_MENU_READY 5
static void check(int ok,const char* e){if(!ok)throw std::runtime_error(e);}
static std::string first_css_browser_draw_json_quote(const std::string& value){return std::string("\"")+value+"\"";}
struct Clock {void reset(){}} menu_clock,audio_clock;
namespace melee_web {struct MenuPreparationState{enum class Phase{Idle};};}
struct Preparation {bool busy(){return false;} auto phase(){return melee_web::MenuPreparationState::Phase::Idle;}} preparation;
struct Start {int value=7;};struct MeleeWebMenuMatchSelection{uint32_t random_seed=0x3bb84c53;Start start;};
static bool getter_ok=true;static unsigned getter_calls=0;static int selected_value=7;
static int melee_web_menu_host_stadium_c1a_selection(void*,MeleeWebMenuMatchSelection* s,char* e,unsigned n){++getter_calls;if(!getter_ok){snprintf(e,n,"get refused");return 0;}s->start.value=selected_value;return 1;}
namespace melee_web::stadium_first_css_diagnostic {
static std::string hex32(uint32_t x){char b[9];snprintf(b,sizeof b,"%08x",x);return b;}
static void write_start(std::ostream& o,const Start& s){o<<"{\"value\":"<<s.value<<'}';}}
static bool retirement_changes_seed=false;
struct World {void close(){source_world=false;if(retirement_changes_seed)seed_ptr=&foreign_seed;}};
static std::unique_ptr<World> world;
static std::map<std::string,std::vector<uint8_t>> files;
struct MeleeWebStadiumSetupSnapshot {uint32_t version=1;uint64_t world_generation=9,world_ticks=0;uint32_t source_frame=0;int32_t map_id=2,initial_countdown=4,current_countdown=4,parameter_x0=3,parameter_x4=5;uint32_t constructor_randi_call_count=1,constructor_random_bound_valid=1;int32_t constructor_random_bound=2;uint32_t constructor_random_result_valid=1;int32_t constructor_random_result=1;uint32_t source_seed_before=17,source_seed_after=19;int32_t ground_source_heap_handle=3;uint32_t ground_requested_bytes=256;uint64_t ground_allocation_generation=12;int32_t object_source_heap_handle=3;uint32_t object_requested_bytes=128;uint64_t object_allocation_generation=11;uint32_t constructor_callback_count=1,source_parameter_roots_agree=1;};
static bool setup_snapshot_ok=true;static unsigned setup_snapshot_calls=0;
namespace melee_web {
static std::vector<std::string> stadium_c1a_asset_names(const MeleeWebMenuMatchSelection&){return {"stage","fighter"};}
struct RuntimeArchiveCache {explicit RuntimeArchiveCache(const decltype(files)&){}};
enum class GameplayMatchConstruction{Deferred};enum class GameplayMatchDiagnostic{StadiumReady};
struct GameplayMatchSession {unsigned steps=0,frames=0;bool done=false;Start data;GameplayMatchSession(const decltype(files)&,const MeleeWebMenuMatchSelection& s,RuntimeArchiveCache&,GameplayMatchConstruction,const MeleeWebPadState&,GameplayMatchDiagnostic):data(s.start){source_world=true;++generation;}bool advance_construction(){++steps;return done=steps==2;}unsigned source_frames(){return frames;}bool construction_complete(){return done;}uint32_t random_seed(){return 0x35a455b9;}const Start& start_data(){return data;}bool diagnostic_stadium_setup_snapshot(MeleeWebStadiumSetupSnapshot* out,char* e,size_t n){++setup_snapshot_calls;if(!setup_snapshot_ok){snprintf(e,n,"setup snapshot refused");return false;}*out=MeleeWebStadiumSetupSnapshot{};return true;}};
}
static std::unique_ptr<melee_web::GameplayMatchSession> match;
static std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
enum class AssetDestination{None,StadiumC1a,FirstSssVsSetup};
static AssetDestination asset_destination=AssetDestination::None;static bool asset_committed=false,asset_selection_valid=false;
static uint32_t asset_generation=0;static MeleeWebMenuMatchSelection asset_selection;
static std::vector<std::string> requested_assets;
struct Scope{unsigned pending=0,active=0;unsigned pending_generation(){return pending;}unsigned active_file_count(){return active;}} asset_scope;
static uint64_t diagnostic_audio_render_calls=274,diagnostic_audio_render_frames=146133;
struct Entry{bool complete=true,failed=false;std::string actual="{\"phase\":\"vs_entry\",\"random_seed_hex\":\"3bb84c53\",\"start\":{\"value\":7}}";} first_sss_vs_entry;
struct Prefix{bool complete=true,failed=false;} first_sss_prefix_sequence;
struct Observation{std::string observation="{\"old\":true,\"first_sss_vs_setup\":{}}";} first_css_browser_draw;
static unsigned refresh_calls=0;static void first_css_browser_draw_refresh_observation(){++refresh_calls;}
static std::string message;
static void request_assets(AssetDestination d,const MeleeWebMenuMatchSelection* s){asset_destination=d;asset_selection=*s;asset_selection_valid=true;requested_assets=melee_web::stadium_c1a_asset_names(*s);++asset_generation;asset_scope.pending=asset_generation;}
int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe);
static int approval=1;static bool reenter=false;
static void close();
static int approve(){if(reenter){reenter=false;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0);}return approval;}
#define EM_ASM_INT(...) approve()
"""
TEST=r"""
static void reset(){match.reset();archive_cache.reset();setup_snapshot_ok=true;setup_snapshot_calls=0;first_sss_vs_setup={};first_sss_vs_entry={};first_sss_prefix_sequence={};world=std::make_unique<World>();source_world=true;generation=7;seed=17;seed_ptr=&seed;retirement_changes_seed=false;approval=1;reenter=false;getter_ok=true;getter_calls=0;selected_value=7;asset_destination=AssetDestination::None;asset_generation=0;asset_committed=false;asset_selection_valid=false;asset_scope={};files.clear();host_pad={};running=pending=faulted=host_entered=first_use_draw_pending=false;}
static void reset_text_probe(){first_sss_text_tail_probe={};first_sss_text_tail_retained_json.clear();text_probe_value=text_probe_sets=sis_capture_calls=membership_begin_calls=membership_end_calls=0;sis_capture_ok=membership_begin_ok=true;membership_snapshot_live=false;}
static void commit(){files={{"stage",{1}},{"fighter",{2}}};asset_scope.active=2;asset_scope.pending=0;asset_committed=true;}
std::string first_sss_vs_setup_current_json(){return "{}";}
static void close(){FirstSssTextTailCloseGuard guard{first_sss_text_tail_probe.opted_in};throw std::runtime_error("controlled close failure");}
int main(){
 reset();reset_text_probe();assert(!first_sss_vs_setup_service());assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);assert(text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);assert(!world&&first_sss_vs_setup.parent_seed_restored&&first_sss_vs_setup.getter_calls==1);assert(first_sss_vs_setup.input!=&host_pad&&first_sss_vs_setup.history=="{\"old\":true}");assert(melee_web_native_menu_stadium_c1a_asset_scope(1)==1);assert(melee_web_native_menu_stadium_c1a_asset_scope(2)==0);
 assert(first_sss_vs_setup_service()&&!match);commit();assert(first_sss_vs_setup_service()&&match&&!first_sss_vs_setup.complete);assert(setup_snapshot_calls==0);assert(first_sss_vs_setup_service()&&!first_sss_vs_setup.complete);assert(setup_snapshot_calls==0);assert(first_sss_vs_setup_service()&&first_sss_vs_setup.complete&&first_sss_vs_setup.captured);assert(setup_snapshot_calls==1&&first_sss_vs_setup.stadium_setup_state.find("zero-tick-setup-state-v1")!=std::string::npos);assert(first_sss_vs_setup.source_frames==0&&first_sss_vs_setup.host_tick_calls==0&&first_sss_vs_setup.host_draw_calls==0);assert(first_sss_vs_setup.actual.find("35a455b9")!=std::string::npos);assert(first_sss_vs_setup.match_world_generation!=first_sss_vs_setup.prior_world_generation);assert(text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);auto actual=first_sss_vs_setup.actual;auto steps=match->steps;assert(first_sss_vs_setup_service()&&match->steps==steps);assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&first_sss_vs_setup.actual==actual);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();first_sss_vs_setup_service();first_sss_vs_setup_service();setup_snapshot_ok=false;assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!first_sss_vs_setup.complete&&!first_sss_vs_setup.captured&&first_sss_vs_setup.actual.empty()&&first_sss_vs_setup.stadium_setup_state.empty()&&setup_snapshot_calls==1);assert(first_sss_vs_setup.error=="setup snapshot refused");
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);assert(first_sss_text_tail_probe.opted_in&&text_probe_sets==0);first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.complete&&first_sss_text_tail_probe.probe_enabled&&text_probe_value==1);assert(text_probe_sets==1&&sis_capture_calls==1&&membership_begin_calls==1&&membership_snapshot_live);assert(first_sss_text_tail_probe.sis_lease_captured&&first_sss_text_tail_probe.membership_snapshot_active);first_sss_vs_setup_fail("controlled setup failure");assert(text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active);assert(first_sss_text_tail_retained_json.find("melee-web-stadium-first-sss-text-tail-retained-v1")!=std::string::npos&&first_sss_text_tail_retained_json.find("cleanup_done\":true")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(2)==0);assert(first_sss_vs_setup.failed&&!first_sss_text_tail_probe.opted_in&&text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(membership_snapshot_live);first_sss_text_tail_unload_refused("controlled unload refusal");assert(first_sss_text_tail_probe.unload_refused&&text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_retained_json.find("controlled unload refusal")!=std::string::npos);
 reset_text_probe();first_sss_text_tail_probe.opted_in=true;first_sss_text_tail_probe.probe_enabled=true;HSD_SisLib_C1TextProbeSet(1);first_sss_text_tail_probe.membership_snapshot_active=true;membership_snapshot_live=true;{FirstSssTextTailCloseGuard guard{true};}assert(text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1&&first_sss_text_tail_probe.cleanup_done);assert(first_sss_text_tail_retained_json.find("native-close-returned")!=std::string::npos);
 reset_text_probe();refresh_calls=0;first_sss_text_tail_probe.opted_in=true;first_sss_text_tail_probe.probe_enabled=true;HSD_SisLib_C1TextProbeSet(1);first_sss_text_tail_probe.membership_snapshot_active=true;membership_snapshot_live=true;assert(melee_web_native_menu_unload()==0);assert(message=="controlled close failure"&&refresh_calls==1);assert(first_sss_text_tail_probe.unload_refused&&first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_retained_json.find("native-close-threw")!=std::string::npos&&first_sss_text_tail_retained_json.find("controlled close failure")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);sis_capture_ok=false;first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&!first_sss_vs_setup.complete);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active);assert(first_sss_text_tail_probe.error=="capture refused"&&first_sss_text_tail_retained_json.find("capture refused")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);membership_begin_ok=false;first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&!first_sss_vs_setup.complete);assert(sis_capture_calls==1&&membership_begin_calls==1&&membership_end_calls==0);assert(first_sss_text_tail_probe.sis_lease_captured&&first_sss_text_tail_probe.membership_snapshot_attempted&&!first_sss_text_tail_probe.membership_snapshot_started);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active&&!membership_snapshot_live&&text_probe_value==0);assert(first_sss_vs_setup.error=="membership refused"&&first_sss_text_tail_probe.error=="membership refused"&&first_sss_text_tail_retained_json.find("membership refused")!=std::string::npos);
 reset();approval=0;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==0&&first_sss_vs_setup.failed);
 reset();reenter=true;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==0);
 reset();selected_value=8;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==1&&first_sss_vs_setup.getter_calls==1);
 reset();retirement_changes_seed=true;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&!world&&!first_sss_vs_setup.requested&&!first_sss_vs_setup.parent_seed_restored);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);host_pad.bytes[81]=1;commit();assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);assert(first_sss_vs_setup.error.find("PAD changed")!=std::string::npos);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);asset_scope.pending=2;assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);assert(melee_web_native_menu_stadium_c1a_asset_scope(1)==0);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();files.erase("stage");assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();first_sss_vs_setup_service();match->frames=1;first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&first_sss_vs_setup.source_frames==1&&!first_sss_vs_setup.captured);
 assert(continuation_service_calls==0);
 puts("PASS actual Setup begin/classifier/handoff/service: one-use, refusal, seed/PAD/generation, deferred completion and stop controls");
}
"""
class FirstSssVsSetupEndpointTests(OwnedWorkspaceTests):
 def test_actual_setup_service_and_refusals(self):
  source=(ROOT/'src/gameplay_menu_browser.cpp').read_text();menu=(ROOT/'src/gameplay_menu_browser_menu.cpp').read_text()
  a=source.index('struct FirstSssVsSetupState {');b=source.index('std::string first_sss_vs_setup_json(){',a)
  body=PRELUDE+source[a:b]
  for name in ['bool first_sss_vs_setup_asset_pending(unsigned generation)','bool first_sss_vs_setup_finish_handoff()']:
   body+=_function(source,name)
  body+=_function(menu,'int melee_web_native_menu_stadium_c1a_asset_scope(unsigned generation)')
  body+='bool finish_asset_handoff(){asset_destination=AssetDestination::None;asset_committed=false;return first_sss_vs_setup_finish_handoff();}'
  body+=_function(source,'void first_sss_vs_setup_fail(const char* error)')
  body+=_function(source,'bool first_sss_vs_setup_service()')+_function(source,'int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe)')+_function(source,'int melee_web_native_menu_unload()')+TEST
  w=self.new_workspace(ROOT,'first-sss-vs-setup-');cpp=w/'control.cpp';exe=w/'control';cpp.write_text(body)
  argv=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
  r=subprocess.run(argv,capture_output=True,text=True);(w/'compile.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(exe)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
