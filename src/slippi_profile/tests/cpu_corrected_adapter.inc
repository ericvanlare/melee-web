/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independent-reference correction diagnostic. The host owns cross-instance
 * equality; native success means only a checked local operation completed. */
#include <new>
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
    SlippiParticleCellEndpointHeader particle_header{};
    SlippiParticleCellEndpointRow particle_row{};
    uint32_t endpoint_expected_frame=0;
} cpu_corrected;

struct CpuCorrectedReuseEvidence {
    uint32_t schema=1,bytes=sizeof(CpuCorrectedReuseEvidence);
    uint32_t windows_captured=0,windows_retired=0,active_capsules=0,max_active_capsules=0;
    uint32_t window_first_sample=0,window_pre_frame=0,window_post_frame=0,reserved=0;
};
static_assert(sizeof(CpuCorrectedReuseEvidence)==40);
using CpuCorrectedOwnerTuple=decltype(match->diagnostic_cpu_owners());
struct CpuCorrectedReuseControl {
    bool enabled=false,origin_set=false;
    const melee_web::GameplayMatchSession* session=nullptr;
    CpuCorrectedOwnerTuple owners{};
    uint32_t first_capture_frame=0,previous_post_frame=0;
    HSD_GObj* profile_owner=nullptr;
    uint32_t configured_seed=0;
    bool initializer=false,profile=false;
} cpu_corrected_reuse;
CpuCorrectedReuseEvidence cpu_corrected_reuse_evidence{};

/* Once reuse is explicitly armed, fence off the legacy CPU adapter for the
 * lifetime of this page. The corrected adapter uses its own entry points. */
