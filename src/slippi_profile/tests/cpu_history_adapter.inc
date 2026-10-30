/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Private retained-PRE source-only history adapter. The host owns cross-page
 * equality; this adapter exposes only checked local state and copied receipts. */
#if defined(MELEE_WEB_SYNCTEST_CORRECTED_CPU)
namespace {
struct CpuHistoryMetadata {
    uint32_t schema=1,bytes=sizeof(CpuHistoryMetadata),phase=0,last_command=0;
    uint32_t first_failure=0,pre_frame=0,current_frame=0,sealed_end=0;
    uint32_t incarnation=0,restore_count=0,captures=0,retirements=0;
    uint32_t active_capsules=0,max_active_capsules=0,forward_steps=0,replay_steps=0;
    uint32_t draw_submissions=0,source_step_no_draw_mask=0;
    uint32_t commit_result=UINT32_MAX,reserved=0;
};
static_assert(sizeof(CpuHistoryMetadata)==80);
using CpuHistoryOwnerTuple=decltype(match->diagnostic_cpu_owners());
struct CpuHistoryControl {
    enum Phase : uint32_t {Disabled=0,Captured=1,Recording=2,PredictedSealed=3,
        Correcting=4,CorrectedSealed=5,Retired=6,Failed=7};
    enum EndpointBit : uint32_t {CapturedPre=1u<<0,ArmedPre=1u<<1,RestoredPre=1u<<2,
        PredictedCpu=1u<<3,PredictedDraw=1u<<4,CorrectedCpu=1u<<5,CorrectedDraw=1u<<6};
    Phase phase=Disabled;
    bool enabled=false,draw_pending=false,draw_completed=false;
    bool endpoint_closed=false,motor_receipt_valid=false;
    uint32_t endpoint_mask=0,endpoint_batch_bit=0;
    uint32_t origin_frame=0;
    const melee_web::GameplayMatchSession* session=nullptr;
    CpuHistoryOwnerTuple owners{};
    HSD_GObj* profile_owner=nullptr;
    uint32_t configured_seed=0;
    bool initializer=false,profile=false;
    CpuHistoryMetadata evidence{};
    std::array<uint8_t,64> motor_receipt{};
} cpu_history;
static_assert(sizeof(MeleeWebCpuMotorCommitReceipt)==64);

bool cpu_history_active(){return cpu_history.enabled;}
void cpu_history_forbid(std::vector<melee_web::CpuIntervalSpan>& spans){
    spans.push_back({&cpu_history,sizeof(cpu_history)});
}
bool cpu_history_same_owner(const CpuHistoryOwnerTuple& a,const CpuHistoryOwnerTuple& b){
    return a.storage==b.storage&&a.context==b.context&&a.flow==b.flow&&a.audio==b.audio&&
        a.world==b.world&&a.context_epoch==b.context_epoch;
}
void cpu_history_sync(){
    auto& e=cpu_history.evidence;
    e.schema=1;e.bytes=sizeof(e);e.reserved=0;
    e.first_failure=cpu_restore.first_failure;
    if(cpu_restore.first_failure)cpu_history.phase=CpuHistoryControl::Failed;
    e.phase=cpu_history.phase;
    e.current_frame=match?match->source_frames():e.current_frame;
    e.active_capsules=e.captures-e.retirements;
    if(e.active_capsules>e.max_active_capsules)e.max_active_capsules=e.active_capsules;
}
uint32_t cpu_history_fail(uint32_t code,const char* message){
    if(!cpu_restore.first_failure)cpu_fail(code,message);
    cpu_history.evidence.first_failure=cpu_restore.first_failure;
    cpu_history.phase=CpuHistoryControl::Failed;
    cpu_history_sync();
    return cpu_restore.first_failure?cpu_restore.first_failure:(code?code:0x550u);
}
bool cpu_history_interactive_safe(uint32_t frame){
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
    if(!melee_web_snapshot_actual_stage_is_final_destination())return false;
#endif
    if(!cpu_history.enabled||!match||match.get()!=cpu_history.session||
       match->source_frames()!=frame||observation.frame!=frame||
       !observation.ready||observation.ending||observation.complete||observation.paused||
       observation.stocks[0]!=4||observation.stocks[1]!=4||
       rng_profile_gobj!=cpu_history.profile_owner||
       configured_source_seed!=cpu_history.configured_seed||
       native_initializer_enabled!=cpu_history.initializer||
       rng_profile_enabled!=cpu_history.profile)return false;
    const auto owners=match->diagnostic_cpu_owners();
    if(!cpu_history_same_owner(owners,cpu_history.owners)||owners.frame!=frame)return false;
    for(unsigned i=0;i<2;++i)
        if(melee_web_synctest_motion_boundary(observation.motion[i])!=0||
           !melee_web_synctest_is_mario_kind(observation.players[i].fighter_kind))return false;
    return true;
}
bool cpu_history_interior_pending(){
    return match&&cpu_restore.capsule&&
        cpu_restore.capsule->interior_hps_pending(*match);
}
bool cpu_history_boundary(uint32_t frame,bool interval){
    if(!cpu_history_interactive_safe(frame)||cpu_boundary(cpu_history_interior_pending()))return false;
    if(interval&&(!cpu_restore.capsule||!cpu_restore.capsule->interval_boundary(*match)))return false;
    return true;
}
bool cpu_history_draw_diagnostics(std::array<uint8_t,132>& bytes){
    if(melee_web_synctest_draw_diagnostics_size()!=bytes.size())return false;
    const auto* source=static_cast<const uint8_t*>(melee_web_synctest_draw_diagnostics());
    if(!source)return false;
    std::memcpy(bytes.data(),source,bytes.size());return true;
}
bool cpu_history_source_sample(uint32_t input_frame,uint32_t& sample){
    if(input_frame<cpu_history.origin_frame||
       input_frame-cpu_history.origin_frame>UINT32_MAX-300u)return false;
    sample=300u+(input_frame-cpu_history.origin_frame);return true;
}
uint32_t cpu_history_capture(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    if(!h.enabled||cpu_restore.first_failure||
       (h.phase!=H::Disabled&&h.phase!=H::Retired)||
       cpu_restore.state!=CpuRestoreControl::Configured||!cpu_restore.configured_statics||
       cpu_restore.capsule||cpu_restore.two_active||cpu_restore.seven_active||
       cpu_corrected.active||cpu_corrected.reader.active()||cpu_corrected_reuse.enabled||
       e.captures==UINT32_MAX||e.captures!=e.retirements||e.active_capsules||
       h.draw_pending||payload_admission_boundary(false)!=0)
        return cpu_history_fail(0x550u,"history capture phase/configuration refused");
    const uint32_t frame=match?match->source_frames():UINT32_MAX;
    if(!cpu_history_boundary(frame,false)||frame!=observation.frame||
       frame>UINT32_MAX-MELEE_WEB_CPU_MOTOR_MAX_SPAN)
        return cpu_history_fail(0x551u,"history PRE source/owner boundary refused");
    cpu_restore.profile_owner=h.profile_owner;cpu_restore.profile=h.profile;
    cpu_restore.configured_seed=h.configured_seed;cpu_restore.initializer=h.initializer;
    cpu_restore.pre_phase=audio_phase;cpu_restore.pre_profile=rng_profile_observation;
    cpu_restore.pre_expected_frame=rng_profile_expected_callback_frame;
    std::memcpy(&cpu_restore.seven_pre_observation,&observation,sizeof(observation));
    payload_admission_capture_begin_frame=last_completed_frame_id;
    payload_admission_capture_end_frame=last_completed_frame_id;
    melee_web::CpuCaptureDiagnostic diagnostic{};
    cpu_restore.capsule=melee_web::GameplayCpuCheckpoint::capture_history(
        *match,std::move(cpu_restore.configured_statics),diagnostic);
    if(!cpu_restore.capsule)
        return cpu_history_fail(0x552u,diagnostic.message[0]?diagnostic.message.data():
                                "history PRE capsule capture refused");
    if(!cpu_history_interactive_safe(frame))
        return cpu_history_fail(0x553u,"history PRE changed during capture");
    if(!e.captures)h.origin_frame=frame;
    ++e.captures;e.active_capsules=1;e.max_active_capsules=1;
    e.pre_frame=frame;e.current_frame=frame;e.sealed_end=0;e.incarnation=0;e.restore_count=0;
    e.forward_steps=0;e.replay_steps=0;e.draw_submissions=0;e.source_step_no_draw_mask=0;
    e.commit_result=UINT32_MAX;h.endpoint_mask=0;h.endpoint_batch_bit=0;
    h.endpoint_closed=false;h.draw_pending=false;h.draw_completed=false;h.motor_receipt_valid=false;
    h.phase=H::Captured;cpu_restore.state=CpuRestoreControl::CapturedSeven;
    cpu_restore.evidence.state=cpu_restore.state;
    cpu_history_sync();return 0;
}
uint32_t cpu_history_begin(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const bool first=e.incarnation==0;
    const bool allowed=first?h.phase==H::Captured:
        ((h.phase==H::PredictedSealed||h.phase==H::CorrectedSealed)&&h.endpoint_closed);
    const uint32_t expected=first?e.pre_frame:e.sealed_end;
    if(!allowed||e.incarnation>=3u||!cpu_restore.capsule||h.draw_pending||
       cpu_corrected.reader.active()||!cpu_history_boundary(expected,false)||
       !cpu_restore.capsule->begin_history_incarnation(*match))
        return cpu_history_fail(0x554u,"history forward incarnation begin refused");
    ++e.incarnation;e.current_frame=expected;e.sealed_end=0;
    h.endpoint_mask=0;h.endpoint_batch_bit=0;h.endpoint_closed=false;
    h.draw_completed=false;h.phase=H::Recording;
    cpu_history_sync();return 0;
}
uint32_t cpu_history_source_step(bool replay){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t expected=e.current_frame;
    const bool phase_ok=replay?h.phase==H::Correcting:h.phase==H::Recording;
    const uint32_t elapsed=expected-e.pre_frame;
    if(!phase_ok||!cpu_restore.capsule||h.draw_pending||
       expected<e.pre_frame||elapsed>=7u||e.incarnation<1u||e.incarnation>3u||
       e.restore_count>2u||(replay?e.replay_steps!=elapsed:e.forward_steps!=elapsed)||
       (replay?(expected>=e.sealed_end||e.replay_steps>=7u):e.forward_steps>=7u)||
       (e.incarnation==1u&&e.forward_steps==0u&&e.restore_count==0u&&
        !(h.endpoint_mask&H::ArmedPre))||
       !cpu_history_boundary(expected,true))
        return cpu_history_fail(replay?0x556u:0x557u,"history source-only tick precondition refused");
    std::array<uint8_t,132> before{},after{};
    if(!cpu_history_draw_diagnostics(before))
        return cpu_history_fail(0x558u,"history source-only draw diagnostics unavailable");
    uint32_t sample=0;if(!cpu_history_source_sample(expected,sample))
        return cpu_history_fail(0x559u,"history source sample overflow");
    int result=0;error[0]='\0';{
        CpuClassPieceTraceScope trace(replay?72u:66u,MELEE_WEB_CLASS_PIECE_TRACE_STEP,expected,sample);
        result=melee_web_snapshot_step_raw();
    }
    if(result!=1)return cpu_checked_source_fail(replay?0x55au:0x55bu,
        replay?"history corrective raw step refused":"history forward raw step refused",
        "raw source step returned failure without a diagnostic");
    const uint32_t next=expected+1u;
    if(!cpu_history_draw_diagnostics(after)||before!=after||
       !cpu_history_boundary(next,true)||match->source_frames()!=next||observation.frame!=next)
        return cpu_history_fail(replay?0x55cu:0x55du,
            "history source-only step changed draw work or failed its live boundary");
    e.current_frame=next;
    if(replay){++e.replay_steps;e.source_step_no_draw_mask|=2u;}
    else{++e.forward_steps;e.source_step_no_draw_mask|=1u;}
    h.draw_completed=false;
    cpu_history_sync();return 0;
}
uint32_t cpu_history_seal(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t actual=match?match->source_frames():UINT32_MAX;
    if(h.phase!=H::Recording||!cpu_restore.capsule||h.draw_pending||h.draw_completed||
       actual!=e.current_frame||actual<=e.pre_frame||actual-e.pre_frame>7u||
       e.forward_steps!=actual-e.pre_frame||!cpu_history_boundary(actual,true)||
       !cpu_restore.capsule->seal_history_incarnation(*match))
        return cpu_history_fail(0x55eu,"history actual END seal refused");
    e.sealed_end=actual;e.current_frame=actual;h.endpoint_mask=0;h.endpoint_closed=false;
    h.phase=H::PredictedSealed;cpu_history_sync();return 0;
}
uint32_t cpu_history_archive(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t need=H::PredictedCpu|H::PredictedDraw;
    if(h.phase!=H::PredictedSealed||h.endpoint_closed||h.draw_pending||
       (h.endpoint_mask&need)!=need||!cpu_restore.capsule||
       !cpu_history_boundary(e.sealed_end,false)||
       !cpu_restore.capsule->history_endpoint_admit(*match)||
       !cpu_restore.capsule->archive_history_endpoint(*match))
        return cpu_history_fail(0x55fu,"predicted history endpoint archive refused");
    cpu_history_sync();return 0;
}
uint32_t cpu_history_restore(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t need=H::PredictedCpu|H::PredictedDraw;
    if(h.phase!=H::PredictedSealed||h.endpoint_closed||e.restore_count>=2u||
       (h.endpoint_mask&need)!=need||!cpu_restore.capsule||h.draw_pending||
       !cpu_history_boundary(e.sealed_end,false)||
       !cpu_restore.capsule->history_endpoint_admit(*match))
        return cpu_history_fail(0x560u,"history PRE restore admission refused");
    const auto result=cpu_restore.capsule->restore_history_pre(*match);
    e.commit_result=static_cast<uint32_t>(result);
    if(result!=melee_web::GameplayCpuCommitResult::Ok)
        return cpu_history_fail(0x561u,"aggregate history PRE restore failed");
    audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
    rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
    std::memcpy(&observation,&cpu_restore.seven_pre_observation,sizeof(observation));
    ++e.restore_count;e.current_frame=e.pre_frame;e.replay_steps=0;
    h.endpoint_mask=0;h.endpoint_batch_bit=0;h.endpoint_closed=false;
    h.draw_pending=false;h.draw_completed=false;h.phase=H::Correcting;
    if(!cpu_history_boundary(e.pre_frame,true))
        return cpu_history_fail(0x562u,"restored history PRE observer boundary refused");
    cpu_history_sync();return 0;
}
uint32_t cpu_history_finish(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t span=e.sealed_end-e.pre_frame;
    if(h.phase!=H::Correcting||!cpu_restore.capsule||h.draw_pending||
       e.replay_steps!=span||e.current_frame!=e.sealed_end||
       !cpu_history_boundary(e.sealed_end,true)||
       !cpu_restore.capsule->finish_history_replay(*match))
        return cpu_history_fail(0x563u,"history corrected replay did not reach the same actual END");
    h.endpoint_mask=0;h.endpoint_closed=false;h.draw_completed=false;
    h.phase=H::CorrectedSealed;cpu_history_sync();return 0;
}
uint32_t cpu_history_draw(bool idle){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    if(idle){
        if(!h.draw_pending||payload_admission_boundary(true)!=0||
           !cpu_history_interactive_safe(e.current_frame))
            return cpu_history_fail(0x564u,"history draw idle/source boundary refused");
        if(h.phase==H::Recording){
            if(!cpu_restore.capsule||!cpu_restore.capsule->interval_boundary(*match))
                return cpu_history_fail(0x565u,"history interior draw interval refused");
        }else if((h.phase!=H::PredictedSealed&&h.phase!=H::CorrectedSealed)||
                 !cpu_restore.capsule||!cpu_restore.capsule->history_endpoint_admit(*match))
            return cpu_history_fail(0x566u,"history final draw endpoint boundary refused");
        h.draw_pending=false;h.draw_completed=true;cpu_history_sync();return 0;
    }
    const bool recording=h.phase==H::Recording&&e.forward_steps>0u;
    const bool predicted=(h.phase==H::PredictedSealed&&
        (h.endpoint_mask&H::PredictedCpu)&&!(h.endpoint_mask&H::PredictedDraw));
    const bool corrected=(h.phase==H::CorrectedSealed&&
        (h.endpoint_mask&H::CorrectedCpu)&&!(h.endpoint_mask&H::CorrectedDraw));
    constexpr uint32_t draw_submission_budget=MELEE_WEB_CPU_MOTOR_MAX_SPAN+2u;
    /* At most seven forward-span presentations plus one final presentation for
     * each of the two permitted corrected endpoints. */
    if((!recording&&!predicted&&!corrected)||h.draw_pending||h.draw_completed||
       e.draw_submissions>=draw_submission_budget||
       !cpu_history_boundary(e.current_frame,recording)||
       ((predicted||corrected)&&!cpu_restore.capsule->history_endpoint_admit(*match)))
        return cpu_history_fail(0x567u,"history draw submission is out of order");
    const uint32_t input_frame=e.current_frame?e.current_frame-1u:0u;
    uint32_t sample=0;if(!cpu_history_source_sample(input_frame,sample))
        return cpu_history_fail(0x568u,"history draw sample overflow");
    int result=0;error[0]='\0';{
        CpuClassPieceTraceScope trace(67u,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.current_frame,sample);
        result=melee_web_synctest_draw_present();
    }
    if(result!=1)return cpu_checked_source_fail(0x569u,"history draw refused",
        "source draw returned failure without a diagnostic");
    if(payload_admission_submission_boundary()!=0||
       !cpu_history_interactive_safe(e.current_frame))
        return cpu_history_fail(0x56au,"history draw submission boundary refused");
    h.draw_pending=true;h.draw_completed=false;++e.draw_submissions;cpu_history_sync();return 0;
}
uint32_t cpu_history_close(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t selected_need=h.phase==H::PredictedSealed?
        (H::PredictedCpu|H::PredictedDraw):(H::CorrectedCpu|H::CorrectedDraw);
    if((h.phase!=H::PredictedSealed&&h.phase!=H::CorrectedSealed)||h.endpoint_closed||
       h.draw_pending||(h.endpoint_mask&selected_need)!=selected_need||
       !cpu_restore.capsule||!cpu_history_boundary(e.sealed_end,false)||
       !cpu_restore.capsule->history_endpoint_admit(*match)||
       !cpu_restore.capsule->close_history_endpoint(*match))
        return cpu_history_fail(0x56bu,"selected history endpoint close refused");
    h.endpoint_closed=true;cpu_history_sync();return 0;
}
uint32_t cpu_history_retire(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& e=h.evidence;
    const uint32_t selected_need=h.phase==H::PredictedSealed?
        (H::PredictedCpu|H::PredictedDraw):(H::CorrectedCpu|H::CorrectedDraw);
    if((h.phase!=H::PredictedSealed&&h.phase!=H::CorrectedSealed)||!h.endpoint_closed||
       h.draw_pending||(h.endpoint_mask&selected_need)!=selected_need||
       e.retirements==UINT32_MAX||!cpu_restore.capsule||
       !cpu_history_boundary(e.sealed_end,false)||
       !cpu_restore.capsule->retire_history_confirmed(*match))
        return cpu_history_fail(0x56cu,"confirmed history retirement refused");
    const auto* receipt=melee_web_cpu_interval_motor_commit_receipt();
    if(!receipt||melee_web_cpu_interval_motor_commit_receipt_bytes()!=sizeof(h.motor_receipt))
        return cpu_history_fail(0x56du,"history retirement motor receipt unavailable");
    std::memcpy(h.motor_receipt.data(),receipt,h.motor_receipt.size());
    h.motor_receipt_valid=true;
    ++e.retirements;e.active_capsules=0;h.phase=H::Retired;
    cpu_restore.capsule.reset();cpu_restore.configured_statics.reset();
    cpu_restore.state=CpuRestoreControl::Idle;cpu_restore.evidence.state=cpu_restore.state;
    cpu_restore.close_requested=false;h.draw_pending=false;cpu_history_sync();return 0;
}
uint32_t cpu_history_execute(uint32_t command){
    switch(command){
    case 64:return cpu_history_capture();
    case 65:return cpu_history_begin();
    case 66:return cpu_history_source_step(false);
    case 67:return cpu_history_draw(false);
    case 68:return cpu_history_draw(true);
    case 69:return cpu_history_seal();
    case 70:return cpu_history_archive();
    case 71:return cpu_history_restore();
    case 72:return cpu_history_source_step(true);
    case 73:return cpu_history_finish();
    case 74:return cpu_history_close();
    case 75:return cpu_history_retire();
    default:return cpu_history_fail(0x56eu,"unknown retained history command");
    }
}
uint32_t cpu_history_endpoint_begin(uint32_t view,uint32_t provenance){
    using H=CpuHistoryControl;using V=melee_web::GameplayCpuEndpointView;
    using P=melee_web::GameplayCpuEndpointProvenance;
    auto& h=cpu_history;auto& c=cpu_corrected;auto& e=h.evidence;
    const auto v=static_cast<V>(view);const auto p=static_cast<P>(provenance);
    uint32_t expected=UINT32_MAX,bit=0;bool allowed=false;
    if(v==V::Pre&&p==P::SharedPre){
        expected=e.pre_frame;
        if(h.phase==H::Captured){allowed=true;bit=H::CapturedPre;}
        else if(h.phase==H::Recording&&e.incarnation==1u&&e.restore_count==0u){allowed=true;bit=H::ArmedPre;}
        else if(h.phase==H::Correcting&&e.restore_count>0u){allowed=true;bit=H::RestoredPre;}
    }else if(v==V::CpuEnd&&p==P::Reference&&h.phase==H::PredictedSealed){
        expected=e.sealed_end;allowed=true;bit=H::PredictedCpu;
    }else if(v==V::DrawEnd&&p==P::Reference&&h.phase==H::PredictedSealed){
        expected=e.sealed_end;allowed=h.draw_completed;bit=H::PredictedDraw;
    }else if(v==V::CpuEnd&&p==P::Corrected&&h.phase==H::CorrectedSealed){
        expected=e.sealed_end;allowed=true;bit=H::CorrectedCpu;
    }else if(v==V::DrawEnd&&p==P::Corrected&&h.phase==H::CorrectedSealed){
        expected=e.sealed_end;allowed=h.draw_completed;bit=H::CorrectedDraw;
    }
    if(!h.enabled||!allowed||!cpu_restore.capsule||cpu_restore.first_failure||
       cpu_restore.inflight||c.reader.active()||payload_admission_runtime_boundary()!=0||
       !cpu_history_boundary(expected,false)||
       ((v!=V::Pre)&&!cpu_restore.capsule->history_endpoint_admit(*match)))
        return cpu_history_fail(0x56fu,"history endpoint begin phase/owner admission refused");
    c.endpoint_expected_frame=expected;h.endpoint_batch_bit=bit;
    cpu_restore.inflight=true;
    if(!c.reader.begin_history(*match,*cpu_restore.capsule,v,p)){
        cpu_restore.inflight=false;h.endpoint_batch_bit=0;
        return cpu_history_fail(0x570u,"history endpoint reader admission refused");
    }
    return 0;
}
uint32_t cpu_history_endpoint_record(uint32_t domain,uint32_t kind,uint32_t index){
    auto& h=cpu_history;auto& c=cpu_corrected;
    c.row={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||
       !cpu_history_interactive_safe(c.endpoint_expected_frame))
        return cpu_history_fail(0x571u,"history endpoint row outside admitted batch");
    c.row=c.reader.record(static_cast<melee_web::GameplayCpuEndpointDomain>(domain),
        static_cast<melee_web::GameplayCpuEndpointRowKind>(kind),index);
    if(c.row.status)return cpu_history_fail(0x572u,"history endpoint row refused");
    return 0;
}
uint32_t cpu_history_particle_header(){
    auto& c=cpu_corrected;c.particle_header={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||
       !cpu_history_interactive_safe(c.endpoint_expected_frame))
        return cpu_history_fail(0x573u,"history particle header outside admitted batch");
    if(!c.reader.particle_header(c.particle_header))
        return cpu_history_fail(0x574u,"history particle endpoint header refused");
    return 0;
}
uint32_t cpu_history_particle_row(uint32_t kind,uint32_t index){
    auto& c=cpu_corrected;c.particle_row={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||
       !cpu_history_interactive_safe(c.endpoint_expected_frame))
        return cpu_history_fail(0x575u,"history particle row outside admitted batch");
    if(!c.reader.particle_row(kind,index,c.particle_row))
        return cpu_history_fail(0x576u,"history particle endpoint row refused");
    return 0;
}
uint32_t cpu_history_endpoint_end(){
    using H=CpuHistoryControl;auto& h=cpu_history;auto& c=cpu_corrected;
    if(!cpu_restore.inflight||!c.reader.active())
        return cpu_history_fail(0x577u,"history endpoint end without active batch");
    const bool admitted=c.reader.revalidate();cpu_restore.inflight=false;
    const bool boundary=payload_admission_runtime_boundary()==0&&
        cpu_history_boundary(c.endpoint_expected_frame,false)&&
        (h.endpoint_batch_bit==H::CapturedPre||h.endpoint_batch_bit==H::ArmedPre||
         h.endpoint_batch_bit==H::RestoredPre||
         cpu_restore.capsule->history_endpoint_admit(*match));
    if(!admitted||!boundary||cpu_restore.first_failure){h.endpoint_batch_bit=0;
        return cpu_history_fail(0x578u,"history endpoint batch revalidation refused");}
    h.endpoint_mask|=h.endpoint_batch_bit;h.endpoint_batch_bit=0;
    cpu_history_sync();return 0;
}
}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_cpu_history_enable(uint32_t schema){
    using H=CpuHistoryControl;auto& h=cpu_history;
    if(schema!=1u||h.enabled||cpu_restore.state!=CpuRestoreControl::Idle||
       cpu_restore.inflight||cpu_restore.capsule||cpu_restore.configured_statics||
       cpu_restore.first_failure||cpu_corrected.active||cpu_corrected.reader.active()||
       cpu_corrected_reuse.enabled||payload_admission_state!=PayloadAdmissionState::Idle||
       payload_admission_first_failure||payload_admission_inflight||
       payload_admission_close_requested||payload_admission_shutdown_requested||
       payload_admission_runtime_boundary()!=0||payload_admission_boundary(false)!=0||!match)
        return 0x579u;
    const auto owners=match->diagnostic_cpu_owners();
    if(!owners.storage||!owners.context||!owners.flow||!owners.audio||!owners.world||
       !owners.context_epoch||owners.frame!=observation.frame||
       match->source_frames()!=observation.frame||!observation.ready||observation.ending||
       observation.complete||observation.paused||observation.stocks[0]!=4||observation.stocks[1]!=4)
        return 0x57au;
    for(unsigned i=0;i<2;++i)
        if(melee_web_synctest_motion_boundary(observation.motion[i])!=0||
           !melee_web_synctest_is_mario_kind(observation.players[i].fighter_kind))return 0x57au;
    h.enabled=true;h.session=match.get();h.owners=owners;h.profile_owner=rng_profile_gobj;
    h.configured_seed=configured_source_seed;h.initializer=native_initializer_enabled;
    h.profile=rng_profile_enabled;h.origin_frame=0;h.phase=H::Disabled;
    h.evidence.last_command=0;cpu_history_sync();return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_cpu_history_command(uint32_t command){
    using H=CpuHistoryControl;auto& h=cpu_history;
    if(!h.enabled)return 0x57bu;
    h.evidence.last_command=command;
    if(cpu_restore.inflight)return cpu_history_fail(0x57cu,"history command reentered native work");
    if(payload_admission_runtime_boundary()!=0)
        return cpu_history_fail(0x57du,"history command runtime/Asyncify boundary refused");
    if(cpu_restore.first_failure){cpu_history_sync();return cpu_restore.first_failure;}
    cpu_restore.inflight=true;uint32_t result=0;
    melee_web_lobj_writer_phase(command);melee_web_tobj_writer_phase(command);
    try{result=cpu_history_execute(command);}
    catch(const std::exception& failure){result=cpu_history_fail(0x57eu,failure.what());}
    catch(...){result=cpu_history_fail(0x57fu,"history command threw an unknown exception");}
    cpu_restore.inflight=false;
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    cpu_history_sync();
    return cpu_restore.first_failure?cpu_restore.first_failure:result;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void*
melee_web_synctest_cpu_history_evidence(){cpu_history_sync();return &cpu_history.evidence;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_cpu_history_evidence_bytes(){return sizeof(cpu_history.evidence);}
extern "C" EMSCRIPTEN_KEEPALIVE const void*
melee_web_synctest_cpu_history_motor_evidence(){
    using H=CpuHistoryControl;auto& h=cpu_history;
    if(!h.enabled||(h.phase!=H::PredictedSealed&&h.phase!=H::CorrectedSealed)||
       !cpu_restore.capsule||!match||cpu_restore.inflight||cpu_corrected.reader.active()||
       !cpu_history_interactive_safe(h.evidence.sealed_end)||
       !cpu_restore.capsule->history_endpoint_admit(*match))return nullptr;
    return cpu_restore.capsule->motor_evidence(*match);
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_cpu_history_motor_evidence_bytes(){
    return cpu_history.enabled?melee_web_cpu_interval_motor_evidence_bytes():0u;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void*
melee_web_synctest_cpu_history_motor_commit_receipt(){
    return cpu_history.enabled&&cpu_history.phase==CpuHistoryControl::Retired&&
        cpu_history.motor_receipt_valid?cpu_history.motor_receipt.data():nullptr;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_cpu_history_motor_commit_receipt_bytes(){
    return cpu_history.enabled&&cpu_history.phase==CpuHistoryControl::Retired&&
        cpu_history.motor_receipt_valid?static_cast<uint32_t>(cpu_history.motor_receipt.size()):0u;
}
#endif
