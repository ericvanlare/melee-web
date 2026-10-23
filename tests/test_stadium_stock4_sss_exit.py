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
        # Selection comparison belongs to prepared-setup validation, not this
        # stopped-boundary fixture. Keep only the actual helpers exercised here.
        helpers=(cut(text,'bool current_sss_exit_profile_valid(',
                     'std::string current_sss_selection_difference(')+
                 cut(text,'void current_sss_exit_before_leave(',
                     'int current_sss_exit_arm('))
        preparation=cut(text,'void begin_preparation(){','bool preparation_uses_source_draws(){')
        audio=cut(text,'  if(!audio_before_construction&&!audio_elapsed.stalled',
                  '#if !defined(MELEE_WEB_PUBLIC_RUNTIME)\n  replay_boundary_mark("ordinary_audio_boundary_returned"')
        includes=''.join('#include "'+str(BASE/'src'/name)+'"\n' for name in
                         ['menu_preparation_state.hpp','source_frame_sequence.hpp'])
        fixture=r'''
#define MELEE_WEB_PUBLIC_RUNTIME 1
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#define EM_ASM(...) events.push_back("preparation-notify")
enum{MELEE_WEB_MENU_HOST_SCENE_SSS=2,MELEE_WEB_MENU_READY=5,St_Kind_PStadium=3,Gm_PKind_Human=0,MatchKind_Time=0};
enum class CurrentSssExitProfile{Time120,Stock4None};
struct Rules{int stkind=3,match_kind=1,time_limit=0,xB=-1;bool is_vs=true,is_stock=true,timer_enabled=false,timer_counts_up=false,disable_pausing=false,is_teams=false;uint64_t x20=UINT64_MAX;};
struct Player{int ckind=8,slot_type=0,color=0,stocks=4;};
struct StartMeleeData{Rules rules;Player players[2];};
struct MeleeWebMenuMatchSelection{StartMeleeData start;struct{uint32_t controller=0,stocks=0,costume=0,sub_color=0;}players[4];uint32_t player_count=2,random_seed=0xe2624ac3,hud_layout=0;uint16_t unlocked_characters=2047,unlocked_stages=2047;uint8_t save_profile_present=1,opening_demo=0,sudden_death=0;MeleeWebMenuMatchSelection(){start.players[0].color=1;}};
using Selection=MeleeWebMenuMatchSelection;
struct Recipe{Selection selection;std::vector<int> frames=std::vector<int>(2006);std::unique_ptr<int> initial_css{new int};};namespace melee_web{using RetailReplayRecipe=::Recipe;}std::unique_ptr<Recipe> replay{new Recipe};
struct State{CurrentSssExitProfile profile=CurrentSssExitProfile::Stock4None;bool armed=true,leave_started=false,terminal_pending=false,terminal=false,terminal_waiting_for_audio=false,notified=false,failed=false,notification_received=false,notification_in_host_tick=false,notification_last_tick_returned=true,host_tick_in_flight=false,last_host_tick_returned=true;size_t notification_cursor=270,notification_steps=0,notification_draws=0,terminal_cursor=0,terminal_steps=0,terminal_draws=0;uint64_t leave_world_generation=0,leave_asset_requests=0,terminal_asset_requests=0;unsigned leave_asset_generation=0;const void* leave_world=nullptr;const void* leave_cache=nullptr;const Recipe* recipe=nullptr;const int* context=nullptr;const int* host_owner=nullptr;struct{int state=2;struct{bool captured=true;uint64_t world_generation=7;}note;}observation;}current_sss_exit;
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

    def test_actual_callback_lifecycle_preserves_distinct_arm_and_leave_generations(self):
        text=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        host_text=(ROOT/'src/gameplay_menu_host.c').read_text()
        note=cut(host_text,'static void current_sss_exit_note(', 'int melee_web_menu_host_arm_current_sss_exit(')
        # C permits this implicit void-pointer conversion; adapt only its
        # spelling to compile the same callback body in the combined C++ fixture.
        note=note.replace('MeleeWebMenuHost* h=data;',
                          'MeleeWebMenuHost* h=static_cast<MeleeWebMenuHost*>(data);')
        host_arm=cut(host_text,'int melee_web_menu_host_arm_current_sss_exit(', 'static unsigned host_first_sss_prefix_count(')
        notify=cut(text,'void current_sss_exit_notify(', 'void current_sss_exit_tick_begin(')
        helpers=(cut(text,'bool current_sss_exit_profile_valid(',
                     'std::string current_sss_selection_difference(')+
                 cut(text,'void current_sss_exit_before_leave(',
                     'int current_sss_exit_arm('))
        arm=cut(text,'int current_sss_exit_arm(',
                'int current_sss_prepared_setup_begin(')
        preparation=cut(text,'void begin_preparation(){','bool preparation_uses_source_draws(){')
        getter=cut(text,'const char* melee_web_native_menu_stadium_current_sss_prepared_start_observe(){',
                   'int melee_web_native_menu_stadium_current_sss_prepared_setup_begin(')
        fixture=r'''
