"""Actual Setup functions with controlled services; not runtime equivalence."""
from pathlib import Path
import os, shlex, subprocess, unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(__file__).resolve().parents[1]
PRELUDE=r"""
#include <cassert>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include <cstdint>
#include <memory>
#include <array>
#include <vector>
#include <map>
#include <sstream>
#include <string>
#include <stdexcept>
#include <exception>
#include <cstdio>
#include <cstring>
#define MELEE_WEB_PAD_STATE_BYTES 822
struct MeleeWebPadState {std::array<uint8_t,822> bytes{};};
// Storage declarations for the adjacent opt-in state; this fixture executes only Setup.
struct MeleeWebMenuHost;
struct MeleeWebStadiumIdleSnapshot {};
struct MeleeWebStadiumGoAlignmentSnapshot {};
// Unused Gate2 event storage only; these Setup fixtures do not observe GO.
struct MeleeWebStadiumGoAlignmentEvent {};
static unsigned continuation_service_calls=0;
static bool matched_setup_cf_service(){++continuation_service_calls;throw std::runtime_error("default Setup unexpectedly invoked C/F continuation");}

struct MeleeWebDiagnosticSisOwner {void* heap=nullptr;uint64_t world_generation=0,allocation_generation=0;int source_heap_handle=0;uint64_t source_epoch=0;};
struct MeleeWebRetiredSisLease {MeleeWebDiagnosticSisOwner prior;size_t requested_bytes=0;int retirement_verified=0;};
static int text_probe_value=0,text_probe_sets=0,sis_capture_calls=0,membership_begin_calls=0,membership_end_calls=0;
static bool sis_capture_ok=true,membership_begin_ok=true,membership_snapshot_live=false;
static void HSD_SisLib_C1TextProbeSet(int enabled){text_probe_value=enabled;++text_probe_sets;}
static int melee_web_diagnostic_sis_capture(MeleeWebRetiredSisLease* lease,char* error,size_t size){++sis_capture_calls;if(!sis_capture_ok){snprintf(error,size,"capture refused");return 0;}lease->prior.heap=(void*)1;lease->prior.world_generation=9;lease->prior.source_heap_handle=3;lease->prior.allocation_generation=11;lease->prior.source_epoch=13;lease->requested_bytes=0x4800;return 1;}
static int melee_web_stadium_text_membership_snapshot_begin(const MeleeWebDiagnosticSisOwner*,char* error,size_t size){++membership_begin_calls;if(!membership_begin_ok){snprintf(error,size,"membership refused");return 0;}membership_snapshot_live=true;return 1;}
static void melee_web_stadium_text_membership_snapshot_end(){++membership_end_calls;membership_snapshot_live=false;}
static void melee_web_pad_state_free(MeleeWebPadState* p){delete p;}
static int melee_web_pad_state_encode(const MeleeWebPadState* p,uint8_t* out,size_t n){if(!p||!out||n!=822)return 0;memcpy(out,p->bytes.data(),n);return 1;}
static MeleeWebPadState* melee_web_pad_state_decode(const uint8_t* b,size_t n,char*,unsigned){if(n!=822)return nullptr;auto* p=new MeleeWebPadState;memcpy(p->bytes.data(),b,n);return p;}
static MeleeWebPadState host_pad;
static uint32_t seed=17,foreign_seed=19,*seed_ptr=&seed;
static uint64_t generation=7;static bool source_world=true;
static uint64_t melee_web_gameplay_generation(){return source_world?generation:0;}
static bool melee_web_gameplay_world_exists(){return source_world;}
static bool running=false,pending=false,faulted=false,host_entered=false,menu_scene_rebuild_pending=false;
static bool first_use_draw_pending=false,world_exposed=true,stadium_c1a_armed=false,scoped_assets=true,scoped_disc_import=true;
static void* host=(void*)1;static void *results=nullptr,*prize=nullptr;
[[maybe_unused]] static bool source_session_owned=true;
static bool net_active=false;static bool melee_web_net_active(){return net_active;}
static int host_source_scene=0,host_phase=5;
static int melee_web_menu_host_source_scene(void*){return host_source_scene;}
static int melee_web_menu_host_phase(void*){return host_phase;}
static const MeleeWebPadState* melee_web_menu_host_input(void*){return &host_pad;}
#define MELEE_WEB_MENU_READY 5
static void check(int ok,const char* e){if(!ok)throw std::runtime_error(e);}
static std::string first_css_browser_draw_json_quote(const std::string& value){return std::string("\"")+value+"\"";}
struct Clock {void reset(){}} menu_clock,audio_clock;
namespace melee_web {struct MenuPreparationState{enum class Phase{Idle,WaitingForAudio};};}
struct Preparation {melee_web::MenuPreparationState::Phase p=melee_web::MenuPreparationState::Phase::Idle;bool busy(){return p!=melee_web::MenuPreparationState::Phase::Idle;}bool waiting_for_audio(){return p==melee_web::MenuPreparationState::Phase::WaitingForAudio;}auto phase(){return p;}void reset(){p=melee_web::MenuPreparationState::Phase::Idle;}} preparation;
struct Start {int value=7;};struct MeleeWebMenuMatchSelection{uint32_t random_seed=0x3bb84c53,hud_layout=0,player_count=2;uint16_t unlocked_characters=2047,unlocked_stages=2047;uint8_t save_profile_present=1,opening_demo=0,sudden_death=0;struct{uint32_t controller=0,stocks=4,costume=0,sub_color=0;}players[4];Start start;};
struct Recipe{uint32_t seed=0x312151c3;MeleeWebMenuMatchSelection selection;std::unique_ptr<int> initial_css{new int(1)};};
// Storage aliases only: preserve this fixture's existing Recipe/int context
// model for the adjacent, unselected Gate2 state. No Gate2 admission is modeled.
namespace melee_web {
using RetailReplayRecipe = ::Recipe;
using RetailReplayInitialCssContext = int;
}
static std::unique_ptr<Recipe> replay;
enum class CurrentSssExitProfile{Time120,Stock4None};
struct LeaveNote{bool captured=false;uint64_t world_generation=0,session_ticks=0;};
struct LeaveObservation{unsigned state=0;LeaveNote note;};
struct CurrentSssExitState{bool armed=false,terminal=false,failed=false,prepared_start_attempted=false,prepared_start_captured=false,prepared_start_selection_captured=false,prepared_setup_attempted=false,prepared_setup_approved=false,prepared_setup_failed=false,prepared_setup_consumed=false,notified=false,last_host_tick_returned=false,host_tick_in_flight=false;CurrentSssExitProfile profile=CurrentSssExitProfile::Time120;Recipe* recipe=nullptr;const int* context=nullptr;void* host_owner=nullptr;void* leave_world=nullptr;void* leave_cache=nullptr;uint64_t leave_world_generation=0,terminal_cursor=0,notification_cursor=0,notification_order=0,leave_asset_requests=0;unsigned leave_asset_generation=0,terminal_steps=0,terminal_draws=0,notification_steps=0,notification_draws=0,terminal_asset_requests=0;LeaveObservation observation;MeleeWebMenuMatchSelection prepared_start_selection;const MeleeWebPadState* prepared_start_input_owner=nullptr;std::array<uint8_t,822> prepared_start_input_bytes{};std::string prepared_start_json,prepared_setup_error;};
static CurrentSssExitState current_sss_exit;
static bool getter_ok=true;static unsigned getter_calls=0;static int selected_value=7;static MeleeWebMenuMatchSelection host_selection;
static int melee_web_menu_host_stadium_c1a_selection(void*,MeleeWebMenuMatchSelection* s,char* e,unsigned n){++getter_calls;if(!getter_ok){snprintf(e,n,"get refused");return 0;}*s=host_selection;s->start.value=selected_value;return 1;}
namespace melee_web::stadium_first_css_diagnostic {
static std::string hex32(uint32_t x){char b[9];snprintf(b,sizeof b,"%08x",x);return b;}
static void write_start(std::ostream& o,const Start& s){o<<"{\"value\":"<<s.value<<'}';}}
static bool retirement_changes_seed=false;static unsigned close_calls=0;[[maybe_unused]] static bool close_should_fail=true;
struct World {void close(){++close_calls;source_world=false;if(retirement_changes_seed)seed_ptr=&foreign_seed;}};
static std::unique_ptr<World> world;
static std::map<std::string,std::vector<uint8_t>> files;
enum {MELEE_WEB_STADIUM_SETUP_PHASE_ENTRY=1,MELEE_WEB_STADIUM_SETUP_PHASE_PARAMETERS=2,MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL=3,MELEE_WEB_STADIUM_SETUP_PHASE_UP_RETURN=4,MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_CALL=5,MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_RETURN=6,MELEE_WEB_STADIUM_SETUP_PHASE_COMMIT=7};
struct MeleeWebStadiumSetupConstructorPhaseEvent {uint32_t ordinal=0,phase=0,parameter_values_valid=0;int32_t parameter_x0=0,parameter_x4=0;uint32_t scalar_value_valid=0;int32_t scalar_value=0;uint32_t source_seed_valid=0,source_seed=0;};
struct MeleeWebStadiumSetupSnapshot {uint32_t version=2;uint64_t world_generation=9,world_ticks=0;uint32_t source_frame=0;int32_t map_id=2,initial_countdown=4,current_countdown=4,parameter_x0=3,parameter_x4=5;uint32_t constructor_randi_call_count=1,constructor_random_bound_valid=1;int32_t constructor_random_bound=2;uint32_t constructor_random_result_valid=1;int32_t constructor_random_result=1;uint32_t source_seed_before=17,source_seed_after=19;int32_t ground_source_heap_handle=3;uint32_t ground_requested_bytes=256;uint64_t ground_allocation_generation=12;int32_t object_source_heap_handle=3;uint32_t object_requested_bytes=128;uint64_t object_allocation_generation=11;uint32_t constructor_callback_count=1,source_parameter_roots_agree=1;uint32_t constructor_phase_count=0;MeleeWebStadiumSetupConstructorPhaseEvent constructor_phases[5]{};};
static bool setup_snapshot_ok=true;static unsigned setup_snapshot_calls=0;[[maybe_unused]] static uint32_t setup_seed_to_return=0x35a455b9;
static uint32_t constructed_selection_seed=0;
namespace melee_web {
static std::vector<std::string> stadium_c1a_asset_names(const MeleeWebMenuMatchSelection&){return {"stage","fighter"};}
struct RuntimeArchiveCache {explicit RuntimeArchiveCache(const decltype(files)&){}};
enum class GameplayMatchConstruction{Deferred};enum class GameplayMatchDiagnostic{StadiumReady};
struct GameplayMatchSession {unsigned steps=0,frames=0;bool done=false;Start data;GameplayMatchSession(const decltype(files)&,const MeleeWebMenuMatchSelection& s,RuntimeArchiveCache&,GameplayMatchConstruction,const MeleeWebPadState&,GameplayMatchDiagnostic):data(s.start){constructed_selection_seed=s.random_seed;source_world=true;++generation;}bool advance_construction(){++steps;return done=steps==2;}unsigned source_frames(){return frames;}bool construction_complete(){return done;}uint32_t random_seed(){return setup_seed_to_return;}const Start& start_data(){return data;}bool diagnostic_stadium_setup_snapshot(MeleeWebStadiumSetupSnapshot* out,char* e,size_t n){++setup_snapshot_calls;if(!setup_snapshot_ok){snprintf(e,n,"setup snapshot refused");return false;}*out=MeleeWebStadiumSetupSnapshot{};return true;}};
}
static std::unique_ptr<melee_web::GameplayMatchSession> match;
static std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
enum class AssetDestination{None,StadiumC1a,FirstSssVsSetup};
static AssetDestination asset_destination=AssetDestination::None;static bool asset_committed=false,asset_selection_valid=false;
static uint32_t asset_generation=0;static MeleeWebMenuMatchSelection asset_selection;
static std::vector<std::string> requested_assets;
struct Scope{unsigned pending=0,active=0;unsigned pending_generation(){return pending;}unsigned active_file_count(){return active;}} asset_scope;
static uint64_t diagnostic_audio_render_calls=274,diagnostic_audio_render_frames=146133;
struct Entry{bool attempted=false,complete=true,failed=false;std::string actual="{\"phase\":\"vs_entry\",\"random_seed_hex\":\"3bb84c53\",\"start\":{\"value\":7}}";} first_sss_vs_entry;
struct Prefix{bool armed=false,complete=false,failed=false;} first_sss_prefix_sequence;
struct Observation{std::string observation="{\"old\":true,\"first_sss_vs_setup\":{}}";} first_css_browser_draw;
static unsigned refresh_calls=0;static void first_css_browser_draw_refresh_observation(){++refresh_calls;}
static std::string message;static unsigned current_sss_asset_request_calls=0;[[maybe_unused]] static size_t replay_cursor=270;
[[maybe_unused]] static int pending_load_count=0,transport_active=1,transport_configured=1,transport_busy=0;
struct ReplayFrames{bool pending_value=false;unsigned step_value=0,draw_value=0;bool pending(){return pending_value;}unsigned steps(){return step_value;}unsigned draws(){return draw_value;}}replay_source_frames;
[[maybe_unused]] static int HSD_SynthSFXGetPendingLoadCount(){return pending_load_count;}[[maybe_unused]] static int melee_web_audio_bank_transport_active(){return transport_active;}
[[maybe_unused]] static int melee_web_audio_bank_transport_configured(){return transport_configured;}[[maybe_unused]] static int melee_web_audio_bank_transport_busy(){return transport_busy;}
static void request_assets(AssetDestination d,const MeleeWebMenuMatchSelection* s,const void* opening=nullptr,int opening_state=-1){(void)opening;(void)opening_state;archive_cache.reset();files.clear();asset_scope.active=0;asset_destination=d;asset_selection=*s;asset_selection_valid=true;requested_assets=melee_web::stadium_c1a_asset_names(*s);++asset_generation;asset_scope.pending=asset_generation;++current_sss_asset_request_calls;}
int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe);
static int approval=1;[[maybe_unused]] static int approval_calls=0;static bool reenter=false;
#if defined(TEST_CURRENT_PREPARED)
int current_sss_prepared_setup_begin();
#endif
[[maybe_unused]] static bool prepared_reenter=false;
[[maybe_unused]] static int prepared_mutation=0,prepared_nested_result=99;
static unsigned gate2_fixture_close_calls=0;
int melee_web_native_menu_unload();
static void close();
static int approve(){++approval_calls;if(reenter){reenter=false;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0);}
#if defined(TEST_CURRENT_PREPARED)
 if(prepared_reenter){prepared_reenter=false;prepared_nested_result=current_sss_prepared_setup_begin();}
 switch(prepared_mutation){case 1:faulted=true;break;case 2:results=(void*)2;break;case 3:asset_generation++;break;case 4:selected_value++;break;case 5:host_pad.bytes[81]=1;break;case 6:replay->initial_css.reset();break;case 7:pending_load_count++;break;case 8:transport_busy=1;break;case 9:first_sss_vs_entry.attempted=true;break;case 10:net_active=true;break;case 11:host_phase=3;break;case 12:archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);match=std::make_unique<melee_web::GameplayMatchSession>(files,host_selection,*archive_cache,melee_web::GameplayMatchConstruction::Deferred,host_pad,melee_web::GameplayMatchDiagnostic::StadiumReady);break;case 13:host=nullptr;break;case 14:host_source_scene=1;break;default:break;}
#endif
 return approval;}
#define EM_ASM_INT(...) approve()
"""
TEST=r"""
static void reset(){match.reset();archive_cache.reset();setup_snapshot_ok=true;setup_snapshot_calls=0;first_sss_vs_setup={};first_sss_vs_entry={};first_sss_prefix_sequence={};first_sss_prefix_sequence.complete=true;world=std::make_unique<World>();source_world=true;generation=7;seed=17;seed_ptr=&seed;retirement_changes_seed=false;approval=1;reenter=false;getter_ok=true;getter_calls=0;selected_value=7;asset_destination=AssetDestination::None;asset_generation=0;asset_committed=false;asset_selection_valid=false;asset_scope={};files.clear();host_pad={};running=pending=faulted=host_entered=first_use_draw_pending=false;}
static void reset_text_probe(){first_sss_text_tail_probe={};first_sss_text_tail_retained_json.clear();text_probe_value=text_probe_sets=sis_capture_calls=membership_begin_calls=membership_end_calls=0;sis_capture_ok=membership_begin_ok=true;membership_snapshot_live=false;}
static void commit(){files={{"stage",{1}},{"fighter",{2}}};asset_scope.active=2;asset_scope.pending=0;asset_committed=true;}
std::string first_sss_vs_setup_current_json(){return "{}";}
static void close(){++gate2_fixture_close_calls;FirstSssTextTailCloseGuard guard{first_sss_text_tail_probe.opted_in};throw std::runtime_error("controlled close failure");}
int main(){
 assert_no_later_route_fixture();
 assert_gate2_unload_outside_fixture();
 // The legacy124 owner has completed its prefix; current270 has not entered it.
 reset();assert(first_sss_prefix_sequence.complete);
 first_sss_prefix_sequence.complete=false;
 assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0);
 assert(world&&getter_calls==0&&!first_sss_vs_setup.requested);
 reset();reset_text_probe();assert(!first_sss_vs_setup_service());assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);assert(text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);assert(!world&&first_sss_vs_setup.parent_seed_restored&&first_sss_vs_setup.getter_calls==1);assert(first_sss_vs_setup.input!=&host_pad&&first_sss_vs_setup.history=="{\"old\":true}");assert(melee_web_native_menu_stadium_c1a_asset_scope(1)==1);assert(melee_web_native_menu_stadium_c1a_asset_scope(2)==0);
 assert(first_sss_vs_setup_service()&&!match);commit();assert(first_sss_vs_setup_service()&&match&&!first_sss_vs_setup.complete);assert(setup_snapshot_calls==0);assert(first_sss_vs_setup_service()&&!first_sss_vs_setup.complete);assert(setup_snapshot_calls==0);assert(first_sss_vs_setup_service()&&first_sss_vs_setup.complete&&first_sss_vs_setup.captured);assert(setup_snapshot_calls==1&&first_sss_vs_setup.stadium_setup_state.find("zero-tick-setup-state-v1")!=std::string::npos);assert(first_sss_vs_setup.source_frames==0&&first_sss_vs_setup.host_tick_calls==0&&first_sss_vs_setup.host_draw_calls==0);assert(first_sss_vs_setup.actual.find("35a455b9")!=std::string::npos);assert(first_sss_vs_setup.match_world_generation!=first_sss_vs_setup.prior_world_generation);assert(text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);auto actual=first_sss_vs_setup.actual;auto steps=match->steps;assert(first_sss_vs_setup_service()&&match->steps==steps);assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&first_sss_vs_setup.actual==actual);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();first_sss_vs_setup_service();first_sss_vs_setup_service();setup_snapshot_ok=false;assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!first_sss_vs_setup.complete&&!first_sss_vs_setup.captured&&first_sss_vs_setup.actual.empty()&&first_sss_vs_setup.stadium_setup_state.empty()&&setup_snapshot_calls==1);assert(first_sss_vs_setup.error=="setup snapshot refused");
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);assert(first_sss_text_tail_probe.opted_in&&text_probe_sets==0);first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.complete&&first_sss_text_tail_probe.probe_enabled&&text_probe_value==1);assert(text_probe_sets==1&&sis_capture_calls==1&&membership_begin_calls==1&&membership_snapshot_live);assert(first_sss_text_tail_probe.sis_lease_captured&&first_sss_text_tail_probe.membership_snapshot_active);first_sss_vs_setup_fail("controlled setup failure");assert(text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active);assert(first_sss_text_tail_retained_json.find("melee-web-stadium-first-sss-text-tail-retained-v1")!=std::string::npos&&first_sss_text_tail_retained_json.find("cleanup_done\":true")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(2)==0);assert(first_sss_vs_setup.failed&&!first_sss_text_tail_probe.opted_in&&text_probe_sets==0&&sis_capture_calls==0&&membership_begin_calls==0);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(membership_snapshot_live);first_sss_text_tail_unload_refused("controlled unload refusal");assert(first_sss_text_tail_probe.unload_refused&&text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_retained_json.find("controlled unload refusal")!=std::string::npos);
 reset_text_probe();first_sss_text_tail_probe.opted_in=true;first_sss_text_tail_probe.probe_enabled=true;HSD_SisLib_C1TextProbeSet(1);first_sss_text_tail_probe.membership_snapshot_active=true;membership_snapshot_live=true;{FirstSssTextTailCloseGuard guard{true};}assert(text_probe_value==0&&!membership_snapshot_live&&membership_end_calls==1&&first_sss_text_tail_probe.cleanup_done);assert(first_sss_text_tail_retained_json.find("native-close-returned")!=std::string::npos);
 reset_text_probe();refresh_calls=0;first_sss_text_tail_probe.opted_in=true;first_sss_text_tail_probe.probe_enabled=true;HSD_SisLib_C1TextProbeSet(1);first_sss_text_tail_probe.membership_snapshot_active=true;membership_snapshot_live=true;assert(melee_web_native_menu_unload()==0);assert(message=="controlled close failure"&&refresh_calls==1);assert(first_sss_text_tail_probe.unload_refused&&first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active&&!membership_snapshot_live&&membership_end_calls==1);assert(first_sss_text_tail_retained_json.find("native-close-threw")!=std::string::npos&&first_sss_text_tail_retained_json.find("controlled close failure")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);sis_capture_ok=false;first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&!first_sss_vs_setup.complete);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active);assert(first_sss_text_tail_probe.error=="capture refused"&&first_sss_text_tail_retained_json.find("capture refused")!=std::string::npos);
 reset();reset_text_probe();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(1)==1);membership_begin_ok=false;first_sss_vs_setup_service();commit();first_sss_vs_setup_service();first_sss_vs_setup_service();first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&!first_sss_vs_setup.complete);assert(sis_capture_calls==1&&membership_begin_calls==1&&membership_end_calls==0);assert(first_sss_text_tail_probe.sis_lease_captured&&first_sss_text_tail_probe.membership_snapshot_attempted&&!first_sss_text_tail_probe.membership_snapshot_started);assert(first_sss_text_tail_probe.cleanup_done&&!first_sss_text_tail_probe.probe_enabled&&!first_sss_text_tail_probe.membership_snapshot_active&&!membership_snapshot_live&&text_probe_value==0);assert(first_sss_vs_setup.error=="membership refused"&&first_sss_text_tail_probe.error=="membership refused"&&first_sss_text_tail_retained_json.find("membership refused")!=std::string::npos);
 reset();approval=0;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==0&&first_sss_vs_setup.failed);
 reset();reenter=true;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==0);
 reset();selected_value=8;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&world&&getter_calls==1&&first_sss_vs_setup.getter_calls==1);
 reset();retirement_changes_seed=true;assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==0&&!world&&!first_sss_vs_setup.requested&&!first_sss_vs_setup.parent_seed_restored);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);host_pad.bytes[81]=1;commit();assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);assert(first_sss_vs_setup.error.find("PAD changed")!=std::string::npos);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);asset_scope.pending=2;assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);assert(melee_web_native_menu_stadium_c1a_asset_scope(1)==0);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();files.erase("stage");assert(first_sss_vs_setup_service()&&first_sss_vs_setup.failed&&!match);
 reset();assert(melee_web_native_menu_stadium_first_sss_vs_setup_begin(0)==1);commit();first_sss_vs_setup_service();match->frames=1;first_sss_vs_setup_service();assert(first_sss_vs_setup.failed&&first_sss_vs_setup.source_frames==1&&!first_sss_vs_setup.captured);
 assert(continuation_service_calls==0);
 assert(gate2_abort_ready_calls==1&&gate2_abort_calls==0);
 assert(!later_sss.armed&&completed_matches==0&&later_getter_calls==0&&later_context_calls==1);
 puts("PASS extracted legacy Setup source begin/classifier/handoff/service with controlled services; excludes new 270 admission");
}
"""
PREPARED_TEST=r"""
static unsigned native_close_calls=0;
static void close(){
 ++gate2_fixture_close_calls;
 ++native_close_calls;
 if(close_should_fail)throw std::runtime_error("controlled native close failure");
 if(world){world->close();world.reset();}
 source_world=false;host=nullptr;match.reset();archive_cache.reset();
 results=prize=nullptr;preparation.reset();running=pending=false;
 asset_destination=AssetDestination::None;asset_scope.pending=0;
}
static void reset_prepared(){
 match.reset();archive_cache.reset();world.reset();files.clear();requested_assets.clear();
 first_sss_vs_setup=FirstSssVsSetupState{};matched_setup_cf=MatchedSetupCfState{};
 current_sss_exit=CurrentSssExitState{};first_sss_text_tail_probe=FirstSssTextTailProbeState{};
 first_sss_text_tail_retained_json.clear();first_sss_vs_entry=Entry{};
 first_sss_prefix_sequence=Prefix{};first_sss_text_tail_retained_json.clear();
 source_world=true;generation=7;seed=0xe2624ac3;foreign_seed=19;seed_ptr=&seed;
 running=pending=faulted=host_entered=menu_scene_rebuild_pending=false;
 first_use_draw_pending=false;world_exposed=true;stadium_c1a_armed=false;
 scoped_assets=scoped_disc_import=source_session_owned=true;
 host=(void*)1;results=prize=nullptr;net_active=false;host_source_scene=0;host_phase=5;
 host_pad={};host_selection=MeleeWebMenuMatchSelection{};host_selection.random_seed=seed;selected_value=7;getter_ok=true;getter_calls=0;
 preparation.reset();preparation.p=melee_web::MenuPreparationState::Phase::WaitingForAudio;
 replay=std::make_unique<Recipe>();replay->seed=0x312151c3;
 replay->selection=host_selection;replay->selection.random_seed=replay->seed;
 world=std::make_unique<World>();archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 asset_destination=AssetDestination::None;asset_committed=false;asset_selection_valid=false;
 asset_generation=4;asset_selection=MeleeWebMenuMatchSelection{};asset_scope={};
 current_sss_asset_request_calls=0;replay_cursor=270;replay_source_frames={};
 pending_load_count=0;transport_active=transport_configured=1;transport_busy=0;
 diagnostic_audio_render_calls=274;diagnostic_audio_render_frames=146133;
 constructed_selection_seed=0;
 first_sss_vs_setup.origin=FirstSssVsSetupOrigin::LegacyEntry124;
 seed_ptr=&seed;retirement_changes_seed=false;close_calls=0;native_close_calls=0;
 close_should_fail=true;approval=1;approval_calls=0;reenter=false;
 prepared_reenter=false;prepared_mutation=0;prepared_nested_result=99;
 message.clear();refresh_calls=0;running=pending=false;

 current_sss_exit.armed=true;current_sss_exit.profile=CurrentSssExitProfile::Stock4None;
 current_sss_exit.terminal=true;current_sss_exit.recipe=replay.get();
 current_sss_exit.context=replay->initial_css.get();current_sss_exit.host_owner=host;
 current_sss_exit.leave_world=world.get();current_sss_exit.leave_cache=archive_cache.get();
 current_sss_exit.leave_world_generation=7;current_sss_exit.leave_asset_generation=asset_generation;
 current_sss_exit.leave_asset_requests=0;current_sss_exit.terminal_cursor=270;
 current_sss_exit.notification_cursor=270;current_sss_exit.notification_order=543;
 current_sss_exit.observation.state=2;current_sss_exit.observation.note.captured=true;
 current_sss_exit.observation.note.world_generation=7;current_sss_exit.notified=true;
 current_sss_exit.last_host_tick_returned=true;current_sss_exit.host_tick_in_flight=false;
 current_sss_exit.prepared_start_attempted=true;current_sss_exit.prepared_start_captured=true;
 current_sss_exit.prepared_start_selection_captured=true;
 current_sss_exit.prepared_start_selection=host_selection;
 current_sss_exit.prepared_start_json="{\"prepared\":true}";
 current_sss_exit.prepared_start_input_owner=&host_pad;
 assert(melee_web_pad_state_encode(&host_pad,current_sss_exit.prepared_start_input_bytes.data(),
       current_sss_exit.prepared_start_input_bytes.size()));
}
static void assert_no_destructive_handoff(){
 assert(close_calls==0&&native_close_calls==0&&current_sss_asset_request_calls==0);
 assert(asset_destination==AssetDestination::None&&!first_sss_vs_setup.attempted);
 assert(world&&source_world&&preparation.waiting_for_audio());
 assert(asset_scope.pending_generation()==0&&requested_assets.empty());
}
static void commit_prepared_assets(){
 files={{"stage",{1}},{"fighter",{2}}};asset_scope.active=2;
 asset_scope.pending=0;asset_committed=true;
}
int main(){
 assert_no_later_route_fixture();
 assert_gate2_unload_outside_fixture();
 reset_prepared();
 assert(current_sss_prepared_setup_begin()==1);
 assert(approval_calls==1&&current_sss_exit.prepared_setup_approved&&
        current_sss_exit.prepared_setup_consumed&&!current_sss_exit.prepared_setup_failed);
 assert(close_calls==1&&current_sss_asset_request_calls==1&&first_sss_vs_setup.requested);
 assert(first_sss_vs_setup.origin==FirstSssVsSetupOrigin::CurrentPreparedStock4_270);
 assert(!world&&!source_world&&!preparation.waiting_for_audio());
 assert(replay_cursor==270&&replay_source_frames.steps()==0&&replay_source_frames.draws()==0);
 assert(melee_web_native_menu_stadium_c1a_asset_scope(asset_generation)==1);
 commit_prepared_assets();assert(first_sss_vs_setup_service());
 assert(match&&first_sss_vs_setup.import_committed&&!first_sss_vs_setup.complete);
 assert(constructed_selection_seed==0xe2624ac3&&
        constructed_selection_seed!=replay->seed);
 assert(first_sss_vs_setup_service()&&!first_sss_vs_setup.complete);
 assert(first_sss_vs_setup_service()&&first_sss_vs_setup.complete&&first_sss_vs_setup.captured);
 assert(first_sss_vs_setup.source_frames==0&&first_sss_vs_setup.host_tick_calls==0&&
        first_sss_vs_setup.host_draw_calls==0&&setup_snapshot_calls==1);
 assert(first_sss_vs_setup.stadium_setup_state.find("zero-tick-setup-state-v1")!=std::string::npos);
 close_should_fail=false;assert(melee_web_native_menu_unload()==1);
 assert(native_close_calls==1&&!match&&!archive_cache&&!world&&!source_world&&!host);

 reset_prepared();current_sss_exit.terminal_cursor=269;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==0);assert_no_destructive_handoff();
 reset_prepared();current_sss_exit.host_owner=(void*)2;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==0);assert_no_destructive_handoff();
 reset_prepared();pending_load_count=1;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);assert_no_destructive_handoff();

 // The v8 recipe seed belongs to first CSS. Keep the post-SSS accepted seed
 // distinct and verify the existing construction receives the live one.
 reset_prepared();assert(replay->seed==0x312151c3&&
        replay->selection.random_seed==replay->seed&&
        current_sss_exit.prepared_start_selection.random_seed==0xe2624ac3);
 assert(current_sss_prepared_setup_begin()==1&&asset_selection.random_seed==0xe2624ac3);
 assert(close_calls==1&&current_sss_asset_request_calls==1&&
        asset_destination==AssetDestination::FirstSssVsSetup);

 reset_prepared();host_selection.random_seed++;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);
 assert(current_sss_exit.prepared_setup_error.find("live-vs-accepted selection differs at random_seed")!=std::string::npos);
 assert(current_sss_exit.prepared_setup_error.find("recipe_initial_css=0x312151c3")!=std::string::npos);
 assert_no_destructive_handoff();

 reset_prepared();replay->selection.players[0].stocks++;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);
 assert(current_sss_exit.prepared_setup_error.find("live-vs-recipe configuration differs at players[0].stocks")!=std::string::npos);
 assert_no_destructive_handoff();

 reset_prepared();selected_value++;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);
 assert(current_sss_exit.prepared_setup_error.find("live-vs-accepted selection differs at start serialized source payload differs")!=std::string::npos);
 assert_no_destructive_handoff();

 reset_prepared();prepared_reenter=true;
 assert(current_sss_prepared_setup_begin()==0&&prepared_nested_result==0&&approval_calls==1);
 assert(current_sss_exit.prepared_setup_failed&&
        current_sss_exit.prepared_setup_error=="Prepared Setup handoff is one-use");
 assert_no_destructive_handoff();

 for(int mutation=1;mutation<=14;++mutation){
  reset_prepared();prepared_mutation=mutation;
  assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);
  assert(current_sss_exit.prepared_setup_failed&&!current_sss_exit.prepared_setup_error.empty());
  assert_no_destructive_handoff();
 }

 reset_prepared();retirement_changes_seed=true;
 assert(current_sss_prepared_setup_begin()==0&&approval_calls==1);
 assert(close_calls==1&&!world&&!source_world&&seed_ptr==&foreign_seed);
 assert(current_sss_asset_request_calls==0&&!first_sss_vs_setup.requested&&
        first_sss_vs_setup.failed&&current_sss_exit.prepared_setup_failed);

 reset_prepared();assert(current_sss_prepared_setup_begin()==1);
 assert(melee_web_native_menu_stadium_c1a_asset_scope(asset_generation)==1);
 selected_value=8;commit_prepared_assets();assert(first_sss_vs_setup_service());
 assert(first_sss_vs_setup.failed&&!match&&first_sss_vs_setup.error.find("selection changed")!=std::string::npos);
 reset_prepared();assert(current_sss_prepared_setup_begin()==1);
 assert(melee_web_native_menu_stadium_c1a_asset_scope(asset_generation)==1);
 host_pad.bytes[81]=1;commit_prepared_assets();assert(first_sss_vs_setup_service());
 assert(first_sss_vs_setup.failed&&!match&&first_sss_vs_setup.error.find("PAD changed")!=std::string::npos);

 assert(gate2_abort_ready_calls==1&&gate2_abort_calls==0);
 assert(!later_sss.armed&&completed_matches==0&&later_getter_calls==0&&later_context_calls==1);
 puts("PASS extracted current 270 prepared Setup source admission, approval rechecks, asset handoff and controlled unload; legacy 124 remains separate");
}
"""
def unarmed_later_asset_fixture(source):
  return '#include "'+str(ROOT/'src/stadium_later_reference.hpp')+'"\n'+r"""
// Gate2 close/abort is outside this Legacy124/Current270 fixture. These are
// rejecting leaves, not stand-ins for the separately controlled native close.
static unsigned gate2_abort_ready_calls=0,gate2_abort_calls=0;
static bool later_b_gate2_unarmed_abort_ready(const MatchedSetupCfState&){
 ++gate2_abort_ready_calls;
 throw std::runtime_error("Gate2 abort is outside this legacy/current Setup fixture");
}
static int later_b_gate2_unarmed_abort(){
 ++gate2_abort_calls;
 throw std::runtime_error("Gate2 close is outside this legacy/current Setup fixture");
}
static void assert_gate2_unload_outside_fixture(){
 assert(!matched_setup_cf.later_b_gate2_attempted&&gate2_abort_ready_calls==0&&gate2_abort_calls==0);
 const auto closes_before=gate2_fixture_close_calls;
 matched_setup_cf.later_b_gate2_attempted=true;
 assert(melee_web_native_menu_unload()==0);
 assert(message=="Gate2 abort is outside this legacy/current Setup fixture");
 assert(gate2_abort_ready_calls==1&&gate2_abort_calls==0&&gate2_fixture_close_calls==closes_before);
 matched_setup_cf=MatchedSetupCfState{};
 assert(!matched_setup_cf.later_b_gate2_attempted);
}
// Legacy/current Setup only: no later route is armed in these fixtures.
struct {bool armed=false;unsigned stage=0,requested_generation=0;
const melee_web::stadium_later_reference::Context* reference=&melee_web::stadium_later_reference::legacy;} later_sss;
static const auto& later_sss_reference_context(){
 if(later_sss.armed)throw std::runtime_error("later descriptor outside legacy/current fixture");
 return melee_web::stadium_later_reference::legacy;
}
template<class State>static bool later_sss_notification_order_owned(const State&){
 throw std::runtime_error("later order outside legacy/current fixture");
}
static unsigned later_context_calls=0;
// No later owner exists in Legacy124 or Current270 fixtures. These leaves
// fail closed if an extracted source branch accidentally selects that route.
static unsigned completed_matches=0,later_getter_calls=0;
static int melee_web_menu_host_later_sss_prepared_selection(
    void*,MeleeWebMenuMatchSelection*,char*,unsigned){
 ++later_getter_calls;
 throw std::runtime_error("later selection is outside this legacy/current Setup fixture");
}
static bool later_sss_context_owned(){
 ++later_context_calls;
 throw std::runtime_error("later owner service is outside this Setup fixture");
}
"""+_function(source,'bool later_sss_asset_pending(unsigned generation)')+r"""
static void assert_no_later_route_fixture(){
 assert(!later_sss.armed&&completed_matches==0&&later_getter_calls==0&&
        !later_sss_asset_pending(0)&&!later_sss_asset_pending(1));
 assert(later_context_calls==0);
 later_sss.armed=true;later_sss.stage=1;bool refused=false;
 try{(void)later_sss_asset_pending(1);}catch(const std::runtime_error&){refused=true;}
 assert(refused&&later_context_calls==1);later_sss={};
 assert(!later_sss_asset_pending(1)&&later_context_calls==1);
}
"""

