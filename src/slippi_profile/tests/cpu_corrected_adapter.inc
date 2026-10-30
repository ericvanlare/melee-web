/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independent-reference correction diagnostic. The host owns cross-instance
 * equality; native success means only a checked local operation completed. */
#if defined(MELEE_WEB_SYNCTEST_CORRECTED_CPU)
namespace {
struct CpuCorrectedControl {
    enum Phase : uint32_t { Idle, Recording, BaselineDrawSubmitted, BaselineCpuEnd,
        BaselineFinalSubmitted, BaselineReady, Correcting, CorrectedCpuEnd,
        CorrectedFinalSubmitted, CorrectedReady, Complete };
    Phase phase=Idle;
    uint32_t baseline_steps=0,corrected_steps=0;
    bool active=false;
    CpuSevenTickEvidence evidence{};
    Observation pre_observation{};
    melee_web::GameplayCpuEndpointReader reader;
    melee_web::GameplayCpuEndpointRecord row{};
} cpu_corrected;

bool cpu_corrected_active(){return cpu_corrected.active;}
void cpu_corrected_forbid(std::vector<melee_web::CpuIntervalSpan>& spans){
    spans.push_back({&cpu_corrected,sizeof(cpu_corrected)});
}
bool cpu_corrected_diag(unsigned index){
    auto& e=cpu_corrected.evidence;
    if(index>=e.diagnostics.size()||melee_web_synctest_draw_diagnostics_size()!=132)return false;
    const auto* bytes=static_cast<const uint8_t*>(melee_web_synctest_draw_diagnostics());
    if(!bytes)return false;
    std::memcpy(e.diagnostics[index].data(),bytes,132);e.diagnostic_mask|=1u<<index;return true;
}
bool cpu_corrected_interval(){
    return match&&cpu_restore.capsule&&cpu_restore.capsule->interval_boundary(*match);
}
uint32_t cpu_corrected_error(uint32_t code,const char* message){return cpu_fail(code,message);}
uint32_t cpu_corrected_draw(uint32_t command,bool corrected){
    auto& c=cpu_corrected;auto& e=c.evidence;
    if(cpu_boundary()||!cpu_seven_safe(e.post_frame))
        return cpu_corrected_error(0x530u,"corrected-route final draw boundary refused");
    int result=0;error[0]='\0';{
        CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.post_frame,306);
        result=melee_web_synctest_draw_present();
    }
    if(result!=1)return cpu_checked_source_fail(0x531u,"corrected-route final draw refused",
        "source draw returned failure without a diagnostic");
    if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_seven_safe(e.post_frame))
        return cpu_corrected_error(0x532u,"corrected-route final draw submission refused");
    c.phase=corrected?CpuCorrectedControl::CorrectedFinalSubmitted:CpuCorrectedControl::BaselineFinalSubmitted;
    return 0;
}

/* 32 capture+begin, 33..39 seven baseline steps (first six also draw),
 * 40 last baseline draw, 41 idle, 42 restore corrected, 43..49 seven no-draw
 * corrections, 50 final present, 51 idle, 52 release local diagnostic.
 * No command accepts an incoming reference, equality bit, address or size. */
