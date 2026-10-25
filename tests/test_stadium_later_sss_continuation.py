"""Actual extracted selector mechanics, not an executed game lifecycle.
No source game/graphics/audio/Results service is replaced with a success stub.
"""
from pathlib import Path
import subprocess
from test_stadium_current_sss_exit import run
from owned_test_workspace import OwnedWorkspaceTests
ROOT = Path(__file__).resolve().parents[1]


def cut(path, start, end):
    text = (ROOT / path).read_text()
    assert text.count(start) == text.count(end) == 1
    return text[text.index(start):text.index(end)]


def function(text,signature):
    assert text.count(signature)==1,signature
    start=text.index(signature);opening=text.index('{',start);depth=0
    for i in range(opening,len(text)):
        if text[i]=='{':depth+=1
        elif text[i]=='}':
            depth-=1
            if depth==0:return text[start:i+1]
    raise ValueError(signature)



# Retained original return_css3362: translated only by actual source field decoders.
RETURNED_CSS_RAW = 'const unsigned char raw_css[]={'+','.join(map(str,bytes([
    0,0,0,1,128,77,103,48,255,255,255,0,0,0,0,0,
    48,0,6,76,131,0,0,0,0,0,0,255,255,110,0,3,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    255,255,255,255,255,255,255,255,0,0,0,0,63,128,0,0,
    63,128,0,0,63,128,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    8,0,4,1,0,255,0,0,9,0,120,0,64,0,4,1,
    0,0,0,0,0,0,0,0,63,128,0,0,63,128,0,0,
    63,128,0,0,8,0,4,0,0,255,0,0,9,0,120,0,
    64,0,4,1,0,0,0,0,0,0,0,0,63,128,0,0,
    63,128,0,0,63,128,0,0,26,3,4,0,0,255,0,0,
    9,0,120,0,64,0,4,1,0,0,0,0,0,0,0,0,
    63,128,0,0,63,128,0,0,63,128,0,0,26,3,4,0,
    0,255,0,0,9,0,120,0,64,0,4,1,0,0,0,0,
    0,0,0,0,63,128,0,0,63,128,0,0,63,128,0,0,
    33,3,4,0,0,255,0,0,9,0,120,0,64,0,4,0,
    0,0,0,0,0,0,0,0,63,128,0,0,63,128,0,0,
    63,128,0,0,33,3,4,0,0,255,0,0,9,0,120,0,
    64,0,4,0,0,0,0,0,0,0,0,0,63,128,0,0,
    63,128,0,0,63,128,0,0,
])))+'};\n'

