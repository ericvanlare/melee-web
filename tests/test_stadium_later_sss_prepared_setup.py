"""Extracted actual native handoff with explicit owner/service leaves; no game/ABI parity."""
from pathlib import Path
import os
from owned_test_workspace import OwnedWorkspaceTests
from test_stadium_current_sss_exit import run
ROOT=Path(os.environ.get('MELEE_LATER_SETUP_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
BASE=Path(os.environ.get('MELEE_LATER_SETUP_BASE_ROOT',ROOT))

def cut(s,a,b):
    assert s.count(a)==s.count(b)==1,(a,b)
    return s[s.index(a):s.index(b)]

class LaterSssPreparedSetupControls(OwnedWorkspaceTests):
    def test_actual_handoff_approval_retirement_and_import(self):
        text=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        declarations=(cut(text,'enum class CurrentSssExitProfile : unsigned', '// Private occurrence1 capability,')+
                      cut(text,'struct LaterSssContinuation {','bool later_sss_recipe_valid(const melee_web::RetailReplayRecipe&);')+
                      cut(text,'enum class FirstSssVsSetupOrigin : unsigned', 'enum class StadiumCfObservationScope'))
        difference=cut(text,'std::string current_sss_selection_difference(', 'void current_sss_exit_before_leave(')
        begin=cut(text,'int current_sss_prepared_setup_begin(){','\n#endif\n\nvoid tick(){')
        finish=cut(text,'bool first_sss_vs_setup_finish_handoff(){','\n#endif\nbool finish_asset_handoff(){')
        fixture='#include "'+str(ROOT/'src/private_sha256.hpp')+'"\n#include "'+str(ROOT/'src/stadium_later_reference.hpp')+'"\n'+r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
// Modeled payload/service leaves. Actual state declarations and handoff bodies follow.
constexpr size_t MELEE_WEB_PAD_STATE_BYTES=4;
struct MeleeWebPadState{std::array<uint8_t,4> bytes{1,2,3,4};};
void melee_web_pad_state_free(MeleeWebPadState* p){delete p;}
int melee_web_pad_state_encode(const MeleeWebPadState* p,uint8_t* b,size_t n){if(!p||n!=4)return 0;std::memcpy(b,p->bytes.data(),4);return 1;}
MeleeWebPadState* melee_web_pad_state_decode(const uint8_t* b,size_t n,char*,size_t){if(n!=4)return nullptr;auto* p=new MeleeWebPadState;std::memcpy(p->bytes.data(),b,4);return p;}
struct StartMeleeData{int tag=3;};
struct MeleeWebMenuMatchSelection{StartMeleeData start;struct{uint32_t controller=0,stocks=4,costume=0,sub_color=0;}players[4];uint32_t player_count=2,random_seed=0x7fc17cf1,hud_layout=0;uint16_t unlocked_characters=2047,unlocked_stages=2047;uint8_t save_profile_present=1,opening_demo=0,sudden_death=0;};
struct MeleeWebMenuHost{MeleeWebPadState input;};
struct MeleeWebMenuCurrentSssExitObservation{int state=2;struct{bool captured=true;uint64_t world_generation=6,session_ticks=535;}note;};
struct MeleeWebStadiumSetupSnapshot{};
uint32_t parent_seed=0x7fc17cf1,other_seed=0;uint32_t* seed_ptr=&parent_seed;
bool world_exists=true,corrupt_retirement=false;uint64_t generation=6;
std::vector<std::string> events;
namespace melee_web{
struct RetailReplayInitialCssContext{};
struct Span{unsigned scene=0,first_frame=0,last_frame=0;};
struct RetailReplayRecipe{unsigned version=8;bool diagnostic_entity_prefix=false;std::vector<int> frames=std::vector<int>(2006);std::vector<Span> spans;bool whole_session()const{return version==8;}std::unique_ptr<RetailReplayInitialCssContext> initial_css{new RetailReplayInitialCssContext};MeleeWebMenuMatchSelection selection;uint32_t seed=123;};
struct RuntimeArchiveCache{explicit RuntimeArchiveCache(const std::map<std::string,std::vector<uint8_t>>&){}};
enum class GameplayMatchConstruction{Deferred};enum class GameplayMatchDiagnostic{StadiumReady};
struct GameplayMatchSession{GameplayMatchSession(const std::map<std::string,std::vector<uint8_t>>&,const MeleeWebMenuMatchSelection&,RuntimeArchiveCache&,GameplayMatchConstruction,const MeleeWebPadState&,GameplayMatchDiagnostic){events.push_back("construct-deferred");world_exists=true;++generation;}};
struct GameplayMenuWorld{void close(){events.push_back("close-world");world_exists=false;if(corrupt_retirement)seed_ptr=&other_seed;}};
namespace stadium_first_css_diagnostic{std::string hex32(uint32_t v){return std::to_string(v);}void write_start(std::ostream& out,const StartMeleeData& s){out<<s.tag;}}
}
std::unique_ptr<melee_web::RetailReplayRecipe> replay;
MeleeWebMenuHost host_object,*host=&host_object;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
std::unique_ptr<melee_web::GameplayMatchSession> match;
bool results=false,prize=false,faulted=false,host_entered=false,world_exposed=true;
bool running=false,pending=false,menu_scene_rebuild_pending=false,stadium_c1a_armed=false;
bool scoped_assets=true,scoped_disc_import=true,source_session_owned=true,first_use_draw_pending=false;
size_t replay_cursor=940;unsigned completed_matches=1,asset_generation=2;bool asset_selection_valid=true;
uint64_t diagnostic_audio_render_calls=0,diagnostic_audio_render_frames=0;
MeleeWebMenuMatchSelection live_selection,asset_selection;
std::vector<std::string> requested_assets{"asset"};std::map<std::string,std::vector<uint8_t>> files{{"asset",{1}}};
enum class AssetDestination{None,FirstSssVsSetup};AssetDestination asset_destination=AssetDestination::None;
struct Scope{unsigned pending=0;unsigned pending_generation(){return pending;}size_t active_file_count(){return files.size();}}asset_scope;
melee_web::MenuPreparationState preparation;
struct Clock{void reset(){assert(preparation.phase()==melee_web::MenuPreparationState::Phase::Idle);events.push_back("clock-reset");}}menu_clock,audio_clock;
struct Frames{bool pending(){return false;}size_t steps(){return 0;}size_t draws(){return 0;}}replay_source_frames;
struct{bool attempted=false;}first_sss_vs_entry;
struct{bool armed=false,complete=false,failed=false;}first_sss_prefix_sequence;
struct{bool opted_in=false,armed=false;}matched_setup_cf;
struct{bool opted_in=false;}first_sss_text_tail_probe;
std::string message;unsigned old_getter_calls=0,later_getter_calls=0,requests=0,approval_calls=0;
std::function<void()> approval_hook;bool approved=true;
void check(bool ok,const char* e){if(!ok)throw std::runtime_error(e);}
enum{MELEE_WEB_MENU_READY=5};int host_phase=5,host_scene=0;
int melee_web_menu_host_phase(MeleeWebMenuHost*){return host_phase;}
int melee_web_menu_host_source_scene(MeleeWebMenuHost*){return host_scene;}
uint64_t melee_web_gameplay_generation(){return generation;}
bool melee_web_gameplay_world_exists(){return world_exists;}
const MeleeWebPadState* melee_web_menu_host_input(MeleeWebMenuHost* h){return h?&h->input:nullptr;}
int melee_web_menu_host_stadium_c1a_selection(MeleeWebMenuHost*,MeleeWebMenuMatchSelection* out,char*,size_t){++old_getter_calls;*out=live_selection;return 1;}
int melee_web_menu_host_later_sss_prepared_selection(MeleeWebMenuHost*,MeleeWebMenuMatchSelection* out,char*,size_t){++later_getter_calls;*out=live_selection;return 1;}
bool melee_web_net_active(){return false;}
int HSD_SynthSFXGetPendingLoadCount(){return 0;}int melee_web_audio_bank_transport_active(){return 1;}int melee_web_audio_bank_transport_configured(){return 1;}int melee_web_audio_bank_transport_busy(){return 0;}
void first_css_browser_draw_refresh_observation(){}
void first_sss_vs_setup_fail(const char*);
namespace melee_web{std::vector<std::string> stadium_c1a_asset_names(const MeleeWebMenuMatchSelection&){return {"asset"};}}
void request_assets(AssetDestination d,const MeleeWebMenuMatchSelection* s,void*,int){events.push_back("request-assets");++requests;asset_destination=d;asset_selection=*s;++asset_generation;asset_scope.pending=asset_generation;archive_cache.reset();}
int current_sss_prepared_setup_begin();
int approval(){++approval_calls;if(approval_hook)approval_hook();return approved?1:0;}
#define EM_ASM_INT(...) approval()
'''
        main=r'''
void first_sss_vs_setup_fail(const char* e){first_sss_vs_setup.failed=true;first_sss_vs_setup.error=e;}
void reset(bool later=true){
 match.reset();world=std::make_unique<melee_web::GameplayMenuWorld>();archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 replay=std::make_unique<melee_web::RetailReplayRecipe>();host=&host_object;host_object.input=MeleeWebPadState{};
 current_sss_exit=CurrentSssExitState{};first_sss_vs_setup=FirstSssVsSetupState{};later_sss=LaterSssContinuation{};
 auto& o=current_sss_exit;o.profile=CurrentSssExitProfile::Stock4None;o.armed=o.terminal=o.notified=true;
 o.recipe=replay.get();o.context=replay->initial_css.get();o.host_owner=host;o.leave_world=world.get();o.leave_cache=archive_cache.get();
 o.leave_world_generation=generation=6;o.observation.note.world_generation=6;o.observation.note.session_ticks=535;
 o.prepared_start_attempted=o.prepared_start_captured=o.prepared_start_selection_captured=true;o.prepared_start_json="captured";
 live_selection=MeleeWebMenuMatchSelection{};o.prepared_start_selection=live_selection;replay->selection=live_selection;
 o.prepared_start_input_owner=&host->input;o.prepared_start_input_bytes=host->input.bytes;
 o.last_host_tick_returned=true;o.terminal_cursor=o.notification_cursor=replay_cursor=later?940:270;o.notification_order=later?1887:543;
 later_sss.armed=later;later_sss.stage=4;completed_matches=later?1:0;
 asset_generation=o.leave_asset_generation=2;current_sss_asset_request_calls=o.leave_asset_requests=0;
 parent_seed=live_selection.random_seed;seed_ptr=&parent_seed;world_exists=true;corrupt_retirement=false;world_exposed=true;
 running=pending=host_entered=faulted=menu_scene_rebuild_pending=false;host_phase=5;host_scene=0;
 asset_destination=AssetDestination::None;asset_scope.pending=0;preparation.reset();assert(preparation.request());
 assert(preparation.waiting_for_audio()&&preparation.phase()==melee_web::MenuPreparationState::Phase::WaitingForAudio);
 approval_hook={};approved=true;approval_calls=old_getter_calls=later_getter_calls=requests=0;events.clear();
}
void commit(){asset_destination=AssetDestination::None;asset_scope.pending=0;assert(!first_sss_vs_setup_finish_handoff());}
int main(){
 for(bool later:{false,true}){reset(later);assert(current_sss_prepared_setup_begin()==1);assert(requests==1&&approval_calls==1&&!world&&seed_ptr==&parent_seed&&parent_seed==0x7fc17cf1);assert((later?later_getter_calls:old_getter_calls)==1);assert(first_sss_vs_setup.origin==(later?FirstSssVsSetupOrigin::CurrentPreparedLater_940:FirstSssVsSetupOrigin::CurrentPreparedStock4_270));assert((events==std::vector<std::string>{"clock-reset","clock-reset","close-world","request-assets"}));commit();assert(first_sss_vs_setup.import_committed&&match&&generation==7);assert((later?later_getter_calls:old_getter_calls)==2);}
 for(int f=0;f<17;++f){reset();auto& o=current_sss_exit;
 if(f==0)o.context=nullptr;if(f==1)o.host_owner=nullptr;if(f==2)o.leave_world=nullptr;if(f==3)o.leave_cache=nullptr;
 if(f==4)later_sss.stage=3;if(f==5)completed_matches=0;if(f==6)replay_cursor=939;if(f==7)o.notification_order=1886;
 if(f==8)o.observation.note.session_ticks=940;if(f==9)++generation;if(f==10)o.prepared_start_selection.random_seed++;
 if(f==11)host->input.bytes[0]++;if(f==12)replay->selection.start.tag++;if(f==13)approved=false;
 if(f==14)host_phase=3;if(f==15)o.terminal_steps=1;if(f==16)++current_sss_asset_request_calls;
 assert(current_sss_prepared_setup_begin()==0&&requests==0&&world&&o.prepared_setup_failed);}
 // Reentrant/changed approval must fail before retirement or any request.
 for(int f=0;f<5;++f){reset();approval_hook=[f](){if(f==0)current_sss_prepared_setup_begin();if(f==1)current_sss_exit.context=nullptr;if(f==2)later_sss.armed=false;if(f==3)host->input.bytes[0]++;if(f==4)live_selection.random_seed++;};assert(current_sss_prepared_setup_begin()==0&&requests==0&&world);}
 reset();corrupt_retirement=true;assert(current_sss_prepared_setup_begin()==0&&requests==0&&!world&&first_sss_vs_setup.failed);
 reset();assert(current_sss_prepared_setup_begin()==1);assert(current_sss_prepared_setup_begin()==0&&requests==1&&current_sss_exit.prepared_setup_failed);
 for(int f=0;f<9;++f){reset();assert(current_sss_prepared_setup_begin()==1);
 if(f==0)current_sss_exit.context=nullptr;if(f==1)later_sss.stage=3;if(f==2)replay_cursor=939;
 if(f==3)live_selection.random_seed++;if(f==4)asset_selection.start.tag++;
 if(f==5)host->input.bytes[0]++;if(f==6)seed_ptr=&other_seed;if(f==7)parent_seed++;
 if(f==8)current_sss_prepared_setup_begin();
 bool refused=false;try{commit();}catch(const std::runtime_error&){refused=true;}assert(refused&&!match&&!first_sss_vs_setup.import_committed);}
 // Legacy124 keeps its existing generic stopped-host handoff, without either current getter.
 reset(false);world->close();world.reset();archive_cache.reset();auto& s=first_sss_vs_setup;s.attempted=s.requested=true;s.requested_generation=asset_generation;s.origin=FirstSssVsSetupOrigin::LegacyEntry124;s.host_input=&host->input;s.owned_input=std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)>(new MeleeWebPadState,melee_web_pad_state_free);s.input=s.owned_input.get();s.input_bytes=host->input.bytes;s.parent_seed_owner=seed_ptr;s.parent_seed=*seed_ptr;s.prior_world_generation=6;asset_selection=live_selection;commit();assert(old_getter_calls==0&&later_getter_calls==0&&match);
 std::cout<<"PASS actual handoff/profile1/later940/Legacy124 with modeled services; no native game/ABI/RNG-constructor proof\n";
}
'''
        # Real dependency/type/API: reset() -> Idle; request() -> WaitingForAudio.
        # Clock leaf observes Idle after the actual handoff resets preparation.
        include='#include "'+str(BASE/'src/menu_preparation_state.hpp')+'"\n'
        source=include+fixture+declarations+difference+begin+finish+main
        run(source,self.new_workspace(BASE,'later-sss-prepared-setup-'))
