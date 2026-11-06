/* SPDX-License-Identifier: GPL-3.0-or-later
 * Private terminal ordinary-runtime integration. Included after the ordinary
 * audio/close helpers, inside melee_web_menu_browser. No fixture driver. */
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC)
using melee_web::RuntimeMatchHistory;
using melee_web::GameplayMatchSession;
using melee_web::RuntimeArchiveCache;
namespace {
struct MatchHistoryStatus {
 uint32_t schema=1,bytes=128,owner_state{},first_failure{},pending_command{},last_completed_command{};
 uint32_t request_seq{},complete_seq{},expected_frame{},local_port{},remote_port{},reserved{};
 melee_web::RuntimeMatchHistoryMetadata history{};
 uint32_t input_ready{},gpu_pending{},endpoint_active{},renderer_errors{};
};
/* Copied immediately through the private pacing endpoint. Offsets are fixed:
 * schema/bytes 0/4, phase/frame/steps/pending/stall/reason 8..28,
 * elapsed/debt/trigger/threshold 32/40/48/56. */
struct MatchHistoryPacingSample {
 uint32_t schema=1,bytes=64,phase{},current_frame{},steps{},pending_steps{},stalled{},stall_reason{};
 double elapsed_ms{},pending_ticks{},triggering_value{},threshold{};
};
static_assert(sizeof(MatchHistoryPacingSample)==64);
static_assert(offsetof(MatchHistoryPacingSample,phase)==8);
static_assert(offsetof(MatchHistoryPacingSample,current_frame)==12);
static_assert(offsetof(MatchHistoryPacingSample,steps)==16);
static_assert(offsetof(MatchHistoryPacingSample,pending_steps)==20);
static_assert(offsetof(MatchHistoryPacingSample,stalled)==24);
static_assert(offsetof(MatchHistoryPacingSample,stall_reason)==28);
static_assert(offsetof(MatchHistoryPacingSample,elapsed_ms)==32);
static_assert(offsetof(MatchHistoryPacingSample,pending_ticks)==40);
static_assert(offsetof(MatchHistoryPacingSample,triggering_value)==48);
static_assert(offsetof(MatchHistoryPacingSample,threshold)==56);
struct MatchHistoryInput {
 uint32_t schema=1,bytes=168,input_ready{},corrected_ready{};
 uint64_t poll_serial{};
 uint32_t local_port{},remote_port{};
 uint8_t raw[44]{},forward[44]{},corrected[44]{};
 uint32_t reserved{};
};
using MatchHistoryPollTicket=melee_web::RuntimeMatchHistoryPollTicket;
using MatchHistoryEpisodeInputRow=melee_web::RuntimeMatchHistoryInputReceipt;
static_assert(sizeof(MatchHistoryStatus)==128);
static_assert(sizeof(MatchHistoryInput)==168);
static_assert(sizeof(MatchHistoryPollTicket)==88);
static_assert(sizeof(MatchHistoryEpisodeInputRow)==176);
static_assert(sizeof(MeleeWebNetChecksumRecord)==64);
enum MatchHistoryStartState : uint32_t {
 MatchHistoryStartDisabled=0,
 MatchHistoryStartEnabled=1,
 MatchHistoryStartOfferReady=2,
 MatchHistoryStartAgreed=3,
 MatchHistoryStartConsumed=4,
 MatchHistoryStartReadyHeld=5,
 MatchHistoryStartPublishing=6,
};
struct MatchHistoryPublication {
 uint32_t schema=2,bytes=48,state{},source_frame{},source_draws{},pad_policy=1,first_use_pending{},reserved{};
 uint64_t prior_submission{},source_submission{};
};
static_assert(sizeof(MatchHistoryPublication)==48);
constexpr size_t kMatchHistoryStartPrefixLimit=1024; // Private probe storage limit, independent of GO timing.
struct MatchHistoryStartTick {
 uint64_t poll_serial{},draw_submission{};
 uint32_t tick_before{},tick_after{},source_frame_before{},source_frame_after{};
 bool hud_ready_before{},hud_ready_after{};
 std::array<uint8_t,44> pad{};
};
struct MatchHistoryStart {
 uint32_t state{};
 MatchHistoryPublication publication{};
 double settle_started{};
 bool prefix_final_drawn{};
 unsigned initial_resident_transfer_drains{},final_resident_transfer_drains{};
 std::vector<MatchHistoryStartTick> prefix;
 std::string prefix_json;
 uint32_t local_menu_seed{};
 uint32_t negotiated_seed{};
 uint32_t reserved{};
 uint64_t source_asset_generation{};
 uint64_t handoff_asset_generation{};
 MeleeWebMenuHost* enabled_host{};
 MeleeWebMenuHost* source_host{};
 GameplayMatchSession* match_owner{};
 MeleeWebMenuMatchSelection local_selection{};
 MeleeWebMenuMatchSelection agreed_selection{};
 bool agreement_candidate_ready{};
 std::string local_payload;
 std::string offer_json;
 std::string agreement_json;
 std::string live_identity_json;
 std::string runtime_sha256;
 std::string asset_identity_sha256;
 std::string save_profile_sha256;
 std::string profile_identity;
};
struct MatchHistoryControl {
 MatchHistoryStatus status{};
 MatchHistoryInput input{};
 MatchHistoryStart start{};
 std::array<char,512> error{};
 std::unique_ptr<RuntimeMatchHistory> capsule;
 melee_web::RuntimeMatchHistoryPrefixHandoff prefix_handoff{};
 std::unique_ptr<melee_web::GameplayCpuStatics> statics;
 std::vector<melee_web::CpuStaticRow> configured_rows;
 MeleeWebMenuMatchSelection selection{};
 GameplayMatchSession* match_owner{};
 RuntimeArchiveCache* archive_owner{};
 MeleeWebMenuHost* host_owner{};
 uint64_t asset_owner{};
 uint64_t submitted_frame{},cache_flush_frame{};
 uint64_t presentation_serial{};
 uint32_t presentation_command{},presentation_request_sequence{};
 std::array<char,2048> renderer_preparation{};
 std::array<char,512> presentation_json{};
 std::array<char,1024> prefix_handoff_json{};
 MatchHistoryPacingSample pacing{};
 bool pacing_started{},pacing_failed{};
 uint32_t requested_rebase_end{};
 uint32_t requested_ticket_id{};
 std::array<uint8_t,44> requested_plan{};
 std::array<uint8_t,11> remote{};
 bool remote_ready{},dispatching{},identity_ready{},episode_enabled{},arena_enabled{},inline_dispatch{};
 melee_web::GameplayCpuArenaEndpoint arena_endpoint{};
 std::string identity;
 std::array<std::string,4> identity_host;
 melee_web::GameplayCpuEndpointRecord record{};
 SlippiParticleCellEndpointHeader particle_header{};
 SlippiParticleCellEndpointRow particle_row{};
 MeleeWebCpuMotorEvidence motor{};
 MeleeWebCpuMotorCommitReceipt motor_commit{};
 MeleeWebNetChecksumRecord checksum{};
} match_history;
extern "C" char __global_base, __data_end;

bool history_refuse(const char* text) noexcept {
 auto& c=match_history;
 if(!c.status.first_failure){
  c.status.first_failure=1;
  std::snprintf(c.error.data(),c.error.size(),"%s",text?text:"Private match history refused");
 }
 return false;
}

bool history_runtime_idle() noexcept;
const char* history_gpu_idle_failure() noexcept;
const char* history_source_idle_failure(const GameplayMatchSession&) noexcept;
bool history_gpu_idle() noexcept;
bool history_source_idle(const GameplayMatchSession&) noexcept;
void history_encode_pad(uint8_t*,const PADStatus&) noexcept;

bool history_start_export_idle() noexcept {
 return !match_history_in_tick&&!match_history.dispatching&&
  !match_history.status.pending_command&&history_runtime_idle();
}

void history_write_selection(std::ostringstream& out,
                             const MeleeWebMenuMatchSelection& selection) {
 out<<"{\"player_count\":"<<selection.player_count
    <<",\"random_seed\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(selection.random_seed)<<"\""
    <<",\"hud_layout\":"<<selection.hud_layout
    <<",\"unlocked_characters\":"<<selection.unlocked_characters
    <<",\"unlocked_stages\":"<<selection.unlocked_stages
    <<",\"save_profile_present\":"<<unsigned(selection.save_profile_present)
    <<",\"opening_demo\":"<<unsigned(selection.opening_demo)
    <<",\"sudden_death\":"<<unsigned(selection.sudden_death)
    <<",\"compatibility_players\":[";
 for(unsigned index=0;index<4;++index){
  if(index)out<<',';
  const auto& player=selection.players[index];
  out<<"{\"controller\":"<<player.controller
     <<",\"stocks\":"<<player.stocks
     <<",\"costume\":"<<player.costume
     <<",\"sub_color\":"<<player.sub_color<<'}';
 }
 out<<"],\"start\":";
 melee_web::stadium_first_css_diagnostic::write_start(out,selection.start);
 out<<'}';
}

std::string history_start_payload(const MeleeWebMenuMatchSelection& selection) {
 using namespace melee_web::slippi_profile::detail;
 validate_pointer_free_start(selection.start.rules);
 validate_ordinary_mario_fd_stock_start(selection);
 std::ostringstream out;
 out.imbue(std::locale::classic());
 history_write_selection(out,selection);
 return out.str();
}

std::string history_start_offer_json(const MeleeWebMenuMatchSelection& selection,
    const melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement& host,
    uint32_t local_menu_seed) {
 using namespace melee_web::slippi_profile::detail;
 validate_host_agreement(host);
 std::ostringstream out;
 out.imbue(std::locale::classic());
 out<<"{\"schema\":\"melee-web-runtime-match-start-offer-v1\""
    <<",\"runtime_sha256\":\""<<host.runtime_sha256<<'\"'
    <<",\"asset_identity_sha256\":\""<<host.asset_identity_sha256<<'\"'
    <<",\"save_profile_sha256\":\""<<host.save_profile_sha256<<'\"'
    <<",\"profile_identity\":\""<<host.profile_identity<<'\"'
    <<",\"local_menu_rng_seed_provenance\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(local_menu_seed)<<"\""
    <<",\"selection\":";
 history_write_selection(out,selection);
 out<<'}';
 return out.str();
}

std::string history_start_agreement_json(const MeleeWebMenuMatchSelection& source,
    uint32_t negotiated_seed,
    const melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement& host) {
 using namespace melee_web::slippi_profile::detail;
 validate_host_agreement(host);
 auto agreed=source;
 agreed.random_seed=negotiated_seed;
 std::ostringstream out;
 out.imbue(std::locale::classic());
 out<<"{\"schema\":\"melee-web-runtime-match-start-agreement-v1\""
    <<",\"runtime_sha256\":\""<<host.runtime_sha256<<'\"'
    <<",\"asset_identity_sha256\":\""<<host.asset_identity_sha256<<'\"'
    <<",\"save_profile_sha256\":\""<<host.save_profile_sha256<<'\"'
    <<",\"profile_identity\":\""<<host.profile_identity<<'\"'
    <<",\"expected_match_source_frame\":0"
    <<",\"initial_selection_seed\":\""
    <<melee_web::stadium_first_css_diagnostic::hex32(negotiated_seed)<<"\""
    <<",\"pad_initialization_policy\":\"source-default-match-pad-v1\""
    <<",\"selection\":";
 history_write_selection(out,agreed);
 out<<'}';
 return out.str();
}

std::string_view history_start_string_argument(const char* pointer,size_t bytes) {
 const uintptr_t begin=reinterpret_cast<uintptr_t>(pointer);
 if(!pointer||!bytes||bytes>65536||begin+uint64_t(bytes)+1>emscripten_get_heap_size()||
    begin+uint64_t(bytes)+1<=begin||pointer[bytes]!=0)
  throw std::runtime_error("Agreed start identity extent refused");
 return {pointer,bytes};
}

size_t history_start_c_string_length(const char* pointer,size_t maximum) {
 const uintptr_t begin=reinterpret_cast<uintptr_t>(pointer);
 const uintptr_t heap=emscripten_get_heap_size();
 if(!pointer||begin>=heap||begin+1<=begin)
  throw std::runtime_error("Match-start host identity string extent refused");
 const size_t available=static_cast<size_t>(heap-begin);
 const size_t bounded=std::min(maximum,available-1);
 for(size_t length=0;length<=bounded;++length)if(pointer[length]==0)return length;
 throw std::runtime_error("Match-start host identity string is unterminated");
}
melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement
history_start_host_agreement(const char* runtime,const char* assets,
                            const char* save,const char* profile) {
 using melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement;
 const auto runtime_length=history_start_c_string_length(runtime,64);
 const auto assets_length=history_start_c_string_length(assets,64);
 const auto save_length=history_start_c_string_length(save,64);
 const auto profile_length=history_start_c_string_length(
     profile,sizeof(SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID)-1);
 return {std::string_view(runtime,runtime_length),
         std::string_view(assets,assets_length),
         std::string_view(save,save_length),
         std::string_view(profile,profile_length)};
}

void history_start_bind_host_agreement(
    const melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement& host) {
 using namespace melee_web::slippi_profile::detail;
 validate_host_agreement(host);
 auto& start=match_history.start;
 if(start.runtime_sha256.empty()) {
  start.runtime_sha256=host.runtime_sha256;
  start.asset_identity_sha256=host.asset_identity_sha256;
  start.save_profile_sha256=host.save_profile_sha256;
  start.profile_identity=host.profile_identity;
  return;
 }
 require(host.runtime_sha256==start.runtime_sha256&&
         host.asset_identity_sha256==start.asset_identity_sha256&&
         host.save_profile_sha256==start.save_profile_sha256&&
         host.profile_identity==start.profile_identity,
         "Match-start host identity changed during the one-shot agreement");
}

melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement
history_start_bound_host_agreement() {
 auto& start=match_history.start;
 return {start.runtime_sha256,start.asset_identity_sha256,
         start.save_profile_sha256,start.profile_identity};
}

bool history_start_source_stable(bool source_generation) {
 auto& start=match_history.start;
 if(!start.source_host||host!=start.source_host||host_entered||world||match||results||prize||
    replay||melee_web_net_active()||faulted||running||pending||
    preparation.phase()!=melee_web::MenuPreparationState::Phase::Constructing||
    asset_destination!=AssetDestination::None||
    melee_web_menu_host_mode_kind(host)!=GM_VS||
    melee_web_menu_host_phase(host)!=MELEE_WEB_MENU_READY||
    (source_generation&&asset_generation!=start.source_asset_generation))
  return false;
 MeleeWebMenuMatchSelection current{};char error[256]{};
 if(!melee_web_menu_host_selection(host,&current,error,sizeof(error)))return false;
 if(current.random_seed!=start.local_menu_seed||
    history_start_payload(current)!=start.local_payload)return false;
 return true;
}

bool history_start_capture_offer_impl(const MeleeWebMenuMatchSelection& selection) {
 auto& start=match_history.start;
 if(start.state!=MatchHistoryStartEnabled||!host||host!=start.enabled_host||
    host_entered||melee_web_menu_host_mode_kind(host)!=GM_VS||
    melee_web_menu_host_phase(host)!=MELEE_WEB_MENU_READY||
    replay||melee_web_net_active()||faulted||match||results||prize||
    pending||running||
    preparation.phase()!=melee_web::MenuPreparationState::Phase::Constructing||
    asset_destination!=AssetDestination::None)
  return history_refuse("Original-menu agreed-start offer owner refused");
 try {
  start.local_selection=selection;
  start.local_menu_seed=selection.random_seed;
  start.source_host=host;
  start.source_asset_generation=asset_generation;
  start.local_payload=history_start_payload(selection);
  start.state=MatchHistoryStartOfferReady;
  running=false;pending=false;menu_clock.reset();audio_clock.reset();
  return true;
 }catch(const std::exception& error){return history_refuse(error.what());}
}

bool history_start_finish_renderer_persistence() {
 const auto status=aurora_pipeline_cache_status();
 if(status==AURORA_PIPELINE_CACHE_READY)return true;
 if(status!=AURORA_PIPELINE_CACHE_PENDING)
  return history_refuse("Agreed source publication renderer cache persistence is unavailable");

 // The strict descriptor requires a drained cache transaction as well as an
 // idle GPU. Ordinary play defers writes. This private callback already owns
 // the stopped, prepared match and can finish that transaction before asking
 // Aurora for the same strict descriptor used by terminal takeover.
 const auto* owner=match_history.start.match_owner;
 if(!owner||match.get()!=owner||!owner->construction_complete()||owner->source_frames()!=0||
    pending||running||faulted||melee_web_cpu_interval_active()||!history_runtime_idle())
  return history_refuse("Agreed renderer cache flush requires the stopped frame-zero owner");
 if(const auto* error=history_source_idle_failure(*owner))return history_refuse(error);
 const auto submitted=aurora_browser_submission_status();
 const auto* stats=aurora_get_stats();
 if(submitted.pendingFramePackets||submitted.pendingStagingBuffers||submitted.workerBusy||
    !stats||stats->queuedPipelines||stats->lastBeginFrameId!=stats->lastEndFrameId||
    match_history_renderer_errors)
  return history_refuse("Agreed renderer cache flush requires idle submission and pipelines");
 if(!aurora_flush_pipeline_cache()||aurora_pipeline_cache_status()!=AURORA_PIPELINE_CACHE_READY)
  return history_refuse("Agreed source publication renderer cache flush failed");
 if(match.get()!=owner||!history_runtime_idle())
  return history_refuse("Agreed renderer cache flush lost the frame-zero runtime owner");
 if(const auto* error=history_source_idle_failure(*owner))return history_refuse(error);
 return true;
}

std::string history_start_serialize_prefix(const MatchHistoryStart& start) {
 std::ostringstream out;
 out.imbue(std::locale::classic());
 out<<"{\"schema\":\"melee-web-runtime-match-start-prefix-v1\","
    <<"\"input_provenance\":\"ordinary-local-four-port-pad-v1\","
    <<"\"source_ticks\":"<<start.prefix.size()
    <<",\"source_draws\":"<<start.publication.source_draws
    <<",\"initial_resident_transfer_drains\":"<<start.initial_resident_transfer_drains
    <<",\"final_resident_transfer_drains\":"<<start.final_resident_transfer_drains
    <<",\"final_ready_tick\":"<<start.prefix.back().tick_after<<",\"rows\":[";
 const char hex[]="0123456789abcdef";
 for(const auto& row:start.prefix){
  if(&row!=start.prefix.data())out<<',';
  out<<"{\"poll_serial\":\""<<row.poll_serial<<"\",\"tick_before\":"<<row.tick_before
     <<",\"tick_after\":"<<row.tick_after<<",\"source_frame_before\":"<<row.source_frame_before
     <<",\"source_frame_after\":"<<row.source_frame_after
     <<",\"hud_ready_before\":"<<(row.hud_ready_before?"true":"false")
     <<",\"hud_ready_after\":"<<(row.hud_ready_after?"true":"false")<<",\"pad_hex\":\"";
  for(const auto byte:row.pad)out<<hex[byte>>4]<<hex[byte&15];
  out<<"\",\"draw_submission\":\""<<row.draw_submission<<"\"}";
 }
 out<<"],\"live_identity\":"<<start.live_identity_json<<'}';
 return out.str();
}

bool history_start_enter_ready_hold_impl() {
 auto& start=match_history.start;
 if(start.state!=MatchHistoryStartPublishing||!start.prefix_final_drawn||start.prefix.empty()||
    start.publication.source_draws!=start.prefix.size()||first_use_draw_pending||
    !start.match_owner||
    match.get()!=start.match_owner||!match->construction_complete()||
    !match->ready()||match->source_frames()!=0||match->start_selection().random_seed!=start.negotiated_seed||
    history_start_payload(match->start_selection())!=
       history_start_payload(start.agreed_selection)||pending||faulted||
    preparation.phase()!=melee_web::MenuPreparationState::Phase::Idle||
    replay||melee_web_net_active()||asset_destination!=AssetDestination::None||
    match->paused()||match->ending()||match->complete())
  return history_refuse("Agreed match did not reach its actual prepared frame-zero owner");
 if(const auto* error=history_gpu_idle_failure())return history_refuse(error);
 try {
  start.live_identity_json=melee_web::slippi_profile::serialize_live_match_identity(
      *match,history_start_bound_host_agreement());
  start.prefix_json=history_start_serialize_prefix(start);
 }catch(const std::exception& error){return history_refuse(error.what());}
 start.publication.state=MatchHistoryStartReadyHeld;
 start.publication.first_use_pending=0;
 start.state=MatchHistoryStartReadyHeld;
 running=false;menu_clock.reset();audio_clock.reset();
 return true;
}

bool history_start_complete_resident_transfers(const GameplayMatchSession& session,unsigned& drains) {
 // The original match constructor starts its resident music asynchronously.
 // Complete only its existing DevCom work, in the transport order used by
 // the audio renderer, before admitting the strict source-idle boundary.
 if(!session.diagnostic_source_files_quiescent()||melee_web_source_file_drive_busy())
  return history_refuse("Agreed initial audio transfer requires quiescent match source files");
 if(!melee_web_source_memory_healthy())
  return history_refuse("Agreed initial audio transfer requires healthy source memory");
 const bool bank_pending=melee_web_audio_bank_transport_busy();
 const bool stream_pending=melee_web_audio_stream_source_loading();
 if(!bank_pending&&!stream_pending&&DVDGetDriveStatus()==DVD_STATE_END)return true;
 auto* audio=session.audio();
 if(!audio)
  return history_refuse("Agreed initial audio transfer lost its exact audio owner");
 if(bank_pending&&!melee_web_audio_bank_transport_active())
  return history_refuse("Agreed initial SSM transfer has no active bank transport");
 if(stream_pending&&!melee_web_audio_stream_owned(audio))
  return history_refuse("Agreed initial HPS transfer lost its exact stream owner");
 const bool stream_owned=melee_web_audio_stream_owned(audio);
 if(bank_pending||stream_owned)++drains;
 if(bank_pending)melee_web_audio_bank_transport_pump();
 if(stream_owned){
  char error[256]{};
  if(!melee_web_audio_stream_pump_for(audio,error,sizeof(error)))
   return history_refuse(error[0]?error:"Agreed initial HPS transfer failed");
 }
 return true;
}

bool history_start_publication_begin() {
 auto& start=match_history.start;
 if(start.state!=MatchHistoryStartConsumed||start.publication.source_draws)
  return history_refuse("Agreed source publication was not an unconsumed one-shot draw");
 if(!first_use_draw_pending)
  return history_refuse("Agreed source publication has no pending first-use draw");
 if(!start.match_owner||match.get()!=start.match_owner||!match->construction_complete())
  return history_refuse("Agreed source publication lost its constructed match owner");
 if(match->source_frames()!=0||match->ready())
  return history_refuse("Agreed source publication requires the original pre-intro owner");
 if(pending||running||faulted||preparation.phase()!=melee_web::MenuPreparationState::Phase::Idle)
  return history_refuse("Agreed source publication requires stopped, idle scene preparation");
 if(!history_runtime_idle())
  return history_refuse("Agreed source publication requires the idle main-thread Asyncify runtime");
 if(melee_web_cpu_interval_active())
  return history_refuse("Agreed source publication requires an unarmed CPU interval");
 if(history_start_payload(match->start_selection())!=history_start_payload(start.agreed_selection))
  return history_refuse("Agreed source publication selection differs from its agreement");
 if(!history_start_complete_resident_transfers(*match,start.initial_resident_transfer_drains))return false;
 if(const auto* error=history_source_idle_failure(*match))return history_refuse(error);
 if(!history_start_finish_renderer_persistence())return false;
 if(const auto* error=history_gpu_idle_failure())return history_refuse(error);
 const auto* stats=aurora_get_stats();
 if(!stats)return history_refuse("Agreed source publication has no actual renderer stats");
 start.prefix.reserve(kMatchHistoryStartPrefixLimit);
 start.publication.prior_submission=stats->lastEndFrameId;
 start.publication.source_frame=match->source_frames();
 start.publication.first_use_pending=first_use_draw_pending;
 start.publication.state=MatchHistoryStartPublishing;
 start.state=MatchHistoryStartPublishing;
 return true;
}

bool history_start_prefix_running() noexcept {
 const auto& start=match_history.start;
 return start.state==MatchHistoryStartPublishing&&!start.prefix_final_drawn;
}

void history_start_before_tick(const PADStatus* pad,uint64_t poll_serial) {
 auto& start=match_history.start;
 check(history_start_prefix_running()&&start.match_owner&&match.get()==start.match_owner&&
       running&&!faulted&&!pending&&!match->paused()&&!match->ending()&&!match->complete()&&
       start.publication.source_draws==start.prefix.size(),
       "Agreed startup tick lost its live source/draw owner");
 check(start.prefix.size()<kMatchHistoryStartPrefixLimit,"Agreed startup input prefix exceeded its 1024-tick cap");
 MatchHistoryStartTick row{};
 row.poll_serial=poll_serial;
 row.tick_before=gm_801A4BA8();
 row.source_frame_before=match->source_frames();
 row.hud_ready_before=match->ready();
 check(row.poll_serial&&row.tick_before!=UINT32_MAX&&!row.source_frame_before&&!row.hud_ready_before&&
       (start.prefix.empty()||(row.poll_serial>=start.prefix.back().poll_serial&&
                              row.tick_before==start.prefix.back().tick_after)),
       "Agreed startup tick input or original traversal boundary refused");
 for(unsigned port=0;port<4;++port)history_encode_pad(row.pad.data()+port*11,pad[port]);
 start.prefix.push_back(row);
}

bool history_start_after_tick() {
 auto& row=match_history.start.prefix.back();
 row.tick_after=gm_801A4BA8();
 row.source_frame_after=match->source_frames();
 row.hud_ready_after=match->ready();
 check(row.tick_after==row.tick_before+1u&&!row.source_frame_after&&
       !match->paused()&&!match->ending()&&!match->complete(),
       "Agreed startup source tick left its original intro/gameplay-frame-zero boundary");
 return row.hud_ready_after;
}

void history_start_after_draw(bool drew_source) {
 auto& start=match_history.start;
 const auto* stats=aurora_get_stats();
 check(drew_source&&start.match_owner&&match.get()==start.match_owner&&match->source_frames()==0&&
       start.prefix.size()==size_t(start.publication.source_draws)+1&&stats&&
       stats->lastEndFrameId>std::max(start.publication.prior_submission,start.publication.source_submission)&&
       stats->lastBeginFrameId==stats->lastEndFrameId&&!match_history_renderer_errors,
       "Agreed startup source draw did not complete its original tick and submission");
 auto& row=start.prefix.back();
 check(row.tick_after==gm_801A4BA8()&&row.hud_ready_after==match->ready(),
       "Agreed startup source draw changed its tick or HUD boundary");
 row.draw_submission=stats->lastEndFrameId;
 start.publication.source_submission=row.draw_submission;
 ++start.publication.source_draws;
 if(row.hud_ready_after){
  check(!first_use_draw_pending&&preparation.request_render_settle(false),
        "Agreed HUD-ready startup draw could not enter renderer-only settle");
  start.prefix_final_drawn=true;
  start.settle_started=emscripten_get_now();
  running=false;menu_clock.reset();audio_clock.reset();
 }
}

bool history_start_service_publication() {
 auto& start=match_history.start;
 if(!start.prefix_final_drawn||start.state!=MatchHistoryStartPublishing)return false;
 try {
  if(start.publication.source_draws!=start.prefix.size()||!start.match_owner||match.get()!=start.match_owner||
     !match->ready()||match->source_frames()!=0||pending||running||faulted||melee_web_cpu_interval_active()||
     emscripten_get_now()-start.settle_started>=10000)
   throw std::runtime_error("Agreed source publication settle lost its bounded owner");
  // Reuse renderer-only scene preparation. No input, source tick/draw, PCM
  // or RuntimeFiles pump. Finish only any already-queued resident audio I/O.
  if(preparation.warming()){
   const bool deferred=prepare_deferred_pipelines();
   const auto before=aurora_stats_snapshot();
   check(aurora_pipeline_service_preparation(),"Agreed renderer-only service refused");
   const auto after=aurora_stats_snapshot();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
   const bool pipelines_ready=melee_web::pipeline_preparation::status().ready;
#else
   const bool pipelines_ready=true;
#endif
   const bool activity=deferred||before.queuedPipelines!=after.queuedPipelines||
    before.createdPipelines!=after.createdPipelines;
   (void)preparation.observe_render(false,after.queuedPipelines+(pipelines_ready?0u:1u),activity);
  }else if(preparation.arming()){
   const auto submitted=aurora_browser_submission_status();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
   const bool pipelines_ready=melee_web::pipeline_preparation::status().ready&&audio_ready_for_preparation();
#else
   const bool pipelines_ready=true;
#endif
   const bool complete=!submitted.pendingFramePackets&&!submitted.pendingStagingBuffers&&!submitted.workerBusy;
   if(complete&&pipelines_ready){
    if(!history_start_complete_resident_transfers(*match,start.final_resident_transfer_drains))
     throw std::runtime_error(match_history.error.data());
    if(const auto* error=history_source_idle_failure(*match))throw std::runtime_error(error);
    if(!history_start_finish_renderer_persistence())
     throw std::runtime_error(match_history.error.data());
    check(preparation.arm(true),"Agreed startup renderer settle lost its arming phase");
    if(!history_start_enter_ready_hold_impl())throw std::runtime_error(match_history.error.data());
   }
  }else throw std::runtime_error("Agreed source publication left renderer-only preparation");
 }catch(const std::exception& error){history_refuse(error.what());faulted=true;running=false;}
 return true;
}

bool history_start_is_enabled_impl() noexcept {
 return match_history.start.state==MatchHistoryStartEnabled;
}
bool history_start_offer_waiting_impl() noexcept {
 return match_history.start.state==MatchHistoryStartOfferReady;
}
bool history_start_ready_hold_pending_impl() noexcept {
 return match_history.start.state==MatchHistoryStartConsumed&&
        match_history.start.match_owner!=nullptr;
}
bool history_start_asset_match_pending_impl() noexcept {
 const auto& start=match_history.start;
 return start.state==MatchHistoryStartConsumed&&!start.match_owner&&
        start.handoff_asset_generation!=0;
}

bool history_start_begin_agreed_match() {
 auto& start=match_history.start;
 try {
  if(start.state!=MatchHistoryStartAgreed||!history_start_source_stable(true)||
     !start.agreement_candidate_ready||start.agreement_json.empty())
   throw std::runtime_error("Agreed match start lost its exact source selection/preparation owner");
  auto source_equivalent=start.agreed_selection;
  source_equivalent.random_seed=start.local_menu_seed;
  if(history_start_payload(source_equivalent)!=start.local_payload)
   throw std::runtime_error("Agreed match start changed a source-selected field other than its seed");
  asset_selection=start.agreed_selection;asset_selection_valid=true;
  match_message=selected_match_message(start.agreed_selection);
  prior_vs_source_frames=final_sd_source_frames=0;prior_vs_terminal={};
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
  melee_web::pipeline_preparation::match(start.agreed_selection);
#endif
  start.state=MatchHistoryStartConsumed;
  start.match_owner=nullptr;
  if(scoped_assets){
   request_assets(AssetDestination::Match,&start.agreed_selection);
   start.handoff_asset_generation=asset_generation;
   return true;
  }
  if(!archive_cache)throw std::runtime_error("Agreed direct match handoff lost its source archive cache");
  const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
  // The private agreement explicitly selects the existing source-default PAD
  // constructor overload; vanilla keeps its actual menu-history overload.
  match=std::make_unique<GameplayMatchSession>(files,start.agreed_selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred);
  start.match_owner=match.get();start.handoff_asset_generation=asset_generation;
  const double constructed=emscripten_get_now();
  report_construction("match-enter-step-agreed-start",started,constructed,constructed,
                      before,aurora_stats_snapshot());
  running=false;message="Preparing agreed original match...";
  return true;
 }catch(const std::exception& error){
  history_refuse(error.what());faulted=true;running=false;return false;
 }
}

void history_start_finish_asset_match_impl() {
 auto& start=match_history.start;
 try {
  if(!history_start_asset_match_pending_impl()||match||!archive_cache||
     asset_generation!=start.handoff_asset_generation||!asset_selection_valid||
     history_start_payload(asset_selection)!=history_start_payload(start.agreed_selection)||
     host!=start.source_host||host_entered||
     melee_web_menu_host_phase(host)!=MELEE_WEB_MENU_READY||faulted)
   throw std::runtime_error("Agreed async asset handoff changed its source selection/owner");
  match=std::make_unique<GameplayMatchSession>(files,start.agreed_selection,*archive_cache,
      melee_web::GameplayMatchConstruction::Deferred);
  start.match_owner=match.get();
  running=false;message="Preparing agreed original match...";
 }catch(const std::exception& error){
  history_refuse(error.what());faulted=true;running=false;
 }
}

bool history_start_ordinary_callback_impl() {
 auto& start=match_history.start;
 // Ordinary unload revokes the private owners and retires the source session.
 // Its later ownerless callbacks still own the top-level renderer/cache drain.
 if(!host&&!world&&!match&&!results&&!prize&&!source_session_owned)return false;
 if(start.state==MatchHistoryStartOfferReady||
    start.state==MatchHistoryStartReadyHeld||
    (start.state!=MatchHistoryStartDisabled&&match_history.status.first_failure))return true;
 if(history_start_service_publication())return true;
 if(start.state==MatchHistoryStartAgreed){
  (void)history_start_begin_agreed_match();return true;
 }
 return false;
}

const char* history_gpu_idle_failure() noexcept {
 const auto busy=aurora_browser_submission_status();
 const auto* d=static_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observe_descriptor());
 if(!d||d->schemaVersion!=5||d->byteSize!=aurora_browser_quiescence_descriptor_bytes()||
    d->frameDescriptorStride!=sizeof(AuroraBrowserFrameStagingDescriptor)||
    d->submissionDescriptorStride!=sizeof(AuroraBrowserSubmissionDescriptor)||
    d->frameDescriptorCount!=d->frameSlotCount||d->submissionDescriptorCount!=d->stagingSlotCount)
  return "Renderer quiescence descriptor schema or extent refused";
 const uint64_t frames=uint64_t(d->frameDescriptorOffset)+uint64_t(d->frameDescriptorCount)*d->frameDescriptorStride;
 const uint64_t submissions=uint64_t(d->submissionDescriptorOffset)+uint64_t(d->submissionDescriptorCount)*d->submissionDescriptorStride;
 if(d->frameDescriptorOffset<sizeof(*d)||frames>d->submissionDescriptorOffset||submissions!=d->byteSize)
  return "Renderer quiescence descriptor table ranges refused";
 if(busy.pendingFramePackets||busy.pendingStagingBuffers||busy.workerBusy||
    d->pendingFramePackets||d->pendingStagingBuffers||d->workerBusy)
  return "Renderer submission packets, staging buffers or worker are busy";
 if(d->queuedPipelines)return "Renderer still has queued pipelines";
 if(d->lastBeginFrameId!=d->lastEndFrameId)return "Renderer frame begin/end IDs differ";
 if(d->staleCallbackCount)return "Renderer recorded a stale completion callback";
 if(match_history_renderer_errors)return "Renderer recorded an error";
 return nullptr;
}
bool history_gpu_idle() noexcept {return !history_gpu_idle_failure();}
bool history_runtime_idle() noexcept {
 return emscripten_is_main_runtime_thread()&&emscripten_is_main_browser_thread()&&
  emscripten_has_asyncify()==1&&match_history_asyncify_idle()==1;
}
const char* history_source_idle_failure(const GameplayMatchSession& session) noexcept {
 if(!session.diagnostic_source_files_quiescent())return "Match source file scope is not quiescent";
 if(melee_web_source_file_drive_busy())return "Match source file drive is busy";
 if(melee_web_audio_bank_transport_busy())return "Match audio bank transport is busy";
 if(melee_web_audio_stream_source_loading())return "Match audio stream source is loading";
 if(DVDGetDriveStatus()!=DVD_STATE_END)return "Match DVD drive is not at DVD_STATE_END";
 if(!melee_web_source_memory_healthy())return "Match source memory is unhealthy";
 return nullptr;
}
bool history_source_idle(const GameplayMatchSession& session) noexcept {
 return !history_source_idle_failure(session);
}
melee_web::RuntimeMatchHistoryGateResult history_source_boundary(
 const GameplayMatchSession& session,uint32_t frame) noexcept {
 using G=melee_web::RuntimeMatchHistoryGateResult;
 auto& c=match_history;
 try {
  if(!match_history_terminal||match.get()!=&session||c.match_owner!=&session||
     c.archive_owner!=archive_cache.get()||c.host_owner!=host||c.asset_owner!=asset_generation||
     running||pending||preparation.busy()||replay||menu_scene_rebuild_pending||
     first_use_draw_pending||asset_destination!=AssetDestination::None||faulted||melee_web_net_active())
   return G::MatchOwnerChanged;
  if(session.source_frames()!=frame)return G::WrongFrame;
  if(!session.construction_complete()||session.paused()||session.ending()||session.complete()||
     std::memcmp(&c.selection,&session.start_selection(),sizeof(c.selection))!=0)
   return G::IdentityRefused;
  if(const char* source_failure=history_source_idle_failure(session)){
   std::fprintf(stderr,"MATCH_HISTORY_SOURCE_BOUNDARY_REFUSED expected_frame=%u reason=%s\n",
       frame,source_failure);
   return G::SourceFilesBusy;
  }
  if(!history_runtime_idle())return G::RuntimeBusy;
  return G::Admitted;
 }catch(...){return G::IdentityRefused;}
}
melee_web::RuntimeMatchHistoryGateResult history_boundary(
 const GameplayMatchSession& session,melee_web::RuntimeMatchHistoryGatePoint point,
 uint32_t frame,void*) noexcept {
 using G=melee_web::RuntimeMatchHistoryGateResult;
 const auto source=history_source_boundary(session,frame);
 if(source!=G::Admitted)return source;
 if(!history_gpu_idle())return G::RendererBusy;
 if(point==melee_web::RuntimeMatchHistoryGatePoint::PresentComplete){
  const auto* d=static_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observe_descriptor());
  if(!d||d->lastEndFrameId!=match_history.submitted_frame)return G::RendererBusy;
 }
 return G::Admitted;
}

