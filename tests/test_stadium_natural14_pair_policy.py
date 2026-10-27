"""Actual natural14 selector/observation/terminal leaves, modeled world services.

No game execution, source state injection, or original/native parity is claimed.
"""
from pathlib import Path
import os,shlex,subprocess
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(os.environ.get('MELEE_NATURAL14_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
SOURCE=Path(os.environ.get('MELEE_NATURAL14_BROWSER_SOURCE',ROOT/'src/gameplay_menu_browser.cpp'))

def typedef(path,name):
    source=path.read_text();start=source.index('typedef struct '+name+' {')
    return source[start:source.index('} '+name+';',start)+len('} '+name+';')]

class StadiumNatural14PairPolicyTests(OwnedWorkspaceTests):
    def actual_program(self):
        source=SOURCE.read_text()
        prefix=r'''#include <array>
#include <cstdint>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
using u8=uint8_t;using s8=int8_t;using u16=uint16_t;using s16=int16_t;using u32=uint32_t;
#define MELEE_WEB_MATCH_MAX_PLAYERS 4
constexpr int OUTCOME_NO_CONTEST=7,OUTCOME_ELIMINATION=2;
'''
        prefix+=typedef(ROOT/'.deps/melee/extern/dolphin/include/dolphin/pad.h','PADStatus')+'\n'
        prefix+=typedef(ROOT/'src/gameplay_stadium_display_owner.h','MeleeWebStadiumIdleSnapshot')+'\n'
        prefix+=typedef(ROOT/'src/gameplay_match_context.h','MeleeWebStadiumPadQueueObservation')+'\n'
        prefix+=r'''
// Explicit modeled state/service leaves. Actual policy functions below are
// extracted verbatim; no source gameplay callback is substituted successfully.
struct Lifetime {bool mode14_observed=false,mode14_neutral_release=false,window_closed=false,
 terminal_observed=false,pause_seen=false,pause_release_seen=false,mode9_observed=false;
 uint64_t generation=7,ticks=0,state_samples=0,tail_ticks=0,span_first=100,span_last=5100,draws=0;
 unsigned stock_losses=0;std::array<int,2> last_stocks{4,4};std::string terminal;};
struct State {bool full_pair=true,route_prepared=true,native_retired=false,natural14_pair=true,
 stockout_pair=false,failed=false,action_open=false,go_observed=true;
 unsigned match_index=1;uint64_t go_world_tick=80;
 uint32_t mode14_source_tick=3700,last_selected_source_tick=3702;
 std::array<Lifetime,2> lifetimes{};} stadium_paired_v11;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static bool owned=true;
static bool stadium_paired_v11_owner_valid(){return owned;}
static std::vector<std::string> emitted;
static void stadium_paired_v11_emit(const char* kind,const std::string&){emitted.emplace_back(kind);}
static struct Match {bool completed=true;int result=OUTCOME_NO_CONTEST;
 bool complete(){return completed;}int outcome(int& winner){winner=-1;return result;}} model_match,*match=&model_match;
static uint64_t replay_cursor=5101;
static std::string terminal_match_observation="{actual_modeled_terminal:true}";
'''
        for sig in ['bool stadium_paired_v11_full_active(){',
                    'bool stadium_paired_v11_pause_pad(const PADStatus* pads){',
                    'bool stadium_paired_v11_natural14_selector_valid(',
                    'void stadium_paired_v11_natural14_observe(',
                    'void stadium_paired_v11_before_retire(){']:
            prefix+=_function(source,sig)+'\n'
        prefix+=r'''
template<class F>void rejects(F f){bool refused=false;try{f();}catch(const std::runtime_error&){refused=true;}assert(refused);}
[[maybe_unused]] static MeleeWebStadiumIdleSnapshot idle(int mode=1){MeleeWebStadiumIdleSnapshot x{};
 x.map2_phase=0;x.map2_form=5;x.map2_history_e0=x.map2_history_e2=-1;
 x.map5_root_relation=1;x.display_mode=mode;x.display_selected_player=0;return x;}
[[maybe_unused]] static MeleeWebStadiumPadQueueObservation queue(){MeleeWebStadiumPadQueueObservation q{};
 q.player_count=2;q.slots[0]=q.controllers[0]=0;q.slots[1]=q.controllers[1]=1;
 q.queue_slots[2].err=q.queue_slots[3].err=-1;return q;}
static void reset(){stadium_paired_v11={};owned=true;model_match={};emitted.clear();replay_cursor=5101;}
'''
        return prefix

    def run_actual(self,label,main):
        scratch=self.new_workspace(ROOT,'natural14-'+label+'-')
        source=scratch/'control.cpp';binary=scratch/'control';source.write_text(self.actual_program()+main)
        r=subprocess.run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],capture_output=True,text=True)
        (scratch/'compile.stdout').write_text(r.stdout);(scratch/'compile.stderr').write_text(r.stderr)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        r=subprocess.run([str(binary)],capture_output=True,text=True)
        (scratch/'control.stdout').write_text(r.stdout);(scratch/'control.stderr').write_text(r.stderr)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_actual_selector_mode14_and_neutral_release_refusals(self):
        self.run_actual('selected-source',r'''
int main(){
 assert(stadium_paired_v11_natural14_selector_valid(3700,3702,5001));
 for(auto p:std::array<std::array<uint32_t,2>,5>{{{0,3702},{3500,3702},{3700,3700},{3700,3699},{3700,5001}}})
  assert(!stadium_paired_v11_natural14_selector_valid(p[0],p[1],5001));
 reset();auto q=queue();auto x=idle();
 stadium_paired_v11_natural14_observe(x,q,3699);
 x=idle(14);stadium_paired_v11_natural14_observe(x,q,3700);
 assert(stadium_paired_v11.lifetimes[1].mode14_observed);
 stadium_paired_v11_natural14_observe(x,q,3702);
 assert(stadium_paired_v11.lifetimes[1].mode14_neutral_release);
 for(unsigned which=0;which<10;which++){
  reset();auto y=idle(14);auto p=queue();
  if(which==0)y.map2_phase=1;
  if(which==1)y.map2_form=9;
  if(which==2)y.map2_gate=1;
  if(which==3)y.map2_history_e0=5;
  if(which==4)y.map5_root_relation=0;
  if(which==5)y.display_selected_player=2;
  if(which==6)stadium_paired_v11.stockout_pair=true;
  if(which==7)stadium_paired_v11.match_index=0;
  if(which==8)stadium_paired_v11.lifetimes[1].window_closed=true;
  if(which==9)stadium_paired_v11.natural14_pair=false;
  rejects([&]{stadium_paired_v11_natural14_observe(y,p,3700);});
 }
 reset();rejects([&]{stadium_paired_v11_natural14_observe(idle(14),q,3699);});
 reset();rejects([&]{stadium_paired_v11_natural14_observe(idle(),q,3700);});
 for(unsigned which=0;which<6;which++){
  reset();stadium_paired_v11.lifetimes[1].mode14_observed=true;auto p=queue();
  if(which==0)p.queue_slots[0].stickX=80;
  if(which==1)p.queue_slots[1].button=1;
  if(which==2)p.queue_slots[2].err=0;
  if(which==3)p.slots[1]=0;
  if(which==4)p.controllers[1]=0;
  if(which==5)p.player_count=1;
  rejects([&]{stadium_paired_v11_natural14_observe(idle(14),p,3702);});
 }
 std::cout<<"PASS actual selector/mode14/source-owner/neutral-release leaves MODELED_WORLD_ONLY\n";
}
''')

    def test_actual_terminal_requires_selected_window_and_normal_nocontest(self):
        self.run_actual('terminal',r'''
static void ready(){auto& s=stadium_paired_v11;auto& l=s.lifetimes[1];
 l.window_closed=l.mode14_observed=l.mode14_neutral_release=l.pause_seen=l.pause_release_seen=true;
 l.state_samples=s.last_selected_source_tick-s.go_world_tick+1;l.ticks=l.span_last-l.span_first+1;}
int main(){
 reset();ready();stadium_paired_v11_before_retire();assert(stadium_paired_v11.lifetimes[1].terminal_observed);
 assert(emitted.size()==1&&emitted[0]=="terminal");
 for(unsigned which=0;which<9;which++){
  reset();ready();auto& l=stadium_paired_v11.lifetimes[1];
  if(which==0)l.mode14_observed=false;
  if(which==1)l.mode14_neutral_release=false;
  if(which==2)l.state_samples--;
  if(which==3)l.pause_release_seen=false;
  if(which==4)model_match.result=OUTCOME_ELIMINATION;
  if(which==5)model_match.completed=false;
  if(which==6)replay_cursor--;
  if(which==7)owned=false;
  if(which==8)l.ticks--;
  rejects([]{stadium_paired_v11_before_retire();});assert(!l.terminal_observed);
 }
 reset();stadium_paired_v11.match_index=0;auto& a=stadium_paired_v11.lifetimes[0];
 a.pause_seen=a.pause_release_seen=true;a.ticks=a.span_last-a.span_first+1;
 stadium_paired_v11_before_retire();assert(a.window_closed&&a.terminal_observed&&a.state_samples==0);
 assert(emitted.size()==2&&emitted[0]=="window_closed"&&emitted[1]=="terminal");
 std::cout<<"PASS actual terminal/selected-span/NoContest/refused-owner leaves MODELED_WORLD_ONLY\n";
}
''')
