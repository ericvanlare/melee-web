"""Asset-free control of the actual diagnostic endpoint; no game ownership claim."""
from pathlib import Path
import os
import shlex
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r"""
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <stdexcept>
#include <cstdio>
struct PrefixNative {int state=2,last_tick_result=3,exit_captured=1,selected_stage_captured=1;unsigned input_index=124,consumed_inputs=124,host_tick_calls=124,host_draw_calls=124,matched_ticks=124,matched_draw_enters=124,matched_draw_returns=124;bool transition_requested=true;};
using MeleeWebMenuSssPrefixProfile=int;
enum{MELEE_WEB_MENU_SSS_PREFIX_LEGACY_124=1,MELEE_WEB_MENU_SSS_PREFIX_FRESH_132=2};
struct Prefix {unsigned aurora_begin_calls=132,aurora_end_calls=132;bool armed=true,kicked=true,attempted=true;void* callback_host_owner=(void*)1;const void* callback_world_owner=(void*)2;uint64_t callback_world_generation=7;int prefix_profile=MELEE_WEB_MENU_SSS_PREFIX_LEGACY_124;bool complete=true,failed=false,frame_end_returned=true;PrefixNative native;};
static Prefix first_sss_prefix_sequence;
struct World {void* get(){return (void*)2;} explicit operator bool()const{return true;}};static World world;
static uint64_t melee_web_gameplay_generation(){return 7;}
static bool running=false,pending=false,faulted=false,host_entered=false,menu_scene_rebuild_pending=false;
static void *host=(void*)1,*match=nullptr,*results=nullptr,*prize=nullptr,*replay=nullptr;
namespace melee_web { struct MenuPreparationState {enum class Phase{Idle};}; }
struct Preparation {bool busy(){return false;} auto phase(){return melee_web::MenuPreparationState::Phase::Idle;}} preparation;
enum class AssetDestination {None};static AssetDestination asset_destination=AssetDestination::None;
static uint64_t diagnostic_audio_render_calls=274,diagnostic_audio_render_frames=146133;
static unsigned audio_phase=20;
static bool melee_web_net_active(){return false;}
static int melee_web_menu_host_source_scene(void*){return 0;}
static int melee_web_menu_host_phase(void*){return 5;}
#define MELEE_WEB_MENU_READY 5
static void check(int ok,const char* e){if(!ok)throw std::runtime_error(e);}
struct Start {int fixture_value=7;};
struct MeleeWebMenuMatchSelection {uint32_t random_seed=0;Start start;};
static bool getter_ok=true; static int getter_calls=0,approval=1;static bool reenter=false,extra_audio=false;
static int melee_web_menu_host_stadium_c1a_selection(void*,MeleeWebMenuMatchSelection* s,char* e,unsigned n){++getter_calls;if(!getter_ok){snprintf(e,n,"checked owner refused");return 0;}s->random_seed=0x3bb84c53;return 1;}
namespace melee_web::stadium_first_css_diagnostic {
static std::string hex32(uint32_t x){char b[9];snprintf(b,sizeof b,"%08x",x);return b;}
static void write_start(std::ostream& o,const Start& s){o<<"{\"fixture_value\":"<<s.fixture_value<<'}';}
}
static void first_css_browser_draw_refresh_observation(){}
int melee_web_native_menu_stadium_first_sss_vs_entry_kick();
static int approve(){if(reenter){reenter=false;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);}if(extra_audio)++diagnostic_audio_render_calls;return approval;}
#define EM_ASM_INT(...) approve()
"""
TEST = r"""
static void reset(){first_sss_vs_entry={};first_sss_prefix_sequence={};approval=1;getter_ok=true;getter_calls=0;reenter=false;extra_audio=false;diagnostic_audio_render_calls=274;diagnostic_audio_render_frames=146133;audio_phase=20;}
int main(){
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==1);assert(getter_calls==1&&first_sss_vs_entry.captured&&first_sss_vs_entry.complete);assert(first_sss_vs_entry.actual.find("3bb84c53")!=std::string::npos);auto retained=first_sss_vs_entry.actual;
 assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(first_sss_vs_entry.failed&&!first_sss_vs_entry.complete&&getter_calls==1&&first_sss_vs_entry.actual==retained);
 reset();approval=0;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==0&&first_sss_vs_entry.error=="VS Entry endpoint requires strict host prefix approval");
 reset();getter_ok=false;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==1&&!first_sss_vs_entry.captured&&first_sss_vs_entry.error=="checked owner refused");
 reset();reenter=true;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==0&&first_sss_vs_entry.error=="VS Entry endpoint is one-use");
 reset();extra_audio=true;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==1&&first_sss_vs_entry.captured&&!first_sss_vs_entry.actual.empty());assert(first_sss_vs_entry.error=="VS Entry endpoint performed unexpected audio work");
 reset();first_sss_prefix_sequence.native.consumed_inputs=123;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==0&&first_sss_vs_entry.failed);
 reset();first_sss_prefix_sequence.prefix_profile=MELEE_WEB_MENU_SSS_PREFIX_FRESH_132;assert(melee_web_native_menu_stadium_first_sss_vs_entry_kick()==0);assert(getter_calls==0);
 // Fixed fresh wrapper: same getter, exact profile/count and retained callback ownership.
 auto fresh=[](){reset();first_sss_prefix_sequence.prefix_profile=MELEE_WEB_MENU_SSS_PREFIX_FRESH_132;auto& n=first_sss_prefix_sequence.native;
 n.input_index=n.consumed_inputs=n.host_tick_calls=n.host_draw_calls=n.matched_ticks=n.matched_draw_enters=n.matched_draw_returns=132;};
 fresh();assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==1&&getter_calls==1);assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==1);
 reset();assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 for(int i=0;i<7;i++){fresh();unsigned* counts[]={&first_sss_prefix_sequence.native.input_index,&first_sss_prefix_sequence.native.consumed_inputs,&first_sss_prefix_sequence.native.host_tick_calls,&first_sss_prefix_sequence.native.host_draw_calls,&first_sss_prefix_sequence.native.matched_ticks,&first_sss_prefix_sequence.native.matched_draw_enters,&first_sss_prefix_sequence.native.matched_draw_returns};*counts[i]=131;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);}
 fresh();first_sss_prefix_sequence.callback_host_owner=(void*)3;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.callback_world_owner=(void*)3;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.callback_world_generation=8;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 for(int i=0;i<8;i++){fresh();bool* flags[]={&first_sss_prefix_sequence.armed,&first_sss_prefix_sequence.kicked,&first_sss_prefix_sequence.attempted,&first_sss_prefix_sequence.frame_end_returned,&first_sss_prefix_sequence.complete,&running,&pending,&faulted};*flags[i]=i<5?false:true;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);running=pending=faulted=false;}
 fresh();first_sss_prefix_sequence.failed=true;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.aurora_begin_calls=131;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.aurora_end_calls=133;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.native.selected_stage_captured=0;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();first_sss_prefix_sequence.native.exit_captured=0;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();host_entered=true;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);host_entered=false;
 fresh();approval=0;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==0);
 fresh();getter_ok=false;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&getter_calls==1&&!first_sss_vs_entry.captured);
 fresh();extra_audio=true;assert(melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()==0&&first_sss_vs_entry.captured);
 puts("PASS actual endpoint one-use, approval/refusal, reentrancy and capture-before-failure controls");
}
"""
class FirstSssVsEntryEndpointTests(OwnedWorkspaceTests):
    def test_actual_endpoint_one_use_and_failures(self):
        source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        start=source.index('struct FirstSssVsEntryState {')
        end=source.index('std::string first_sss_vs_entry_json()', start)
        state=source[start:end]
        function='\n'.join(_function(source, signature) for signature in ['static int first_sss_ready_start_kick(MeleeWebMenuSssPrefixProfile profile)', 'int melee_web_native_menu_stadium_first_sss_vs_entry_kick()', 'int melee_web_native_menu_stadium_fresh_sss_132_ready_start_kick()'])
        workspace=self.new_workspace(ROOT,'first-sss-vs-entry-')
        cpp=workspace/'endpoint.cpp';binary=workspace/'endpoint'
        cpp.write_text(PRELUDE+state+function+TEST)
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)]
        compiled=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (workspace/'compile.log').write_text(compiled.stdout)
        self.assertEqual(compiled.returncode,0,compiled.stdout)
        result=subprocess.run([str(binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        (workspace/'run.log').write_text(result.stdout)
        self.assertEqual(result.returncode,0,result.stdout)
if __name__=='__main__':unittest.main()
