/* Private browser adapter; included only by the existing Aurora diagnostic
 * synctest. Default fixture/Node/player paths have no activation or exports. */
#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
extern "C" unsigned char __global_base;
extern "C" unsigned char __data_end;
namespace {
struct CpuRestoreEvidence {
    uint32_t schema=1,bytes=sizeof(CpuRestoreEvidence),state=0,first_failure=0;
    uint32_t pre_frame=0,post_frame=0,commit_result=UINT32_MAX,equality=0;
    Observation baseline{},replay{};
    std::array<float,1068> baseline_pcm{},replay_pcm{};
    std::array<uint8_t,44> baseline_pad{},replay_pad{};
    RngProfileObservation baseline_profile{},replay_profile{};
    TransferDiagnostic baseline_transfer{},replay_transfer{};
    uint32_t checked_domains=0,matched_domains=0;
    melee_web::CpuComparison comparison{};
};
static_assert(offsetof(CpuRestoreEvidence,baseline)==32&&sizeof(CpuRestoreEvidence)==11416);
// Independent read-only evidence: the original CPU pair ABI remains unchanged.
struct CpuDrawnEvidence {
    uint32_t schema=1,bytes=sizeof(CpuDrawnEvidence),mode=0,stage=0;
    CpuRestoreEvidence pair{};
    std::array<std::array<uint8_t,132>,3> draw_diagnostics{};
    uint32_t draw_mask=0;
};
static_assert(offsetof(CpuDrawnEvidence,pair)==16&&sizeof(CpuDrawnEvidence)==11832);
struct CpuTwoTickEvidence {
    uint32_t schema=1,bytes=sizeof(CpuTwoTickEvidence),mode=2,state=0;
    uint32_t first_failure=0,pre_frame=0,post_frame=0,commit_result=UINT32_MAX;
    uint32_t cpu_equality=0,drawn_equality=0,checked_cpu=0,matched_cpu=0;
    uint32_t checked_drawn=0,matched_drawn=0,diagnostic_mask=0,last_command=0;
    std::array<CpuRestoreEvidence,2> tick_pairs{};
    CpuRestoreEvidence drawn_pair{};
    std::array<std::array<uint8_t,132>,8> diagnostics{};
};
static_assert(offsetof(CpuTwoTickEvidence,tick_pairs)==64&&
              offsetof(CpuTwoTickEvidence,drawn_pair)==22896&&
              offsetof(CpuTwoTickEvidence,diagnostics)==34312&&sizeof(CpuTwoTickEvidence)==35368);
struct CpuRestoreControl {
    enum State:uint32_t {Idle,Configured,Captured,Equal,DrawSubmitted,Complete,Failed,Closed,BaselineSubmitted,
        CapturedTwo,Baseline0SubmittedTwo,Baseline1SubmittedTwo,TwoEqual,TwoDrawSubmitted};
    State state=Idle;
    uint32_t first_failure=0,pre_phase=0,pre_expected_frame=0;
    RngProfileObservation pre_profile{};
    HSD_GObj* profile_owner=nullptr;
    uint32_t configured_seed=0;
    bool initializer=false,profile=false,inflight=false,close_requested=false;
    bool source_close_inflight=false;
    std::unique_ptr<melee_web::GameplayCpuStatics> configured_statics;
    std::unique_ptr<melee_web::GameplayCpuCheckpoint> capsule;
    CpuRestoreEvidence evidence{};
    CpuDrawnEvidence drawn{};
    CpuTwoTickEvidence two{};
    MeleeWebSourcePayloadFailureWitness failure_witness{1,sizeof(MeleeWebSourcePayloadFailureWitness)};
    MeleeWebClassPieceWitness class_piece_witness{1,sizeof(MeleeWebClassPieceWitness)};
    MeleeWebClassPieceCaptureWitness class_piece_capture_witness{1,sizeof(MeleeWebClassPieceCaptureWitness),0,0,1};
    MeleeWebSourceAllocationOriginWitness origin_witness{1,sizeof(MeleeWebSourceAllocationOriginWitness)};
    MeleeWebLObjWriterEvidence lobj_writer_evidence{1,sizeof(MeleeWebLObjWriterEvidence)};
    MeleeWebTObjWriterEvidence tobj_writer_evidence{1,sizeof(MeleeWebTObjWriterEvidence)};
    bool two_active=false;
} cpu_restore;
bool cpu_drawn_diagnostics(unsigned index){
    if(index>=cpu_restore.drawn.draw_diagnostics.size()||melee_web_synctest_draw_diagnostics_size()!=132)return false;
    const auto* bytes=static_cast<const uint8_t*>(melee_web_synctest_draw_diagnostics());
    if(!bytes)return false;
    std::memcpy(cpu_restore.drawn.draw_diagnostics[index].data(),bytes,132);
    cpu_restore.drawn.draw_mask|=1u<<index;return true;
}
uint32_t cpu_fail(uint32_t code,const char* message){
    if(!cpu_restore.first_failure){
        cpu_restore.first_failure=code?code:0x400u;
        cpu_restore.state=CpuRestoreControl::Failed;
        std::snprintf(error,sizeof(error),"CPU restore: %s",message);
    }
    cpu_restore.evidence.first_failure=cpu_restore.first_failure;
    cpu_restore.evidence.state=cpu_restore.state;
    if(cpu_restore.drawn.mode)cpu_restore.drawn.pair.first_failure=cpu_restore.first_failure;
    if(cpu_restore.two_active){cpu_restore.two.first_failure=cpu_restore.first_failure;cpu_restore.two.state=cpu_restore.state;}
    return cpu_restore.first_failure;
}
uint32_t cpu_checked_source_fail(uint32_t code,const char* boundary,
                                 const char* fallback){
    // Checked source calls write error directly. Preserve only the current
    // call's cause before cpu_fail replaces it, without overlapping buffers.
    char message[sizeof(error)]{};
    std::snprintf(message,sizeof(message),"%s: %s",boundary,
                  error[0]?error:fallback);
    return cpu_fail(code,message);
}
bool cpu_tuple_unchanged(){return match&&cpu_restore.profile_owner==rng_profile_gobj&&
    cpu_restore.configured_seed==configured_source_seed&&cpu_restore.initializer==native_initializer_enabled&&
    cpu_restore.profile==rng_profile_enabled;}
uint32_t cpu_boundary(){
    const uint32_t b=payload_admission_boundary(false);
    if(b)return cpu_fail(0x410u+b,"unsafe source/runtime/Aurora boundary");
    if(payload_admission_state!=PayloadAdmissionState::Idle)
        return cpu_fail(0x430u,"payload dry run and CPU restore are mutually exclusive");
    return 0;
}
bool cpu_native_active(){return cpu_restore.state!=CpuRestoreControl::Idle;}
struct CpuClassPieceTraceScope {
    uint32_t command;
    CpuClassPieceTraceScope(uint32_t command_,uint32_t scope,uint32_t frame,uint32_t sample)
      :command(command_){melee_web_source_memory_class_piece_slab_trace_context(command,scope,frame,sample);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        melee_web_source_memory_slist_trace_context(command,scope,frame,sample);
#endif
    }
    ~CpuClassPieceTraceScope(){melee_web_source_memory_class_piece_slab_trace_context(
        command,MELEE_WEB_CLASS_PIECE_TRACE_COMMAND,0,0);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    melee_web_source_memory_slist_trace_context(command,MELEE_WEB_CLASS_PIECE_TRACE_COMMAND,0,0);
#endif
    }
};
uint32_t cpu_close(){
    if(cpu_restore.capsule)cpu_restore.capsule->disarm();
    cpu_restore.configured_statics.reset();
    if(cpu_restore.state!=CpuRestoreControl::Complete&&cpu_restore.state!=CpuRestoreControl::Failed&&
       cpu_restore.state!=CpuRestoreControl::Idle&&cpu_restore.state!=CpuRestoreControl::Closed)
        cpu_fail(0x431u,"CLOSE before completed restore/replay/present");
    /* Leases deliberately remain here until the ordinary source-close hook
     * observes source owner retirement. CLOSE is not a destruction claim. */
    cpu_restore.state=CpuRestoreControl::Closed;cpu_restore.evidence.state=cpu_restore.state;
    if(cpu_restore.two_active)cpu_restore.two.state=cpu_restore.state;
    return cpu_restore.first_failure;
}

bool cpu_two_diag(unsigned index){
    auto& e=cpu_restore.two;
    if(index>=e.diagnostics.size()||melee_web_synctest_draw_diagnostics_size()!=132)return false;
    const auto* bytes=static_cast<const uint8_t*>(melee_web_synctest_draw_diagnostics());
    if(!bytes)return false;
    std::memcpy(e.diagnostics[index].data(),bytes,132);e.diagnostic_mask|=1u<<index;return true;
}
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
extern "C" int melee_web_snapshot_actual_stage_is_final_destination(void);
#endif
bool cpu_two_safe(uint32_t expected){
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
    if(!melee_web_snapshot_actual_stage_is_final_destination())return false;
#endif
    if(!cpu_tuple_unchanged()||match->source_frames()!=expected||observation.frame!=expected||
       !observation.ready||observation.ending||observation.complete||observation.paused||
       observation.stocks[0]!=4||observation.stocks[1]!=4)return false;
    for(unsigned i=0;i<2;++i)
        if(melee_web_synctest_motion_boundary(observation.motion[i])!=0||
           !melee_web_synctest_is_mario_kind(observation.players[i].fighter_kind))return false;
    return true;
}
void cpu_two_raw(CpuRestoreEvidence& e,bool replay){
    if(replay){e.replay=observation;e.replay_pcm=pcm;e.replay_pad=input_bytes;
        e.replay_profile=rng_profile_observation;e.replay_transfer=transfer_diagnostic;}
    else{e.baseline=observation;e.baseline_pcm=pcm;e.baseline_pad=input_bytes;
        e.baseline_profile=rng_profile_observation;e.baseline_transfer=transfer_diagnostic;}
}
bool cpu_two_raw_equal(const CpuRestoreEvidence& e){
    return !std::memcmp(&e.baseline,&e.replay,sizeof(Observation))&&
        !std::memcmp(e.baseline_pcm.data(),e.replay_pcm.data(),sizeof(pcm))&&
        e.baseline_pad==e.replay_pad&&
        !std::memcmp(&e.baseline_profile,&e.replay_profile,sizeof(RngProfileObservation))&&
        !std::memcmp(&e.baseline_transfer,&e.replay_transfer,sizeof(TransferDiagnostic));
}
bool cpu_two_interval(){
    if(!match||!cpu_restore.capsule||!cpu_restore.capsule->interval_boundary(*match)){
        if(cpu_restore.capsule)cpu_restore.two.tick_pairs[0].comparison=cpu_restore.capsule->comparison();
        return false;
    }return true;
}
uint32_t cpu_execute_two(uint32_t command){
    using C=CpuRestoreControl;auto& e=cpu_restore.two;
    if(command==8)cpu_restore.two_active=true;
#if !defined(MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK) || !MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK
    return cpu_fail(0x4b0u,"two-tick route requires the fixed v6 logical-frame profile");
#else
    if(command==8){
        if(cpu_restore.state!=C::Configured||!cpu_restore.configured_statics)
            return cpu_fail(0x4b1u,"two-tick CAPTURE requires one configuration");
        if(cpu_boundary())return cpu_restore.first_failure;
        cpu_restore.profile_owner=rng_profile_gobj;cpu_restore.profile=rng_profile_enabled;
        cpu_restore.configured_seed=configured_source_seed;cpu_restore.initializer=native_initializer_enabled;
        e.pre_frame=observation.frame;
        if(e.pre_frame>UINT32_MAX-2u||!cpu_two_safe(e.pre_frame))
            return cpu_fail(0x4b2u,"two-tick source frame/interactive owner refused");
        cpu_restore.pre_phase=audio_phase;cpu_restore.pre_profile=rng_profile_observation;
        cpu_restore.pre_expected_frame=rng_profile_expected_callback_frame;
        payload_admission_capture_begin_frame=last_completed_frame_id;
        payload_admission_capture_end_frame=last_completed_frame_id;
        if(!cpu_two_diag(0))return cpu_fail(0x4c4u,"two-tick capture diagnostics unavailable");
        melee_web::CpuCaptureDiagnostic diagnostic{};
        cpu_restore.capsule=melee_web::GameplayCpuCheckpoint::capture(*match,
            std::move(cpu_restore.configured_statics),diagnostic,melee_web::CpuCheckpointSpan::TwoTicks);
        if(!cpu_restore.capsule){e.tick_pairs[0].comparison=diagnostic.comparison;return cpu_fail(0x434u,diagnostic.message.data());}
        if(cpu_boundary())return cpu_restore.first_failure;
        const auto* live=static_cast<const uint8_t*>(melee_web_synctest_draw_diagnostics());
        if(!live||std::memcmp(live,e.diagnostics[0].data(),132))return cpu_fail(0x4c4u,"two-tick capture drew");
        e.post_frame=e.pre_frame+2u;
        for(unsigned i=0;i<2;++i){e.tick_pairs[i].pre_frame=e.pre_frame+i;e.tick_pairs[i].post_frame=e.pre_frame+i+1u;}
        cpu_restore.state=C::CapturedTwo;return 0;
    }
    if(!cpu_restore.two_active||!cpu_restore.capsule)return cpu_fail(0x4b1u,"two-tick command has no capsule");
    if(command==9){
        if(cpu_restore.state!=C::CapturedTwo||cpu_boundary()||!cpu_two_safe(e.pre_frame))
            return cpu_fail(0x4b3u,"first baseline boundary refused");
        if(!cpu_restore.capsule->begin_record(*match)||!cpu_two_interval())
            return cpu_fail(0x4b3u,"two-tick record interval refused");
        int result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(9,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame,300);
            result=melee_web_snapshot_step(300);
        }cpu_two_raw(e.tick_pairs[0],false);
        if(result!=1)return cpu_checked_source_fail(0x4b4u,"baseline sample300 refused",
            "source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_two_safe(e.pre_frame+1u)||!cpu_two_interval())
            return cpu_fail(0x4b4u,"baseline sample300 refused");
        if(!cpu_two_diag(1)||e.diagnostics[0]!=e.diagnostics[1])return cpu_fail(0x4c4u,"baseline CPU300 drew");
        melee_web_tobj_writer_boundary(1,observation.frame);
        int draw_result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(9,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.pre_frame+1u,300);
            draw_result=melee_web_synctest_draw_present();
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
            melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_DRAW);
#endif
        }
        if(draw_result!=1){
            melee_web_tobj_writer_boundary(0,0);
            return cpu_checked_source_fail(0x4b5u,"baseline draw300 refused",
                "source draw returned failure without a diagnostic");
        }
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_two_safe(e.pre_frame+1u)){
            melee_web_tobj_writer_boundary(0,0);return cpu_fail(0x4b5u,"baseline draw300 submission refused");
        }
        melee_web_tobj_writer_boundary(0,0);
        cpu_restore.state=C::Baseline0SubmittedTwo;return 0;
    }
    if(command==10){
        if(cpu_restore.state!=C::Baseline0SubmittedTwo||payload_admission_boundary(true)!=0||
           !cpu_two_safe(e.pre_frame+1u)||!cpu_two_interval())return cpu_fail(0x4b6u,"baseline300 native idle refused");
        if(!cpu_two_diag(2))return cpu_fail(0x4c4u,"baseline300 idle diagnostics unavailable");
        int result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(10,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+1u,301);
            result=melee_web_snapshot_step(301);
        }cpu_two_raw(e.tick_pairs[1],false);
        if(result!=1)return cpu_checked_source_fail(0x4b7u,"baseline sample301 refused",
            "source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_two_safe(e.post_frame)||!cpu_two_interval())
            return cpu_fail(0x4b7u,"baseline sample301 refused");
        if(!cpu_two_diag(3)||e.diagnostics[2]!=e.diagnostics[3])return cpu_fail(0x4c4u,"baseline CPU301 drew");
        if(!cpu_restore.capsule->seal_post(*match))return cpu_fail(0x4b8u,"two-tick final CPU endpoint seal refused");
        melee_web_tobj_writer_boundary(2,observation.frame);
        int draw_result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(10,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.post_frame,301);
            draw_result=melee_web_synctest_draw_present();
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
            melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_DRAW);