def returned_css_control_source(root, menu_source=None):
    """Actual typed validator/owner fields; lifecycle callbacks are modeled leaves."""
    root = Path(root)
    source = menu_source if menu_source is not None else (root / 'src/gameplay_menu.c').read_text()
    rules = (root / 'src/gameplay_match_rules.c').read_text()
    includes = r'''
#include "gameplay_menu.h"
#include "gameplay_content.h"
#include "gameplay_player_selection.h"
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <memory>
#include <iostream>
'''
    owner_type = function(source, 'struct MeleeWebMenuSession {') + ';\nstatic MeleeWebMenuSession* owner;\n'
    codecs = source[source.index('static u16 reference_be16('):source.index('/* The capture is taken at the CSS boundary')]
    declarations = '\n'.join(function(source, x) for x in [
        'static int fail(char* error, size_t error_size, const char* message)\n',
        'static int failf(', 'static int ok(char* error, size_t error_size)\n',
        'static int session_live(', 'static int check_runtime(',
        'static int run_scheduler(', 'static int observe_transition('])
    validators = source[source.index('int melee_web_menu_character_available('):source.index('MeleeWebMenuSession* melee_web_menu_session_create(')]
    css_exit = function(source, 'int melee_web_menu_leave_css(')
    sss_enter = function(source, 'int melee_web_menu_enter_sss(')
    tick = function(source, 'int melee_web_menu_tick(')
    sss_exit = function(source, 'int melee_web_menu_leave_sss(')
    stop = '    if (session->sss.start_game) {'
    assert sss_exit.count(stop) == 1
    # Deliberately exercise only the real epilogue before any next VS preparation.
    sss_exit = sss_exit[:sss_exit.index(stop)] + '    return ok(error, error_size);\n}'
    sss_exit = sss_exit.replace('int melee_web_menu_leave_sss(', 'int actual_sss_exit_prefix(', 1)
    timer = function(rules, 'int melee_web_match_timer_supported(')
    browser = (root / 'src/gameplay_menu_browser.cpp').read_text()
    context = function(browser, 'bool later_sss_context_owned(){')
    marker = '   check(later_sss.stage==3&&later_sss_context_owned()&&replay_cursor==675&&completed_matches==1&&'
    assert browser.count(marker) == 1
    i = browser.index(marker); j = browser.index('   later_sss.stage=4;', i) + len('   later_sss.stage=4;')
    handoff = 'void actual_handoff(){if(later_sss.armed){' + browser[i:j] + '}}'
    browser_control = r'''
namespace browser_control {
struct Recipe{std::unique_ptr<int> initial_css{new int(1)};};
std::unique_ptr<Recipe> replay;int native_host,foreign_host;int* host;
struct {bool armed=true,failed=false;const Recipe* recipe=nullptr;const int* context=nullptr;int* host_owner=nullptr;}current_sss_exit;
struct {bool armed=true;unsigned stage=3;}later_sss;
unsigned replay_cursor=675,completed_matches=1;int destination=0;
void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
int melee_web_menu_host_results_destination(int*){return destination;}
''' + context + handoff + r'''
void reset(){replay=std::make_unique<Recipe>();host=&native_host;
 current_sss_exit={true,false,replay.get(),replay->initial_css.get(),host};later_sss={true,3};
 replay_cursor=675;completed_matches=1;destination=0;}
void exercise(){
 reset();actual_handoff();assert(later_sss.stage==4);
 for(int f=0;f<8;++f){reset();
  if(f==0)replay_cursor=674;
  if(f==1)later_sss.stage=2;
  if(f==2)completed_matches=2;
  if(f==3)host=&foreign_host;
  if(f==4)current_sss_exit.failed=true;
  if(f==5)current_sss_exit.context=nullptr;
  if(f==6)current_sss_exit.recipe=nullptr;
  if(f==7)destination=192;
  bool refused=false;try{actual_handoff();}catch(const std::runtime_error&){refused=true;}assert(refused&&later_sss.stage==3+(f==1?-1:0));
 }
}
}
'''

    leaves = r'''
// MODELED LEAVES: no HSD/assets/menu/game callback is executed in this fixture.
std::vector<int> calls;bool poison_exit=false,leaf_check=true;
static int runtime_check(void*,MeleeWebMenuScene,char*,size_t){calls.push_back(5);return leaf_check;}
static int scheduler(void*,char*,size_t){calls.push_back(7);return 1;}
static int transition(void*,MeleeWebMenuScene,int* request,char*,size_t){calls.push_back(8);*request=0;return 1;}
static int scene_enter(void*,MeleeWebMenuScene,char*,size_t){calls.push_back(45);return 1;}
static int scene_exit(void*,MeleeWebMenuScene,char*,size_t){calls.push_back(20);return 1;}
static int melee_web_menu_gobj_snapshot(MeleeWebMenuSession*,char*,size_t){calls.push_back(40);return 1;}
static void melee_web_menu_gobj_snapshot_clear(MeleeWebMenuSession*){}
static int melee_web_menu_gobj_teardown(MeleeWebMenuSession*,char*,size_t){calls.push_back(25);return 1;}
extern "C" void mnCharSel_Scene_OnFrame(){calls.push_back(6);}
extern "C" void mnStageSel_Scene_OnFrame(){calls.push_back(16);}
extern "C" void mnCharSel_Scene_OnExit(void*){calls.push_back(10);if(poison_exit)owner->css.vs.start.rules.is_vs=1;}
extern "C" void mnStageSel_Scene_OnEnter(void*){calls.push_back(50);}
extern "C" void mnStageSel_Scene_OnExit(void*){calls.push_back(90);}
'''
    return includes + owner_type + codecs + declarations + timer + validators + leaves + css_exit + sss_enter + tick + sss_exit + RETURNED_CSS_RAW + browser_control + r'''
MeleeWebMenuSession owned{},foreign{};char error[256]{};
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static void passive(void*,MeleeWebMenuSession* s,const SSSData*,uint64_t,int){
 assert(s==owner&&s->current_sss_exit_state==2&&s->current_sss_exit_seen==2);calls.push_back(99);
}
#endif
void reset(){
 owned={};owner=&owned;calls.clear();poison_exit=false;leaf_check=true;
 owned.css.unk_0x0=reference_be16(raw_css);owned.css.match_type=raw_css[2];owned.css.pending_scene_change=raw_css[3];
 owned.css.vs.loser=(s8)raw_css[8];owned.css.vs.ordered_stage_index=(s8)raw_css[9];
 owned.css.vs.winner=(s8)raw_css[10];owned.css.vs.unk_0x3=raw_css[11];owned.css.vs.unk_0x4=raw_css[12];
 owned.css.vs.unk_0x5=raw_css[13];owned.css.vs.unk_0x6=raw_css[14];owned.css.vs.unk_0x7=raw_css[15];
 decode_reference_rules(&owned.css.vs.start.rules,raw_css+0x10);
 for(unsigned i=0;i<6;++i)decode_reference_player(&owned.css.vs.start.players[i],raw_css+0x70+0x24*i);
 owned.phase=MELEE_WEB_MENU_CSS;owned.css_open=1;
 owned.runtime={nullptr,runtime_check,scheduler,transition,scene_enter,scene_exit};
 owned.returned_cache_rules=owned.css.vs.start.rules;owned.returned_cache_valid=1;
 owned.post_vs_mode=owned.css.vs;owned.post_vs_mode_valid=1;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 owned.stadium_c1a_enabled=1;owned.current_sss_exit_state=1;
 owned.current_sss_exit_occurrence=owned.current_sss_exit_seen=1;owned.current_sss_exit_note=passive;
#endif
}
int main(){
 browser_control::exercise();
 static_assert(sizeof(CSSData)==0x148&&sizeof(StartMeleeRules)==0x60&&sizeof(PlayerInitData)==0x24);
 reset();const CSSData original=owned.css;const StartMeleeRules retained=owned.returned_cache_rules;
 assert(!melee_web_menu_stage_available(St_Kind_PStadium));
 assert(!melee_web_menu_css_cache_selection_valid(&owned.css,&retained));
 CSSData isolated=owned.css;isolated.vs.start.rules.stkind=MELEE_WEB_MENU_FD_ST_KIND;
 assert(melee_web_menu_css_cache_selection_valid(&isolated,&retained));
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 assert(css_progress_valid(&owned.css)&&melee_web_menu_css_selection_valid(&owned.css));
 assert(melee_web_menu_tick(&owned,error,sizeof(error))==MELEE_WEB_MENU_RESULT_TICKED);
 assert((calls==std::vector<int>{5,6,7,8}));
 assert(std::memcmp(&owned.css,&original,sizeof(original))==0);
 calls.clear();owned.transition_requested=1;
 assert(melee_web_menu_leave_css(&owned,error,sizeof(error)));
 assert((calls==std::vector<int>{10,20,25}));assert(owned.phase==MELEE_WEB_MENU_SSS_READY&&!owned.css_open);
 assert(std::memcmp(&owned.css,&original,sizeof(original))==0&&std::memcmp(&owned.returned_cache_rules,&retained,sizeof(retained))==0);
 calls.clear();assert(melee_web_menu_enter_sss(&owned,error,sizeof(error)));
 assert((calls==std::vector<int>{5,8,40,45,50}));
 assert(owned.phase==MELEE_WEB_MENU_SSS&&owned.sss_open&&sss_selection_valid_for_session(&owned));
 assert(!melee_web_menu_sss_selection_valid(&owned.sss)); // Ordinary Stage3 still closed.
 assert(std::memcmp(&owned.sss.vs,&original.vs,sizeof(original.vs))==0);
 owned.sss.start_game=1;owned.transition_requested=1;calls.clear();
 assert(actual_sss_exit_prefix(&owned,error,sizeof(error)));
 assert((calls==std::vector<int>{90,99,20,25}));
 assert(owned.current_sss_exit_state==2&&owned.current_sss_exit_seen==2&&!owned.sss_open);
 assert(sss_selection_valid_for_session(&owned)); // Consumed closed epilogue only.
 for(auto phase:{MELEE_WEB_MENU_CSS,MELEE_WEB_MENU_SSS_READY,MELEE_WEB_MENU_READY}){
  owned.phase=phase;assert(!later_sss_returned_cache_rules(&owned,1));assert(!melee_web_menu_css_selection_valid(&owned.css));}
 owned.phase=MELEE_WEB_MENU_SSS;owned.sss_open=1;assert(!sss_selection_valid_for_session(&owned));
 owned.sss_open=0;owned.phase=MELEE_WEB_MENU_READY;owned.stadium_c1a_ready=1;owned.match_vs=owned.sss.vs;
 assert(!melee_web_menu_stadium_c1a_ready_selection_valid(&owned)); // Existing retained=NULL readiness remains closed.
 for(int fault=0;fault<15;++fault){reset();
  if(fault==0)owned.current_sss_exit_state=0;
  if(fault==1)owned.current_sss_exit_occurrence=0;
  if(fault==2)owned.current_sss_exit_seen=0;
  if(fault==3)owned.current_sss_exit_seen=2;
  if(fault==4)owned.returned_cache_valid=0;
  if(fault==5)owned.post_vs_mode_valid=0;
  if(fault==6)owned.stadium_c1a_enabled=0;
  if(fault==7)owned.selection_rejected=1;
  if(fault==8)owned.transition_failed=1;
  if(fault==9)owned.current_sss_exit_note=nullptr;
  if(fault==10)owned.training_mode_scene=1;
  if(fault==11)owned.post_vs_mode.start.rules.time_limit=1;
  if(fault==12)owned.returned_cache_rules.time_limit=1;
  if(fault==13)owned.phase=MELEE_WEB_MENU_CLOSED;
  if(fault==14)owner=&foreign;
  assert(!css_progress_valid(&owned.css)&&!melee_web_menu_css_selection_valid(&owned.css));
 }
 reset();owned.phase=MELEE_WEB_MENU_SSS;owned.sss_open=1;owned.sss.vs=owned.css.vs;owned.sss.force_stage_id=-1;
 foreign=owned;assert(!sss_selection_valid_for_session(&foreign));
 reset();CSSData copy=owned.css;assert(!css_progress_valid(&copy)&&!melee_web_menu_css_selection_valid(&copy));
 for(int fault=0;fault<8;++fault){reset();
  if(fault==0)owned.css.vs.start.rules.is_vs=1;
  if(fault==1)owned.css.vs.start.rules.is_stock=1;
  if(fault==2)owned.css.vs.start.rules.xB=2;
  if(fault==3)owned.css.vs.start.players[0].stocks=0;
  if(fault==4)owned.css.vs.start.players[1].slot=1;
  if(fault==5)owned.css.vs.start.rules.is_teams=2;
  if(fault==6)owned.css.vs.start.rules.time_limit=1;
  if(fault==7)owned.css.vs.start.rules.stkind=0x7fff;
  assert(!melee_web_menu_css_selection_valid(&owned.css));
 }
 reset();owned.transition_requested=1;poison_exit=true;
 assert(!melee_web_menu_leave_css(&owned,error,sizeof(error))); // Actual post-OnExit validator refuses mutation.
 assert((calls==std::vector<int>{10,20,25}));
 reset();owned.phase=MELEE_WEB_MENU_SSS_READY;owned.css_open=0;leaf_check=false;
 assert(!melee_web_menu_enter_sss(&owned,error,sizeof(error)));assert((calls==std::vector<int>{5}));
 // Existing fresh-cache/private-first-A behavior is retained, separate from occurrence1 cache.
 reset();owned.returned_cache_valid=owned.post_vs_mode_valid=0;owned.current_sss_exit_state=0;
 owned.sss.vs=owned.css.vs;auto& fresh_start=owned.sss.vs.start;fresh_start.rules.match_kind=MatchKind_Time;fresh_start.rules.xB=2;
 for(auto& p:fresh_start.players)p.stocks=0;owned.sss.force_stage_id=-1;
 assert(stadium_c1a_sss_selection_valid(&owned.sss,nullptr));
#else
 assert(!css_progress_valid(&owned.css)&&!melee_web_menu_css_selection_valid(&owned.css));
#endif
 // Ordinary FD retained-cache route remains valid; no private capability required.
 reset();owned.css.vs.start.rules.stkind=MELEE_WEB_MENU_FD_ST_KIND;
 owned.returned_cache_rules=owned.css.vs.start.rules;owned.post_vs_mode=owned.css.vs;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 owned.current_sss_exit_state=0;owned.stadium_c1a_enabled=0;
#endif
 assert(css_progress_valid(&owned.css)&&melee_web_menu_css_selection_valid(&owned.css));
 CSSData fresh=owned.css;fresh.vs.start.rules.match_kind=MatchKind_Time;fresh.vs.start.rules.xB=2;
 for(auto& p:fresh.vs.start.players)p.stocks=0;
 assert(melee_web_menu_css_cache_selection_valid(&fresh,nullptr));
 std::cout<<"PASS actual typed CSS/SSS cache admission COMPONENT_ONLY; private owner/callback seams and public/fresh refusals; no game or browser execution\n";
}
'''