bool cpu_corrected_active(){return cpu_corrected.active||cpu_corrected_reuse.enabled;}
void cpu_corrected_forbid(std::vector<melee_web::CpuIntervalSpan>& spans){
    spans.push_back({&cpu_corrected,sizeof(cpu_corrected)});
    spans.push_back({&cpu_corrected_reuse,sizeof(cpu_corrected_reuse)});
    spans.push_back({&cpu_corrected_reuse_evidence,sizeof(cpu_corrected_reuse_evidence)});
}
bool cpu_corrected_same_owner(const CpuCorrectedOwnerTuple& a,
                              const CpuCorrectedOwnerTuple& b){
    return a.storage==b.storage&&a.context==b.context&&a.flow==b.flow&&
        a.audio==b.audio&&a.world==b.world&&a.context_epoch==b.context_epoch;
}
bool cpu_corrected_reuse_metadata_header_valid(){
    const auto& metadata=cpu_corrected_reuse_evidence;
    return metadata.schema==1&&metadata.bytes==sizeof(metadata)&&metadata.reserved==0;
}
bool cpu_corrected_reuse_live(uint32_t expected_frame){
    if(!cpu_corrected_reuse.enabled)return true;
    if(!cpu_corrected_reuse.origin_set||!match||match.get()!=cpu_corrected_reuse.session||
       !cpu_tuple_unchanged())return false;
    const auto owners=match->diagnostic_cpu_owners();
    return cpu_corrected_same_owner(owners,cpu_corrected_reuse.owners)&&
        match->source_frames()==expected_frame&&owners.frame==expected_frame&&
        observation.frame==expected_frame&&expected_frame>=cpu_corrected_reuse.first_capture_frame;
}
bool cpu_corrected_reuse_sample(uint32_t input_frame,uint32_t expected_live_frame,
                                uint32_t& sample){
    if(!cpu_corrected_reuse.enabled)return false;
    if(input_frame<cpu_corrected_reuse.first_capture_frame||
       input_frame-cpu_corrected_reuse.first_capture_frame>UINT32_MAX-300u||
       !cpu_corrected_reuse_live(expected_live_frame))return false;
    sample=300u+(input_frame-cpu_corrected_reuse.first_capture_frame);
    return true;
}
bool cpu_corrected_reuse_prepare_capture(uint32_t frame,uint32_t& sample,
                                         CpuCorrectedOwnerTuple& owners){
    if(!cpu_corrected_reuse.enabled)return true;
    auto& evidence=cpu_corrected_reuse_evidence;
    if(!cpu_corrected_reuse_metadata_header_valid()||
       !match||frame!=observation.frame||frame!=match->source_frames()||
       evidence.windows_captured>=2u||
       evidence.active_capsules!=0||evidence.windows_retired!=evidence.windows_captured)
        return false;
    owners=match->diagnostic_cpu_owners();
    if(!owners.storage||!owners.context||!owners.flow||!owners.audio||!owners.world||
       !owners.context_epoch||owners.frame!=frame||match.get()!=cpu_corrected_reuse.session||
       !cpu_corrected_same_owner(owners,cpu_corrected_reuse.owners))return false;
    if(cpu_corrected_reuse.origin_set){
        if(match.get()!=cpu_corrected_reuse.session||
           !cpu_corrected_same_owner(owners,cpu_corrected_reuse.owners)||
           frame<cpu_corrected_reuse.previous_post_frame||
           rng_profile_gobj!=cpu_corrected_reuse.profile_owner||
           configured_source_seed!=cpu_corrected_reuse.configured_seed||
           native_initializer_enabled!=cpu_corrected_reuse.initializer||
           rng_profile_enabled!=cpu_corrected_reuse.profile)return false;
    }else if(evidence.windows_captured||evidence.windows_retired||evidence.max_active_capsules||
             cpu_restore.first_failure||cpu_restore.state!=CpuRestoreControl::Configured){
        return false;
    }
    const uint32_t origin=cpu_corrected_reuse.origin_set?
        cpu_corrected_reuse.first_capture_frame:frame;
    if(frame<origin||frame-origin>UINT32_MAX-300u)return false;
    sample=300u+(frame-origin);
    return true;
}
bool cpu_corrected_reuse_publish_capture(uint32_t sample,uint32_t pre_frame,
                                         uint32_t post_frame,
                                         const CpuCorrectedOwnerTuple& owners){
    if(!cpu_corrected_reuse.enabled)return true;
    auto& reuse=cpu_corrected_reuse;auto& evidence=cpu_corrected_reuse_evidence;
    if(!cpu_corrected_reuse_metadata_header_valid()||evidence.windows_captured>=2u||evidence.active_capsules!=0||
       evidence.windows_captured!=evidence.windows_retired||
       post_frame<pre_frame||post_frame-pre_frame!=7u)return false;
    if(!reuse.origin_set){
        reuse.origin_set=true;reuse.session=match.get();reuse.owners=owners;
        reuse.first_capture_frame=pre_frame;reuse.profile_owner=rng_profile_gobj;
        reuse.configured_seed=configured_source_seed;reuse.initializer=native_initializer_enabled;
        reuse.profile=rng_profile_enabled;
    }else if(match.get()!=reuse.session||!cpu_corrected_same_owner(owners,reuse.owners)||
             pre_frame<reuse.previous_post_frame)return false;
    ++evidence.windows_captured;evidence.active_capsules=1;
    if(evidence.max_active_capsules<evidence.active_capsules)
        evidence.max_active_capsules=evidence.active_capsules;
    evidence.window_first_sample=sample;evidence.window_pre_frame=pre_frame;
    evidence.window_post_frame=post_frame;return true;
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
    uint32_t sample=306u;
    if(cpu_corrected_reuse.enabled&&
       !cpu_corrected_reuse_sample(e.pre_frame+6u,e.post_frame,sample))
        return cpu_corrected_error(0x53du,"corrected-route final draw reuse cursor refused");
    int result=0;error[0]='\0';{
        CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.post_frame,sample);
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
 * corrections, 50 final present, 51 idle, 52 legacy release, 53 confirmed
 * reuse retirement. Command 53 never closes the live match.
 * No command accepts an incoming reference, equality bit, address or size. */
uint32_t cpu_corrected_execute(uint32_t command){
    using C=CpuCorrectedControl;auto& c=cpu_corrected;auto& e=c.evidence;
#if !defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) || !MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    return cpu_corrected_error(0x510u,"corrected route requires the private v18 profile");
#else
    if(command==32){
        if(c.active||cpu_restore.state!=CpuRestoreControl::Configured||!cpu_restore.configured_statics||
           cpu_restore.two_active||cpu_restore.seven_active||
           (cpu_corrected_reuse.enabled&&
            (cpu_restore.capsule||cpu_corrected_reuse_evidence.active_capsules!=0)))
            return cpu_corrected_error(0x511u,"corrected capture requires one unused configuration");
        if(cpu_boundary())return cpu_restore.first_failure;
        const uint32_t capture_frame=match?match->source_frames():0u;
        uint32_t first_sample=300u;CpuCorrectedOwnerTuple reuse_owners{};
        if(cpu_corrected_reuse.enabled&&
           !cpu_corrected_reuse_prepare_capture(capture_frame,first_sample,reuse_owners))
            return cpu_corrected_error(0x512u,"corrected reuse capture owner/cursor refused");
        c.active=true;e.schema=2;e.mode=7;
        cpu_restore.profile_owner=rng_profile_gobj;cpu_restore.profile=rng_profile_enabled;
        cpu_restore.configured_seed=configured_source_seed;cpu_restore.initializer=native_initializer_enabled;
        e.pre_frame=observation.frame;
        if(e.pre_frame>UINT32_MAX-7u||!cpu_seven_safe(e.pre_frame))
            return cpu_corrected_error(0x512u,"corrected capture source/frame boundary refused");
        e.post_frame=e.pre_frame+7u;
        if(e.pre_frame!=capture_frame)
            return cpu_corrected_error(0x512u,"corrected capture source cursor changed");
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
        if(!cpu_corrected_reuse_publish_capture(first_sample,e.pre_frame,e.post_frame,reuse_owners))
            return cpu_corrected_error(0x514u,"corrected reuse capture metadata refused");
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
        uint32_t sample=300u+index;
        if(cpu_corrected_reuse.enabled&&
           !cpu_corrected_reuse_sample(e.pre_frame+index,e.pre_frame+index,sample))
            return cpu_corrected_error(0x519u,"corrected baseline reuse cursor refused");
        int result=0;error[0]='\0';{
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+index,sample);
            result=melee_web_snapshot_step_raw();
        }
        cpu_seven_raw(e.tick_pairs[index],false);
        if(result!=1)return cpu_checked_source_fail(0x51au,"corrected-route baseline step refused",
            "raw source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_seven_safe(e.pre_frame+index+1u)||
           !cpu_corrected_reuse_live(e.pre_frame+index+1u)||
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
            uint32_t draw_sample=sample;
            if(cpu_corrected_reuse.enabled&&
               !cpu_corrected_reuse_sample(e.pre_frame+index,e.pre_frame+index+1u,draw_sample))
                return cpu_corrected_error(0x51eu,"corrected baseline draw reuse cursor refused");
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_DRAW,e.pre_frame+index+1u,draw_sample);
            drawn=melee_web_synctest_draw_present();
        }
        if(drawn!=1)return cpu_checked_source_fail(0x51du,"corrected-route baseline draw refused",
            "source draw returned failure without a diagnostic");
        if(cpu_restore.first_failure||payload_admission_submission_boundary()!=0||!cpu_seven_safe(e.pre_frame+index+1u)||
           !cpu_corrected_reuse_live(e.pre_frame+index+1u))
            return cpu_corrected_error(0x51eu,"corrected baseline draw submission refused");
        c.phase=C::BaselineDrawSubmitted;return 0;
    }
    if(command==40){
        if(c.phase!=C::BaselineCpuEnd)return cpu_corrected_error(0x51fu,"baseline final draw is out of order");
        return cpu_corrected_draw(command,false);
    }
    if(command==41){
        if(c.phase!=C::BaselineFinalSubmitted||payload_admission_boundary(true)!=0||
           !cpu_seven_safe(e.post_frame)||!cpu_corrected_reuse_live(e.post_frame)||!cpu_corrected_diag(7))
            return cpu_corrected_error(0x520u,"baseline final draw idle refused");
        cpu_seven_raw(e.drawn_pair,false);c.phase=C::BaselineReady;return 0;
    }
    if(command==42){
        if(c.phase!=C::BaselineReady||cpu_boundary()||!cpu_seven_safe(e.post_frame)||
           !cpu_corrected_reuse_live(e.post_frame))
            return cpu_corrected_error(0x521u,"corrected restore boundary refused");
        e.commit_result=static_cast<uint32_t>(cpu_restore.capsule->restore_corrected(*match));
        if(e.commit_result!=static_cast<uint32_t>(melee_web::GameplayCpuCommitResult::Ok)){
            e.tick_pairs[0].comparison=cpu_restore.capsule->comparison();
            return cpu_corrected_error(0x522u,"corrected aggregate admission/commit refused");}
        audio_phase=cpu_restore.pre_phase;rng_profile_observation=cpu_restore.pre_profile;
        rng_profile_expected_callback_frame=cpu_restore.pre_expected_frame;
        std::memcpy(&observation,&c.pre_observation,sizeof(observation));
        if(cpu_boundary()||!cpu_seven_safe(e.pre_frame)||!cpu_corrected_reuse_live(e.pre_frame)||
           !cpu_corrected_interval())
            return cpu_corrected_error(0x523u,"corrected restored observer/source boundary refused");
        c.phase=C::Correcting;return 0;
    }
    if(command>=43&&command<=49){
        const unsigned index=command-43u;
        if(c.phase!=C::Correcting||index!=c.corrected_steps||cpu_boundary()||
           !cpu_seven_safe(e.pre_frame+index)||!cpu_corrected_reuse_live(e.pre_frame+index)||
           !cpu_corrected_interval())
            return cpu_corrected_error(0x524u,"correction command is out of order or source refused");
        int result=0;error[0]='\0';{
            uint32_t sample=300u+index;
            if(cpu_corrected_reuse.enabled&&
               !cpu_corrected_reuse_sample(e.pre_frame+index,e.pre_frame+index,sample))
                return cpu_corrected_error(0x524u,"correction reuse cursor refused");
            CpuClassPieceTraceScope trace(command,MELEE_WEB_CLASS_PIECE_TRACE_STEP,e.pre_frame+index,sample);
            result=melee_web_snapshot_step_raw();
        }
        auto& pair=e.tick_pairs[index];cpu_seven_raw(pair,true);pair.commit_result=e.commit_result;
        if(result!=1)return cpu_checked_source_fail(0x525u,"corrected source step refused",
            "raw source step returned failure without a diagnostic");
        if(cpu_restore.first_failure||cpu_boundary()||!cpu_seven_safe(e.pre_frame+index+1u)||
           !cpu_corrected_reuse_live(e.pre_frame+index+1u)||
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
           !cpu_seven_safe(e.post_frame)||!cpu_corrected_reuse_live(e.post_frame)||!cpu_corrected_diag(15))
            return cpu_corrected_error(0x529u,"corrected final draw idle refused");
        cpu_seven_raw(e.drawn_pair,true);c.phase=C::CorrectedReady;return 0;
    }
    if(command==52){
        if(cpu_corrected_reuse.enabled)
            return cpu_corrected_error(0x52cu,"reuse mode requires confirmed retirement command");
        if((c.phase!=C::BaselineReady&&c.phase!=C::CorrectedReady)||cpu_boundary())
            return cpu_corrected_error(0x52au,"local diagnostic release is out of order");
        cpu_restore.capsule->disarm();c.phase=C::Complete;cpu_restore.state=CpuRestoreControl::Complete;return 0;
    }
    if(command==53){
        auto& reuse_evidence=cpu_corrected_reuse_evidence;
        if(!cpu_corrected_reuse.enabled||!cpu_corrected_reuse_metadata_header_valid()||
           (c.phase!=C::BaselineReady&&c.phase!=C::CorrectedReady)||
           !c.active||!cpu_restore.capsule||c.reader.active()||
           reuse_evidence.active_capsules!=1||
           reuse_evidence.windows_captured!=reuse_evidence.windows_retired+1u||
           reuse_evidence.max_active_capsules!=1||cpu_restore.close_requested||
           payload_admission_close_requested||payload_admission_shutdown_requested||
           cpu_boundary()||payload_admission_boundary(true)!=0||
           !cpu_seven_safe(e.post_frame)||!cpu_corrected_reuse_live(e.post_frame))
            return cpu_corrected_error(0x53eu,"confirmed reuse retirement boundary refused");
        const uint32_t post_frame=e.post_frame;
        if(!cpu_restore.capsule->retire_confirmed(*match))
            return cpu_corrected_error(0x53fu,"confirmed reuse retirement proof refused");
        // retire_confirmed revoked the capsule's owners. Destroy it before
        // publishing the slot as idle or permitting another configuration.
        cpu_restore.capsule.reset();
        reuse_evidence.active_capsules=0;
        ++reuse_evidence.windows_retired;
        cpu_corrected_reuse.previous_post_frame=post_frame;
        c.~CpuCorrectedControl();::new(static_cast<void*>(&c)) CpuCorrectedControl{};
        cpu_restore.configured_statics.reset();
        cpu_restore.state=CpuRestoreControl::Idle;
        // Reconstruct in the owned storage: assigning {} can materialize a
        // 93KB seven-tick temporary in every command's Wasm stack frame.
        std::destroy_at(&cpu_restore.evidence);std::construct_at(&cpu_restore.evidence);
        std::destroy_at(&cpu_restore.drawn);std::construct_at(&cpu_restore.drawn);
        std::destroy_at(&cpu_restore.two);std::construct_at(&cpu_restore.two);
        std::destroy_at(&cpu_restore.seven);std::construct_at(&cpu_restore.seven);
        cpu_restore.two_active=false;cpu_restore.seven_active=false;
        cpu_restore.close_requested=false;
        return 0;
    }
    return cpu_corrected_error(0x52bu,"unknown corrected command");
