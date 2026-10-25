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


class LaterSssContinuationControls(OwnedWorkspaceTests):
    def test_actual_browser_finite_topology_and_occurrence_owner_selectors(self):
        context = cut('src/gameplay_menu_browser.cpp', 'bool later_sss_context_owned(){',
                      'bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe&);')
        actual = cut('src/gameplay_menu_browser.cpp', 'bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe& recipe){',
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