#endif
        }
        if(draw_result!=1){
            melee_web_tobj_writer_boundary(0,0);
            return cpu_checked_source_fail(0x4b9u,"baseline draw301 refused",
                "source draw returned failure without a diagnostic");
        }
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_two_safe(e.post_frame)){
            melee_web_tobj_writer_boundary(0,0);return cpu_fail(0x4b9u,"baseline draw301 submission refused");
        }
        melee_web_tobj_writer_boundary(0,0);
        cpu_restore.state=C::Baseline1SubmittedTwo;return 0;
    }
    if(command==11){
        if(cpu_restore.state!=C::Baseline1SubmittedTwo||payload_admission_boundary(true)!=0||!cpu_two_safe(e.post_frame))
            return cpu_fail(0x4bau,"baseline301 native idle refused");
        if(!cpu_two_diag(4))return cpu_fail(0x4c4u,"baseline301 idle diagnostics unavailable");
        if(!cpu_restore.capsule->seal_drawn_reference(*match)){
            e.drawn_pair.comparison=cpu_restore.capsule->comparison();
            return cpu_fail(0x4bbu,"two-tick read-only drawn endpoint seal refused");
        }
        e.drawn_pair.pre_frame=e.post_frame;e.drawn_pair.post_frame=e.post_frame;cpu_two_raw(e.drawn_pair,false);
        if(cpu_boundary()||!cpu_two_safe(e.post_frame))return cpu_fail(0x4bcu,"two-tick precommit boundary refused");
        e.commit_result=static_cast<uint32_t>(cpu_restore.capsule->restore(*match));
        if(e.commit_result!=static_cast<uint32_t>(melee_web::GameplayCpuCommitResult::Ok))
            return cpu_fail(0x450u+e.commit_result,"two-tick aggregate admission/commit refused");
        audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
        rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
        for(unsigned i=0;i<2;++i){
            if(!cpu_two_interval())return cpu_fail(0x4bdu,"two-tick replay interval refused before step");
            int result=0;error[0]='\0';{
                CpuClassPieceTraceScope trace(11,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+i,300u+i);
                result=melee_web_snapshot_step(300u+i);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
                melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_REPLAY_STEP);
#endif
            }
            auto& pair=e.tick_pairs[i];cpu_two_raw(pair,true);pair.commit_result=e.commit_result;
            if(result!=1)return cpu_checked_source_fail(0x4bdu+i,
                i==0?"replay sample300 refused":"replay sample301 refused",
                "source step returned failure without a diagnostic");
            if(cpu_restore.first_failure||cpu_boundary()||!cpu_two_safe(e.pre_frame+i+1u)||!cpu_two_interval())
                return cpu_fail(0x4bdu+i,"two-tick replay source boundary refused");
            if(!cpu_two_diag(5u+i)||e.diagnostics[4]!=e.diagnostics[5u+i])return cpu_fail(0x4c4u,"corrective CPU tick drew");
            if(!cpu_two_raw_equal(pair))return cpu_fail(0x4bfu,"two-tick per-sample raw evidence differs");
            pair.equality=1;
        }
        const bool matched=cpu_restore.capsule->finish_replay(*match);
        e.checked_cpu=cpu_restore.capsule->checked_domains();e.matched_cpu=cpu_restore.capsule->matched_domains();
        auto& final=e.tick_pairs[1];final.checked_domains=e.checked_cpu;final.matched_domains=e.matched_cpu;
        final.comparison=cpu_restore.capsule->comparison();
        if(!matched&&final.comparison.domain==static_cast<uint32_t>(melee_web::CpuComparisonDomain::Payload)&&
           final.comparison.status==2)
        {
            cpu_restore.failure_witness=cpu_restore.capsule->payload_failure_witness();
            cpu_restore.origin_witness=cpu_restore.capsule->allocation_origin_witness();
            cpu_restore.class_piece_witness=cpu_restore.capsule->class_piece_witness();
            cpu_restore.class_piece_capture_witness=cpu_restore.capsule->class_piece_capture_witness();
            if(const auto* writer=cpu_restore.capsule->lobj_writer_witness())cpu_restore.lobj_writer_evidence=*writer;
            if(const auto* writer=cpu_restore.capsule->tobj_writer_witness())cpu_restore.tobj_writer_evidence=*writer;
        }
        if(!matched)return cpu_fail(0x4c0u,"two-tick final CPU typed equality refused");
        e.cpu_equality=1;cpu_restore.state=C::TwoEqual;return 0;
    }
    if(command==12){
        if(cpu_restore.state!=C::TwoEqual||!e.cpu_equality||cpu_boundary()||!cpu_two_safe(e.post_frame))
            return cpu_fail(0x4c1u,"two-tick final present requires accepted CPU equality");
        error[0]='\0';
        const int draw_result=melee_web_synctest_draw_present();
        if(draw_result!=1)return cpu_checked_source_fail(0x4c1u,"two-tick final draw refused",
            "source draw returned failure without a diagnostic");
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_two_safe(e.post_frame))
            return cpu_fail(0x4c1u,"two-tick final draw submission refused");
        cpu_restore.state=C::TwoDrawSubmitted;return 0;
    }
    if(command==13){
        if(cpu_restore.state!=C::TwoDrawSubmitted||payload_admission_boundary(true)!=0||!cpu_two_safe(e.post_frame))
            return cpu_fail(0x4c2u,"two-tick final native idle refused");
        if(!cpu_two_diag(7))return cpu_fail(0x4c4u,"two-tick final diagnostics unavailable");
        cpu_two_raw(e.drawn_pair,true);e.drawn_pair.commit_result=e.commit_result;
        const bool matched=cpu_restore.capsule->matches_drawn_reference(*match);
        e.checked_drawn=cpu_restore.capsule->checked_domains();e.matched_drawn=cpu_restore.capsule->matched_domains();
        e.drawn_pair.checked_domains=e.checked_drawn;e.drawn_pair.matched_domains=e.matched_drawn;
        e.drawn_pair.comparison=cpu_restore.capsule->comparison();
        if(!matched||!cpu_two_raw_equal(e.drawn_pair))return cpu_fail(0x4c3u,"two-tick final drawn endpoint differs");
        e.drawn_pair.equality=1;e.drawn_equality=1;cpu_restore.capsule->disarm();cpu_restore.state=C::Complete;return 0;
    }
    return cpu_fail(0x4c5u,"unknown two-tick command");
