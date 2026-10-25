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