class FirstSssVsSetupEndpointTests(OwnedWorkspaceTests):
 # These controls execute extracted native source functions against controlled
 # services; they do not execute the browser runtime.
 def test_extracted_legacy_setup_source_control_and_refusals(self):
  source=(ROOT/'src/gameplay_menu_browser.cpp').read_text();menu=(ROOT/'src/gameplay_menu_browser_menu.cpp').read_text()
  a=source.index('enum class FirstSssVsSetupOrigin : unsigned');b=source.index('std::string first_sss_vs_setup_json(){',a)
  body=PRELUDE+source[a:b]
  for name in ('const char* later_b_gate2_constructor_phase_name(uint32_t phase)',
               'const char* later_b_gate2_constructor_phase_scalar_name(uint32_t phase)',
               'std::string later_b_gate2_constructor_phase_trace_json(const MeleeWebStadiumSetupSnapshot& setup)'):
   body+=_function(source,name)+'\n'
  body+=unarmed_later_asset_fixture(source)
  body+=_function(source,'std::string current_sss_selection_difference(const MeleeWebMenuMatchSelection& a,')
  body+=_function(source,'bool current_sss_selection_equal(const MeleeWebMenuMatchSelection& a,')
  for name in ['bool first_sss_vs_setup_asset_pending(unsigned generation)','bool first_sss_vs_setup_finish_handoff()']:
   body+=_function(source,name)
  body+=_function(menu,'int melee_web_native_menu_stadium_c1a_asset_scope(unsigned generation)')
  body+='bool finish_asset_handoff(){asset_destination=AssetDestination::None;asset_committed=false;return first_sss_vs_setup_finish_handoff();}'
  body+=_function(source,'void first_sss_vs_setup_fail(const char* error)')
  body+=_function(source,'bool first_sss_vs_setup_service()')+_function(source,'int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe)')+_function(source,'int melee_web_native_menu_unload()')+TEST
  w=self.new_workspace(ROOT,'first-sss-vs-setup-');cpp=w/'control.cpp';exe=w/'control';cpp.write_text(body)
  argv=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
  r=subprocess.run(argv,capture_output=True,text=True);(w/'compile.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(exe)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_extracted_current_prepared_setup_admission_rechecks(self):
  source=(ROOT/'src/gameplay_menu_browser.cpp').read_text();menu=(ROOT/'src/gameplay_menu_browser_menu.cpp').read_text()
  a=source.index('enum class FirstSssVsSetupOrigin : unsigned');b=source.index('std::string first_sss_vs_setup_json(){',a)
  body='#define TEST_CURRENT_PREPARED 1\n'+PRELUDE+source[a:b]
  for name in ('const char* later_b_gate2_constructor_phase_name(uint32_t phase)',
               'const char* later_b_gate2_constructor_phase_scalar_name(uint32_t phase)',
               'std::string later_b_gate2_constructor_phase_trace_json(const MeleeWebStadiumSetupSnapshot& setup)'):
   body+=_function(source,name)+'\n'
  body+=unarmed_later_asset_fixture(source)
  body+=_function(source,'std::string current_sss_selection_difference(const MeleeWebMenuMatchSelection& a,')
  body+=_function(source,'bool current_sss_selection_equal(const MeleeWebMenuMatchSelection& a,')
  for name in ['bool first_sss_vs_setup_asset_pending(unsigned generation)','bool first_sss_vs_setup_finish_handoff()']:
   body+=_function(source,name)
  body+=_function(menu,'int melee_web_native_menu_stadium_c1a_asset_scope(unsigned generation)')
  body+='bool finish_asset_handoff(){asset_destination=AssetDestination::None;asset_committed=false;return first_sss_vs_setup_finish_handoff();}'
  body+=_function(source,'void first_sss_vs_setup_fail(const char* error)')
  body+=_function(source,'bool first_sss_vs_setup_service()')
  body+=_function(source,'int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe)')
  body+=_function(source,'int current_sss_prepared_setup_begin()')
  body+=_function(source,'int melee_web_native_menu_unload()')+PREPARED_TEST
  w=self.new_workspace(ROOT,'current-sss-prepared-setup-');cpp=w/'control.cpp';exe=w/'control';cpp.write_text(body)
  argv=shlex.split(os.environ.get('CXX','c++'))+['-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)]
  r=subprocess.run(argv,capture_output=True,text=True);(w/'compile.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  r=subprocess.run([str(exe)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