struct MatchHistoryRendererSample {
 AuroraBrowserQuiescenceLease lease{};
 unsigned cache_status{},renderer_errors{};
 const char* strict_failure{};
};
MatchHistoryRendererSample history_renderer_sample() noexcept {
 MatchHistoryRendererSample sample{};
 (void)aurora_browser_quiescence_capture_lease(&sample.lease);
 sample.cache_status=aurora_pipeline_cache_status();
 sample.renderer_errors=match_history_renderer_errors;
 sample.strict_failure=history_gpu_idle_failure();
 return sample;
}
bool history_renderer_can_flush(const MatchHistoryRendererSample& sample,
 uint64_t submitted,uint64_t already_flushed) noexcept {
 const auto& lease=sample.lease;
 return submitted&&submitted!=already_flushed&&sample.strict_failure&&
  sample.cache_status==AURORA_PIPELINE_CACHE_PENDING&&
  lease.pipelineCacheStatus==AURORA_PIPELINE_CACHE_PENDING&&
  lease.stagingGeneration&&lease.currentFrameIndex!=UINT32_MAX&&
  lease.lastBeginFrameId==submitted&&lease.lastEndFrameId==submitted&&
  !lease.pendingFramePackets&&!lease.pendingStagingBuffers&&!lease.workerBusy&&
  !lease.queuedPipelines&&!lease.staleCallbackCount&&!sample.renderer_errors;
}
std::array<char,768> history_renderer_sample_json(const MatchHistoryRendererSample& s) noexcept {
 const auto& x=s.lease;
 std::array<char,768> out{};
 std::snprintf(out.data(),out.size(),
  "{\"cacheStatus\":%u,\"leaseCacheStatus\":%u,\"pendingPackets\":%u,\"pendingStaging\":%u,"
  "\"workerBusy\":%u,\"queuedPipelines\":%u,\"rendererErrors\":%u,\"frameIndex\":%u,"
  "\"stagingGeneration\":\"%llu\",\"beginFrame\":\"%llu\",\"endFrame\":\"%llu\","
  "\"registration\":\"%llu\",\"callback\":\"%llu\",\"staleCallbacks\":\"%llu\",\"strictFailure\":\"%s\"}",
  s.cache_status,x.pipelineCacheStatus,x.pendingFramePackets,x.pendingStagingBuffers,x.workerBusy,
  x.queuedPipelines,s.renderer_errors,x.currentFrameIndex,
  static_cast<unsigned long long>(x.stagingGeneration),static_cast<unsigned long long>(x.lastBeginFrameId),
  static_cast<unsigned long long>(x.lastEndFrameId),static_cast<unsigned long long>(x.lastRegistrationSequence),
  static_cast<unsigned long long>(x.lastCallbackSequence),static_cast<unsigned long long>(x.staleCallbackCount),
  s.strict_failure?s.strict_failure:"");
 return out;
}