#endif
}
uint32_t cpu_execute(uint32_t command){
    if(command>=8&&command<=13)return cpu_execute_two(command);
    if(cpu_restore.two_active)return cpu_fail(0x4c5u,"legacy command cannot change two-tick route");
    using C=CpuRestoreControl;
    if(command==1){
        if(cpu_restore.state!=C::Configured||!cpu_restore.configured_statics)
            return cpu_fail(0x432u,"CAPTURE requires one reviewed configuration");
        if(cpu_boundary())return cpu_restore.first_failure;
        if(observation.stocks[0]!=4||observation.stocks[1]!=4||observation.ending)
            return cpu_fail(0x433u,"CAPTURE requires the fixed four-stock nonending fixture");
        cpu_restore.pre_phase=audio_phase;cpu_restore.pre_profile=rng_profile_observation;
        cpu_restore.pre_expected_frame=rng_profile_expected_callback_frame;
        cpu_restore.profile_owner=rng_profile_gobj;cpu_restore.profile=rng_profile_enabled;
        cpu_restore.configured_seed=configured_source_seed;cpu_restore.initializer=native_initializer_enabled;
        cpu_restore.evidence.pre_frame=observation.frame;
        payload_admission_capture_begin_frame=last_completed_frame_id;
        payload_admission_capture_end_frame=last_completed_frame_id;
        melee_web::CpuCaptureDiagnostic capture_diagnostic{};
        cpu_restore.capsule=melee_web::GameplayCpuCheckpoint::capture(*match,
            std::move(cpu_restore.configured_statics),capture_diagnostic);
        if(!cpu_restore.capsule){
            cpu_restore.evidence.comparison=capture_diagnostic.comparison;
            return cpu_fail(0x434u,capture_diagnostic.message.data());
        }
        /* Preserve the boundary's own first code/message. A post-capture
         * boundary refusal is distinct from a null factory result. */
        if(cpu_boundary())return cpu_restore.first_failure;
        cpu_restore.state=C::Captured;return 0;
    }
    if(command==2||command==6){
        if(cpu_restore.state!=C::Captured||!cpu_restore.capsule||!cpu_tuple_unchanged())
            return cpu_fail(0x435u,"RESTORE_REPLAY requires unchanged captured owner");
        if(cpu_boundary())return cpu_restore.first_failure;
        if(match->source_frames()!=cpu_restore.evidence.pre_frame||
           !cpu_restore.capsule->begin_record(*match))return cpu_fail(0x436u,"record interval refused");
        error[0]='\0';
        if(melee_web_snapshot_step(300)!=1)return cpu_checked_source_fail(0x437u,
            "baseline sample300 refused","source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_tuple_unchanged())return cpu_fail(0x438u,"baseline boundary failed");
        auto& e=cpu_restore.evidence;e.baseline=observation;e.baseline_pcm=pcm;e.baseline_pad=input_bytes;
        e.baseline_profile=rng_profile_observation;e.baseline_transfer=transfer_diagnostic;e.post_frame=observation.frame;
        if(e.post_frame!=e.pre_frame+1u||observation.stocks[0]!=4||observation.stocks[1]!=4||observation.ending||
           !cpu_restore.capsule->seal_post(*match))return cpu_fail(0x439u,"post baseline admission/seal refused");
        if(command==6){
            cpu_restore.drawn.mode=1;
            if(!cpu_drawn_diagnostics(0))return cpu_fail(0x4a8u,"CPU reference diagnostics unavailable");
            error[0]='\0';
            if(melee_web_synctest_draw_present()!=1)return cpu_checked_source_fail(0x4a0u,
                "baseline source draw refused","source draw returned failure without a diagnostic");
            if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0)
                return cpu_fail(0x4a1u,"baseline source draw submission refused");
            cpu_restore.drawn.stage=1;cpu_restore.state=C::BaselineSubmitted;return 0;
        }
    }
    if(command==2||command==7){
        auto& e=cpu_restore.evidence;
        if(command==7){
            if(cpu_restore.state!=C::BaselineSubmitted||cpu_restore.drawn.mode!=1||!cpu_tuple_unchanged())
                return cpu_fail(0x4a2u,"drawn replay requires submitted baseline");
            // Actual native observer must prove idle after the unchanged host wait.
            if(payload_admission_boundary(true)!=0||observation.frame!=e.post_frame)
                return cpu_fail(0x4a3u,"baseline native idle/source boundary refused");
            if(!cpu_restore.capsule->seal_drawn_reference(*match)){
                cpu_restore.drawn.pair.comparison=cpu_restore.capsule->comparison();
                return cpu_fail(0x4a4u,"read-only drawn endpoint capture refused; inspect typed comparison (None/detail1 is opaque grouped helper)");
            }
            auto& d=cpu_restore.drawn.pair;d.pre_frame=e.pre_frame;d.post_frame=e.post_frame;
            d.baseline=observation;d.baseline_pcm=pcm;d.baseline_pad=input_bytes;
            d.baseline_profile=rng_profile_observation;d.baseline_transfer=transfer_diagnostic;
            if(!cpu_drawn_diagnostics(1))return cpu_fail(0x4a9u,"baseline idle diagnostics unavailable");
            cpu_restore.drawn.stage=2;
        }
        if(cpu_boundary()||!cpu_tuple_unchanged())return cpu_fail(0x43au,"precommit boundary failed");
        const auto result=cpu_restore.capsule->restore(*match);e.commit_result=static_cast<uint32_t>(result);
        if(result!=melee_web::GameplayCpuCommitResult::Ok)return cpu_fail(0x450u+e.commit_result,"aggregate admission/commit refused");
        /* Plain fixture POD copies finish the transaction. No callback, await,
         * allocation or fallible source operation occurs between writers. */
        audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
        rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
        error[0]='\0';
        if(melee_web_snapshot_step(300)!=1)return cpu_checked_source_fail(0x470u,
            "replay sample300 refused","source step returned failure without a diagnostic");
        e.replay=observation;e.replay_pcm=pcm;e.replay_pad=input_bytes;
        e.replay_profile=rng_profile_observation;e.replay_transfer=transfer_diagnostic;
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_tuple_unchanged())
            return cpu_fail(0x471u,"replay boundary refused before equality");
        const bool owner_equality=cpu_restore.capsule->finish_replay(*match);
        e.checked_domains=cpu_restore.capsule->checked_domains();
        e.matched_domains=cpu_restore.capsule->matched_domains();
        e.comparison=cpu_restore.capsule->comparison();
        if(!owner_equality||
           std::memcmp(&e.baseline,&e.replay,sizeof(Observation))||
           std::memcmp(e.baseline_pcm.data(),e.replay_pcm.data(),sizeof(pcm))||
           e.baseline_pad!=e.replay_pad||std::memcmp(&e.baseline_profile,&e.replay_profile,sizeof(RngProfileObservation))||
           std::memcmp(&e.baseline_transfer,&e.replay_transfer,sizeof(TransferDiagnostic)))
            return cpu_fail(0x471u,"exact CPU/PAD/native PCM/observation replay equality failed");
        if(cpu_restore.drawn.mode){
            const auto& d=cpu_restore.drawn.pair;
            if(std::memcmp(&d.baseline,&e.replay,sizeof(Observation))||
               std::memcmp(d.baseline_pcm.data(),e.replay_pcm.data(),sizeof(pcm))||d.baseline_pad!=e.replay_pad||
               std::memcmp(&d.baseline_profile,&e.replay_profile,sizeof(RngProfileObservation))||
               std::memcmp(&d.baseline_transfer,&e.replay_transfer,sizeof(TransferDiagnostic)))
                return cpu_fail(0x4a5u,"drawn-reference versus correction raw evidence differs");
            if(!cpu_drawn_diagnostics(2)||cpu_restore.drawn.draw_diagnostics[1]!=cpu_restore.drawn.draw_diagnostics[2])
                return cpu_fail(0x4aau,"correction changed draw diagnostics");
            cpu_restore.drawn.stage=3;
        }else cpu_restore.capsule->disarm();
        e.equality=1;cpu_restore.state=C::Equal;return 0;
    }
    if(command==3){
        if(cpu_restore.state!=C::Equal||!cpu_restore.evidence.equality)
            return cpu_fail(0x480u,"DRAW requires accepted CPU replay equality");
        if(cpu_boundary())return cpu_restore.first_failure;
        error[0]='\0';
        if(melee_web_synctest_draw_present()!=1)return cpu_checked_source_fail(0x481u,
            "final source draw refused","source draw returned failure without a diagnostic");
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0)
            return cpu_fail(0x482u,"final draw submission boundary failed");
        cpu_restore.state=C::DrawSubmitted;return 0;
    }
    if(command==4){
        if(cpu_restore.state!=C::DrawSubmitted||payload_admission_boundary(true)!=0||
           !cpu_tuple_unchanged()||observation.frame!=cpu_restore.evidence.post_frame)
            return cpu_fail(0x483u,"postdraw idle/source boundary refused");
        if(cpu_restore.drawn.mode){
            auto& d=cpu_restore.drawn.pair;
            d.replay=observation;d.replay_pcm=pcm;d.replay_pad=input_bytes;
            d.replay_profile=rng_profile_observation;d.replay_transfer=transfer_diagnostic;
            const bool matched=cpu_restore.capsule->matches_drawn_reference(*match);
            d.checked_domains=cpu_restore.capsule->checked_domains();d.matched_domains=cpu_restore.capsule->matched_domains();
            d.comparison=cpu_restore.capsule->comparison();d.commit_result=cpu_restore.evidence.commit_result;
            if(!matched)return cpu_fail(0x4a6u,"postdraw typed owner equality refused");
            if(std::memcmp(&d.baseline,&d.replay,sizeof(Observation))||
               std::memcmp(d.baseline_pcm.data(),d.replay_pcm.data(),sizeof(pcm))||d.baseline_pad!=d.replay_pad||
               std::memcmp(&d.baseline_profile,&d.replay_profile,sizeof(RngProfileObservation))||
               std::memcmp(&d.baseline_transfer,&d.replay_transfer,sizeof(TransferDiagnostic)))
                return cpu_fail(0x4a7u,"postdraw raw evidence differs");
            d.equality=1;cpu_restore.drawn.stage=4;cpu_restore.capsule->disarm();
        }
        cpu_restore.state=C::Complete;return 0;
    }
    return cpu_fail(0x484u,"unknown command");
}
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_restore_configure(uint32_t address,uint32_t count){
    try {
    if(cpu_restore.state!=CpuRestoreControl::Idle||cpu_restore.inflight||cpu_boundary())return cpu_fail(0x490u,"configuration order/boundary refused");
    const uint64_t bytes=uint64_t(count)*sizeof(melee_web::CpuStaticRow);
    if(!address||!count||address%alignof(melee_web::CpuStaticRow)||bytes>SIZE_MAX||
       uint64_t(address)+bytes>emscripten_get_heap_size())return cpu_fail(0x491u,"configuration extent refused");
    const auto clock=melee_web_source_clock_cpu_storage();const auto cadence=melee_web_source_cadence_cpu_storage();
    const auto boot=melee_web_gameplay_bootstrap_cpu_storage();
    const auto owners=match->diagnostic_cpu_owners();
    const auto audio=melee_web_audio_cpu_storage(owners.audio,melee_web_audio_generation(owners.audio));
    if(!audio.command_budget||!audio.sound_mode)return cpu_fail(0x492u,"typed audio storage unavailable");
    if((clock.profile_phase && clock.profile_phase_bytes!=sizeof(uint32_t)) ||
       (!clock.profile_phase && clock.profile_phase_bytes))return cpu_fail(0x492u,"typed profile clock extent unavailable");
    if(!clock.record||!clock.process_mask||!cadence.record||!boot.ticks||!boot.disabled_links)
        return cpu_fail(0x492u,"typed source storage unavailable");
    std::vector<melee_web::CpuIntervalSpan> forbidden={
        {clock.record,clock.record_bytes},{clock.process_mask,clock.process_mask_bytes},{cadence.record,cadence.record_bytes},
        {boot.ticks,boot.ticks_bytes},{boot.disabled_links,boot.disabled_links_bytes},
        {audio.command_budget,sizeof(uint32_t)},{audio.sound_mode,sizeof(uint32_t)}};
#define CPU_FORBID(value) forbidden.push_back({&(value),sizeof(value)})
    if(clock.profile_phase)forbidden.push_back({clock.profile_phase,clock.profile_phase_bytes});
    CPU_FORBID(cpu_restore);CPU_FORBID(files);CPU_FORBID(match);CPU_FORBID(archive_cache);CPU_FORBID(attempted);CPU_FORBID(error);
    CPU_FORBID(audio_phase);CPU_FORBID(pcm);CPU_FORBID(input_bytes);CPU_FORBID(configured_source_seed);CPU_FORBID(native_initializer_enabled);
    CPU_FORBID(rng_profile_enabled);CPU_FORBID(rng_profile_gobj);CPU_FORBID(rng_profile_observation);CPU_FORBID(rng_profile_expected_callback_frame);
    CPU_FORBID(observation);CPU_FORBID(transfer_diagnostic);CPU_FORBID(initializer_diagnostic);CPU_FORBID(initializer_json);
    CPU_FORBID(ending_mask_diagnostic);CPU_FORBID(ending_mask_json);CPU_FORBID(renderer_ready);CPU_FORBID(renderer_backend);
    CPU_FORBID(last_completed_frame_id);CPU_FORBID(renderer_error_count);CPU_FORBID(fifo_buffer);CPU_FORBID(submission_json);
    CPU_FORBID(audio_range_anchor);CPU_FORBID(audio_range_generation_anchor);
    CPU_FORBID(match_context_range_anchor_address);CPU_FORBID(match_context_range_anchor_extent);
    CPU_FORBID(match_context_range_anchor_world_generation);CPU_FORBID(match_context_range_anchor_owner_epoch);CPU_FORBID(match_context_range_anchor_set);
#if defined(MELEE_WEB_STAGE_ARENA_OWNER_INVENTORY)
    CPU_FORBID(melee_web_synctest_stage_arena_diagnostic_storage);
#endif
    CPU_FORBID(audio_range_wire);CPU_FORBID(match_context_range_wire);CPU_FORBID(payload_admission_snapshot);
    CPU_FORBID(payload_admission_state);CPU_FORBID(payload_admission_first_failure);CPU_FORBID(payload_admission_first_message);
    CPU_FORBID(payload_admission_capture_source_frame);CPU_FORBID(payload_admission_capture_begin_frame);CPU_FORBID(payload_admission_capture_end_frame);
    CPU_FORBID(payload_admission_cleanup_done);CPU_FORBID(payload_admission_inflight);CPU_FORBID(payload_admission_close_requested);CPU_FORBID(payload_admission_shutdown_requested);
#undef CPU_FORBID
    cpu_restore.configured_statics=melee_web::GameplayCpuStatics::configure(
        {reinterpret_cast<const melee_web::CpuStaticRow*>(uintptr_t(address)),count},
        {reinterpret_cast<uintptr_t>(&__global_base),reinterpret_cast<uintptr_t>(&__data_end)},forbidden);
    if(!cpu_restore.configured_statics)return cpu_fail(0x493u,"noncanonical or overlapping configuration");
    cpu_restore.state=CpuRestoreControl::Configured;cpu_restore.evidence.state=cpu_restore.state;return 0;
    }catch(const std::exception& failure){return cpu_fail(0x499u,failure.what());}
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_restore_command(uint32_t command){
    if(cpu_restore.inflight){if(command==5)cpu_restore.close_requested=true;return cpu_fail(0x494u,"reentrant command refused");}
    if(payload_admission_runtime_boundary()!=0)return cpu_fail(0x495u,"runtime/Asyncify boundary refused");
    if(command==5){if(cpu_restore.two_active)cpu_restore.two.last_command=5;return cpu_close();}
    if(cpu_restore.first_failure)return cpu_restore.first_failure;
    if((command>=8&&command<=13)||cpu_restore.two_active)cpu_restore.two.last_command=command;
    melee_web_source_memory_class_piece_slab_trace_context(
        command,MELEE_WEB_CLASS_PIECE_TRACE_COMMAND,0,0);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    melee_web_source_memory_slist_trace_context(command,MELEE_WEB_CLASS_PIECE_TRACE_COMMAND,0,0);
#endif
    melee_web_lobj_writer_phase(command);
    melee_web_tobj_writer_phase(command);
    cpu_restore.inflight=true;uint32_t result=0;
    try{result=cpu_execute(command);}catch(const std::exception& failure){result=cpu_fail(0x496u,failure.what());}
    cpu_restore.inflight=false;
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    cpu_restore.evidence.state=cpu_restore.state;
    if(cpu_restore.two_active)cpu_restore.two.state=cpu_restore.state;
    return cpu_restore.first_failure?cpu_restore.first_failure:result;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_restore_evidence(){return &cpu_restore.evidence;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_restore_evidence_bytes(){return sizeof(cpu_restore.evidence);}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_drawn_evidence(){return &cpu_restore.drawn;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_drawn_evidence_bytes(){return sizeof(cpu_restore.drawn);}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_two_tick_evidence(){return &cpu_restore.two;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_two_tick_evidence_bytes(){return sizeof(cpu_restore.two);}
// Private fixed owned buffer; never export a C sret function or caller pointer.
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_payload_failure_witness(){return &cpu_restore.failure_witness;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_payload_failure_witness_bytes(){return sizeof(cpu_restore.failure_witness);}
extern "C" int melee_web_synctest_cpu_before_source_close(){
    if(cpu_restore.inflight){cpu_restore.close_requested=true;cpu_fail(0x497u,"source close reentered command");return 0;}
    if(cpu_restore.capsule&&payload_admission_runtime_boundary()!=0){cpu_fail(0x49au,"source close requires normal runtime boundary");return 0;}
    if(cpu_restore.capsule)cpu_restore.capsule->disarm();return 1;
}
extern "C" int melee_web_synctest_cpu_after_source_close(){
    if(cpu_restore.capsule){
        if(!cpu_restore.capsule->source_owners_retired(match.get()))return 0;
        cpu_restore.capsule.reset();
    }return 1;
}
#endif

extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_allocation_origin_witness(){return &cpu_restore.origin_witness;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_allocation_origin_witness_bytes(){return sizeof(cpu_restore.origin_witness);}

extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_class_piece_witness(){return &cpu_restore.class_piece_witness;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_class_piece_witness_bytes(){return sizeof(cpu_restore.class_piece_witness);}

extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_class_piece_capture_witness(){return &cpu_restore.class_piece_capture_witness;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_class_piece_capture_witness_bytes(){return sizeof(cpu_restore.class_piece_capture_witness);}

#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_lobj_writer_evidence(){if(cpu_restore.capsule)if(const auto* writer=cpu_restore.capsule->lobj_writer_witness())cpu_restore.lobj_writer_evidence=*writer;return &cpu_restore.lobj_writer_evidence;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_lobj_writer_evidence_bytes(){return sizeof(cpu_restore.lobj_writer_evidence);}
#endif

#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_tobj_writer_evidence(){if(cpu_restore.capsule)if(const auto* writer=cpu_restore.capsule->tobj_writer_witness())cpu_restore.tobj_writer_evidence=*writer;return &cpu_restore.tobj_writer_evidence;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_tobj_writer_evidence_bytes(){return sizeof(cpu_restore.tobj_writer_evidence);}

#endif