class LaterSssContinuationControls(OwnedWorkspaceTests):
    def test_actual_browser_finite_topology_and_occurrence_owner_selectors(self):
        context = cut('src/gameplay_menu_browser.cpp', 'bool later_sss_context_owned(){',
                      'bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe&);')
        actual = cut('src/gameplay_menu_browser.cpp', 'bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe& recipe){',
                     'bool later_sss_pause_pads(')
        actual += cut('src/gameplay_menu_browser.cpp', 'bool later_sss_selected_leave(){',
                      'bool current_sss_exit_profile_valid(')
        fixture = r'''
#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <cstddef>
namespace melee_web {
struct Span {unsigned scene,first_frame,last_frame;};
struct RetailReplayRecipe {unsigned version=8;bool diagnostic_entity_prefix=false;
 std::vector<int> frames=std::vector<int>(2006);std::vector<Span> spans;
 std::unique_ptr<int> initial_css{new int(1)};bool whole_session()const{return version==8;}};
}
struct {bool armed=false;unsigned stage=0,requested_generation=7;}later_sss;
std::unique_ptr<melee_web::RetailReplayRecipe> replay(new melee_web::RetailReplayRecipe);
int host_object,foreign_host;int* host=&host_object;
struct {bool armed=true,failed=false,notified=false;const melee_web::RetailReplayRecipe* recipe;
 const int* host_owner;const int* context;}current_sss_exit;
unsigned replay_cursor=270,completed_matches=0,asset_generation=7;
int source_scene=2;bool results_route_active=false,match=false,results=false,prize=false;
enum class AssetDestination{None,StadiumC1a};AssetDestination asset_destination=AssetDestination::StadiumC1a;
enum{MELEE_WEB_MENU_HOST_SCENE_SSS=2};
int melee_web_menu_host_source_scene(const int*){return source_scene;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void refuses(F f){bool caught=false;try{f();}catch(const std::runtime_error&){caught=true;}assert(caught);}
'''
        main = r'''
int main(){
 const unsigned scene[]={1,2,3,4,1,2,3,4,1,2,3,4};
 const unsigned first[]={0,145,270,462,675,807,940,1132,1343,1476,1601,1794};
 const unsigned last[]={144,269,461,674,806,939,1131,1342,1475,1600,1793,2005};
 for(unsigned i=0;i<12;++i)replay->spans.push_back({scene[i],first[i],last[i]});
 assert(later_sss_recipe_valid(*replay));
 for(auto& span:replay->spans){++span.first_frame;assert(!later_sss_recipe_valid(*replay));--span.first_frame;
  ++span.last_frame;assert(!later_sss_recipe_valid(*replay));--span.last_frame;
  ++span.scene;assert(!later_sss_recipe_valid(*replay));--span.scene;}
 replay->frames.pop_back();assert(!later_sss_recipe_valid(*replay));replay->frames.push_back(0);
 replay->version=9;assert(!later_sss_recipe_valid(*replay));replay->version=8;
 current_sss_exit.recipe=replay.get();current_sss_exit.context=replay->initial_css.get();current_sss_exit.host_owner=host;
 assert(later_sss_selected_leave()); // Unarmed legacy dispatch stays unchanged.
 later_sss.armed=true;assert(!later_sss_selected_leave()); // A proceeds at global270.
 replay_cursor=940;refuses([]{later_sss_selected_leave();});
 later_sss.stage=4;completed_matches=1;assert(later_sss_selected_leave());
 ++replay_cursor;refuses([]{later_sss_selected_leave();});--replay_cursor;
 host=&foreign_host;refuses([]{later_sss_selected_leave();});host=&host_object;
 ++completed_matches;refuses([]{later_sss_selected_leave();});--completed_matches;
 int foreign_context;current_sss_exit.context=&foreign_context;refuses([]{later_sss_selected_leave();});
 current_sss_exit.context=replay->initial_css.get();
 later_sss.stage=1;replay_cursor=270;assert(later_sss_asset_pending(7));
 assert(!later_sss_asset_pending(6));
 auto captured=std::move(replay->initial_css);replay->initial_css=std::make_unique<int>(9);
 assert(!later_sss_asset_pending(7));refuses([]{later_sss_selected_leave();});
 replay->initial_css=std::move(captured);
 asset_generation=8;assert(!later_sss_asset_pending(7));
 asset_generation=7;current_sss_exit.failed=true;assert(!later_sss_asset_pending(7));
}
'''
        run(fixture + context + actual + main, self.new_workspace(ROOT, 'later-sss-selectors-'))

    def test_actual_private_pause_window_and_ordinary_refusal(self):
        browser=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        header=(ROOT/'src/gameplay_match_flow.h').read_text()
        flow=(ROOT/'src/gameplay_match_flow.c').read_text()
        struct=header[header.index('typedef struct MeleeWebMatchPauseObservation {'):header.index('} MeleeWebMatchPauseObservation;')+len('} MeleeWebMatchPauseObservation;')]
        helpers='\n'.join(function(browser,s) for s in ['bool later_sss_pause_pads(','bool later_sss_pause_before(','void later_sss_pause_after('])
        branch=function(browser,'    else if(replay_whole){').replace('else if(replay_whole)','if(replay_whole)',1)
        getter=function(flow,'int melee_web_match_flow_pause_observation(')
        result_anchor='  check(later_sss.stage==2&&later_sss_context_owned()&&later_sss.pause_finished&&replay_cursor==462&&completed_matches==1,'
        assert browser.count(result_anchor)==1
        i=browser.index(result_anchor);j=browser.index('  later_sss.stage=3;',i)+len('  later_sss.stage=3;')
        result_gate='void actual_results_gate(){if(later_sss.armed){'+browser[i:j]+'}}'

        prefix=r'''
    #include <array>
    #include <cstdint>
    #include <cstddef>
    #include <memory>
    #include <stdexcept>
    #include <cassert>
    #include <iostream>
    #define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
    constexpr int OUTCOME_NONE=0,OUTCOME_NO_CONTEST=7;
    void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
    struct PADStatus {unsigned button=0;int stickX=0,stickY=0,substickX=0,substickY=0;unsigned triggerLeft=0,triggerRight=0,analogA=0,analogB=0;int err=0;};
    '''+struct+r'''
    // Explicit modeled leaf state, not a substituted source MatchClock layout.
    struct lbl_8046B6A0_t {unsigned pause_timer=0;int match_result=0;};
    struct MeleeWebMatchFlow {uint64_t generation=1;int complete=0;};
    MeleeWebMatchFlow owned_flow;const MeleeWebMatchFlow* flow_owner=&owned_flow;
    uint64_t live_generation=1;uint32_t source_frame=56;int source_paused=1;
    lbl_8046B6A0_t clock_fields;bool clock_present=true;
    int live(const MeleeWebMatchFlow* flow){return flow&&flow==flow_owner&&flow->generation==live_generation;}
    lbl_8046B6A0_t* gm_16AE_GetUnkData_0(){return clock_present?&clock_fields:nullptr;}
    uint32_t melee_web_match_source_frames(){return source_frame;}
    int melee_web_match_flow_paused(const MeleeWebMatchFlow*){return source_paused;}
    '''+getter+r'''
    struct Frame {std::array<PADStatus,4> pads;};struct Recipe {std::array<Frame,2006> frames;};
    struct ModelMatch {MeleeWebMatchPauseObservation observed{};bool getter_ok=true;
     bool diagnostic_pause_observation(MeleeWebMatchPauseObservation& out){if(!getter_ok)return false;out=observed;return true;}
     bool paused(){return observed.paused;}};
    std::unique_ptr<ModelMatch> match;std::unique_ptr<Recipe> replay;
    struct {bool armed=true;unsigned stage=2;const ModelMatch* pause_match_owner=nullptr;uint64_t pause_generation=0;size_t pause_next_cursor=450;bool pause_finished=false;} later_sss;
    bool context_ok=true,topology_ok=true,replay_whole=true;size_t replay_cursor=450;unsigned completed_matches=0;
    bool later_sss_context_owned(){return context_ok&&bool(replay);}
    bool later_sss_recipe_valid(const Recipe&){return topology_ok;}
    '''+helpers+'\n'+result_gate+'\nvoid actual_browser_pre_tick(const PADStatus* sample){'+branch+'}\n'+r'''
    PADStatus* sample(){return replay->frames[replay_cursor].pads.data();}
    void reset(){match=std::make_unique<ModelMatch>();replay=std::make_unique<Recipe>();later_sss={};later_sss.armed=true;later_sss.stage=2;later_sss.pause_next_cursor=450;later_sss.pause_match_owner=match.get();context_ok=topology_ok=replay_whole=true;completed_matches=0;replay_cursor=450;
     for(size_t c=450;c<=461;++c){auto& p=replay->frames[c].pads;p[2].err=p[3].err=-1;if(c==450||c==461){p[0].button=0x1160;p[0].triggerLeft=p[0].triggerRight=255;}}
     match->observed={9,56,0,0,0,0};}
    void ready451(){reset();replay_cursor=451;later_sss.pause_next_cursor=451;later_sss.pause_generation=9;match->observed={9,56,10,1,0,0};}
    template<class F>void refuses(F f){bool no=false;try{f();}catch(const std::runtime_error&){no=true;}assert(no);}
    int main(){
     reset();for(size_t c=450;c<=461;++c){replay_cursor=c;actual_browser_pre_tick(sample());
      match->observed={9,56,unsigned(c==461?0:460-c),1,c==461?7:0,c==461?1:0};later_sss_pause_after();}
     assert(later_sss.pause_finished&&later_sss.pause_next_cursor==462);
     replay_cursor=462;completed_matches=1;actual_results_gate();assert(later_sss.stage==3);
     later_sss.stage=2;later_sss.pause_finished=false;refuses([]{actual_results_gate();});
     reset();sample()[0].triggerRight=0;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.pause_timer=9;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.frame=57;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.result=7;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.complete=1;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.paused=0;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.generation=10;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->getter_ok=false;refuses([]{actual_browser_pre_tick(sample());});
     ready451();later_sss.pause_match_owner=nullptr;refuses([]{actual_browser_pre_tick(sample());});
     ready451();later_sss.stage=3;refuses([]{actual_browser_pre_tick(sample());});
     ready451();context_ok=false;refuses([]{actual_browser_pre_tick(sample());});
     ready451();topology_ok=false;refuses([]{actual_browser_pre_tick(sample());});
     ready451();completed_matches=1;refuses([]{actual_browser_pre_tick(sample());});
     ready451();later_sss.pause_next_cursor=450;refuses([]{actual_browser_pre_tick(sample());});
     ready451();later_sss.pause_finished=true;refuses([]{actual_browser_pre_tick(sample());});
     ready451();PADStatus foreign[4]{};foreign[2].err=foreign[3].err=-1;foreign[1].button=1;refuses([&]{actual_browser_pre_tick(foreign);});
     ready451();sample()[3].err=0;refuses([]{actual_browser_pre_tick(sample());});
     ready451();match->observed.pause_timer=9;later_sss_pause_after();refuses([]{later_sss_pause_after();});
     ready451();match->observed.pause_timer=8;refuses([]{later_sss_pause_after();});
     ready451();match->observed.generation=10;refuses([]{later_sss_pause_after();});
     ready451();replay_cursor=461;later_sss.pause_next_cursor=461;match->observed={9,56,0,1,0,0};refuses([]{later_sss_pause_after();});
     ready451();later_sss.armed=false;refuses([]{actual_browser_pre_tick(sample());});
     ready451();replay_cursor=449;refuses([]{actual_browser_pre_tick(sample());});
     ready451();replay_cursor=462;refuses([]{actual_browser_pre_tick(sample());});
     ready451();later_sss.armed=false;match->observed.paused=0;actual_browser_pre_tick(sample());
     // Actual copied-field getter: false leaves sentinel intact; valid false relations copied faithfully.
     MeleeWebMatchPauseObservation o{99,99,99,99,99,99};MeleeWebMatchFlow foreign_flow;
     assert(!melee_web_match_flow_pause_observation(&foreign_flow,&o)&&o.generation==99);
     live_generation=2;assert(!melee_web_match_flow_pause_observation(&owned_flow,&o)&&o.generation==99);live_generation=1;
     clock_present=false;assert(!melee_web_match_flow_pause_observation(&owned_flow,&o)&&o.generation==99);clock_present=true;
     assert(!melee_web_match_flow_pause_observation(&owned_flow,nullptr));
     source_paused=0;source_frame=57;clock_fields.pause_timer=9;clock_fields.match_result=7;owned_flow.complete=0;
     assert(melee_web_match_flow_pause_observation(&owned_flow,&o));assert(o.generation==1&&o.frame==57&&o.pause_timer==9&&!o.paused&&o.result==7&&!o.complete);
     std::cout<<"PASS extracted private pause guard, ordinary pause refusal, copied-field getter; MODELED_LEAF_COMPONENT_ONLY\n";
    }
    '''
        run(prefix, self.new_workspace(ROOT, 'later-sss-pause-'))

    def test_actual_returned_stadium_css_cache_admission_and_public_refusals(self):
        from check_gameplay import node_runtime
        workspace = self.new_workspace(ROOT, 'later-sss-returned-cache-')
        cpp = workspace / 'actual-returned-cache.cpp'
        cpp.write_text(returned_css_control_source(ROOT))
        for mode, defines in [('private', ['-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC']), ('ordinary', [])]:
            js = workspace / (mode + '.js')
            compiler = ROOT / '.deps/emsdk/upstream/emscripten/em++'
            commands = [
                [str(compiler), str(cpp), '-o', str(js), '-std=gnu++20', '-O0', '-fexceptions',
                 '-include', str(ROOT / 'src/gameplay_compat.h'), '-DAURORA', '-DAURORA_ENABLE_GX', '-DTARGET_PC',
                 '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-I'+str(ROOT/'src'),
                 '-I'+str(ROOT/'build/gameplay-source/src'), '-I'+str(ROOT/'.deps/aurora/include')] + defines,
                [str(node_runtime()), str(js)],
            ]
            for index, command in enumerate(commands):
                result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=60)
                (workspace / (mode + '-' + str(index) + '.log')).write_text(result.stdout)
                self.assertEqual(result.returncode, 0, result.stdout)
            self.assertIn('COMPONENT_ONLY', result.stdout)

    def test_actual_session_second_selector_and_consumed_callback(self):
        actual = cut('src/gameplay_menu.c', 'int melee_web_menu_current_sss_exit_select_second(',
                     'int melee_web_menu_arm_first_css_return(')
        text = (ROOT / 'src/gameplay_menu.c').read_text()
        first = text.index('    if (session->current_sss_exit_state==1 &&')
        callback = text[first:text.index('#endif', first)]
        fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
