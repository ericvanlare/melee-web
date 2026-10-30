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
struct MatchHistoryInput {
 uint32_t schema=1,bytes=168,input_ready{},corrected_ready{};
 uint64_t poll_serial{};
 uint32_t local_port{},remote_port{};
 uint8_t raw[44]{},forward[44]{},corrected[44]{};
 uint32_t reserved{};
};
static_assert(sizeof(MatchHistoryStatus)==128);
static_assert(sizeof(MatchHistoryInput)==168);
struct MatchHistoryControl {
 MatchHistoryStatus status{};
 MatchHistoryInput input{};
 std::array<char,512> error{};
 std::unique_ptr<RuntimeMatchHistory> capsule;
 std::unique_ptr<melee_web::GameplayCpuStatics> statics;
 std::vector<melee_web::CpuStaticRow> configured_rows;
 MeleeWebMenuMatchSelection selection{};
 GameplayMatchSession* match_owner{};
 RuntimeArchiveCache* archive_owner{};
 MeleeWebMenuHost* host_owner{};
 uint64_t asset_owner{};
 uint64_t submitted_frame{};
 std::array<uint8_t,11> remote{};
 bool remote_ready{},dispatching{},identity_ready{};
 std::string identity;
 melee_web::GameplayCpuEndpointRecord record{};
 SlippiParticleCellEndpointHeader particle_header{};
 SlippiParticleCellEndpointRow particle_row{};
 MeleeWebCpuMotorEvidence motor{};
 MeleeWebCpuMotorCommitReceipt motor_commit{};
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

bool history_gpu_idle() noexcept {
 const auto busy=aurora_browser_submission_status();
 const auto* d=static_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observe_descriptor());
 if(!d||d->schemaVersion!=5||d->byteSize!=aurora_browser_quiescence_descriptor_bytes()||
    d->frameDescriptorStride!=sizeof(AuroraBrowserFrameStagingDescriptor)||
    d->submissionDescriptorStride!=sizeof(AuroraBrowserSubmissionDescriptor)||
    d->frameDescriptorCount!=d->frameSlotCount||d->submissionDescriptorCount!=d->stagingSlotCount)
  return false;
 const uint64_t frames=uint64_t(d->frameDescriptorOffset)+uint64_t(d->frameDescriptorCount)*d->frameDescriptorStride;
 const uint64_t submissions=uint64_t(d->submissionDescriptorOffset)+uint64_t(d->submissionDescriptorCount)*d->submissionDescriptorStride;
 return d->frameDescriptorOffset>=sizeof(*d)&&frames<=d->submissionDescriptorOffset&&
  submissions==d->byteSize&&!busy.pendingFramePackets&&!busy.pendingStagingBuffers&&!busy.workerBusy&&
  !d->pendingFramePackets&&!d->pendingStagingBuffers&&!d->workerBusy&&
  !d->queuedPipelines&&d->lastBeginFrameId==d->lastEndFrameId&&
  !d->staleCallbackCount&&match_history_renderer_errors==0;
}
bool history_runtime_idle() noexcept {
 return emscripten_is_main_runtime_thread()&&emscripten_is_main_browser_thread()&&
  emscripten_has_asyncify()==1&&match_history_asyncify_idle()==1;
}
bool history_source_idle(const GameplayMatchSession& session) noexcept {
 return session.diagnostic_source_files_quiescent()&&
  !melee_web_source_file_drive_busy()&&!melee_web_audio_bank_transport_busy()&&
  !melee_web_audio_stream_source_loading()&&DVDGetDriveStatus()==DVD_STATE_END&&
  melee_web_source_memory_healthy();
}
melee_web::RuntimeMatchHistoryGateResult history_boundary(
 const GameplayMatchSession& session,melee_web::RuntimeMatchHistoryGatePoint point,
 uint32_t frame,void*) noexcept {
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
  if(!history_source_idle(session))return G::SourceFilesBusy;
  if(!history_runtime_idle())return G::RuntimeBusy;
  if(!history_gpu_idle())return G::RendererBusy;
  if(point==melee_web::RuntimeMatchHistoryGatePoint::PresentComplete){
   const auto* d=static_cast<const AuroraBrowserQuiescenceDescriptor*>(aurora_browser_quiescence_observe_descriptor());
   if(!d||d->lastEndFrameId!=c.submitted_frame)return G::RendererBusy;
  }
  return G::Admitted;
 }catch(...){return G::IdentityRefused;}
}
bool history_audio_step(GameplayMatchSession& session,void*) noexcept {
 try {char error[256]{};render_audio_tick(session.audio(),error,sizeof(error));return true;}
 catch(const std::exception& e){return history_refuse(e.what());}
 catch(...){return history_refuse("Ordinary audio tick threw an unknown exception");}
}
melee_web::RuntimeMatchHistoryPresentResult history_present(
 GameplayMatchSession& session,uint32_t,void*) noexcept {
 using P=melee_web::RuntimeMatchHistoryPresentResult;
 try {
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
  match_history.submitted_frame=after->lastEndFrameId;
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
 c.input.poll_serial=c.capsule->poll_serial();c.input.local_port=c.status.local_port;c.input.remote_port=c.status.remote_port;
 c.input.input_ready=c.capsule->retained_input(0)!=nullptr;
 c.input.corrected_ready=c.capsule->retained_input(2)!=nullptr;
 uint8_t* arrays[]={c.input.raw,c.input.forward,c.input.corrected};
 for(unsigned k=0;k<3;k++)if(const auto* row=c.capsule->retained_input(k))
  for(unsigned p=0;p<4;p++)history_encode_pad(arrays[k]+p*11,row[p]);
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
    if(okay){c.status.owner_state=2;c.status.endpoint_active=0;}
   }
  }else if(!c.status.first_failure&&match&&match.get()==c.match_owner){
   switch(command){
   case 1:{
    if(c.capsule||!c.statics||!c.identity_ready){history_refuse("Capture requires identity and one reviewed statics configuration");break;}
    melee_web::CpuCaptureDiagnostic diagnostic{};
    c.capsule=RuntimeMatchHistory::capture(*match,std::move(c.statics),diagnostic,
      history_boundary,history_present,history_audio_step,audio_phase,nullptr);
    okay=bool(c.capsule);if(!okay)history_refuse(diagnostic.message.data());break;
   }
   case 2:okay=c.capsule&&c.capsule->begin();break;
   case 3:{
    if(!c.capsule||!c.remote_ready||c.capsule->phase()!=melee_web::RuntimeMatchHistoryPhase::Armed){history_refuse("Forward requires armed PRE and decoded remote PAD");break;}
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
   case 7:match_history_pcm_frames=0;okay=c.capsule&&c.remote_ready&&c.capsule->correct_from_retained(c.remote.data(),11);c.remote_ready=false;break;
   case 8:okay=c.capsule&&c.capsule->close_selected_endpoint();break;
   case 9:
    if(c.capsule){if(const auto* motor=c.capsule->motor_evidence())c.motor=*motor;okay=c.capsule->retire_confirmed();
     if(okay){if(const auto* receipt=melee_web_cpu_interval_motor_commit_receipt())c.motor_commit=*receipt;else okay=false;}}break;
   default:break;
   }
  }
 }catch(const std::exception& e){history_refuse(e.what());}
 catch(...){history_refuse("Terminal dispatch threw an unknown exception");}
 if(!okay)history_refuse("Terminal match history command refused");
 history_sync();history_copy_input();
 c.status.last_completed_command=command;c.status.complete_seq=c.status.request_seq;c.status.pending_command=0;
 c.dispatching=false;
 return EM_FALSE;
}
} // namespace