bool history_audio_step(GameplayMatchSession& session,void*) noexcept {
 try {char error[256]{};render_audio_tick(session.audio(),error,sizeof(error));return true;}
 catch(const std::exception& e){return history_refuse(e.what());}
 catch(...){return history_refuse("Ordinary audio tick threw an unknown exception");}
}
melee_web::RuntimeMatchHistoryPresentResult history_present(
 GameplayMatchSession& session,uint32_t expected_frame,void*) noexcept {
 using P=melee_web::RuntimeMatchHistoryPresentResult;
 try {
  auto& c=match_history;
  const uint32_t command=c.status.pending_command;
  const uint32_t request_sequence=c.status.request_seq;
  const uint32_t source_frame_before=session.source_frames();
  if((command!=4&&command!=19)||!request_sequence||source_frame_before!=expected_frame||
     c.presentation_serial==UINT64_MAX)return P::Refused;
  const auto* before=static_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observe_descriptor());
  if(!before)return P::Refused;
  const auto prior=before->lastEndFrameId;
  if(!aurora_begin_frame())return P::Refused;
  GXSetCopyClear(GXColor{0,0,0,255},GX_MAX_Z24);
  session.draw();
  aurora_end_frame();
  const auto* after=aurora_get_stats();
  if(!after||after->lastEndFrameId<=prior||after->lastEndFrameId!=after->lastBeginFrameId)
   return P::Refused;
  c.submitted_frame=after->lastEndFrameId;
  const uint32_t source_frame_after=session.source_frames();
  if(source_frame_after!=expected_frame)return P::Refused;
  const uint64_t serial=c.presentation_serial+1u;
  const int bytes=std::snprintf(c.presentation_json.data(),c.presentation_json.size(),
   "{\"schema\":1,\"command\":%u,\"requestSequence\":%u,"
   "\"sourceFrameBefore\":%u,\"sourceFrameAfter\":%u,"
   "\"priorSubmission\":\"%llu\",\"submission\":\"%llu\","
   "\"presentationSerial\":\"%llu\"}",command,request_sequence,
   source_frame_before,source_frame_after,static_cast<unsigned long long>(prior),
   static_cast<unsigned long long>(after->lastEndFrameId),
   static_cast<unsigned long long>(serial));
  if(bytes<=0||static_cast<size_t>(bytes)>=c.presentation_json.size()){
   c.presentation_json={};return P::Refused;
  }
  c.presentation_command=command;c.presentation_request_sequence=request_sequence;
  c.presentation_serial=serial;
  return P::Submitted;
 }catch(const std::exception& e){history_refuse(e.what());return P::Refused;}
 catch(...){history_refuse("Ordinary present threw an unknown exception");return P::Refused;}
}
void history_sync() noexcept {
 auto& c=match_history;
 if(c.capsule){
  c.status.history=c.capsule->metadata();
  if(c.capsule->first_failure()!=melee_web::RuntimeMatchHistoryFailure::None)
   history_refuse(c.capsule->error());
  c.status.input_ready=c.capsule->poll_serial()!=0;
  c.status.endpoint_active=c.capsule->endpoint_active();
 }
 c.status.renderer_errors=match_history_renderer_errors;
 if(c.status.owner_state==2)c.status.gpu_pending=0;
 else c.status.gpu_pending=history_runtime_idle()&&history_gpu_idle()?0:1;
}
void history_encode_pad(uint8_t* to,const PADStatus& p) noexcept {
 to[0]=uint8_t(p.button>>8);to[1]=uint8_t(p.button);
 to[2]=uint8_t(p.stickX);to[3]=uint8_t(p.stickY);to[4]=uint8_t(p.substickX);to[5]=uint8_t(p.substickY);
 to[6]=p.triggerLeft;to[7]=p.triggerRight;to[8]=p.analogA;to[9]=p.analogB;to[10]=uint8_t(p.err);
}
void history_copy_input() noexcept {
 auto& c=match_history;if(!c.capsule)return;
 if(c.capsule->episode_mode()){c.input={};return;}
 c.input.poll_serial=c.capsule->poll_serial();c.input.local_port=c.status.local_port;c.input.remote_port=c.status.remote_port;
 c.input.input_ready=c.capsule->retained_input(0)!=nullptr;
 c.input.corrected_ready=c.capsule->retained_input(2)!=nullptr;
 uint8_t* arrays[]={c.input.raw,c.input.forward,c.input.corrected};
 for(unsigned k=0;k<3;k++)if(const auto* row=c.capsule->retained_input(k))
  for(unsigned p=0;p<4;p++)history_encode_pad(arrays[k]+p*11,row[p]);
}
std::unique_ptr<melee_web::GameplayCpuStatics> history_configure_statics(MatchHistoryControl& c) {
  const melee_web::CpuIntervalSpan forbidden[]={{&c,sizeof(c)},{c.configured_rows.data(),c.configured_rows.size()*sizeof(melee_web::CpuStaticRow)},
   {&match_history_terminal,sizeof(match_history_terminal)},{&match_history_in_tick,sizeof(match_history_in_tick)},
   {&match_history_renderer_errors,sizeof(match_history_renderer_errors)},
   {&match_history_pcm_frames,sizeof(match_history_pcm_frames)},{&audio_phase,sizeof(audio_phase)},
   {&menu_clock,sizeof(menu_clock)},{&audio_clock,sizeof(audio_clock)},{pcm.data(),sizeof(pcm)}};
  return melee_web::GameplayCpuStatics::configure(c.configured_rows,
   {reinterpret_cast<uintptr_t>(&__global_base),reinterpret_cast<uintptr_t>(&__data_end)},forbidden);
}

