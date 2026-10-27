"""Actual StockoutPair policy leaves; source world and MatchEnd services are modeled.

This is component-only evidence. It does not manufacture original observations,
run gameplay, or claim source action/stock/Results parity.
"""
from pathlib import Path
import os
import shlex
import subprocess
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(__file__).resolve().parents[1]

class StadiumStockoutPairPolicyTests(OwnedWorkspaceTests):
    def test_actual_stock_and_terminal_policy_with_fail_closed_leaves(self):
        source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        code=r'''#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
constexpr int OUTCOME_NONE=0,OUTCOME_ELIMINATION=2,OUTCOME_NO_CONTEST=7;
struct MeleeWebMatchStats {uint32_t player_slot=0;int32_t stocks=4,motion_id=14,ground_or_air=0,fighter_kind=8;};
struct Lifetime {bool mode14_observed=false,mode14_neutral_release=false;bool stock_initialized=true,mode9_observed=false,published_result=false,terminal_observed=false,window_closed=false,pause_seen=false,pause_release_seen=false;std::array<int32_t,2> stock_slots{0,1},last_stocks{4,4};unsigned stock_losses=0;uint64_t state_samples=0,tail_ticks=0,ticks=0,draws=0,span_first=0,span_last=0;std::string terminal;};
struct State {bool full_pair=true,route_prepared=true,native_retired=false,stockout_pair=true,natural14_pair=false,first_fire_pair=false,failed=false,action_open=false,go_observed=false;unsigned match_index=1;uint64_t go_world_tick=0;uint32_t mode14_source_tick=0,last_selected_source_tick=0;std::array<Lifetime,2> lifetimes{};} stadium_paired_v11;
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
// Explicit modeled settled-world and source MatchEnd leaves; invalid services refuse.
static bool owner_valid=true;static bool stadium_paired_v11_owner_valid(){return owner_valid;}
static bool publication_valid=true;static int publication_outcome=2,publication_count=1,publication_winner=1;
static int melee_web_match_rules_terminal_result(int* o,int* n,int* winners){if(!publication_valid)return 0;*o=publication_outcome;*n=publication_count;winners[0]=publication_winner;return 1;}
static std::vector<std::string> emitted;
static void stadium_paired_v11_emit(const char* kind,const std::string& text){emitted.push_back(kind);if(std::string(kind)=="published_result")assert(text.find("winners")!=std::string::npos);}
static struct Match {bool completed=true;int result=OUTCOME_ELIMINATION;bool complete(){return completed;}int outcome(int& winner){winner=1;return result;}} model_match,*match=&model_match;
static uint64_t replay_cursor=11;static std::string terminal_match_observation="{}";
'''
        for signature in ['bool stadium_paired_v11_full_active(){',
                          'std::string stadium_paired_v11_stockout_players(',
                          'void stadium_paired_v11_published_result(){',
                          'void stadium_paired_v11_before_retire(){']:
            # The player helper has one declaration and one definition; select
            # exact signature plus actual newline/body delimiter for extraction.
            if signature.startswith('std::string'):
                signature='std::string stadium_paired_v11_stockout_players(\n const std::array<MeleeWebMatchStats,2>& players,int16_t display_mode,uint64_t tick){'
            code+=_function(source,signature)+'\n'
        code+=r'''
static void reset(){stadium_paired_v11={};stadium_paired_v11.full_pair=stadium_paired_v11.route_prepared=stadium_paired_v11.stockout_pair=true;stadium_paired_v11.match_index=1;owner_valid=publication_valid=true;publication_outcome=OUTCOME_ELIMINATION;publication_count=1;publication_winner=1;model_match={};model_match.result=OUTCOME_ELIMINATION;emitted.clear();replay_cursor=11;terminal_match_observation="{}";}
static std::array<MeleeWebMatchStats,2> players(int stocks){std::array<MeleeWebMatchStats,2> p{};p[0].player_slot=0;p[1].player_slot=1;p[0].stocks=stocks;return p;}
template<class F>static void rejects(F f){bool refused=false;try{f();}catch(const std::runtime_error&){refused=true;}assert(refused);}
static void zero_stocks(){for(int n=3;n>=0;--n)(void)stadium_paired_v11_stockout_players(players(n),n==0?9:0,4-n);}
int main(){
 reset();for(int n=4;n>=0;--n){auto text=stadium_paired_v11_stockout_players(players(n),n==0?9:0,5-n);assert(text.find("stocks")!=std::string::npos);}auto& l=stadium_paired_v11.lifetimes[1];assert(l.stock_losses==4&&l.mode9_observed&&l.last_stocks[0]==0&&l.last_stocks[1]==4);
 l.ticks=10;l.span_first=1;l.span_last=10;l.state_samples=10;stadium_paired_v11.go_observed=true;stadium_paired_v11_before_retire();assert(l.window_closed&&l.terminal_observed&&emitted[0]=="window_closed"&&emitted[1]=="terminal");stadium_paired_v11_published_result();assert(l.published_result);rejects([]{stadium_paired_v11_published_result();});
 reset();auto absent=players(4);absent[0].fighter_kind=absent[0].motion_id=absent[0].ground_or_air=-1;auto text=stadium_paired_v11_stockout_players(absent,0,0);assert(text.find("\"fighter_present\":false")!=std::string::npos&&text.find("\"motion_id\":null")!=std::string::npos);
 for(unsigned which=0;which<7;++which){reset();auto p=players(4);if(which==0)p[0].player_slot=1;if(which==1)p[0].stocks=2;if(which==2)p[1].stocks=3;if(which==3)p[0].ground_or_air=2;if(which==4)stadium_paired_v11.stockout_pair=false;if(which==5)stadium_paired_v11.lifetimes[1].stock_initialized=false;if(which==6){p[0].fighter_kind=-1;p[0].ground_or_air=-1;}rejects([&]{stadium_paired_v11_stockout_players(p,0,0);});}
 reset();rejects([]{stadium_paired_v11_stockout_players(players(4),9,0);});
 reset();(void)stadium_paired_v11_stockout_players(players(3),0,0);rejects([]{stadium_paired_v11_stockout_players(players(4),0,1);});
 for(unsigned which=0;which<4;++which){reset();zero_stocks();auto& x=stadium_paired_v11.lifetimes[1];x.terminal_observed=true;if(which==0)publication_valid=false;if(which==1)publication_winner=0;if(which==2)publication_outcome=7;if(which==3)x.mode9_observed=false;rejects([]{stadium_paired_v11_published_result();});assert(!x.published_result);}
 reset();stadium_paired_v11.match_index=0;model_match.result=OUTCOME_NO_CONTEST;auto& a=stadium_paired_v11.lifetimes[0];a.ticks=10;a.span_first=1;a.span_last=10;a.pause_seen=a.pause_release_seen=true;stadium_paired_v11_before_retire();assert(a.terminal_observed&&a.state_samples==0);publication_outcome=OUTCOME_NO_CONTEST;publication_count=0;stadium_paired_v11_published_result();assert(a.published_result);
 reset();stadium_paired_v11.stockout_pair=false;model_match.result=OUTCOME_NO_CONTEST;auto& old=stadium_paired_v11.lifetimes[1];old.window_closed=true;old.state_samples=3501;old.pause_seen=old.pause_release_seen=true;old.span_last=10;stadium_paired_v11_before_retire();assert(old.terminal_observed);assert(emitted.size()==1&&emitted[0]=="terminal");
 std::cout<<"PASS actual stock/mode9/publication/retirement leaves; MODELED_WORLD_COMPONENT_ONLY\n";
}
'''
        work=self.new_workspace(ROOT,'stockout-pair-policy-');src=work/'control.cpp';exe=work/'control';src.write_text(code)
        result=subprocess.run([*shlex.split(os.environ.get('CXX','c++')),'-std=c++20','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],capture_output=True,text=True)
        (work/'compile.stdout').write_text(result.stdout);(work/'compile.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stderr)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        (work/'control.stdout').write_text(result.stdout);(work/'control.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_explicit_selector_and_existing_source_loop_are_preserved(self):
        source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        idle=_function(source,'int melee_web_native_menu_stadium_paired_v11_full_pair_arm(){')
        stock=_function(source,'int melee_web_native_menu_stadium_paired_v11_stockout_pair_arm(){')
        self.assertIn('stadium_paired_v11_full_profile_arm(false)',idle)
        self.assertIn('stadium_paired_v11_full_profile_arm(true)',stock)
        policy=_function(source,'void stadium_paired_v11_full_after_tick('+'uint64_t before,')
        self.assertIn('Original A has no detailed GO/typed22 witnesses',policy)
        self.assertLess(policy.index('if(s.match_index==0)'),policy.index('diagnostic_stadium_go_alignment_snapshot'))
        self.assertIn('!o.paused',policy)
        self.assertIn('match->player_stats(0),match->player_stats(1)',policy)
        pause=_function(source,'bool stadium_paired_v11_pause_before(const PADStatus* sample){')
        self.assertIn('if(s.match_index!=0)return false;',pause)
        self.assertIn('sample==replay->frames[replay_cursor].pads.data()',pause)
        self.assertIn('else if(!l.window_closed)return false;',pause)
        complete=_function(source,'void stadium_paired_v11_complete(){')
        self.assertIn('replay_completion_ready(replay_completion)',complete)
        self.assertIn('s.diagnostic_action_index==s.declared_actions',complete)
        self.assertNotIn('stock_check=',policy)