uint32_t cpu_corrected_execute(uint32_t command){
    using C=CpuCorrectedControl;auto& c=cpu_corrected;auto& e=c.evidence;
#if !defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) || !MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    return cpu_corrected_error(0x510u,"corrected route requires the private v18 profile");
#else
    if(command==32){
        if(c.active||cpu_restore.state!=CpuRestoreControl::Configured||!cpu_restore.configured_statics||
           cpu_restore.two_active||cpu_restore.seven_active)
            return cpu_corrected_error(0x511u,"corrected capture requires one unused configuration");
        if(cpu_boundary())return cpu_restore.first_failure;
        c.active=true;e.schema=2;e.mode=7;
        cpu_restore.profile_owner=rng_profile_gobj;cpu_restore.profile=rng_profile_enabled;
        cpu_restore.configured_seed=configured_source_seed;cpu_restore.initializer=native_initializer_enabled;
        e.pre_frame=observation.frame;e.post_frame=e.pre_frame+7u;
        if(e.pre_frame>UINT32_MAX-7u||!cpu_seven_safe(e.pre_frame))
            return cpu_corrected_error(0x512u,"corrected capture source/frame boundary refused");
        std::memcpy(&c.pre_observation,&observation,sizeof(observation));
        cpu_restore.pre_phase=audio_phase;cpu_restore.pre_profile=rng_profile_observation;
        cpu_restore.pre_expected_frame=rng_profile_expected_callback_frame;
        payload_admission_capture_begin_frame=last_completed_frame_id;
        payload_admission_capture_end_frame=last_completed_frame_id;
        if(!cpu_corrected_diag(0))return cpu_corrected_error(0x513u,"corrected capture diagnostics missing");
        melee_web::CpuCaptureDiagnostic diagnostic{};
        cpu_restore.capsule=melee_web::GameplayCpuCheckpoint::capture(*match,
            std::move(cpu_restore.configured_statics),diagnostic,melee_web::CpuCheckpointSpan::SevenTicks);
        if(!cpu_restore.capsule){e.tick_pairs[0].comparison=diagnostic.comparison;
            return cpu_corrected_error(0x514u,diagnostic.message.data());}
        if(!cpu_restore.capsule->begin_record(*match)||!cpu_corrected_interval()||
           cpu_boundary()||!cpu_seven_live_equals(e.diagnostics[0]))
            return cpu_corrected_error(0x515u,"corrected capture/record boundary refused");
        for(unsigned i=0;i<7;++i){e.tick_pairs[i].pre_frame=e.pre_frame+i;
            e.tick_pairs[i].post_frame=e.pre_frame+i+1u;}
        cpu_restore.state=CpuRestoreControl::CapturedSeven;c.phase=C::Recording;return 0;
    }
    if(!c.active||!cpu_restore.capsule)return cpu_corrected_error(0x516u,"corrected command has no capsule");
    if(command>=33&&command<=39){
        const unsigned index=command-33u;
        if(index!=c.baseline_steps||(index==0?c.phase!=C::Recording:c.phase!=C::BaselineDrawSubmitted))
            return cpu_corrected_error(0x517u,"corrected baseline command is out of order");
        if(index&&(payload_admission_boundary(true)!=0||!cpu_corrected_diag(index)))
            return cpu_corrected_error(0x518u,"corrected baseline prior draw is not idle");
        if(cpu_boundary()||!cpu_seven_safe(e.pre_frame+index)||!cpu_corrected_interval())
            return cpu_corrected_error(0x519u,"corrected baseline source boundary refused");
        int result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+index,300u+index);
            result=melee_web_snapshot_step_raw();
        }
        cpu_seven_raw(e.tick_pairs[index],false);
        if(result!=1)return cpu_checked_source_fail(0x51au,"corrected-route baseline step refused",
            "raw source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_seven_safe(e.pre_frame+index+1u)||
           !cpu_corrected_interval()||!cpu_seven_live_equals(e.diagnostics[index]))
            return cpu_corrected_error(0x51bu,"corrected baseline source/draw boundary refused");
        e.baseline_step_no_draw_mask|=1u<<index;++c.baseline_steps;
        if(index==6){
            if(!cpu_restore.capsule->seal_post(*match)){
                e.tick_pairs[index].comparison=cpu_restore.capsule->comparison();
                return cpu_corrected_error(0x51cu,"corrected baseline CPU seal refused");}
            c.phase=C::BaselineCpuEnd;return 0;
        }
        int drawn=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.pre_frame+index+1u,300u+index);
            drawn=melee_web_synctest_draw_present();
        }
        if(drawn!=1)return cpu_checked_source_fail(0x51du,"corrected-route baseline draw refused",
            "source draw returned failure without a diagnostic");
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_seven_safe(e.pre_frame+index+1u))
            return cpu_corrected_error(0x51eu,"corrected baseline draw submission refused");
        c.phase=C::BaselineDrawSubmitted;return 0;
    }
    if(command==40){
        if(c.phase!=C::BaselineCpuEnd)return cpu_corrected_error(0x51fu,"baseline final draw is out of order");
        return cpu_corrected_draw(command,false);
    }
    if(command==41){
        if(c.phase!=C::BaselineFinalSubmitted||payload_admission_boundary(true)!=0||
           !cpu_seven_safe(e.post_frame)||!cpu_corrected_diag(7))
            return cpu_corrected_error(0x520u,"baseline final draw idle refused");
        cpu_seven_raw(e.drawn_pair,false);c.phase=C::BaselineReady;return 0;
    }
    if(command==42){
        if(c.phase!=C::BaselineReady||cpu_boundary()||!cpu_seven_safe(e.post_frame))
            return cpu_corrected_error(0x521u,"corrected restore boundary refused");
        e.commit_result=static_cast<uint32_t>(cpu_restore.capsule->restore_corrected(*match));
        if(e.commit_result!=static_cast<uint32_t>(melee_web::GameplayCpuCommitResult::Ok)){
            e.tick_pairs[0].comparison=cpu_restore.capsule->comparison();
            return cpu_corrected_error(0x522u,"corrected aggregate admission/commit refused");}
        audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
        rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
        std::memcpy(&observation,&c.pre_observation,sizeof(observation));
        if(cpu_boundary()||!cpu_seven_safe(e.pre_frame)||!cpu_corrected_interval())
            return cpu_corrected_error(0x523u,"corrected restored observer/source boundary refused");
        c.phase=C::Correcting;return 0;
    }
    if(command>=43&&command<=49){
        const unsigned index=command-43u;
        if(c.phase!=C::Correcting||index!=c.corrected_steps||cpu_boundary()||
           !cpu_seven_safe(e.pre_frame+index)||!cpu_corrected_interval())
            return cpu_corrected_error(0x524u,"correction command is out of order or source refused");
        int result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+index,300u+index);
            result=melee_web_snapshot_step_raw();
        }
        auto& pair=e.tick_pairs[index];cpu_seven_raw(pair,true);pair.commit_result=e.commit_result;
        if(result!=1)return cpu_checked_source_fail(0x525u,"corrected source step refused",
            "raw source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_seven_safe(e.pre_frame+index+1u)||
           !cpu_corrected_interval()||!cpu_seven_live_equals(e.diagnostics[7])||!cpu_corrected_diag(8u+index))
            return cpu_corrected_error(0x526u,"corrected no-draw source boundary refused");
        e.replay_step_no_draw_mask|=1u<<index;++c.corrected_steps;
        if(index==6){
            if(!cpu_restore.capsule->finish_corrected(*match)){
                pair.comparison=cpu_restore.capsule->comparison();
                return cpu_corrected_error(0x527u,"corrected local endpoint seal refused");}
            c.phase=C::CorrectedCpuEnd;
        }
        return 0;
    }
    if(command==50){
        if(c.phase!=C::CorrectedCpuEnd)return cpu_corrected_error(0x528u,"corrected final draw is out of order");
        /* Host compares independent CPU_END before issuing this command. No
         * incoming host assertion is promoted to native equality authority. */
        return cpu_corrected_draw(command,true);
    }
    if(command==51){
        if(c.phase!=C::CorrectedFinalSubmitted||payload_admission_boundary(true)!=0||
           !cpu_seven_safe(e.post_frame)||!cpu_corrected_diag(15))
            return cpu_corrected_error(0x529u,"corrected final draw idle refused");
        cpu_seven_raw(e.drawn_pair,true);c.phase=C::CorrectedReady;return 0;
    }
    if(command==52){
        if((c.phase!=C::BaselineReady&&c.phase!=C::CorrectedReady)||cpu_boundary())
            return cpu_corrected_error(0x52au,"local diagnostic release is out of order");
        cpu_restore.capsule->disarm();c.phase=C::Complete;cpu_restore.state=CpuRestoreControl::Complete;return 0;
    }
    return cpu_corrected_error(0x52bu,"unknown corrected command");