bool history_rebase_confirmed(MatchHistoryControl& c,uint32_t end) {
 using Gate=melee_web::RuntimeMatchHistoryGatePoint;
 using Result=melee_web::RuntimeMatchHistoryGateResult;
 const bool had_prefix_handoff=c.prefix_handoff.active();
 const auto prefix_suffix_complete=[&]{
  return !had_prefix_handoff||
      (c.prefix_handoff.active()&&
       c.prefix_handoff.suffix_cursor()==c.prefix_handoff.suffix_count());
 };
 if(!prefix_suffix_complete())
  return history_refuse("Confirmed rebase requires every retained prefix suffix ticket to be forwarded");
 if(c.episode_enabled!=c.arena_enabled||!c.capsule||!c.identity_ready||c.statics||c.remote_ready||
    c.capsule->pre_frame()!=c.status.expected_frame||
    end>UINT32_MAX-MELEE_WEB_CPU_MOTOR_MAX_SPAN||!c.capsule->can_rebase_confirmed(end)||
    history_boundary(*match,Gate::PresentComplete,end,nullptr)!=Result::Admitted)
  return history_refuse("Confirmed rebase requires the actual retired match END");

 auto next_statics=history_configure_statics(c);
 if(!next_statics)return history_refuse("Confirmed rebase could not configure the immutable statics table");
 const auto& host=c.identity_host;
 auto next_identity=melee_web::slippi_profile::serialize_live_match_identity(
  *match,{host[0],host[1],host[2],host[3]});
 if(!prefix_suffix_complete()||!c.capsule->can_rebase_confirmed(end)||
    history_boundary(*match,Gate::PresentComplete,end,nullptr)!=Result::Admitted)
  return history_refuse("Confirmed rebase lost the retired match END");

 // The host must copy the retired window's receipts before requesting rebase;
 // this native gate proves closed+retired ownership, not remote confirmation.
 // This transition never calls source tick, PAD poll, present, or audio.
 c.capsule.reset();
 c.statics=std::move(next_statics);
 c.identity=std::move(next_identity);
 c.status.expected_frame=end;
 c.status.history={};
 c.status.history.pre_frame=c.status.history.current_frame=end;
 c.status.history.pre_audio_phase=c.status.history.current_audio_phase=audio_phase;
 c.status.input_ready=0;
 c.input={};
 c.record={};
 c.particle_header={};
 c.particle_row={};
 c.motor={};
 c.motor_commit={};
 c.checksum={};
 match_history_pcm_frames=0;
 if(had_prefix_handoff){
  c.prefix_handoff.clear();
  c.prefix_handoff_json={};
 }
 return true;
}

bool history_rebase_confirmed_prefix(MatchHistoryControl& c,uint32_t confirmed_end) {
 using Gate=melee_web::RuntimeMatchHistoryGatePoint;
 using Result=melee_web::RuntimeMatchHistoryGateResult;
 if(!c.episode_enabled||!c.arena_enabled||!c.capsule||!c.identity_ready||c.statics||
    c.remote_ready||!match||match.get()!=c.match_owner||
    c.capsule->pre_frame()!=c.status.expected_frame||
    confirmed_end>UINT32_MAX-MELEE_WEB_CPU_MOTOR_MAX_SPAN||
    !c.capsule->can_rebase_confirmed_prefix(confirmed_end,c.prefix_handoff,
                                             c.status.request_seq)||
    history_boundary(*match,Gate::EndpointComplete,confirmed_end,nullptr)!=Result::Admitted)
  return history_refuse("Confirmed-prefix rebase requires its actual retired CPU endpoint and retained suffix");

 auto next_statics=history_configure_statics(c);
 if(!next_statics)return history_refuse("Confirmed-prefix rebase could not configure the immutable statics table");
 const auto& host=c.identity_host;
 auto next_identity=melee_web::slippi_profile::serialize_live_match_identity(
  *match,{host[0],host[1],host[2],host[3]});
 if(!c.capsule->can_rebase_confirmed_prefix(confirmed_end,c.prefix_handoff,
                                            c.status.request_seq)||
    history_boundary(*match,Gate::EndpointComplete,confirmed_end,nullptr)!=Result::Admitted)
  return history_refuse("Confirmed-prefix rebase lost its retired CPU owner or suffix receipts");

 // Prefix transfer preserves the bounded original actual-poll rows. This
 // boundary only reconfigures the same statics/identity at C; it does not tick,
 // poll, publish, draw, or advance audio.
 c.capsule.reset();
 c.statics=std::move(next_statics);
 c.identity=std::move(next_identity);
 c.status.expected_frame=confirmed_end;
 c.status.history={};
 c.status.history.pre_frame=c.status.history.current_frame=confirmed_end;
 c.status.history.pre_audio_phase=c.status.history.current_audio_phase=audio_phase;
 c.status.input_ready=0;
 c.input={};
 c.record={};
 c.particle_header={};
 c.particle_row={};
 c.motor={};
 c.motor_commit={};
 c.checksum={};
 match_history_pcm_frames=0;
 c.prefix_handoff_json={};
 return true;
}

