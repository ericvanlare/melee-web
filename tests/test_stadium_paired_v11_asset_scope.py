"""Actual private importer gate with modeled services; no asset/game parity claim."""
from pathlib import Path
import os
import shlex
import subprocess
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT = Path(__file__).resolve().parents[1]
PRELUDE = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define MELEE_WEB_PAD_STATE_BYTES 822
struct MeleeWebPadState {std::array<uint8_t,822> bytes{};};
struct Start {unsigned value=7;};
struct MeleeWebMenuMatchSelection {Start start;unsigned seed=17;};
struct Recipe {unsigned version=11;std::unique_ptr<int> initial_css{new int(1)};bool whole_session()const{return version==11;}};
namespace melee_web {constexpr unsigned kRetailReplayDrawClosureVersion=11;
static unsigned setup_checks=0,next_index=0;
// Modeled strict setup service: exact declared payload, not unconditional success.
static unsigned retail_replay_next_match_index(const Recipe&,unsigned){return next_index;}
static void retail_replay_validate_match_setup(const Recipe&,unsigned i,const Start& s){++setup_checks;if(i!=0||s.value!=7)throw std::runtime_error("declared setup mismatch");}
static std::vector<std::string> stadium_c1a_asset_names(const MeleeWebMenuMatchSelection& s){if(s.start.value!=7)throw std::runtime_error("manifest profile mismatch");return {"stage","fighter"};}}
static std::unique_ptr<Recipe> replay;
static void* host=(void*)1;
static void *world=nullptr,*match=nullptr,*results=nullptr,*prize=nullptr;
static bool source_session_owned=true,faulted=false,host_entered=false;
static bool net_active=false;static bool melee_web_net_active(){return net_active;}
static struct {bool armed=false;} later_sss,current_sss_exit;
static bool stadium_c1a_armed=false;
static struct {bool opted_in=false,armed=false;} matched_setup_cf;
static unsigned replay_cursor=275,completed_matches=0;
static uint32_t seed=17,foreign_seed=17,*seed_ptr=&seed;
static MeleeWebPadState pad,foreign_pad;
static MeleeWebMenuMatchSelection live_selection;
static bool getter_ok=true;
static int melee_web_menu_host_stadium_c1a_selection(void*,MeleeWebMenuMatchSelection* out,char*,size_t){if(!getter_ok)return 0;*out=live_selection;return 1;}
static const MeleeWebPadState* pad_owner=&pad;
static const MeleeWebPadState* melee_web_menu_host_input(void*){return pad_owner;}
static int melee_web_pad_state_encode(const MeleeWebPadState* p,uint8_t* out,size_t n){if(!p||n!=822)return 0;memcpy(out,p->bytes.data(),n);return 1;}
static bool current_sss_selection_equal(const MeleeWebMenuMatchSelection& a,const MeleeWebMenuMatchSelection& b){return a.start.value==b.start.value&&a.seed==b.seed;}
static void check(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
struct State {bool route_prepared=false,native_retired=false,failed=false,import_pending=false,import_consumed=false,enabled=false;Recipe* recipe_owner=nullptr;const int* context_owner=nullptr;void* route_host_owner=nullptr;uint32_t* parent_seed_owner=nullptr;uint32_t parent_seed=0,requested_generation=0;MeleeWebMenuMatchSelection selected;const MeleeWebPadState* ready_input=nullptr;std::array<uint8_t,822> ready_input_bytes{};} stadium_paired_v11;
enum class AssetDestination{None,StadiumC1a};static AssetDestination asset_destination=AssetDestination::StadiumC1a;
static bool scoped_assets=true,scoped_disc_import=true,asset_committed=false,asset_selection_valid=true;
static unsigned asset_generation=7;
static struct {unsigned pending=7;unsigned pending_generation()const{return pending;}} asset_scope;
static MeleeWebMenuMatchSelection asset_selection;
static std::vector<std::string> requested_assets={"stage","fighter"};
// Unselected existing import routes are explicit modeled leaves.
static bool first_pending=false,later_pending=false;
static bool first_sss_vs_setup_asset_pending(unsigned){return first_pending;}
static bool later_sss_asset_pending(unsigned){return later_pending;}
'''
TEST = r'''
static void reset(){
 replay=std::make_unique<Recipe>();stadium_paired_v11={};auto& s=stadium_paired_v11;
 s.route_prepared=true;s.import_pending=true;s.recipe_owner=replay.get();s.context_owner=replay->initial_css.get();s.route_host_owner=host;s.parent_seed_owner=&seed;s.parent_seed=17;s.requested_generation=7;s.ready_input=&pad;s.ready_input_bytes=pad.bytes;
 seed=17;seed_ptr=&seed;asset_generation=7;asset_scope.pending=7;asset_destination=AssetDestination::StadiumC1a;scoped_assets=scoped_disc_import=asset_selection_valid=true;asset_committed=false;faulted=host_entered=net_active=false;source_session_owned=true;world=match=results=prize=nullptr;completed_matches=0;melee_web::next_index=0;live_selection={};asset_selection={};getter_ok=true;pad_owner=&pad;requested_assets={"stage","fighter"};later_sss.armed=current_sss_exit.armed=stadium_c1a_armed=false;matched_setup_cf={};first_pending=later_pending=false;
}
int main(){
 reset();assert(stadium_paired_v11_asset_pending(7));assert(melee_web_native_menu_stadium_c1a_asset_scope(7)==1);assert(melee_web::setup_checks>0);
 assert(!stadium_paired_v11_asset_pending(0));assert(!stadium_paired_v11_asset_pending(8));
 auto reject=[](auto mutate){reset();mutate();assert(!stadium_paired_v11_asset_pending(7));assert(melee_web_native_menu_stadium_c1a_asset_scope(7)==0);};
 reject([]{replay->version=9;});reject([]{stadium_paired_v11.recipe_owner=nullptr;});reject([]{stadium_paired_v11.context_owner=nullptr;});reject([]{stadium_paired_v11.route_host_owner=(void*)2;});
 reject([]{stadium_paired_v11.import_consumed=true;});reject([]{stadium_paired_v11.import_pending=false;});reject([]{stadium_paired_v11.enabled=true;});reject([]{stadium_paired_v11.failed=true;});reject([]{stadium_paired_v11.native_retired=true;});
 reject([]{asset_generation=8;});reject([]{asset_scope.pending=8;});reject([]{stadium_paired_v11.requested_generation=8;});reject([]{asset_destination=AssetDestination::None;});reject([]{asset_committed=true;});reject([]{asset_selection_valid=false;});reject([]{scoped_disc_import=false;});
 reject([]{seed_ptr=&foreign_seed;});reject([]{seed=18;});reject([]{host_entered=true;});reject([]{world=(void*)1;});reject([]{match=(void*)1;});reject([]{melee_web::next_index=1;});
 reject([]{getter_ok=false;});reject([]{live_selection.start.value=8;});reject([]{asset_selection.seed=18;});reject([]{pad_owner=&foreign_pad;});reject([]{stadium_paired_v11.ready_input_bytes[0]=1;});reject([]{requested_assets.pop_back();});
 reset();stadium_paired_v11.route_prepared=false;assert(melee_web_native_menu_stadium_c1a_asset_scope(7)==0);
 // Existing branches retain their own admission; paired state does not replace them.
 first_pending=true;assert(melee_web_native_menu_stadium_c1a_asset_scope(7)==1);first_pending=false;later_pending=true;assert(melee_web_native_menu_stadium_c1a_asset_scope(7)==1);
}
'''
class StadiumPairedV11AssetScopeTests(OwnedWorkspaceTests):
    def test_actual_native_private_asset_gate_and_refusals(self):
        source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        menu=(ROOT/'src/gameplay_menu_browser_menu.cpp').read_text()
        body=PRELUDE
        for signature in ('bool stadium_paired_v11_recipe(){',
                          'bool stadium_paired_v11_route_owned(){',
                          'void stadium_paired_v11_validate_import_selection(){',
                          'bool stadium_paired_v11_asset_pending(unsigned generation)'):
            self.assertEqual(source.count(signature),1,signature)
            body+=_function(source,signature)+'\n'
        body+=_function(menu,'int melee_web_native_menu_stadium_c1a_asset_scope(unsigned generation)')+TEST
        work=self.new_workspace(ROOT,'paired-v11-private-import-')
        cpp=work/'control.cpp';exe=work/'control';cpp.write_text(body)
        command=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
        result=subprocess.run(command,capture_output=True,text=True)
        (work/'compile.stdout').write_text(result.stdout);(work/'compile.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        (work/'control.stdout').write_text(result.stdout);(work/'control.stderr').write_text(result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