#define MELEE_WEB_PUBLIC_RUNTIME 1
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
#define EM_ASM(...) ((void)0)
enum{MELEE_WEB_MENU_CREATED=0,MELEE_WEB_MENU_READY=5,MELEE_WEB_MENU_SSS=3,MELEE_WEB_MENU_SSS_PREFIX_EXIT=3,MELEE_WEB_HOST_SCENE_NONE=0,MELEE_WEB_HOST_SCENE_SSS=2,MELEE_WEB_MENU_HOST_SCENE_SSS=2,GS_SSS=9,GM_VS=2,gmVsMode_State_Sss=1,St_Kind_PStadium=3,Gm_PKind_Human=0,MatchKind_Time=0};
enum class CurrentSssExitProfile{Time120,Stock4None};enum class AssetDestination{None,Match};
struct Rules{int stkind=3,match_kind=1,time_limit=0,xB=-1;bool is_vs=true,is_stock=true,timer_enabled=false,timer_counts_up=false,disable_pausing=false,is_teams=false;uint64_t x20=UINT64_MAX;};
struct Player{int ckind=8,slot_type=0,color=0,stocks=4;};
struct StartMeleeData{Rules rules;Player players[2];};
struct MeleeWebMenuMatchSelection{StartMeleeData start;struct{uint32_t controller=0,stocks=0,costume=0,sub_color=0;}players[4];uint32_t player_count=2,random_seed=0xe2624ac3,hud_layout=0;uint16_t unlocked_characters=2047,unlocked_stages=2047;uint8_t save_profile_present=1,opening_demo=0,sudden_death=0;MeleeWebMenuMatchSelection(){start.players[0].color=1;}};
struct Recipe{unsigned version=8;bool diagnostic_entity_prefix=false;MeleeWebMenuMatchSelection selection;std::vector<int> frames=std::vector<int>(2006);std::unique_ptr<int> initial_css{new int};};namespace melee_web{using RetailReplayRecipe=::Recipe;}
struct MeleeWebMenuSession{};struct SSSData{int start_game=1,force_stage_id=-1;struct{StartMeleeData start;}vs;};SSSData payload;
struct Note{bool captured=false;int phase=0,host_entered=0,session_phase=0,source_scene=0,scene_kind=0;unsigned scene_frame=0,random_seed=0;uint64_t session_ticks=0,world_generation=0,audio_generation=0;SSSData sss;unsigned char pad_state[4]{};int scene_routing_getters[4]{},owners[8]{};};
using MeleeWebMenuFirstSssPairNoteSnapshot=Note;
struct MeleeWebMenuCurrentSssExitObservation{int state=0;char error[160]{};Note note;};
using MeleeWebMenuCurrentSssExitNotify=void(*)(void*,const MeleeWebMenuCurrentSssExitObservation*);
struct MeleeWebPadState{uint8_t tag=0x5a;};
struct MeleeWebMenuHost{MeleeWebMenuCurrentSssExitObservation current_sss_exit;const void* current_sss_exit_context=nullptr;MeleeWebMenuCurrentSssExitNotify current_sss_exit_notify=nullptr;void* current_sss_exit_notify_data=nullptr;MeleeWebMenuSession* session=nullptr;bool entered=false,drawing=false,vs_mode_owned=true,initial_replay_context=true,stadium_c1a_enabled=false;int source_scene=0,source_mode_kind=GM_VS,first_sss_pair_state=0,first_sss_sequence_operation=0;unsigned seed=42;void* audio=nullptr;uint64_t generation=0,audio_generation=1;MeleeWebPadState input;struct{int scene_kind=GS_SSS;const void* enter_data=nullptr;const void* exit_data=nullptr;}source_scene_info;};
struct State{CurrentSssExitProfile profile=CurrentSssExitProfile::Time120;bool armed=false,notified=false,failed=false,notification_received=false,host_tick_in_flight=false,last_host_tick_returned=true,notification_in_host_tick=false,notification_last_tick_returned=false;bool leave_started=false,terminal_pending=false,terminal=false,terminal_waiting_for_audio=false,prepared_start_attempted=false,prepared_start_captured=false,prepared_start_selection_captured=false;size_t notification_cursor=0,notification_steps=0,notification_draws=0,terminal_cursor=0,terminal_steps=0,terminal_draws=0;uint64_t notification_order=0,order=543,world_generation=0,leave_world_generation=0,leave_asset_requests=0,terminal_asset_requests=0;unsigned leave_asset_generation=0;const Recipe* recipe=nullptr;const int* context=nullptr;MeleeWebMenuHost* host_owner=nullptr;const void* leave_world=nullptr;const void* leave_cache=nullptr;MeleeWebMenuCurrentSssExitObservation observation;std::string error,prepared_start_json,prepared_start_error;MeleeWebMenuMatchSelection prepared_start_selection{};const MeleeWebPadState* prepared_start_input_owner=nullptr;std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> prepared_start_input_bytes{};};State current_sss_exit;
std::unique_ptr<Recipe> replay;std::unique_ptr<int> world,archive_cache;MeleeWebMenuHost host_object,*host=&host_object,*owner=&host_object;MeleeWebMenuSession session;unsigned* seed_ptr=&host_object.seed;
uint64_t generation=7,current_sss_asset_request_calls=9;unsigned asset_generation=4;size_t replay_cursor=0;int phase=0,scene=0,selection_calls=0,pending_menu_source_scene=0;
bool host_entered=false,world_exposed=false,match=false,results=false,prize=false,pending=false,running=false,faulted=false,menu_scene_rebuild_pending=false,replay_trace=true,replay_started=false,scoped_assets=true,scoped_disc_import=true,source_session_owned=true,stadium_c1a_armed=false,transition_audio_continues=false,skip_note=false;
struct{bool armed=false;}first_css_browser_draw;AssetDestination asset_destination=AssetDestination::None;struct{int pending_generation(){return 0;}}asset_scope;
melee_web::MenuPreparationState preparation;melee_web::SourceFrameSequence replay_source_frames;
struct{void reset(){}}menu_clock,audio_clock;struct{void begin(bool,double){}}preparation_profile;std::string message;
void check(bool value,const char* error){if(!value)throw std::runtime_error(error);}uint64_t melee_web_gameplay_generation(){return generation;}
int live(MeleeWebMenuHost* h,char*,size_t){return h==owner&&h->generation==generation&&h->entered&&h->audio;}
const SSSData* melee_web_menu_sss(MeleeWebMenuSession*){return &payload;}int melee_web_menu_phase(MeleeWebMenuSession*){return phase;}
uint64_t melee_web_audio_generation(void*){return 1;}int gm_GetCurrentGameMode(){return GM_VS;}int gm_GetPreviousGameMode(){return 1;}int gm_GetCurrentSceneIndex(){return gmVsMode_State_Sss;}int gm_GetPreviousSceneIndex(){return 0;}
const void* melee_web_current_scene_info(){return &host->source_scene_info;}unsigned gm_801A4BA8(){return 125;}void melee_web_pad_state_capture(unsigned char*){}
int fail(char* e,size_t n,const char* text){if(e&&n)std::snprintf(e,n,"%s",text);return 0;}int ok(char*,size_t){return 1;}
int melee_web_menu_arm_current_sss_exit(MeleeWebMenuSession*,void(*)(void*,MeleeWebMenuSession*,const SSSData*,uint64_t,int),char*,size_t){return 1;}
int melee_web_menu_host_enable_stadium_c1a(MeleeWebMenuHost* h,char*,size_t){h->stadium_c1a_enabled=true;return 1;}
int melee_web_menu_host_source_scene(MeleeWebMenuHost*){return scene;}int melee_web_menu_host_phase(MeleeWebMenuHost*){return phase;}bool melee_web_net_active(){return false;}
void clear_diagnostic_pad(){}double emscripten_get_now(){return 0;}
int melee_web_menu_host_leave(MeleeWebMenuHost*,int,char*,size_t);
int HSD_SynthSFXGetPendingLoadCount(){return 0;}int melee_web_audio_bank_transport_active(){return 1;}
int melee_web_audio_bank_transport_configured(){return 1;}int melee_web_audio_bank_transport_busy(){return 0;}
int melee_web_menu_host_stadium_c1a_selection(MeleeWebMenuHost*,MeleeWebMenuMatchSelection* out,char*,size_t){++selection_calls;*out=replay->selection;return 1;}
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost* value){return value?&value->input:nullptr;}
int melee_web_pad_state_encode(const MeleeWebPadState* input,uint8_t* output,size_t size){if(!input||!output||size!=MELEE_WEB_PAD_STATE_BYTES)return 0;for(size_t i=0;i<size;++i)output[i]=input->tag;return 1;}
std::string first_css_browser_draw_json_quote(const std::string& s){return "\""+s+"\"";}
namespace melee_web{namespace stadium_first_css_diagnostic{std::string hex32(uint32_t){return "e2624ac3";}void write_start(std::ostream& out,const StartMeleeData&){out<<"{}";}}}
'''
        main=r'''