EM_BOOL history_dispatch(double,void*) {
 auto& c=match_history;
 const auto command=c.status.pending_command;
 if(!command||c.dispatching)return EM_FALSE;
 c.dispatching=true;
 bool okay=false;
 try {
  if(command==10){
   if(!history_runtime_idle()||!history_gpu_idle())history_refuse("Cleanup requires actual renderer and Asyncify idle");
   else {
    close();melee_web_input_shutdown();aurora_shutdown();emscripten_cancel_main_loop();
    okay=!match&&!world&&!host&&!results&&!prize&&!c.capsule;
    if(okay){c.prefix_handoff.clear();c.prefix_handoff_json={};
     c.pacing_started=false;
     c.status.owner_state=2;c.status.endpoint_active=0;}
   }
  }else if(!c.status.first_failure&&match&&match.get()==c.match_owner){
   switch(command){
   case 1:{
    if(c.capsule||!c.statics||!c.identity_ready||match->source_frames()!=c.status.expected_frame){history_refuse("Capture requires a frame-bound identity and reviewed statics configuration");break;}
    melee_web::CpuCaptureDiagnostic diagnostic{};
    c.capsule=RuntimeMatchHistory::capture(*match,std::move(c.statics),diagnostic,
      history_boundary,history_present,history_audio_step,audio_phase,nullptr,c.episode_enabled,c.arena_enabled);
    okay=bool(c.capsule);if(!okay)history_refuse(diagnostic.message.data());break;
   }
   case 2:okay=c.capsule&&!c.episode_enabled&&c.capsule->begin();break;
   case 3:{
    if(!c.capsule||c.episode_enabled||!c.remote_ready||c.capsule->phase()!=melee_web::RuntimeMatchHistoryPhase::Armed){history_refuse("Forward requires legacy armed PRE and decoded remote PAD");break;}
    if(history_boundary(*match,melee_web::RuntimeMatchHistoryGatePoint::ForwardBefore,c.capsule->pre_frame(),nullptr)!=melee_web::RuntimeMatchHistoryGateResult::Admitted)break;
    for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event)if(event->type==AURORA_EXIT)throw std::runtime_error("Aurora exit during terminal forward");
    const auto* before=melee_web_input_snapshot();const uint64_t previous=before?before->samples:0;
    if(previous==UINT64_MAX)throw std::runtime_error("Input sample serial exhausted");
    const auto* polled=melee_web_input_poll();
    if(!polled||polled->samples!=previous+1||!polled->ready||!polled->focused||!polled->visible||!polled->active)
     throw std::runtime_error("Actual ordinary input poll owner refused");
    match_history_pcm_frames=0;
    okay=c.capsule->forward_from_poll(polled->raw,polled->samples,c.status.local_port,c.status.remote_port,c.remote.data(),11);
    c.remote_ready=false;break;
   }
   case 4:okay=c.capsule&&c.capsule->submit_present();break;
   case 5:okay=c.capsule&&c.capsule->admit_present_completion();break;
   case 6:okay=c.capsule&&c.capsule->restore_pre();break;
   case 7:match_history_pcm_frames=0;okay=c.capsule&&!c.episode_enabled&&c.remote_ready&&c.capsule->correct_from_retained(c.remote.data(),11);c.remote_ready=false;break;
   case 8:okay=c.capsule&&c.capsule->close_selected_endpoint();break;
   case 9:
    if(c.capsule){if(const auto* motor=c.capsule->motor_evidence())c.motor=*motor;okay=c.capsule->retire_confirmed();
     if(okay){if(const auto* receipt=melee_web_cpu_interval_motor_commit_receipt())c.motor_commit=*receipt;else okay=false;}}break;
   case 11:okay=history_rebase_confirmed(c,c.requested_rebase_end);break;
   case 12:{
    if(!c.episode_enabled||!c.capsule){history_refuse("Episode poll ticket requires explicit pre-capture mode");break;}
    const auto poll_admitted=[&]{
     return c.capsule->can_admit_poll_ticket(c.status.request_seq,c.status.local_port,c.status.remote_port)&&
      history_boundary(*match,melee_web::RuntimeMatchHistoryGatePoint::ForwardBefore,
       c.capsule->current_frame(),nullptr)==melee_web::RuntimeMatchHistoryGateResult::Admitted;
    };
    if(!poll_admitted()){history_refuse("Episode poll ticket preflight refused before Aurora service");break;}
    for(const AuroraEvent* event=aurora_update();event&&event->type!=AURORA_NONE;++event)
     if(event->type==AURORA_EXIT)throw std::runtime_error("Aurora exit during episode input poll");
    if(!poll_admitted()){history_refuse("Episode poll ticket preflight refused before input sampling");break;}
    const auto* before=melee_web_input_snapshot();const uint64_t previous=before?before->samples:0;
    if(previous==UINT64_MAX)throw std::runtime_error("Input sample serial exhausted");
    const auto* polled=melee_web_input_poll();
    if(!polled||polled->samples!=previous+1||!polled->ready||!polled->focused||!polled->visible||!polled->active)
     throw std::runtime_error("Actual ordinary episode input poll owner refused");
    okay=c.capsule->admit_poll_ticket(c.status.request_seq,polled->raw,polled->samples,
      c.status.local_port,c.status.remote_port);break;
   }
   case 13:match_history_pcm_frames=0;
    okay=c.episode_enabled&&c.capsule&&c.capsule->forward_ticket(
      c.requested_ticket_id,c.requested_plan.data(),c.requested_plan.size());break;
   case 14:okay=c.episode_enabled&&c.capsule&&c.capsule->seal_incarnation();break;
   case 15:match_history_pcm_frames=0;
    okay=c.episode_enabled&&c.capsule&&c.capsule->replay_ticket(
      c.requested_ticket_id,c.requested_plan.data(),c.requested_plan.size());break;
   case 16:okay=c.episode_enabled&&c.capsule&&c.capsule->finish_replay();break;
   case 17:okay=c.episode_enabled&&c.capsule&&c.capsule->begin_continuation();break;
   case 18:
    if(c.episode_enabled&&c.capsule){
     if(c.prefix_handoff.active()&&c.prefix_handoff.rebase_request_sequence()&&
        c.capsule->pre_frame()==c.prefix_handoff.confirmed_frame())
      okay=c.capsule->begin_episode(c.prefix_handoff);
     else okay=c.capsule->begin_episode();
    }
    break;
   case 19:okay=c.episode_enabled&&c.arena_enabled&&c.capsule&&
      c.capsule->submit_recording_present();break;
   case 20:
    if(c.episode_enabled&&c.arena_enabled&&c.capsule&&
       c.capsule->finish_confirmed_prefix(c.requested_rebase_end))
     okay=c.capsule->capture_confirmed_prefix_handoff(c.status.request_seq,c.prefix_handoff);
    if(okay)c.prefix_handoff_json={};
    break;
   case 21:okay=c.episode_enabled&&c.arena_enabled&&c.capsule&&
      c.capsule->close_confirmed_prefix();break;
   case 22:
    if(c.episode_enabled&&c.arena_enabled&&c.capsule){
     if(const auto* motor=c.capsule->motor_evidence())c.motor=*motor;
     okay=c.capsule->retire_confirmed_prefix();
     if(okay){if(const auto* receipt=melee_web_cpu_interval_motor_commit_receipt())c.motor_commit=*receipt;else okay=false;}
    }
    break;
   case 23:okay=history_rebase_confirmed_prefix(c,c.requested_rebase_end);break;
   case 24:okay=c.episode_enabled&&c.arena_enabled&&c.capsule&&
      c.capsule->adopt_next_prefix_ticket();break;
   case 25:okay=c.episode_enabled&&c.arena_enabled&&c.capsule&&
      c.presentation_command==19&&c.presentation_serial&&c.submitted_frame&&
      c.presentation_request_sequence<=UINT32_MAX-2u&&
      c.presentation_request_sequence+2u==c.status.request_seq&&
      c.status.last_completed_command==5&&c.status.complete_seq+1u==c.status.request_seq&&
      c.capsule->seal_presented_frontier();break;
   default:break;
   }
  }
 }catch(const std::exception& e){history_refuse(e.what());}
 catch(...){history_refuse("Terminal dispatch threw an unknown exception");}
 history_sync(); // Preserve a specific capsule failure before the generic dispatch fallback.
 if(!okay)history_refuse("Terminal match history command refused");
 history_copy_input();
 c.requested_rebase_end=0;
 c.requested_ticket_id=0;c.requested_plan={};
 c.status.last_completed_command=command;c.status.complete_seq=c.status.request_seq;c.status.pending_command=0;
 c.dispatching=false;
 return EM_FALSE;
}
int history_request(unsigned command,uint32_t end_frame,bool explicit_episode_payload=false) {
 auto& c=match_history;
 const bool episode_command=command>=12&&command<=25;
 const bool legacy_execution=command==2||command==3||command==7;
 const bool unsupported_rebase=command==11&&c.episode_enabled!=c.arena_enabled;
 const bool frame_payload_command=command==13||command==15;
 if(!match_history_terminal||c.status.owner_state!=1||match_history_in_tick||c.dispatching||c.status.pending_command||
    (command!=10&&c.capsule&&c.capsule->endpoint_active())||command<1||command>25||
    (episode_command&&!c.episode_enabled)||(c.episode_enabled&&legacy_execution)||unsupported_rebase||
    (command>=20&&command<=25&&!c.arena_enabled)||
    (frame_payload_command!=explicit_episode_payload)||
    (c.status.first_failure&&command!=10)||c.status.request_seq==UINT32_MAX)return history_refuse("Terminal request admission refused");
 c.requested_rebase_end=end_frame;
 ++c.status.request_seq;c.status.pending_command=command;
 if(c.inline_dispatch){
  (void)history_dispatch(0.0,nullptr);
 }else{
  // Emscripten returns the browser's opaque rAF handle, not EMSCRIPTEN_RESULT.
  (void)emscripten_request_animation_frame(history_dispatch,nullptr);
 }
 return 1;
}
} // namespace

bool history_start_is_enabled() noexcept {return history_start_is_enabled_impl();}
bool history_start_offer_waiting() noexcept {return history_start_offer_waiting_impl();}
bool history_start_capture_offer(const MeleeWebMenuMatchSelection& selection) {
 return history_start_capture_offer_impl(selection);
}
bool history_start_asset_match_pending() noexcept {
 return history_start_asset_match_pending_impl();
}
void history_start_finish_asset_match() {history_start_finish_asset_match_impl();}
bool history_start_ordinary_callback() {return history_start_ordinary_callback_impl();}
bool history_start_ready_hold_pending() noexcept {
 return history_start_ready_hold_pending_impl();
}

void match_history_before_close() noexcept {
 // Revoke host/session identities before destruction. Keep the one-shot state
 // consumed so allocator reuse can never adopt a new owner at the same address.
 match_history.start.enabled_host=nullptr;
 match_history.start.source_host=nullptr;
 match_history.start.match_owner=nullptr;
 if(match_history.capsule)match_history.capsule->disarm_for_teardown();
}
void match_history_after_match_close() {
 auto& c=match_history;
 if(c.capsule){
  check(c.capsule->release_after_match_close(),"Terminal history source-owner retirement refused");
  c.status.history=c.capsule->metadata();c.capsule.reset();
 }
}