enum{MELEE_WEB_MENU_CREATED=0,MELEE_WEB_MENU_SSS_PREFIX_EXIT=3};
typedef struct MeleeWebMenuSession MeleeWebMenuSession;
struct MeleeWebMenuSession {unsigned phase,css_open,sss_open,stadium_c1a_enabled;
 uint64_t ticks;int current_sss_exit_state;unsigned current_sss_exit_occurrence,current_sss_exit_seen;
 void(*current_sss_exit_note)(void*,MeleeWebMenuSession*,const int*,uint64_t,int);
 struct{void* user;}runtime;int sss;};
MeleeWebMenuSession* owner;unsigned notes;
int session_live(MeleeWebMenuSession* s,char* e,size_t n){(void)e;(void)n;return s==owner;}
int fail(char* e,size_t n,const char* m){(void)e;(void)n;(void)m;return 0;}
int ok(char* e,size_t n){(void)e;(void)n;return 1;}
void note(void* d,MeleeWebMenuSession* s,const int* p,uint64_t t,int b){
 (void)d;(void)t;assert(p==&s->sss&&b==3&&s->current_sss_exit_state==2);++notes;}
'''
        main = r'''
void actual_exit(MeleeWebMenuSession* session){CALLBACK}
int main(void){char e[160];MeleeWebMenuSession s={0};owner=&s;s.stadium_c1a_enabled=1;
 assert(!melee_web_menu_current_sss_exit_select_second(&s,e,sizeof(e)));
 s.current_sss_exit_state=1;s.current_sss_exit_note=note;s.ticks=1;
 assert(!melee_web_menu_current_sss_exit_select_second(&s,e,sizeof(e)));s.ticks=0;
 assert(melee_web_menu_current_sss_exit_select_second(&s,e,sizeof(e)));
 assert(!melee_web_menu_current_sss_exit_select_second(&s,e,sizeof(e)));
 actual_exit(&s);assert(notes==0&&s.current_sss_exit_state==1&&s.current_sss_exit_seen==1);
 actual_exit(&s);assert(notes==1&&s.current_sss_exit_state==2&&s.current_sss_exit_seen==2);
 actual_exit(&s);assert(notes==1);return 0;}
'''.replace('CALLBACK', callback)
        run(fixture + actual + main, self.new_workspace(ROOT, 'later-sss-session-'), language='c')

    def test_actual_callback_counter_reset_preserves_pending_source_draw(self):
        header = '#include "' + str(ROOT / 'src/source_frame_sequence.hpp') + '"\n'
        fixture = r'''#include <cassert>
int main(){melee_web::SourceFrameSequence frames;
 frames.did_step();assert(frames.steps()==1&&frames.pending());
 frames.begin_callback();assert(frames.steps()==0&&frames.draws()==0&&frames.pending());
 frames.finish([]{return true;});assert(frames.draws()==1&&!frames.pending());
 frames.begin_callback();assert(frames.steps()==0&&frames.draws()==0&&!frames.pending());}
'''
        run(header + fixture, self.new_workspace(ROOT, 'later-sss-callback-counters-'))

    def test_host_validators_and_real_page_arm_function(self):
        subprocess.run(['node', '--test', str(ROOT / 'tests/stadium_later_sss_exit_observation.test.mjs')],
                       cwd=ROOT, check=True, timeout=45)
