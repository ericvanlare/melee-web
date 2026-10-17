"""Actual helper mechanisms with endpoint doubles; no browser/owner proof."""
import os
from pathlib import Path
import shutil
import subprocess
from owned_test_workspace import OwnedWorkspaceTests
from test_stadium_current_sss_exit import run
ROOT=Path(os.environ.get('MELEE_STADIUM_STOCK4_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
BASE=Path(os.environ.get('MELEE_STADIUM_STOCK4_BASE_ROOT',ROOT))


def cut(text,start,end):
    assert text.count(start)==text.count(end)==1
    return text[text.index(start):text.index(end)]


class Stock4SssExitControls(OwnedWorkspaceTests):
    def test_actual_leave_finish_audio_and_pending_draw(self):
        text=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        helpers=cut(text,'bool current_sss_exit_profile_valid(','int current_sss_exit_arm(')
        preparation=cut(text,'void begin_preparation(){','bool preparation_uses_source_draws(){')
        audio=cut(text,'  if(!audio_before_construction&&!audio_elapsed.stalled',
                  '#if !defined(MELEE_WEB_PUBLIC_RUNTIME)\n  replay_boundary_mark("ordinary_audio_boundary_returned"')
        includes=''.join('#include "'+str(BASE/'src'/name)+'"\n' for name in
                         ['menu_preparation_state.hpp','source_frame_sequence.hpp'])
        fixture=r'''
#define MELEE_WEB_PUBLIC_RUNTIME 1
#include <cassert>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define EM_ASM(...) events.push_back("preparation-notify")
enum{MELEE_WEB_MENU_HOST_SCENE_SSS=2,MELEE_WEB_MENU_READY=5,St_Kind_PStadium=3,Gm_PKind_Human=0,MatchKind_Time=0};
enum class CurrentSssExitProfile{Time120,Stock4None};
struct Rules{int stkind=3,match_kind=1,time_limit=0,xB=-1;bool is_vs=true,is_stock=true,timer_enabled=false,timer_counts_up=false,disable_pausing=false,is_teams=false;uint64_t x20=UINT64_MAX;};
struct Player{int ckind=8,slot_type=0,color=0,stocks=4;};
struct Selection{bool save_profile_present=true;unsigned unlocked_characters=2047,unlocked_stages=2047,player_count=2;struct{Rules rules;Player players[2];}start;Selection(){start.players[0].color=1;}};
struct Recipe{Selection selection;std::vector<int> frames=std::vector<int>(2006);std::unique_ptr<int> initial_css{new int};};namespace melee_web{using RetailReplayRecipe=::Recipe;}std::unique_ptr<Recipe> replay{new Recipe};
struct State{CurrentSssExitProfile profile=CurrentSssExitProfile::Stock4None;bool armed=true,leave_started=false,terminal_pending=false,terminal=false,notified=false,failed=false,notification_received=false,notification_in_host_tick=false,notification_last_tick_returned=true,host_tick_in_flight=false,last_host_tick_returned=true;size_t notification_cursor=270,notification_steps=0,notification_draws=0,terminal_cursor=0,terminal_steps=0,terminal_draws=0;uint64_t leave_asset_requests=0,terminal_asset_requests=0;unsigned leave_asset_generation=0;const void* leave_world=nullptr;const void* leave_cache=nullptr;const Recipe* recipe=nullptr;const int* context=nullptr;const int* host_owner=nullptr;struct{int state=2;struct{bool captured=true;uint64_t world_generation=7;}note;}observation;}current_sss_exit;
std::unique_ptr<int> world{new int},archive_cache{new int};int h;int* host=&h;int* match=nullptr;int* results=nullptr;int* prize=nullptr;
bool host_entered=true,pending=true,running=true,faulted=false,menu_scene_rebuild_pending=false,transition_audio_continues=false;int phase=3,scene=2,pending_menu_source_scene=0;size_t replay_cursor=270;unsigned asset_generation=8;uint64_t current_sss_asset_request_calls=10;
enum class AssetDestination{None,Match};AssetDestination asset_destination=AssetDestination::None;struct{int pending_generation(){return 0;}}asset_scope;
melee_web::MenuPreparationState preparation;melee_web::SourceFrameSequence replay_source_frames;
std::vector<std::string> events;std::string message;struct{void reset(){events.push_back("clock-reset");}}menu_clock,audio_clock;
struct{void begin(bool,double){events.push_back("profile");}}preparation_profile;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}double emscripten_get_now(){return 0;}
void clear_diagnostic_pad(){events.push_back("clear-pad");}int melee_web_menu_host_source_scene(int*){return scene;}int melee_web_menu_host_phase(int*){return phase;}uint64_t melee_web_gameplay_generation(){return 7;}
int melee_web_menu_host_leave(int*,int,char*,size_t){assert(preparation.waiting_for_audio()&&!running&&!pending);events.push_back("leave");current_sss_exit.notified=current_sss_exit.notification_received=true;phase=5;scene=0;return 1;}
void render_audio_tick(int*,char*,size_t){events.push_back("audio");}
'''
        main=r'''
void reset(){current_sss_exit=State{};auto& s=current_sss_exit;s.recipe=replay.get();s.context=replay->initial_css.get();s.host_owner=host;preparation.reset();host_entered=pending=running=true;phase=3;scene=2;faulted=false;asset_destination=AssetDestination::None;replay_cursor=270;events.clear();replay_source_frames=melee_web::SourceFrameSequence{};}
int main(){
assert(current_sss_exit_profile_valid(*replay,CurrentSssExitProfile::Stock4None));
for(int f=0;f<7;f++){auto saved=replay->selection;if(f==0)replay->selection.start.rules.xB=2;if(f==1)replay->selection.start.rules.timer_enabled=true;if(f==2)replay->selection.start.players[0].stocks=3;if(f==3)replay->selection.start.rules.is_teams=true;if(f==4)replay->selection.start.players[0].color=0;if(f==5)replay->selection.unlocked_stages=0;if(f==6)replay->frames.pop_back();assert(!current_sss_exit_profile_valid(*replay,CurrentSssExitProfile::Stock4None));replay->selection=saved;replay->frames.resize(2006);}
auto saved=replay->selection;auto& r=replay->selection.start.rules;r.match_kind=0;r.is_stock=false;r.time_limit=120;r.xB=2;for(auto& p:replay->selection.start.players)p.stocks=3;assert(current_sss_exit_profile_valid(*replay,CurrentSssExitProfile::Time120));assert(!current_sss_exit_profile_valid(*replay,CurrentSssExitProfile::Stock4None));replay->selection=saved;
reset();current_sss_exit_before_leave();begin_preparation();current_sss_exit_after_preparation();assert(current_sss_exit.terminal_pending&&!current_sss_exit.terminal&&preparation.waiting_for_audio());
const bool audio_before_construction=false;struct{bool stalled=false;unsigned steps=2;}audio_elapsed;int* audio_owner=nullptr;char error[256]{};
'''
        tail=r'''
assert((events==std::vector<std::string>{"profile","clear-pad","clock-reset","leave","clock-reset","preparation-notify","audio","audio"}));
replay_source_frames.finish([]{return true;});current_sss_exit_finish_stock4(replay_source_frames.steps(),replay_source_frames.draws(),replay_source_frames.pending());assert(current_sss_exit.terminal&&preparation.waiting_for_audio()&&!host_entered&&!running&&!pending);
for(int f=0;f<6;f++){reset();current_sss_exit_before_leave();begin_preparation();current_sss_exit_after_preparation();if(f==0)++current_sss_asset_request_calls;if(f==1)host_entered=true;if(f==2)preparation.begin_construction(true);if(f==3)++replay_cursor;if(f==4)current_sss_exit.failed=true;if(f==5)asset_destination=AssetDestination::Match;bool refused=false;try{current_sss_exit_finish_stock4(0,0,false);}catch(const std::runtime_error&){refused=true;}assert(refused&&!current_sss_exit.terminal);}
reset();replay_source_frames.did_step(false);replay_source_frames.begin_callback();bool refused=false;try{current_sss_exit_before_leave();}catch(const std::runtime_error&){refused=true;}assert(refused&&host_entered&&!preparation.busy());
reset();scene=1;current_sss_exit_before_leave();assert(!current_sss_exit.leave_started);
}
'''
        run(includes+fixture+helpers+preparation+main+audio+tail,self.new_workspace(BASE,'stock4-sss-stop-'))
        # Static placement complements the actual helper control. It is not a
        # claim that endpoint doubles execute the complete browser callback.
        tick=cut(text,'void tick(){','}\n}\nusing namespace melee_web_menu_browser;')
        self.assertLess(tick.index('menuServiceCommands'),tick.index('service_render_cache_writes();'))
        self.assertLess(tick.index('service_render_cache_writes();'),tick.index('&&current_sss_exit.terminal'))
        self.assertLess(tick.index('&&current_sss_exit.terminal'),tick.index('source_frames.begin_callback();'))
        self.assertLess(tick.index('render_audio_tick(audio_owner'),tick.index('current_sss_exit_finish_stock4('))
        self.assertLess(tick.index('source_frames.finish(present_source);'),tick.index('current_sss_exit_finish_stock4('))
        self.assertEqual(cut(text,'void begin_preparation(){','bool preparation_uses_source_draws(){'),
                         cut((BASE/'src/gameplay_menu_browser.cpp').read_text(),'void begin_preparation(){','bool preparation_uses_source_draws(){'))

    def test_stock4_host_mechanisms(self):
        node=shutil.which('node')
        if not node:self.skipTest('Node required')
        result=subprocess.run([node,'--test',str(ROOT/'tests/stadium_stock4_sss_exit_observation.test.mjs')],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        print(result.stdout,end='');self.assertEqual(result.returncode,0,result.stdout)