int melee_web_menu_host_leave(MeleeWebMenuHost* h,int,char*,size_t){
 assert(preparation.waiting_for_audio()&&!running&&!pending);
 if(!skip_note)current_sss_exit_note(h,h->session,&payload,270,MELEE_WEB_MENU_SSS_PREFIX_EXIT);
 h->entered=false;phase=MELEE_WEB_MENU_READY;scene=0;return 1;
}
void reset(){current_sss_exit=State{};replay=std::make_unique<Recipe>();world=std::make_unique<int>(1);archive_cache=std::make_unique<int>(2);host_object=MeleeWebMenuHost{};host=owner=&host_object;host->session=&session;seed_ptr=&host->seed;generation=7;phase=0;scene=0;replay_cursor=0;host_entered=world_exposed=pending=running=false;faulted=menu_scene_rebuild_pending=skip_note=false;asset_generation=4;current_sss_asset_request_calls=9;asset_destination=AssetDestination::None;selection_calls=0;preparation.reset();replay_source_frames=melee_web::SourceFrameSequence{};
 assert(current_sss_exit_arm(CurrentSssExitProfile::Stock4None)==1);
 char e[160]{};assert(melee_web_menu_host_arm_current_sss_exit(host,current_sss_exit.context,current_sss_exit_notify,&current_sss_exit,e,sizeof(e))==1);
 // Endpoint double for the authored CSS->SSS scene restart. Keep the arm
 // generation; mirror bootstrap's nonzero increment and bind the new host.
 uint64_t next=generation+1;if(next==0)next=1;generation=next;host->generation=generation;
 host->audio=&session;host->entered=true;host->source_scene=MELEE_WEB_HOST_SCENE_SSS;host->source_scene_info.enter_data=host->source_scene_info.exit_data=&payload;
 phase=MELEE_WEB_MENU_SSS;scene=MELEE_WEB_HOST_SCENE_SSS;replay_cursor=270;host_entered=world_exposed=pending=running=true;
 assert(current_sss_exit.world_generation==7&&generation==8&&!current_sss_exit.notified&&!current_sss_exit.terminal&&!current_sss_exit.observation.note.captured);
}
void complete_leave(){current_sss_exit_before_leave();begin_preparation();current_sss_exit_after_preparation();replay_source_frames.finish([]{return true;});current_sss_exit_finish_stock4(0,0,false);}
int main(){reset();complete_leave();auto& s=current_sss_exit;
 assert(s.notified&&s.observation.state==2&&s.observation.note.captured&&s.terminal&&s.world_generation==7&&s.observation.note.world_generation==8&&generation==8);
 std::string result=melee_web_native_menu_stadium_current_sss_prepared_start_observe();std::cout<<result<<std::endl;
 assert(result.find("\"captured\":true")!=std::string::npos&&selection_calls==1);
 assert(result.find("\"world_generation\":8")!=std::string::npos);
 assert(result.find("\"hsd_pending_loads_at_return\":0")!=std::string::npos&&
        result.find("\"bank_transport_active_at_return\":true")!=std::string::npos&&
        result.find("\"bank_transport_configured_at_return\":true")!=std::string::npos&&
        result.find("\"bank_transport_busy_at_return\":false")!=std::string::npos);
 assert(s.world_generation==7&&replay_source_frames.steps()==0&&replay_source_frames.draws()==0&&current_sss_asset_request_calls==9);
 assert(result==melee_web_native_menu_stadium_current_sss_prepared_start_observe()&&selection_calls==1);
 // Retained completed leave must refuse any later owner/generation replacement.
 for(int fault=0;fault<5;++fault){reset();complete_leave();if(fault==0)++generation;if(fault==1)world=std::make_unique<int>(3);if(fault==2)archive_cache=std::make_unique<int>(4);if(fault==3)current_sss_exit.leave_world_generation=0;if(fault==4)current_sss_exit.leave_world_generation=7;result=melee_web_native_menu_stadium_current_sss_prepared_start_observe();assert(result.find("\"captured\":false")!=std::string::npos&&selection_calls==0);}
 // Before callback/terminal completion the getter cannot manufacture a receipt.
 reset();result=melee_web_native_menu_stadium_current_sss_prepared_start_observe();assert(result.find("\"captured\":false")!=std::string::npos&&selection_calls==0&&!current_sss_exit.notified&&!current_sss_exit.terminal);
 // Real note rejects a generation-mismatched host before the browser can approve it.
 reset();current_sss_exit_before_leave();--host->generation;bool refused=false;try{begin_preparation();current_sss_exit_after_preparation();}catch(const std::runtime_error&){refused=true;}assert(refused&&current_sss_exit.failed&&!current_sss_exit.terminal&&!current_sss_exit.observation.note.captured);
 reset();skip_note=true;current_sss_exit_before_leave();refused=false;try{begin_preparation();current_sss_exit_after_preparation();}catch(const std::runtime_error&){refused=true;}assert(refused&&!current_sss_exit.notified&&!current_sss_exit.terminal);
 reset();current_sss_exit_before_leave();host->current_sss_exit_notify=[](void* data,const MeleeWebMenuCurrentSssExitObservation* actual){auto foreign=*actual;--foreign.note.world_generation;current_sss_exit_notify(data,&foreign);};refused=false;try{begin_preparation();current_sss_exit_after_preparation();}catch(const std::runtime_error&){refused=true;}assert(refused&&current_sss_exit.failed&&!current_sss_exit.notified&&!current_sss_exit.terminal);
 reset();generation=0;refused=false;try{current_sss_exit_before_leave();}catch(const std::runtime_error&){refused=true;}assert(refused&&!current_sss_exit.leave_started&&!current_sss_exit.terminal);
}
'''
        includes=''.join('#include "'+str(BASE/'src'/name)+'"\n' for name in
                         ['gameplay_pad_state.h','menu_preparation_state.hpp','source_frame_sequence.hpp'])
        run(includes+fixture+notify+note+host_arm+helpers+arm+preparation+getter+main,
            self.new_workspace(ROOT,'stock4-leave-generation-lifecycle-'))

    def test_actual_prepared_start_getter_is_read_only_and_requires_the_exact_stopped_boundary(self):
        text=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        fn=cut(text,'const char* melee_web_native_menu_stadium_current_sss_prepared_start_observe(){',
               'int melee_web_native_menu_stadium_current_sss_prepared_setup_begin(')
        fixture=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
enum class CurrentSssExitProfile{Time120,Stock4None};enum{MELEE_WEB_MENU_READY=1,St_Kind_PStadium=3};
enum class AssetDestination{None,Match};struct MeleeWebPadState{uint8_t tag=0x5a;};struct Host{MeleeWebPadState input;};struct Recipe{std::unique_ptr<int> initial_css{new int(1)};};
struct StartMeleeRules{int stkind=St_Kind_PStadium;};struct StartMeleeData{StartMeleeRules rules;};
struct MeleeWebMenuMatchSelection{StartMeleeData start;struct{uint32_t controller=0,stocks=0,costume=0,sub_color=0;}players[4];uint32_t player_count=2,random_seed=0xe2624ac3,hud_layout=0;uint16_t unlocked_characters=2047,unlocked_stages=2047;uint8_t save_profile_present=1,opening_demo=0,sudden_death=0;};
struct Note{bool captured=true;uint64_t world_generation=7;};struct Observation{int state=2;Note note;};
struct CurrentSssExitState{bool prepared_start_attempted=false,prepared_start_captured=false,prepared_start_selection_captured=false;
 std::string prepared_start_error,prepared_start_json;bool armed=true,terminal=true,failed=false,notified=true;
 CurrentSssExitProfile profile=CurrentSssExitProfile::Stock4None;Observation observation;
 uint64_t world_generation=7,leave_world_generation=7,leave_asset_generation=4,leave_asset_requests=9,notification_order=543;
 size_t terminal_cursor=270,notification_cursor=270,terminal_steps=0,terminal_draws=0;
 size_t notification_steps=0,notification_draws=0,terminal_asset_requests=0;
 bool last_host_tick_returned=true,host_tick_in_flight=false;const Recipe* recipe=nullptr;
 const int* context=nullptr;Host* host_owner=nullptr;int* leave_world=nullptr;int* leave_cache=nullptr;
 MeleeWebMenuMatchSelection prepared_start_selection{};const MeleeWebPadState* prepared_start_input_owner=nullptr;
 std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> prepared_start_input_bytes{};};
CurrentSssExitState current_sss_exit;std::unique_ptr<Recipe> replay;Host host_object,*host=nullptr;
std::unique_ptr<int> world,archive_cache;uint32_t asset_generation=4;uint64_t current_sss_asset_request_calls=9;
bool host_entered=false,world_exposed=true,match=false,results=false,prize=false,pending=false,running=false;
bool faulted=false,menu_scene_rebuild_pending=false,preparation_waiting=true;
int host_phase=MELEE_WEB_MENU_READY,host_scene=0,pending_asset_generation=0;
uint64_t gameplay_generation=7;AssetDestination asset_destination=AssetDestination::None;
struct AssetScope{int pending_generation()const{return pending_asset_generation;}}asset_scope;
struct Preparation{bool waiting_for_audio()const{return preparation_waiting;}}preparation;
int selection_calls=0,source_steps=0,source_draws=0,asset_calls=0;
int pending_loads=0,transport_active=1,transport_configured=1,transport_busy=0,service_reads=0;
int HSD_SynthSFXGetPendingLoadCount(){++service_reads;return pending_loads;}
int melee_web_audio_bank_transport_active(){++service_reads;return transport_active;}
int melee_web_audio_bank_transport_configured(){++service_reads;return transport_configured;}
int melee_web_audio_bank_transport_busy(){++service_reads;return transport_busy;}
void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
uint64_t melee_web_gameplay_generation(){return gameplay_generation;}
int melee_web_menu_host_phase(Host*){return host_phase;}
int melee_web_menu_host_source_scene(Host*){return host_scene;}
int melee_web_menu_host_stadium_c1a_selection(Host*,MeleeWebMenuMatchSelection* out,char* e,size_t){
 ++selection_calls;(void)e;out->start.rules.stkind=St_Kind_PStadium;return 1;}
const MeleeWebPadState* melee_web_menu_host_input(Host* value){return value?&value->input:nullptr;}
int melee_web_pad_state_encode(const MeleeWebPadState* input,uint8_t* output,size_t size){
 if(!input||!output||size!=MELEE_WEB_PAD_STATE_BYTES)return 0;
 for(size_t i=0;i<size;++i)output[i]=input->tag;return 1;}
std::string first_css_browser_draw_json_quote(const std::string& s){return "\""+s+"\"";}
namespace melee_web{namespace stadium_first_css_diagnostic{
std::string hex32(uint32_t){return "e2624ac3";}
void write_start(std::ostream& out,const StartMeleeData& s){out<<"{\"rules\":{\"stage_kind\":"<<s.rules.stkind<<"}}";}
}}
'''
        main=r'''
void reset(){current_sss_exit=CurrentSssExitState{};auto& s=current_sss_exit;
 replay=std::make_unique<Recipe>();world=std::make_unique<int>(2);archive_cache=std::make_unique<int>(3);
 host=&host_object;s.recipe=replay.get();s.context=replay->initial_css.get();s.host_owner=host;
 s.leave_world=world.get();s.leave_cache=archive_cache.get();asset_generation=4;
 current_sss_asset_request_calls=9;host_entered=false;world_exposed=true;match=results=prize=pending=running=false;
 faulted=menu_scene_rebuild_pending=false;preparation_waiting=true;host_phase=MELEE_WEB_MENU_READY;
 host_scene=0;pending_asset_generation=0;gameplay_generation=7;asset_destination=AssetDestination::None;
 pending_loads=0;transport_active=1;transport_configured=1;transport_busy=0;service_reads=0;
 selection_calls=source_steps=source_draws=asset_calls=0;}
int main(){reset();std::string first=melee_web_native_menu_stadium_current_sss_prepared_start_observe();
 assert(first.find("\"captured\":true")!=std::string::npos&&first.find("\"replay_cursor\":270")!=std::string::npos);
 assert(first.find("\"local_order_rows\":543")!=std::string::npos&&first.find("\"Entry\":false")!=std::string::npos);
 assert(first.find("\"hsd_pending_loads_at_return\":0")!=std::string::npos&&service_reads==4);
 assert(current_sss_exit.prepared_start_selection_captured&&current_sss_exit.prepared_start_selection.random_seed==0xe2624ac3);
 assert(current_sss_exit.prepared_start_input_owner==melee_web_menu_host_input(host));
 assert(current_sss_exit.prepared_start_input_bytes.front()==host->input.tag&&
        current_sss_exit.prepared_start_input_bytes.back()==host->input.tag);
 assert(selection_calls==1&&source_steps==0&&source_draws==0&&asset_calls==0);
 assert(first==melee_web_native_menu_stadium_current_sss_prepared_start_observe()&&selection_calls==1);
 for(int fault=0;fault<21;++fault){reset();auto& s=current_sss_exit;const char* reason="";
  if(fault==0){s.leave_world=nullptr;reason="live_exposed_world_identity";}
  if(fault==1){world_exposed=false;reason="live_exposed_world_identity";}
  if(fault==2){s.leave_cache=nullptr;reason="archive_cache_identity";}
  if(fault==3){host_phase=2;reason="host_ready_phase";}
  if(fault==4){host_scene=2;reason="host_source_scene_none";}
  if(fault==5){host_entered=true;reason="host_left";}
  if(fault==6){pending=true;reason="no_pending_or_running_work";}
  if(fault==7){preparation_waiting=false;reason="waiting_for_audio";}
  if(fault==8){pending_asset_generation=1;reason="no_pending_asset_work";}
  if(fault==9){asset_destination=AssetDestination::Match;reason="no_pending_asset_work";}
  if(fault==10){current_sss_asset_request_calls++;reason="unchanged_asset_owner";}
  if(fault==11){s.terminal_cursor=269;reason="exact_stock4_stop_cursor_and_order";}
  if(fault==12){s.terminal_steps=1;reason="zero_source_steps_and_draws";}
  if(fault==13){s.terminal_asset_requests=1;reason="no_terminal_asset_requests";}
  if(fault==14){s.host_tick_in_flight=true;reason="returned_host_tick";}
  if(fault==15){s.context=nullptr;reason="replay_and_css_context_identity";}
  if(fault==16){gameplay_generation++;reason="world_generation";}
  if(fault==17){pending_loads=1;reason="hsd_pending_loads_at_return";}
  if(fault==18){transport_active=0;reason="bank_transport_active_at_return";}
  if(fault==19){transport_configured=0;reason="bank_transport_configured_at_return";}
  if(fault==20){transport_busy=1;reason="bank_transport_busy_at_return";}
  std::string refused=melee_web_native_menu_stadium_current_sss_prepared_start_observe();
  std::string first_failure=std::string("Prepared Start guard failed: ")+reason;
  assert(refused.find("\"captured\":false")!=std::string::npos&&
         refused.find(first_failure)!=std::string::npos&&selection_calls==0);
  assert(service_reads==(fault<17?0:4));
 }
}
'''
        pad_header='#include "'+str(BASE/'src'/'gameplay_pad_state.h')+'"\n'
        run(pad_header+fixture+fn+main,self.new_workspace(ROOT,'stock4-prepared-start-getter-'))

    def test_prepared_start_export_stays_diagnostic_gated_and_runner_opt_in(self):
        cmake=(ROOT/'cmake/FighterRuntime.cmake').read_text()
        block=cmake[cmake.rindex('if(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)'):]
        exports=block[:block.index('\nendif()')]
        self.assertIn('_melee_web_native_menu_stadium_current_sss_prepared_start_observe',exports)
        self.assertIn("'stadium-current-sss-prepared-start': {type: 'boolean', default: false}",
                      (ROOT/'scripts/capture_whole_session_browser.mjs').read_text())