extern "C" {
int melee_web_native_menu_match_history_start_enable() {
 auto& start=match_history.start;
 try {
  if(!history_start_export_idle()||start.state!=MatchHistoryStartDisabled||match_history_terminal||
     match_history.status.first_failure||!source_session_owned||!host||!host_entered||!world||
     !world_exposed||!melee_web_gameplay_world_exists()||faulted||pending||
     preparation.busy()||asset_destination!=AssetDestination::None||match||results||prize||
     replay||melee_web_net_active()||!aurora_pipeline_complete_draws_enabled()||
     melee_web_menu_host_mode_kind(host)!=GM_VS)
   return history_refuse("Agreed-start opt-in requires the active original VS menu owner with complete draws");
  const int phase=melee_web_menu_host_phase(host);
  if(phase!=MELEE_WEB_MENU_CSS&&phase!=MELEE_WEB_MENU_SSS_READY&&phase!=MELEE_WEB_MENU_SSS)
   return history_refuse("Agreed-start opt-in requires an active CSS or SSS phase");
  start.enabled_host=host;start.state=MatchHistoryStartEnabled;return 1;
 }catch(const std::exception& error){return history_refuse(error.what());}
}
uint32_t melee_web_native_menu_match_history_start_state(){
 return match_history.start.state;
}
const char* melee_web_native_menu_match_history_start_offer_json(
    const char* runtime,const char* assets,const char* save,const char* profile) {
 auto& start=match_history.start;
 try {
  if(!history_start_export_idle()||start.state!=MatchHistoryStartOfferReady||match_history.status.first_failure||
     !history_start_source_stable(true))
   throw std::runtime_error("Agreed-start offer read phase refused");
  const auto host_agreement=history_start_host_agreement(runtime,assets,save,profile);
  history_start_bind_host_agreement(host_agreement);
  start.offer_json=history_start_offer_json(
      start.local_selection,history_start_bound_host_agreement(),start.local_menu_seed);
  return start.offer_json.c_str();
 }catch(const std::exception& error){history_refuse(error.what());return nullptr;}
}
const char* melee_web_native_menu_match_history_start_agreement_json(
    uint32_t seed,const char* runtime,const char* assets,const char* save,const char* profile) {
 auto& start=match_history.start;
 try {
  if(!history_start_export_idle()||start.state!=MatchHistoryStartOfferReady||match_history.status.first_failure||
     start.offer_json.empty()||!history_start_source_stable(true))
   throw std::runtime_error("Agreed-start agreement read phase refused");
  const auto host_agreement=history_start_host_agreement(runtime,assets,save,profile);
  history_start_bind_host_agreement(host_agreement);
  const auto candidate=history_start_agreement_json(
      start.local_selection,seed,history_start_bound_host_agreement());
  if(start.agreement_candidate_ready&&
     (start.negotiated_seed!=seed||start.agreement_json!=candidate))
   throw std::runtime_error("Agreed-start seed candidate changed after it was first bound");
  start.negotiated_seed=seed;start.agreement_candidate_ready=true;
  start.agreement_json=candidate;return start.agreement_json.c_str();
 }catch(const std::exception& error){history_refuse(error.what());return nullptr;}
}
int melee_web_native_menu_match_history_start_commit(
    uint32_t seed,const char* expected,size_t expected_bytes,
    const char* runtime,const char* assets,const char* save,const char* profile) {
 auto& start=match_history.start;
 try {
  if(!history_start_export_idle()||start.state!=MatchHistoryStartOfferReady||match_history.status.first_failure||
     start.offer_json.empty()||!start.agreement_candidate_ready||seed!=start.negotiated_seed||
     !history_start_source_stable(true))
   throw std::runtime_error("Agreed-start commit phase refused");
  const auto expected_agreement=history_start_string_argument(expected,expected_bytes);
  const auto host_agreement=history_start_host_agreement(runtime,assets,save,profile);
  history_start_bind_host_agreement(host_agreement);
  const auto candidate=history_start_agreement_json(
      start.local_selection,seed,history_start_bound_host_agreement());
  if(candidate!=start.agreement_json||expected_agreement!=candidate)
   throw std::runtime_error("Agreed-start peer bytes differ from the source-bound agreement");
  start.agreed_selection=start.local_selection;
  start.agreed_selection.random_seed=seed;
  start.state=MatchHistoryStartAgreed;return 1;
 }catch(const std::exception& error){return history_refuse(error.what());}
}
const char* melee_web_native_menu_match_history_start_live_identity_json(){
 auto& start=match_history.start;
 if(!history_start_export_idle()||start.state!=MatchHistoryStartReadyHeld||!start.match_owner||
    start.match_owner!=match.get()||start.live_identity_json.empty())
  return nullptr;
 return start.live_identity_json.c_str();
}
const char* melee_web_native_menu_match_history_start_prefix_json(){
 const auto& start=match_history.start;
 if(!history_start_export_idle()||start.state!=MatchHistoryStartReadyHeld||
    !start.match_owner||match.get()!=start.match_owner||match->source_frames()!=0||
    start.prefix_json.empty())return nullptr;
 return start.prefix_json.c_str();
}
const void* melee_web_native_menu_match_history_start_publication_data(){
 auto& start=match_history.start;
 if(!history_start_export_idle()||start.state!=MatchHistoryStartReadyHeld||
    !start.match_owner||match.get()!=start.match_owner||match->source_frames()!=0)return nullptr;
 return &start.publication;
}
unsigned melee_web_native_menu_match_history_start_publication_bytes(){return sizeof(MatchHistoryPublication);}
int melee_web_native_menu_match_history_pacing_start(){
 auto& c=match_history;
 using Phase=melee_web::RuntimeMatchHistoryPhase;
 using Gate=melee_web::RuntimeMatchHistoryGatePoint;
 using Result=melee_web::RuntimeMatchHistoryGateResult;
 if(c.pacing_started||c.pacing_failed||!match_history_terminal||c.status.owner_state!=1||
    c.status.first_failure||c.status.pending_command||c.status.complete_seq!=c.status.request_seq||
    c.dispatching||match_history_in_tick||!history_start_export_idle()||!c.identity_ready||
    !c.episode_enabled||!c.arena_enabled||!c.capsule||!match||match.get()!=c.match_owner||
    c.capsule->phase()!=Phase::Armed||c.capsule->current_frame()!=c.capsule->pre_frame()||
    c.capsule->forward_steps()!=0||c.capsule->poll_serial()!=0||c.capsule->restore_count()!=0||
    c.capsule->endpoint_active()||c.status.last_completed_command!=18||
    c.status.request_seq==UINT32_MAX||
    !c.capsule->can_admit_poll_ticket(c.status.request_seq+1u,c.status.local_port,c.status.remote_port)||
    history_boundary(*match,Gate::ForwardBefore,c.capsule->current_frame(),nullptr)!=Result::Admitted)
  return history_refuse("Pacing start requires the exact idle armed PRE after initial preparation");
 // Take-over stopped the ordinary menu loop. Start this owner clock only now,
 // after PRE, identity, arena mode and episode arming have all been admitted.
 // Seed from the native clock so the first later sample measures elapsed time
 // from pacing start rather than dropping an initial frame as the clock anchor.
 menu_clock.reset();
 const auto seeded=menu_clock.tick_with_budget(emscripten_get_now(),true,0u);
 c.pacing={};
 c.pacing.phase=static_cast<uint32_t>(c.capsule->phase());
 c.pacing.current_frame=c.capsule->current_frame();
 c.pacing.stalled=seeded.stalled?1u:0u;
 c.pacing.stall_reason=static_cast<uint32_t>(seeded.reason);
 c.pacing.elapsed_ms=seeded.interval_ms;
 c.pacing.pending_ticks=menu_clock.pending_ticks();
 c.pacing.triggering_value=seeded.triggering_value;
 c.pacing.threshold=seeded.threshold;
 if(seeded.stalled){
  c.pacing_failed=true;
  history_refuse("Native pacing clock could not seed a finite start time");
  return 0;
 }
 c.pacing_started=true;
 return 1;
}
const MatchHistoryPacingSample* melee_web_native_menu_match_history_pacing_sample(unsigned max_steps){
 auto& c=match_history;
 using Phase=melee_web::RuntimeMatchHistoryPhase;
 using Gate=melee_web::RuntimeMatchHistoryGatePoint;
 using Result=melee_web::RuntimeMatchHistoryGateResult;
 if(!c.pacing_started)return nullptr;
 if(c.pacing_failed)return &c.pacing;
 if(max_steps>1u){history_refuse("Pacing budget must be exactly zero or one");return nullptr;}
 // A dispatched export can yield while the renderer's Asyncify continuation is
 // active. Do not read the match or clock until the ordinary runtime is idle.
 if(!history_runtime_idle()){
  history_refuse("Pacing sample requires the idle native runtime");
  return nullptr;
 }
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||!match||
    match.get()!=c.match_owner||c.dispatching||match_history_in_tick)
  {history_refuse("Pacing sample lost its terminal match owner");return nullptr;}

 const auto phase=c.capsule?c.capsule->phase():Phase::Failed;
 const auto current=match->source_frames();
 const bool recording=c.capsule&&c.episode_enabled&&c.arena_enabled&&
     (phase==Phase::Armed||phase==Phase::EpisodeRecording);
 const bool known_serialized_wait=c.status.pending_command!=0;
 const bool known_endpoint_wait=c.capsule&&c.capsule->endpoint_active();
 const bool known_present_wait=phase==Phase::EpisodePresentPending;
 const bool known_unclaimed_ticket=c.status.last_completed_command==12||
     c.status.last_completed_command==24;
 const bool known_forward_present_wait=recording&&phase==Phase::EpisodeRecording&&
     c.status.last_completed_command==13;
 const bool known_prefix_suffix_wait=recording&&c.prefix_handoff.active()&&
     c.prefix_handoff.rebase_request_sequence()&&
     c.prefix_handoff.suffix_cursor()<c.prefix_handoff.suffix_count();
 const bool known_wait=known_serialized_wait||known_endpoint_wait||known_present_wait||
     known_unclaimed_ticket||known_forward_present_wait||known_prefix_suffix_wait||!recording;

 unsigned effective_budget=0;
 if(max_steps==1u&&!known_wait){
  if(!recording||c.capsule->endpoint_active()||c.status.request_seq==UINT32_MAX||
     (phase==Phase::Armed&&c.status.last_completed_command!=18)||
     (phase==Phase::EpisodeRecording&&c.status.last_completed_command!=5&&
      c.status.last_completed_command!=17)){
   history_refuse("Pacing due step is outside the presented native recording boundary");
   return nullptr;
  }
  if(!history_runtime_idle()||!history_gpu_idle()||
     !c.capsule->can_admit_poll_ticket(c.status.request_seq+1u,
          c.status.local_port,c.status.remote_port)||
     history_boundary(*match,Gate::ForwardBefore,current,nullptr)!=Result::Admitted){
   history_refuse("Pacing due step failed native input, owner, or renderer admission");
   return nullptr;
  }
  effective_budget=1u;
 }else if(!known_wait&&recording){
  // Budget zero is an intentional network/host wait, but it must not disguise
  // an owner or renderer failure at an otherwise live recording boundary.
  if(!history_runtime_idle()||!history_gpu_idle()||
     history_boundary(*match,Gate::ForwardBefore,current,nullptr)!=Result::Admitted){
   history_refuse("Pacing wait detected a native owner or renderer failure");
   return nullptr;
  }
 }

 const auto elapsed=menu_clock.tick_with_budget(emscripten_get_now(),true,effective_budget);
 c.pacing={};
 c.pacing.phase=c.capsule?static_cast<uint32_t>(phase):0u;
 c.pacing.current_frame=current;
 c.pacing.steps=elapsed.steps;
 c.pacing.pending_steps=elapsed.pending_steps;
 c.pacing.stalled=elapsed.stalled?1u:0u;
 c.pacing.stall_reason=static_cast<uint32_t>(elapsed.reason);
 c.pacing.elapsed_ms=elapsed.interval_ms;
 c.pacing.pending_ticks=menu_clock.pending_ticks();
 c.pacing.triggering_value=elapsed.triggering_value;
 c.pacing.threshold=elapsed.threshold;
 if(elapsed.stalled){
  c.pacing_failed=true;
  history_refuse("Native pacing clock stalled; terminal due-step admission is disabled");
 }
 return &c.pacing;
}
unsigned melee_web_native_menu_match_history_pacing_sample_bytes(){
 return sizeof(MatchHistoryPacingSample);
}
int melee_web_native_menu_match_history_take_over(uint32_t frame,unsigned local,unsigned remote) {
 auto& c=match_history;
 try {
  const bool agreed_ready=c.start.state==MatchHistoryStartReadyHeld&&
      c.start.match_owner&&c.start.match_owner==match.get()&&frame==0;
  if(c.start.state!=MatchHistoryStartDisabled&&!agreed_ready)
   return history_refuse("Agreed startup requires a completed HUD-ready publication before takeover");
  if(match_history_terminal||match_history_in_tick||c.dispatching||c.status.first_failure||!match||local>=2||remote>=2||local==remote||frame==UINT32_MAX||
     (!running&&!agreed_ready)||pending||faulted||preparation.busy()||replay||menu_scene_rebuild_pending||
     first_use_draw_pending||
     asset_destination!=AssetDestination::None||melee_web_net_active()||!history_runtime_idle()||!history_gpu_idle()||
     match->source_frames()!=frame||!match->construction_complete()||match->paused()||match->ending()||match->complete()||
     !history_source_idle(*match))return history_refuse("Terminal takeover admission refused");
  melee_web::slippi_profile::detail::validate_ordinary_mario_fd_stock_start(match->start_selection());
  melee_web::slippi_profile::detail::validate_pointer_free_start(match->start_selection().start.rules);
  c.selection=match->start_selection();c.match_owner=match.get();c.archive_owner=archive_cache.get();c.host_owner=host;c.asset_owner=asset_generation;
  c.status.expected_frame=frame;c.status.local_port=local;c.status.remote_port=remote;
  match_history_terminal=true;emscripten_cancel_main_loop();running=false;menu_clock.reset();audio_clock.reset();
  c.status.owner_state=1;return 1;
 }catch(const std::exception& e){return history_refuse(e.what());}
}
int melee_web_native_menu_match_history_configure(const melee_web::CpuStaticRow* rows,uint32_t count) {
 auto& c=match_history;
 try {
  if(!match_history_terminal||c.status.first_failure||c.capsule||c.statics||!c.configured_rows.empty()||c.dispatching||!rows||!count||c.status.pending_command)return history_refuse("Statics configuration phase refused");
  const uintptr_t begin=reinterpret_cast<uintptr_t>(rows),end=begin+uint64_t(count)*sizeof(*rows);
  if(end>emscripten_get_heap_size()||end<=begin)return history_refuse("Statics configuration input range refused");
  c.configured_rows.assign(rows,rows+count);
  c.statics=history_configure_statics(c);
  if(!c.statics)return history_refuse("Reviewed statics table refused native bounds/forbidden ranges");
  return 1;
 }catch(const std::exception& e){return history_refuse(e.what());}
}
int melee_web_native_menu_match_history_remote_pad(const uint8_t* bytes,unsigned length) {
 auto& c=match_history;
 if(c.episode_enabled||!match_history_terminal||c.status.first_failure||c.status.pending_command||c.dispatching||
    (c.capsule&&c.capsule->endpoint_active())||!bytes||length!=11||
    reinterpret_cast<uintptr_t>(bytes)+uint64_t(length)>emscripten_get_heap_size()||
    !melee_web_net_validate_pad(bytes,length))return history_refuse("Remote PAD admission refused");
 std::copy_n(bytes,11,c.remote.data());c.remote_ready=true;return 1;
}
int melee_web_native_menu_match_history_request(unsigned command) {
 if(command==11)return history_refuse("Confirmed rebase requires an explicit END frame");
 if(command==20||command==23)
  return history_refuse("Confirmed prefix finish/rebase requires an explicit source frame");
 return history_request(command,0);
}
int melee_web_native_menu_match_history_request_rebase(uint32_t end_frame) {
 return history_request(11,end_frame);
}
int melee_web_native_menu_match_history_request_prefix_finish(uint32_t confirmed_frame) {
 return history_request(20,confirmed_frame);
}
int melee_web_native_menu_match_history_request_prefix_rebase(uint32_t confirmed_frame) {
 return history_request(23,confirmed_frame);
}
int melee_web_native_menu_match_history_episode_enable() {
 auto& c=match_history;
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||c.status.pending_command||
    c.dispatching||c.capsule||c.episode_enabled||c.remote_ready||!c.identity_ready||!c.statics||!match||
    match.get()!=c.match_owner||match->source_frames()!=c.status.expected_frame||
    history_boundary(*match,melee_web::RuntimeMatchHistoryGatePoint::CapturePre,
      c.status.expected_frame,nullptr)!=melee_web::RuntimeMatchHistoryGateResult::Admitted)
  return history_refuse("Episode mode must be explicitly enabled before PRE capture");
 c.episode_enabled=true;return 1;
}
int melee_web_native_menu_match_history_arena_enable() {
 if(!melee_web_native_menu_match_history_episode_enable())return 0;
 match_history.arena_enabled=true;
 return 1;
}
int melee_web_native_menu_match_history_inline_dispatch_enable() {
 auto& c=match_history;
 if(c.inline_dispatch||!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||
    c.status.request_seq||c.status.complete_seq||c.status.last_completed_command||
    c.status.pending_command||c.dispatching||match_history_in_tick||c.capsule||c.remote_ready||
    !c.identity_ready||!c.statics||!match||match.get()!=c.match_owner||
    match->source_frames()!=c.status.expected_frame||!history_start_export_idle()||
    history_boundary(*match,melee_web::RuntimeMatchHistoryGatePoint::CapturePre,
      c.status.expected_frame,nullptr)!=melee_web::RuntimeMatchHistoryGateResult::Admitted)
  return history_refuse("Inline dispatch requires one initial idle pre-capture owner admission");
 c.inline_dispatch=true;
 return 1;
}
int melee_web_native_menu_match_history_request_episode_forward(
 uint32_t ticket_id,const uint8_t* plan,unsigned length) {
 auto& c=match_history;
 const uintptr_t begin=reinterpret_cast<uintptr_t>(plan);
 if(!c.episode_enabled||!ticket_id||!plan||length!=44||
    begin+uint64_t(length)>emscripten_get_heap_size()||c.status.pending_command||c.dispatching||
    c.status.first_failure)return history_refuse("Episode forward plan argument refused");
 std::copy_n(plan,44,c.requested_plan.begin());c.requested_ticket_id=ticket_id;
 return history_request(13,0,true);
}
int melee_web_native_menu_match_history_request_episode_replay(
 uint32_t ticket_id,const uint8_t* plan,unsigned length) {
 auto& c=match_history;
 const uintptr_t begin=reinterpret_cast<uintptr_t>(plan);
 if(!c.episode_enabled||!ticket_id||!plan||length!=44||
    begin+uint64_t(length)>emscripten_get_heap_size()||c.status.pending_command||c.dispatching||
    c.status.first_failure)return history_refuse("Episode replay plan argument refused");
 std::copy_n(plan,44,c.requested_plan.begin());c.requested_ticket_id=ticket_id;
 return history_request(15,0,true);
}
const char* melee_web_native_menu_match_history_rebase_identity_json(){
 const auto& c=match_history;
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||
    !history_start_export_idle()||
    (c.status.last_completed_command!=11&&c.status.last_completed_command!=23)||
    c.capsule||!c.statics||!c.identity_ready||!match||
    history_boundary(*match,c.status.last_completed_command==23?
      melee_web::RuntimeMatchHistoryGatePoint::EndpointComplete:
      melee_web::RuntimeMatchHistoryGatePoint::PresentComplete,
      c.status.expected_frame,nullptr)!=melee_web::RuntimeMatchHistoryGateResult::Admitted)
  return nullptr;
 return c.identity.c_str();
}
const char* melee_web_native_menu_match_history_presentation_json(){
 auto& c=match_history;
 const uint32_t command=c.presentation_command;
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||
    !history_start_export_idle()||c.status.pending_command||c.dispatching||
    c.status.complete_seq!=c.status.request_seq||
    (command!=4&&command!=19)||c.status.last_completed_command!=command||
    c.status.request_seq!=c.presentation_request_sequence||!c.presentation_serial||
    !c.presentation_json[0]||!match||match.get()!=c.match_owner||
    match->source_frames()!=c.status.history.current_frame)return nullptr;
 return c.presentation_json.data();
}
const char* melee_web_native_menu_match_history_renderer_prepare_json(unsigned command){
 auto& c=match_history;
 if((command!=5&&command!=10)||!match_history_terminal||c.status.owner_state!=1||
    c.status.pending_command||c.dispatching||match_history_in_tick)return nullptr;
 using Gate=melee_web::RuntimeMatchHistoryGateResult;
 const bool runtime_idle=history_runtime_idle();
 const uint32_t frame=runtime_idle&&c.capsule?c.capsule->current_frame():c.status.history.current_frame;
 const auto owner=runtime_idle&&match?history_source_boundary(*match,frame):Gate::RuntimeBusy;
 auto before=runtime_idle?history_renderer_sample():MatchHistoryRendererSample{};
 MatchHistoryRendererSample after{};
 bool after_observed=false;
 unsigned action=0;
 if(owner==Gate::Admitted&&c.capsule&&!c.capsule->endpoint_active()&&
    (c.status.last_completed_command==4||c.status.last_completed_command==19)&&
    history_renderer_can_flush(before,c.submitted_frame,c.cache_flush_frame)){
  c.cache_flush_frame=c.submitted_frame;
  const bool flushed=aurora_flush_pipeline_cache();
  const bool still_owned=match&&history_source_boundary(*match,frame)==Gate::Admitted;
  if(still_owned){after=history_renderer_sample();after_observed=true;}
  action=flushed&&still_owned&&!after.strict_failure&&
      after.cache_status==AURORA_PIPELINE_CACHE_READY&&
      after.lease.lastBeginFrameId==c.submitted_frame&&
      after.lease.lastEndFrameId==c.submitted_frame?1u:2u;
  if(action==2)history_refuse("Terminal renderer cache flush did not reach strict quiescence");
 }
 const auto before_json=history_renderer_sample_json(before);
 const auto after_json=history_renderer_sample_json(after);
 const int count=std::snprintf(c.renderer_preparation.data(),c.renderer_preparation.size(),
  "{\"schema\":1,\"command\":%u,\"sourceFrame\":%u,\"ownerGate\":%u,\"observed\":%u,"
  "\"submittedFrame\":\"%llu\",\"action\":%u,\"before\":%s,\"after\":%s}",
  command,frame,static_cast<unsigned>(owner),runtime_idle?1u:0u,
  static_cast<unsigned long long>(c.submitted_frame),action,before_json.data(),after_observed?after_json.data():"null");
 return count>0&&static_cast<size_t>(count)<c.renderer_preparation.size()?c.renderer_preparation.data():nullptr;
}
const MatchHistoryStatus* melee_web_native_menu_match_history_status(){
 // Direct polling during an Asyncify suspension reads only fixed transport.
 // Never enter source owners or Aurora observers while their callback owns
 // the native stack. Completion dispatch refreshes this record itself.
 if(!match_history.dispatching&&!match_history_in_tick&&history_runtime_idle())history_sync();
 return &match_history.status;
}
unsigned melee_web_native_menu_match_history_status_bytes(){return sizeof(MatchHistoryStatus);}
const MatchHistoryInput* melee_web_native_menu_match_history_input(){
 if(!match_history.dispatching&&!match_history.status.pending_command&&!match_history_in_tick&&history_runtime_idle())history_copy_input();
 return &match_history.input;
}
unsigned melee_web_native_menu_match_history_input_bytes(){return sizeof(MatchHistoryInput);}
unsigned melee_web_native_menu_match_history_ticket_bytes(){
 return match_history.episode_enabled?sizeof(MatchHistoryPollTicket):0u;
}
const MatchHistoryPollTicket* melee_web_native_menu_match_history_ticket_data(){
 auto& c=match_history;
 if(!c.episode_enabled||!c.capsule||c.status.first_failure||c.status.pending_command||
    c.status.complete_seq!=c.status.request_seq||
    (c.status.last_completed_command!=12&&c.status.last_completed_command!=24)||
    !history_start_export_idle())return nullptr;
 const auto* ticket=c.capsule->latest_ticket();
 if(!ticket)return nullptr;
 if(c.status.last_completed_command==12)
  return ticket->ticket_id==c.status.request_seq?ticket:nullptr;
 const auto cursor=c.prefix_handoff.suffix_cursor();
 const auto* origin=c.prefix_handoff.origin_row(cursor);
 return origin&&cursor<c.prefix_handoff.suffix_count()&&
    ticket->ticket_id==origin->ticket_id&&ticket->source_frame==origin->source_frame&&
    ticket->incarnation==c.capsule->incarnation()&&ticket->local_port==origin->local_port&&
    ticket->remote_port==origin->remote_port&&ticket->poll_serial==origin->poll_serial&&
    ticket->raw_pad==origin->raw_pad?ticket:nullptr;
}
unsigned melee_web_native_menu_match_history_input_row_bytes(){
 return match_history.episode_enabled?sizeof(MatchHistoryEpisodeInputRow):0u;
}
const MatchHistoryEpisodeInputRow* melee_web_native_menu_match_history_input_row_data(unsigned ordinal){
 auto& c=match_history;
 if(!c.episode_enabled||!c.capsule||c.status.first_failure||c.status.pending_command||
    c.status.complete_seq!=c.status.request_seq||!history_start_export_idle())return nullptr;
 return c.capsule->episode_input_row(ordinal);
}
const char* melee_web_native_menu_match_history_prefix_handoff_json(){
 auto& c=match_history;
 const auto& handoff=c.prefix_handoff;
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||
    !handoff.active()||!history_start_export_idle()||
    c.status.complete_seq!=c.status.request_seq||!match||match.get()!=c.match_owner)
  return nullptr;
 const auto& owners=handoff.owners();
 const auto count=std::snprintf(c.prefix_handoff_json.data(),c.prefix_handoff_json.size(),
  "{\"schema\":\"melee-web-runtime-prefix-handoff-v1\",\"oldPreFrame\":%u,"
  "\"confirmedFrame\":%u,\"selectedEndFrame\":%u,\"oldIncarnation\":%u,"
  "\"finishRequestSequence\":%u,\"rebaseRequestSequence\":%u,"
  "\"suffixCount\":%u,\"suffixCursor\":%u,\"localPort\":%u,\"remotePort\":%u,"
  "\"worldGeneration\":\"%llu\",\"contextEpoch\":\"%llu\","
  "\"ownerStorage\":\"%016llx\",\"ownerContext\":\"%016llx\","
  "\"ownerFlow\":\"%016llx\",\"ownerAudio\":\"%016llx\"}",
  handoff.old_pre_frame(),handoff.confirmed_frame(),handoff.selected_end_frame(),
  handoff.old_incarnation(),handoff.finish_request_sequence(),
  handoff.rebase_request_sequence(),handoff.suffix_count(),handoff.suffix_cursor(),
  handoff.local_port(),handoff.remote_port(),
  static_cast<unsigned long long>(owners.world),
  static_cast<unsigned long long>(owners.context_epoch),
  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owners.storage)),
  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owners.context)),
  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owners.flow)),
  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owners.audio)));
 return count>0&&static_cast<size_t>(count)<c.prefix_handoff_json.size()
    ?c.prefix_handoff_json.data():nullptr;
}
unsigned melee_web_native_menu_match_history_prefix_origin_row_bytes(){
 return match_history.prefix_handoff.active()?sizeof(MatchHistoryEpisodeInputRow):0u;
}
const MatchHistoryEpisodeInputRow*
melee_web_native_menu_match_history_prefix_origin_row_data(unsigned ordinal){
 auto& c=match_history;
 if(!c.prefix_handoff.active()||c.status.first_failure||c.status.pending_command||
    c.status.complete_seq!=c.status.request_seq||!history_start_export_idle())return nullptr;
 return c.prefix_handoff.origin_row(ordinal);
}
const char* melee_web_native_menu_match_history_error(){return match_history.error.data();}
const char* melee_web_native_menu_match_history_identity_json(const char* runtime,const char* assets,const char* save,const char* profile){
 auto& c=match_history;
 try {
  if(!match_history_terminal||c.status.first_failure||c.capsule||c.dispatching||c.status.pending_command||!match||!runtime||!assets||!save||!profile)
   throw std::runtime_error("Identity read phase refused");
  auto bounded=[](const char* p,size_t n){
   const auto address=reinterpret_cast<uintptr_t>(p);
   if(address+uint64_t(n)+1>emscripten_get_heap_size()||p[n]!=0)
    throw std::runtime_error("Identity string extent refused");
   return std::string_view(p,n);
  };
  std::array<std::string,4> supplied{
   std::string(bounded(runtime,64)),std::string(bounded(assets,64)),
   std::string(bounded(save,64)),std::string(bounded(profile,sizeof(SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID)-1))};
  if(c.identity_ready&&c.identity_host!=supplied)
   throw std::runtime_error("Terminal identity host agreement changed");
  auto identity=melee_web::slippi_profile::serialize_live_match_identity(
   *match,{supplied[0],supplied[1],supplied[2],supplied[3]});
  c.identity_host=std::move(supplied);
  c.identity=std::move(identity);
  c.identity_ready=true;return c.identity.c_str();
 }catch(const std::exception& e){history_refuse(e.what());return nullptr;}
}
int melee_web_native_menu_match_history_cpu_endpoint_begin(unsigned view,unsigned provenance){
 auto& c=match_history;
 if(!c.capsule||c.dispatching||c.status.pending_command||c.status.first_failure||
    !c.capsule->begin_endpoint_batch(static_cast<RuntimeMatchHistory::View>(view),static_cast<RuntimeMatchHistory::Provenance>(provenance))){history_refuse("Endpoint begin refused");return 1;}return 0;
}
int melee_web_native_menu_match_history_cpu_endpoint_record(unsigned domain,unsigned kind,unsigned index){
 auto& c=match_history;c.record={};
 if(!c.capsule||c.dispatching||c.status.pending_command||c.status.first_failure)return 1;
 c.record=c.capsule->endpoint_record(static_cast<RuntimeMatchHistory::Domain>(domain),static_cast<RuntimeMatchHistory::RowKind>(kind),index);
 return c.record.status;
}
const void* melee_web_native_menu_match_history_cpu_endpoint_data(){return &match_history.record;}
unsigned melee_web_native_menu_match_history_cpu_endpoint_bytes(){return sizeof(match_history.record);}
const void* melee_web_native_menu_match_history_cpu_arena_endpoint_data(){
 auto& c=match_history;c.arena_endpoint={};
 return c.arena_enabled&&c.capsule&&!c.dispatching&&!c.status.pending_command&&
    !c.status.first_failure&&c.capsule->endpoint_arena(c.arena_endpoint)?&c.arena_endpoint:nullptr;
}
unsigned melee_web_native_menu_match_history_cpu_arena_endpoint_bytes(){
 return match_history.arena_enabled?sizeof(melee_web::GameplayCpuArenaEndpoint):0;
}
int melee_web_native_menu_match_history_cpu_endpoint_end(){
 auto& c=match_history;
 if(!c.capsule||c.dispatching||c.status.pending_command||!c.capsule->end_endpoint_batch()){history_refuse("Endpoint completion refused");return 1;}history_sync();return 0;
}
int melee_web_native_menu_match_history_cpu_particle_endpoint_header(){
 auto& c=match_history;c.particle_header={};
 return c.capsule&&!c.dispatching&&!c.status.pending_command&&!c.status.first_failure&&c.capsule->endpoint_particle_header(c.particle_header)?0:1;
}
const void* melee_web_native_menu_match_history_cpu_particle_endpoint_header_data(){return &match_history.particle_header;}
unsigned melee_web_native_menu_match_history_cpu_particle_endpoint_header_bytes(){return sizeof(match_history.particle_header);}
int melee_web_native_menu_match_history_cpu_particle_endpoint_row(unsigned kind,unsigned index){
 auto& c=match_history;c.particle_row={};
 return c.capsule&&!c.dispatching&&!c.status.pending_command&&!c.status.first_failure&&c.capsule->endpoint_particle_row(kind,index,c.particle_row)?0:1;
}
const void* melee_web_native_menu_match_history_cpu_particle_endpoint_row_data(){return &match_history.particle_row;}
unsigned melee_web_native_menu_match_history_cpu_particle_endpoint_row_bytes(){return sizeof(match_history.particle_row);}
const void* melee_web_native_menu_match_history_motor_data(){
 auto& c=match_history;
 if(c.capsule&&!c.dispatching&&!c.status.pending_command&&!match_history_in_tick&&history_runtime_idle())
  if(const auto* current=c.capsule->motor_evidence())c.motor=*current;
 return &c.motor;
}
unsigned melee_web_native_menu_match_history_motor_bytes(){return sizeof(match_history.motor);}
const void* melee_web_native_menu_match_history_motor_commit_data(){return &match_history.motor_commit;}
unsigned melee_web_native_menu_match_history_motor_commit_bytes(){return sizeof(match_history.motor_commit);}
const void* melee_web_native_menu_match_history_pcm_data(){
 const auto& c=match_history;
 return match_history_terminal&&!c.dispatching&&!c.status.pending_command&&
  c.input.input_ready&&match_history_pcm_frames>0?pcm.data():nullptr;
}
unsigned melee_web_native_menu_match_history_pcm_bytes(){
 return melee_web_native_menu_match_history_pcm_data()?match_history_pcm_frames*2u*sizeof(float):0u;
}
const void* melee_web_native_menu_match_history_checksum_data(){
 using Phase=melee_web::RuntimeMatchHistoryPhase;
 using Gate=melee_web::RuntimeMatchHistoryGatePoint;
 using Result=melee_web::RuntimeMatchHistoryGateResult;
 auto& c=match_history;
 if(!match_history_terminal||c.status.owner_state!=1||c.status.first_failure||
    !history_start_export_idle()||!match||!c.capsule||c.capsule->endpoint_active()||
    c.capsule->first_failure()!=melee_web::RuntimeMatchHistoryFailure::None)
  return nullptr;
 const auto phase=c.capsule->phase();
 const bool corrected=phase==Phase::CorrectedCpuCopied||
     (phase==Phase::CorrectedComplete&&c.episode_enabled&&c.arena_enabled);
 const bool prefix=phase==Phase::PrefixCpuCopied;
 const bool presented=c.episode_enabled&&c.arena_enabled&&
     ((phase==Phase::PresentedComplete&&c.capsule->endpoint_mask()==(1u<<8))||
      (phase==Phase::CorrectedComplete&&(c.capsule->endpoint_mask()&96u)==96u));
 if(phase==Phase::CorrectedComplete&&!presented)return nullptr;
 if(phase!=Phase::PredictedCpuCopied&&!corrected&&!prefix&&!presented)return nullptr;
 const auto pre=c.capsule->pre_frame();
 if(pre!=c.status.expected_frame||pre==UINT32_MAX)return nullptr;
 const auto frame=c.capsule->current_frame();
 if(frame<=pre||c.capsule->sealed_end()!=frame||!c.capsule->poll_serial()||
    history_boundary(*match,Gate::EndpointComplete,frame,nullptr)!=Result::Admitted)return nullptr;
 if(presented&&history_boundary(*match,Gate::PresentComplete,frame,nullptr)!=Result::Admitted)return nullptr;
 const auto span=frame-pre;
 if(prefix){
  const auto old_selected_end=c.capsule->prefix_selected_end();
  if(!c.episode_enabled||!c.arena_enabled||old_selected_end<=frame||
     old_selected_end<=pre||old_selected_end-pre>7u||span>7u||
     c.capsule->forward_steps()!=old_selected_end-pre||
     c.capsule->replay_steps()!=span||!c.capsule->restore_count()||
     c.capsule->restore_count()>2u||!(c.capsule->endpoint_mask()&(1u<<7)))return nullptr;
 }else{
  const bool episode_steps_ok=c.episode_enabled&&span>0&&span<=7u&&
      c.capsule->forward_steps()==span&&
      (corrected?c.capsule->replay_steps()==span:c.capsule->replay_steps()==0);
  const bool legacy_steps_ok=!c.episode_enabled&&span==1&&
      c.capsule->forward_steps()==1&&
      c.capsule->replay_steps()==(corrected?1u:0u);
  if(!episode_steps_ok&&!legacy_steps_ok)return nullptr;
 }
 std::array<PADStatus,PAD_MAX_CONTROLLERS> decoded{};
 const PADStatus* pads=nullptr;uint32_t tick=pre;
 if(c.capsule->episode_mode()){
  const auto ordinal=frame-pre-1u;
  if(frame-pre>7u||ordinal>=7u)return nullptr;
  const auto* row=c.capsule->episode_input_row(ordinal);
  if(!row||row->source_frame+1u!=frame||row->state==0||row->poll_serial==0||
     ((corrected||prefix)&&row->state!=2u))return nullptr;
  const auto& bytes=row->state==2?row->selected_pad:row->forward_pad;
  for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port)
   if(!melee_web_net_decode_pad(bytes.data()+port*MELEE_WEB_NET_PAD_BYTES,
        MELEE_WEB_NET_PAD_BYTES,&decoded[port]))return nullptr;
  pads=decoded.data();tick=frame-1u;
 }else{
  if(frame!=pre+1u)return nullptr;
  pads=c.capsule->retained_input(corrected?2:1);
  if(!pads)return nullptr;
 }
 // The actual consumed match-local input ordinal selects this unchanged
 // 64-byte projection; transport and episode metadata are not hashed.
 MeleeWebNetChecksumRecord observed{};
 melee_web_net_checksum_compute(tick,3,pads,nullptr,&observed);
 if(observed.tick!=tick||observed.frame!=frame||!(observed.flags&MELEE_WEB_NET_HASHED_FIGHTERS)||
    c.capsule->phase()!=phase||
    history_boundary(*match,Gate::EndpointComplete,frame,nullptr)!=Result::Admitted)return nullptr;
 c.checksum=observed;
 return &c.checksum;
}
unsigned melee_web_native_menu_match_history_checksum_bytes(){return sizeof(match_history.checksum);}
} // extern C
#endif
