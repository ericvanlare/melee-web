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
struct CpuRestoreControl {
    enum State:uint32_t {Idle,Configured,Captured,Equal,DrawSubmitted,Complete,Failed,Closed};
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
} cpu_restore;
uint32_t cpu_fail(uint32_t code,const char* message){
    if(!cpu_restore.first_failure){
        cpu_restore.first_failure=code?code:0x400u;
        cpu_restore.state=CpuRestoreControl::Failed;
        std::snprintf(error,sizeof(error),"CPU restore: %s",message);
    }
    cpu_restore.evidence.first_failure=cpu_restore.first_failure;
    cpu_restore.evidence.state=cpu_restore.state;return cpu_restore.first_failure;
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
uint32_t cpu_close(){
    if(cpu_restore.capsule)cpu_restore.capsule->disarm();
    cpu_restore.configured_statics.reset();
    if(cpu_restore.state!=CpuRestoreControl::Complete&&cpu_restore.state!=CpuRestoreControl::Failed&&
       cpu_restore.state!=CpuRestoreControl::Idle&&cpu_restore.state!=CpuRestoreControl::Closed)
        cpu_fail(0x431u,"CLOSE before completed restore/replay/present");
    /* Leases deliberately remain here until the ordinary source-close hook
     * observes source owner retirement. CLOSE is not a destruction claim. */
    cpu_restore.state=CpuRestoreControl::Closed;cpu_restore.evidence.state=cpu_restore.state;
    return cpu_restore.first_failure;
}
uint32_t cpu_execute(uint32_t command){
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
        cpu_restore.capsule=melee_web::GameplayCpuCheckpoint::capture(*match,std::move(cpu_restore.configured_statics));
        if(!cpu_restore.capsule||cpu_boundary())return cpu_fail(0x434u,"typed aggregate capture refused");
        cpu_restore.state=C::Captured;return 0;
    }
    if(command==2){
        if(cpu_restore.state!=C::Captured||!cpu_restore.capsule||!cpu_tuple_unchanged())
            return cpu_fail(0x435u,"RESTORE_REPLAY requires unchanged captured owner");
        if(cpu_boundary())return cpu_restore.first_failure;
        if(match->source_frames()!=cpu_restore.evidence.pre_frame||
           !cpu_restore.capsule->begin_record(*match))return cpu_fail(0x436u,"record interval refused");
        if(melee_web_snapshot_step(300)!=1)return cpu_fail(0x437u,"baseline sample300 failed");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_tuple_unchanged())return cpu_fail(0x438u,"baseline boundary failed");
        auto& e=cpu_restore.evidence;e.baseline=observation;e.baseline_pcm=pcm;e.baseline_pad=input_bytes;
        e.baseline_profile=rng_profile_observation;e.baseline_transfer=transfer_diagnostic;e.post_frame=observation.frame;
        if(e.post_frame!=e.pre_frame+1u||observation.stocks[0]!=4||observation.stocks[1]!=4||observation.ending||
           !cpu_restore.capsule->seal_post(*match))return cpu_fail(0x439u,"post baseline admission/seal refused");
        if(cpu_boundary()||!cpu_tuple_unchanged())return cpu_fail(0x43au,"precommit boundary failed");
        const auto result=cpu_restore.capsule->restore(*match);e.commit_result=static_cast<uint32_t>(result);
        if(result!=melee_web::GameplayCpuCommitResult::Ok)return cpu_fail(0x450u+e.commit_result,"aggregate admission/commit refused");
        /* Plain fixture POD copies finish the transaction. No callback, await,
         * allocation or fallible source operation occurs between writers. */
        audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
        rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
        if(melee_web_snapshot_step(300)!=1)return cpu_fail(0x470u,"replay sample300 failed");
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
        e.equality=1;cpu_restore.capsule->disarm();cpu_restore.state=C::Equal;return 0;
    }
    if(command==3){
        if(cpu_restore.state!=C::Equal||!cpu_restore.evidence.equality)
            return cpu_fail(0x480u,"DRAW requires accepted CPU replay equality");
        if(cpu_boundary())return cpu_restore.first_failure;
        if(melee_web_synctest_draw_present()!=1)return cpu_fail(0x481u,"final source draw failed");
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0)
            return cpu_fail(0x482u,"final draw submission boundary failed");
        cpu_restore.state=C::DrawSubmitted;return 0;
    }
    if(command==4){
        if(cpu_restore.state!=C::DrawSubmitted||payload_admission_boundary(true)!=0||
           !cpu_tuple_unchanged()||observation.frame!=cpu_restore.evidence.post_frame)
            return cpu_fail(0x483u,"postdraw idle/source boundary refused");
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
    if(!clock.record||!clock.process_mask||!cadence.record||!boot.ticks||!boot.disabled_links)
        return cpu_fail(0x492u,"typed source storage unavailable");
    std::vector<melee_web::CpuIntervalSpan> forbidden={
        {clock.record,clock.record_bytes},{clock.process_mask,clock.process_mask_bytes},{cadence.record,cadence.record_bytes},
        {boot.ticks,boot.ticks_bytes},{boot.disabled_links,boot.disabled_links_bytes},
        {audio.command_budget,sizeof(uint32_t)},{audio.sound_mode,sizeof(uint32_t)}};
#define CPU_FORBID(value) forbidden.push_back({&(value),sizeof(value)})
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
    if(command==5)return cpu_close();
    if(cpu_restore.first_failure)return cpu_restore.first_failure;
    cpu_restore.inflight=true;uint32_t result=0;
    try{result=cpu_execute(command);}catch(const std::exception& failure){result=cpu_fail(0x496u,failure.what());}
    cpu_restore.inflight=false;
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    cpu_restore.evidence.state=cpu_restore.state;
    return cpu_restore.first_failure?cpu_restore.first_failure:result;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_restore_evidence(){return &cpu_restore.evidence;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_restore_evidence_bytes(){return sizeof(cpu_restore.evidence);}
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