#endif
}
}

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_corrected_command(uint32_t command){
    cpu_corrected.evidence.last_command=command;
    if(cpu_restore.inflight)return cpu_fail(0x533u,"corrected command reentered native work");
    if(payload_admission_runtime_boundary()!=0)return cpu_fail(0x534u,"corrected command runtime refused");
    if(cpu_restore.first_failure)return cpu_restore.first_failure;
    cpu_restore.inflight=true;uint32_t result=0;
    melee_web_lobj_writer_phase(command);melee_web_tobj_writer_phase(command);
    try{result=cpu_corrected_execute(command);}catch(const std::exception& failure){result=cpu_fail(0x535u,failure.what());}
    cpu_restore.inflight=false;
    cpu_corrected.evidence.last_command=command;
    cpu_corrected.evidence.state=cpu_corrected.phase;
    cpu_corrected.evidence.first_failure=cpu_restore.first_failure;
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    return cpu_restore.first_failure?cpu_restore.first_failure:result;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_corrected_reuse_enable(){
#if !defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) || !MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    return 0x540u;
#else
    // This is a one-time page-lifetime opt-in before any CPU adapter state or
    // capsule exists. Refusal is read-only and leaves the legacy path intact.
    if(cpu_corrected_reuse.enabled||cpu_corrected.phase!=CpuCorrectedControl::Idle||
       cpu_corrected.active||cpu_corrected.reader.active()||cpu_restore.inflight||
       cpu_corrected.evidence.first_failure||cpu_corrected.evidence.last_command||
       cpu_corrected.evidence.state||
       cpu_restore.first_failure||cpu_restore.state!=CpuRestoreControl::Idle||
       cpu_restore.capsule||cpu_restore.configured_statics||cpu_restore.two_active||
       cpu_restore.seven_active||payload_admission_state!=PayloadAdmissionState::Idle||
       payload_admission_first_failure||payload_admission_inflight||
       payload_admission_close_requested||payload_admission_shutdown_requested||
       payload_admission_runtime_boundary()!=0||payload_admission_boundary(false)!=0||!match)
        return 0x540u;
    const auto owners=match->diagnostic_cpu_owners();
    if(!owners.storage||!owners.context||!owners.flow||!owners.audio||!owners.world||
       !owners.context_epoch||owners.frame!=observation.frame||
       match->source_frames()!=observation.frame||!observation.ready||observation.ending||
       observation.complete||observation.paused||observation.stocks[0]!=4||
       observation.stocks[1]!=4)
        return 0x541u;
    for(unsigned i=0;i<2;++i)
        if(melee_web_synctest_motion_boundary(observation.motion[i])!=0||
           !melee_web_synctest_is_mario_kind(observation.players[i].fighter_kind))
            return 0x541u;
    const auto& metadata=cpu_corrected_reuse_evidence;
    if(!cpu_corrected_reuse_metadata_header_valid()||metadata.windows_captured||metadata.windows_retired||metadata.active_capsules||
       metadata.max_active_capsules||metadata.window_first_sample||metadata.window_pre_frame||
       metadata.window_post_frame||metadata.reserved)
        return 0x542u;
    cpu_corrected_reuse.enabled=true;
    cpu_corrected_reuse.session=match.get();
    cpu_corrected_reuse.owners=owners;
    return 0;
#endif
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_corrected_reuse_evidence(){
    return &cpu_corrected_reuse_evidence;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_corrected_reuse_evidence_bytes(){
    return sizeof(cpu_corrected_reuse_evidence);
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
    const uint32_t expected_frame=v==V::Pre?c.evidence.pre_frame:c.evidence.post_frame;
    if(!allowed||!cpu_seven_safe(expected_frame)||!cpu_corrected_reuse_live(expected_frame))
        return cpu_fail(0x537u,"endpoint view/provenance/source phase refused");
    c.endpoint_expected_frame=expected_frame;
    cpu_restore.inflight=true;
    if(!c.reader.begin(*match,*cpu_restore.capsule,v,p)){
        cpu_restore.inflight=false;return cpu_fail(0x538u,"endpoint owner admission refused");}
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_record(uint32_t domain,uint32_t kind,uint32_t index){
    auto& c=cpu_corrected;c.row={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||!cpu_corrected_reuse_live(c.endpoint_expected_frame))
        return cpu_fail(0x539u,"endpoint record outside synchronous batch");
    c.row=c.reader.record(static_cast<melee_web::GameplayCpuEndpointDomain>(domain),
        static_cast<melee_web::GameplayCpuEndpointRowKind>(kind),index);
    if(c.row.status)return cpu_fail(0x53au,"endpoint record refused");
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_endpoint_data(){return &cpu_corrected.row;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_bytes(){return sizeof(cpu_corrected.row);}
// These optional sidecar reads belong to the same synchronous endpoint batch.
// They neither begin a new ownership interval nor admit caller-supplied anchors.
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_particle_endpoint_header(){
    auto& c=cpu_corrected;c.particle_header={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||!cpu_corrected_reuse_live(c.endpoint_expected_frame))
        return cpu_fail(0x543u,"particle endpoint header outside synchronous batch");
    if(!c.reader.particle_header(c.particle_header))
        return cpu_fail(0x544u,"particle endpoint header owner refused");
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_particle_endpoint_header_data(){return &cpu_corrected.particle_header;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_particle_endpoint_header_bytes(){return sizeof(cpu_corrected.particle_header);}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_particle_endpoint_row(uint32_t kind,uint32_t index){
    auto& c=cpu_corrected;c.particle_row={};
    if(!cpu_restore.inflight||!c.reader.active()||cpu_restore.first_failure||
       payload_admission_runtime_boundary()!=0||!cpu_corrected_reuse_live(c.endpoint_expected_frame))
        return cpu_fail(0x545u,"particle endpoint row outside synchronous batch");
    if(!c.reader.particle_row(kind,index,c.particle_row))
        return cpu_fail(0x546u,"particle endpoint row owner refused");
    return 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE const void* melee_web_synctest_cpu_particle_endpoint_row_data(){return &cpu_corrected.particle_row;}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_particle_endpoint_row_bytes(){return sizeof(cpu_corrected.particle_row);}
extern "C" EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_cpu_endpoint_end(){
    auto& c=cpu_corrected;
    if(!cpu_restore.inflight||!c.reader.active())return cpu_fail(0x53bu,"endpoint end without batch");
    const bool admitted=c.reader.revalidate();cpu_restore.inflight=false;
    if(!admitted||payload_admission_runtime_boundary()!=0||cpu_boundary()||
       !cpu_corrected_reuse_live(c.endpoint_expected_frame))
        return cpu_fail(0x53cu,"endpoint batch revalidation refused");
    if(cpu_restore.close_requested){cpu_restore.close_requested=false;cpu_close();}
    return cpu_restore.first_failure;
}
#endif