void match_history_before_close() noexcept {
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
int melee_web_native_menu_match_history_take_over(uint32_t frame,unsigned local,unsigned remote) {
 auto& c=match_history;
 try {
  if(match_history_terminal||match_history_in_tick||c.dispatching||!match||local>=2||remote>=2||local==remote||frame==UINT32_MAX||
     !running||pending||faulted||preparation.busy()||replay||menu_scene_rebuild_pending||first_use_draw_pending||
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
  if(!match_history_terminal||c.status.first_failure||c.capsule||c.statics||c.dispatching||!rows||!count||c.status.pending_command)return history_refuse("Statics configuration phase refused");
  const uintptr_t begin=reinterpret_cast<uintptr_t>(rows),end=begin+uint64_t(count)*sizeof(*rows);
  if(end>emscripten_get_heap_size()||end<=begin)return history_refuse("Statics configuration input range refused");
  c.configured_rows.assign(rows,rows+count);
  const melee_web::CpuIntervalSpan forbidden[]={{&c,sizeof(c)},{c.configured_rows.data(),c.configured_rows.size()*sizeof(*rows)},
   {&match_history_terminal,sizeof(match_history_terminal)},{&match_history_in_tick,sizeof(match_history_in_tick)},
   {&match_history_renderer_errors,sizeof(match_history_renderer_errors)},
   {&match_history_pcm_frames,sizeof(match_history_pcm_frames)},{&audio_phase,sizeof(audio_phase)},
   {&menu_clock,sizeof(menu_clock)},{&audio_clock,sizeof(audio_clock)},{pcm.data(),sizeof(pcm)}};
  c.statics=melee_web::GameplayCpuStatics::configure(c.configured_rows,
   {reinterpret_cast<uintptr_t>(&__global_base),reinterpret_cast<uintptr_t>(&__data_end)},forbidden);
  if(!c.statics)return history_refuse("Reviewed statics table refused native bounds/forbidden ranges");
  return 1;
 }catch(const std::exception& e){return history_refuse(e.what());}
}
int melee_web_native_menu_match_history_remote_pad(const uint8_t* bytes,unsigned length) {
 auto& c=match_history;
 if(!match_history_terminal||c.status.first_failure||c.status.pending_command||c.dispatching||
    (c.capsule&&c.capsule->endpoint_active())||!bytes||length!=11||
    reinterpret_cast<uintptr_t>(bytes)+uint64_t(length)>emscripten_get_heap_size()||
    !melee_web_net_validate_pad(bytes,length))return history_refuse("Remote PAD admission refused");
 std::copy_n(bytes,11,c.remote.data());c.remote_ready=true;return 1;
}
int melee_web_native_menu_match_history_request(unsigned command) {
 auto& c=match_history;
 if(!match_history_terminal||c.status.owner_state!=1||match_history_in_tick||c.dispatching||c.status.pending_command||
    (command!=10&&c.capsule&&c.capsule->endpoint_active())||command<1||command>10||
    (c.status.first_failure&&command!=10)||c.status.request_seq==UINT32_MAX)return history_refuse("Terminal request admission refused");
 ++c.status.request_seq;c.status.pending_command=command;
 if(emscripten_request_animation_frame(history_dispatch,nullptr)!=EMSCRIPTEN_RESULT_SUCCESS){c.status.pending_command=0;return history_refuse("Owned animation-frame request failed");}
 return 1;
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
  c.identity=melee_web::slippi_profile::serialize_live_match_identity(*match,
   {bounded(runtime,64),bounded(assets,64),bounded(save,64),
    bounded(profile,sizeof(SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID)-1)});
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
} // extern C
#endif