#endif
}
}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_corrected_command(uint32_t command){
    auto& e=cpu_corrected.evidence;e.last_command=command;
    if(cpu_restore.inflight)return cpu_fail(0x533u,"corrected command reentered native work");
    if(payload_admission_runtime_boundary()!=0)return cpu_fail(0x534u,"corrected command runtime refused");
    if(cpu_restore.first_failure)return cpu_restore.first_failure;
    cpu_restore.inflight=true;uint32_t result=0;
    melee_web_lobj_writer_phase(command);melee_web_tobj_writer_phase(command);
    try{result=cpu_corrected_execute(command);}catch(const std::exception& failure){result=cpu_fail(0x535u,failure.what());}
    cpu_restore.inflight=false;e.state=cpu_corrected.phase;e.first_failure=cpu_restore.first_failure;
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    return cpu_restore.first_failure?cpu_restore.first_failure:result;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_corrected_evidence(){
    cpu_corrected.evidence.first_failure=cpu_restore.first_failure;
    return &cpu_corrected.evidence;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_corrected_evidence_bytes(){return sizeof(cpu_corrected.evidence);}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_begin(uint32_t view,uint32_t provenance){
    using C=CpuCorrectedControl;using V=melee_web::GameplayCpuEndpointView;
    using P=melee_web::GameplayCpuEndpointProvenance;
    auto& c=cpu_corrected;c.row={};
    if(cpu_restore.inflight||!c.active||!cpu_restore.capsule||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||cpu_boundary())return cpu_fail(0x536u,"endpoint begin boundary refused");
    const auto v=static_cast<V>(view);const auto p=static_cast<P>(provenance);
    const bool allowed=(v==V::Pre&&p==P::SharedPre&&c.phase==C::Recording)||
        (v==V::CpuEnd&&((p==P::Reference&&c.phase==C::BaselineCpuEnd)||
                       (p==P::Corrected&&c.phase==C::CorrectedCpuEnd)))||
        (v==V::DrawEnd&&((p==P::Reference&&c.phase==C::BaselineReady)||
                        (p==P::Corrected&&c.phase==C::CorrectedReady)));
    if(!allowed||!cpu_seven_safe(v==V::Pre?c.evidence.pre_frame:c.evidence.post_frame))
        return cpu_fail(0x537u,"endpoint view/provenance/source phase refused");
    cpu_restore.inflight=true;
    if(!c.reader.begin(*match,*cpu_restore.capsule,v,p)){
        cpu_restore.inflight=false;return cpu_fail(0x538u,"endpoint owner admission refused");}
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_record(uint32_t domain,uint32_t kind,uint32_t index){
    auto& c=cpu_corrected;c.row={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||payload_admission_runtime_boundary()!=0)
        return cpu_fail(0x539u,"endpoint record outside synchronous batch");
    c.row=c.reader.record(static_cast<melee_web::GameplayCpuEndpointDomain>(domain),
        static_cast<melee_web::GameplayCpuEndpointRowKind>(kind),index);
    if(c.row.status)return cpu_fail(0x53au,"endpoint record refused");
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_endpoint_data(){return &cpu_corrected.row;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_bytes(){return sizeof(cpu_corrected.row);}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_end(){
    auto& c=cpu_corrected;
    if(!cpu_restore.inflight||!c.reader.active())return cpu_fail(0x53bu,"endpoint end without batch");
    const bool admitted=c.reader.revalidate();cpu_restore.inflight=false;
    if(!admitted||payload_admission_runtime_boundary()!=0||cpu_boundary())
        return cpu_fail(0x53cu,"endpoint batch revalidation refused");
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    return cpu_restore.first_failure;
}
#endif
