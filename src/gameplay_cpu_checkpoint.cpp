#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_cpu_interval.h"
#include "slippi_profile/particle_source_schedule.h"
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>
#include <cstdio>
extern "C" int melee_web_action_interval_retire_admit(
    const MeleeWebActionInterval*);
extern "C" int melee_web_action_interval_retire_confirmed(
    MeleeWebActionInterval*);
namespace melee_web {
namespace {
bool same_range(const MeleeWebAudioOwnedRange& a,const MeleeWebAudioOwnedRange& b) noexcept {
    return a.kind==b.kind&&a.address==b.address&&a.byte_extent==b.byte_extent&&
        a.owner_identity==b.owner_identity&&a.generation==b.generation;
}
}
std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture(
    const GameplayMatchSession& session,std::unique_ptr<GameplayCpuStatics> statics,
    CpuCaptureDiagnostic& diagnostic,CpuCheckpointSpan span) noexcept {
    return capture_checked(session,std::move(statics),diagnostic,
                           static_cast<std::uint32_t>(span),false);
}
std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture_span(
    const GameplayMatchSession& session,std::unique_ptr<GameplayCpuStatics> statics,
    CpuCaptureDiagnostic& diagnostic,std::uint32_t span) noexcept {
    return capture_checked(session,std::move(statics),diagnostic,span,true);
}
std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture_checked(
    const GameplayMatchSession& session,std::unique_ptr<GameplayCpuStatics> statics,
    CpuCaptureDiagnostic& diagnostic,std::uint32_t span,bool allow_variable) noexcept {
    /* Internal caller supplies fresh private stack storage. No diagnostic
     * pointer is exported and no source owner is written by this attribution. */
    diagnostic={};
    CpuComparisonDomain stage=CpuComparisonDomain::Owner;
    auto refused=[&](CpuComparisonDomain domain,std::uint32_t detail,const char* text){
        diagnostic.comparison={static_cast<std::uint32_t>(domain),1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};
        std::snprintf(diagnostic.message.data(),diagnostic.message.size(),"%s",text);
    };
    auto code=[](CpuCaptureRefusal value){return static_cast<std::uint32_t>(value);};
    const auto ticks=span;
    if((allow_variable&&(ticks==0||ticks>7u))||
       (!allow_variable&&ticks!=static_cast<std::uint32_t>(CpuCheckpointSpan::OneTick)&&
        ticks!=static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks)&&
        ticks!=static_cast<std::uint32_t>(CpuCheckpointSpan::SevenTicks))){
        refused(stage,code(CpuCaptureRefusal::InvalidSpan),allow_variable?
            "Reusable CPU capsule interval must be one through seven ticks":
            "CPU capsule interval is not one, two, or seven ticks");return {};
    }
    std::unique_ptr<GameplayCpuCheckpoint> c(new(std::nothrow) GameplayCpuCheckpoint(ticks,allow_variable));
    if(!c){refused(stage,code(CpuCaptureRefusal::CapsuleAllocation),"CPU capsule allocation returned null");return {};}
    if(!statics){refused(CpuComparisonDomain::Statics,code(CpuCaptureRefusal::MissingStatics),"Configured static owner is missing");return {};}
    try {
        c->owner_=session.diagnostic_cpu_owners();
        if(!c->owner_.storage||!c->owner_.world){
            refused(stage,code(CpuCaptureRefusal::OwnerTuple),"Live session CPU owner tuple is unavailable");return {};
        }
        if(c->owner_.frame>std::numeric_limits<std::uint32_t>::max()-ticks){
            refused(stage,code(CpuCaptureRefusal::SourceFrameWrap),"Source frame cannot advance without wrapping");return {};
        }
        c->statics_=std::move(statics);c->commit_.statics=c->statics_.get();
        c->commit_.audio=c->owner_.audio;c->commit_.flow=c->owner_.flow;c->commit_.context=c->owner_.context;
        char leaf_error[160]{};
        stage=CpuComparisonDomain::Bootstrap;
        c->commit_.bootstrap=melee_web_gameplay_bootstrap_snapshot_capture(leaf_error,sizeof(leaf_error));
        if(!c->commit_.bootstrap){refused(stage,code(CpuCaptureRefusal::NullSnapshot),leaf_error[0]?leaf_error:"Bootstrap snapshot capture returned null without error text");return {};}
        stage=CpuComparisonDomain::Flow;leaf_error[0]=0;
        c->commit_.flow_snapshot=melee_web_match_flow_cpu_snapshot_capture(c->owner_.flow,leaf_error,sizeof(leaf_error));
        if(!c->commit_.flow_snapshot){refused(stage,code(CpuCaptureRefusal::NullSnapshot),leaf_error[0]?leaf_error:"Flow CPU snapshot capture returned null without error text");return {};}
        stage=CpuComparisonDomain::Context;leaf_error[0]=0;
        c->commit_.context_snapshot=melee_web_match_context_snapshot_capture(c->owner_.context,leaf_error,sizeof(leaf_error));
        if(!c->commit_.context_snapshot){refused(stage,code(CpuCaptureRefusal::NullSnapshot),leaf_error[0]?leaf_error:"Match-context snapshot capture returned null without error text");return {};}
        stage=CpuComparisonDomain::Audio;
        c->commit_.audio_snapshot=allow_variable?
            melee_web_audio_snapshot_capture_with_stream(c->owner_.audio):
            melee_web_audio_snapshot_capture(c->owner_.audio);
        if(!c->commit_.audio_snapshot){refused(stage,code(CpuCaptureRefusal::NullSnapshot),"Audio snapshot capture returned null (opaque capture API)");return {};}
        stage=CpuComparisonDomain::Payload;
        auto payload=melee_web_source_memory_payload_snapshot_capture();
        c->commit_.payload=payload.snapshot;
        if(payload.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||!c->commit_.payload){
            refused(stage,static_cast<std::uint32_t>(payload.status),"Source payload capture refused; detail is actual payload status (null snapshot if status is OK)");return {};
        }
        stage=CpuComparisonDomain::Actions;
        c->commit_.actions=melee_web_action_interval_capture();
        if(!c->commit_.actions){refused(stage,code(CpuCaptureRefusal::NullSnapshot),"ActionStore interval capture returned null (opaque capture API)");return {};}
        stage=CpuComparisonDomain::Statics;
        if(!c->statics_->capture()){refused(stage,code(CpuCaptureRefusal::StaticCapture),"Configured static snapshot capture returned false");return {};}
        stage=CpuComparisonDomain::Audio;
        c->audio_generation_=melee_web_audio_generation(c->owner_.audio);
        if(!c->audio_generation_){refused(stage,code(CpuCaptureRefusal::AudioGeneration),"Captured audio owner generation is zero");return {};}
        c->commit_.lobj_anchor=slippi_lobj_anchor_capture();
        if(!slippi_lobj_anchor_admit(c->commit_.lobj_anchor)){
            std::snprintf(leaf_error,sizeof(leaf_error),"Canonical LObj owner capture refused (%u)",(slippi_lobj_failure()|(slippi_tobj_failure()<<16)));
            refused(CpuComparisonDomain::Owner,code(CpuCaptureRefusal::LObjOwner),leaf_error);return {};
        }
        c->commit_.tobj_anchor=slippi_tobj_anchor_capture();
        if(!slippi_tobj_anchor_admit(c->commit_.tobj_anchor)){
            std::snprintf(leaf_error,sizeof(leaf_error),"Canonical TObj owner capture refused (%u)",slippi_tobj_failure());
            refused(CpuComparisonDomain::Owner,code(CpuCaptureRefusal::TObjOwner),leaf_error);return {};
        }
        c->commit_.particle_anchor=slippi_particle_presentation_anchor_capture();
        if(!slippi_particle_presentation_anchor_admit(c->commit_.particle_anchor)){
            refused(CpuComparisonDomain::Owner,code(CpuCaptureRefusal::ParticleOwner),
                    "Particle constructor ownership capture refused");return {};
        }
        // Diagnostic arm is not a CPU-admission condition. It owns no source
        // memory, and its serial/table are never captured or restored.
        c->class_piece_anchor_=melee_web_class_piece_arm();
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        if(span==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
            (void)melee_web_source_memory_slist_target_trace_arm(
                c->commit_.payload,c->class_piece_anchor_,0x80d349c0u,8u,0u);
        if(span==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
            melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_CAPTURE);
#endif
#if !(defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE)
        c->lobj_writer_session_=melee_web_lobj_writer_arm(c->commit_.payload,c->class_piece_anchor_,span==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks)?8u:1u);
#endif
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE && !(defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE)
        if(span==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
            c->tobj_writer_session_=melee_web_tobj_writer_arm(c->commit_.payload,c->class_piece_anchor_,8,c->owner_.frame);
#endif
        // v9 uses stack-local texture matrices: the historical source-writer
        // diagnostic is inapplicable, not a successful source-write proof.
        // v7 relocates lvec: the historical SOURCE writer diagnostic is
        // inapplicable, not a successful source-write observation. Session 0
        // leaves its fixed evidence unavailable; strict payload checks remain.
        return c;
    }catch(const std::bad_alloc&){refused(stage,code(CpuCaptureRefusal::AllocationException),"CPU capture leaf threw std::bad_alloc");return {};}
    catch(...){refused(stage,code(CpuCaptureRefusal::UnexpectedException),"CPU capture leaf threw an unexpected exception");return {};}
}
bool GameplayCpuCheckpoint::same_owner(const GameplayMatchSession& session) const noexcept {
    /* Re-read from the current live session before using any captured handle;
     * typed snapshots then check epoch/generation before source dereferences. */
    const auto live=session.diagnostic_cpu_owners();
    return live.storage&&live.storage==owner_.storage&&live.context==owner_.context&&
        live.flow==owner_.flow&&live.audio==owner_.audio&&live.world==owner_.world&&
        live.context_epoch==owner_.context_epoch&&
        slippi_particle_presentation_anchor_admit(commit_.particle_anchor);
}
bool GameplayCpuCheckpoint::begin_record(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Captured||!same_owner(session)||
       (!slippi_lobj_anchor_admit(commit_.lobj_anchor)||!slippi_tobj_anchor_admit(commit_.tobj_anchor))||
       session.source_frames()!=owner_.frame)return false;
    commit_.effect_token=melee_web_cpu_interval_begin();
    motor_token_=commit_.effect_token;
    if(!commit_.effect_token||melee_web_action_interval_begin_record(commit_.actions)!=1){
        disarm();phase_=Phase::Failed;return false;
    }
    if(!melee_web_audio_snapshot_interval_begin(owner_.audio,commit_.audio_snapshot,
                                               commit_.effect_token)){
        disarm();phase_=Phase::Failed;return false;
    }
    if(reusable_loop_&&!melee_web_cpu_interval_motor_capture_begin(
       commit_.effect_token,owner_.world,owner_.context,owner_.context_epoch,
       static_cast<std::uint32_t>(owner_.frame),span_)){
        disarm();phase_=Phase::Failed;return false;
    }
    phase_=Phase::Recording;
    /* The next ordinary fixture step can replace source Clip borrowers. From
     * here on cleanup retains both pre and first-pass leases until teardown. */
    source_step_possible_=true;return true;
}
bool GameplayCpuCheckpoint::interval_boundary(const GameplayMatchSession& session) noexcept {
    comparison_={};
    auto refuse=[this](CpuComparisonDomain domain,std::uint32_t detail=0){
        comparison_={static_cast<std::uint32_t>(domain),1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};
        if(phase_==Phase::Correcting)phase_=Phase::Failed;
        return false;
    };
    if((phase_!=Phase::Recording&&phase_!=Phase::Replaying&&phase_!=Phase::Correcting)||
       !same_owner(session))
        return refuse(CpuComparisonDomain::Owner);
    if((!slippi_lobj_anchor_admit(commit_.lobj_anchor)||!slippi_tobj_anchor_admit(commit_.tobj_anchor)))
        return refuse(CpuComparisonDomain::Owner,(slippi_lobj_failure()|(slippi_tobj_failure()<<16)));
    if(!melee_web_cpu_interval_admit(commit_.effect_token))
        return refuse(CpuComparisonDomain::Effects,melee_web_cpu_interval_failure());
    // Unlike failure_code, this pure active guard does not require the final
    // replay slots/cache or complete event cursor at an intermediate tick.
    const auto failure=melee_web_action_interval_active_failure(commit_.actions);
    if(failure)return refuse(CpuComparisonDomain::Actions,failure);
    return true;
}
bool GameplayCpuCheckpoint::capture_post_bytes(const GameplayMatchSession&,std::vector<PostBytes>& bytes,AudioRanges& ranges,std::size_t& count) {
    MeleeWebMatchContextRange context{};
    if(melee_web_match_context_range_read(owner_.context,&context,nullptr,0)!=
       MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE)return false;
    const auto boot=melee_web_gameplay_bootstrap_cpu_storage();
    if(!boot.ticks||!boot.disabled_links)return false;
    auto save=[&bytes](const void* p,std::size_t n,CpuComparisonDomain domain,std::uint32_t kind){
        if(!p||!n)return false;
        PostBytes value;value.address=p;value.domain=domain;value.kind=kind;value.bytes.resize(n);
        std::memcpy(value.bytes.data(),p,n);bytes.push_back(std::move(value));return true;
    };
    if(!save(context.address,context.byte_extent,CpuComparisonDomain::Context,0)||
       !save(boot.ticks,boot.ticks_bytes,CpuComparisonDomain::Bootstrap,0)||
       !save(boot.disabled_links,boot.disabled_links_bytes,CpuComparisonDomain::Bootstrap,1))return false;
    struct Visit {std::vector<PostBytes>* bytes;AudioRanges* ranges;std::size_t* count;bool failed;};Visit visit{&bytes,&ranges,&count,false};
    auto visitor=[](const MeleeWebAudioOwnedRange* range,void* data)->int {
        auto& v=*static_cast<Visit*>(data);
        if(!range||!range->address||!range->byte_extent||*v.count>=v.ranges->size()){
            v.failed=true;return 0;
        }
        try {
            PostBytes value;value.address=range->address;value.domain=CpuComparisonDomain::Audio;value.kind=range->kind;value.bytes.resize(range->byte_extent);
            std::memcpy(value.bytes.data(),range->address,range->byte_extent);
            v.bytes->push_back(std::move(value));(*v.ranges)[(*v.count)++]=*range;
            return 1;
        }catch(...){v.failed=true;return 0;}
    };
    const auto scalar=melee_web_audio_cpu_storage(owner_.audio,audio_generation_);
    return melee_web_audio_visit_owned_ranges(owner_.audio,audio_generation_,
        MELEE_WEB_AUDIO_OWNED_RANGE_MAX,visitor,&visit)==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE&&
        !visit.failed&&count&&
        save(scalar.command_budget,sizeof(uint32_t),CpuComparisonDomain::Audio,0x100)&&
        save(scalar.sound_mode,sizeof(uint32_t),CpuComparisonDomain::Audio,0x101);
}
bool GameplayCpuCheckpoint::seal_post(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Recording)return false;
    try {
        if(!same_owner(session)||(!slippi_lobj_anchor_admit(commit_.lobj_anchor)||!slippi_tobj_anchor_admit(commit_.tobj_anchor))||
           session.source_frames()!=owner_.frame+static_cast<std::uint32_t>(span_)||
           !melee_web_cpu_interval_admit(commit_.effect_token)||
           (reusable_loop_&&!melee_web_cpu_interval_motor_baseline_seal(
               commit_.effect_token,session.source_frames()))||
           melee_web_action_interval_seal_record(commit_.actions)!=1){disarm();phase_=Phase::Failed;return false;}
        auto payload=melee_web_source_memory_payload_snapshot_capture();post_payload_=payload.snapshot;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        if(span_==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
            melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_BASELINE_SEAL);
#endif
        post_flow_=melee_web_match_flow_cpu_snapshot_capture(owner_.flow,nullptr,0);
        if(payload.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||!post_payload_||!post_flow_||
           !capture_post_bytes(session,post_bytes_,audio_ranges_,audio_range_count_)||!statics_->seal_post()){
            disarm();phase_=Phase::Failed;return false;
        }
        melee_web_lobj_writer_mark(lobj_writer_session_,1);
        melee_web_tobj_writer_mark(tobj_writer_session_,1);
        post_frame_=session.source_frames();phase_=Phase::Sealed;return true;
    }catch(...){disarm();phase_=Phase::Failed;return false;}
}
GameplayCpuCommitResult GameplayCpuCheckpoint::restore(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Sealed||!same_owner(session)||session.source_frames()!=post_frame_)
        return GameplayCpuCommitResult::MissingOwner;
    if(!melee_web_cpu_interval_motor_restore_admit(
       commit_.effect_token,MELEE_WEB_CPU_MOTOR_LANE_REPLAY))
        return GameplayCpuCommitResult::Effect;
    const auto result=gameplay_cpu_commit(commit_);
    if(result==GameplayCpuCommitResult::Ok)
        melee_web_cpu_interval_motor_restore_commit(commit_.effect_token,
            MELEE_WEB_CPU_MOTOR_LANE_REPLAY);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(result==GameplayCpuCommitResult::Ok&&
       !slippi_lobj_current_set_view_reset_after_restore(commit_.lobj_anchor)){
        // Source restoration has committed. A failed presentation reset is
        // terminal and must not admit corrective source work.
        phase_=Phase::Failed;
        return GameplayCpuCommitResult::UnexpectedWriterFailure;
    }
#endif
    if(result==GameplayCpuCommitResult::Ok&&
       !slippi_particle_source_reset_after_restore(commit_.particle_anchor)){
        phase_=Phase::Failed;
        return GameplayCpuCommitResult::UnexpectedWriterFailure;
    }
    phase_=result==GameplayCpuCommitResult::Ok?Phase::Replaying:Phase::Failed;
    return result;
}
GameplayCpuCommitResult GameplayCpuCheckpoint::restore_corrected(
    const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Sealed||!same_owner(session)||session.source_frames()!=post_frame_)
        return GameplayCpuCommitResult::MissingOwner;
    if(!melee_web_cpu_interval_motor_restore_admit(
       commit_.effect_token,MELEE_WEB_CPU_MOTOR_LANE_CORRECTED))
        return GameplayCpuCommitResult::Effect;
    const auto result=gameplay_cpu_commit_corrected(commit_);
    if(result==GameplayCpuCommitResult::Ok)
        melee_web_cpu_interval_motor_restore_commit(commit_.effect_token,
            MELEE_WEB_CPU_MOTOR_LANE_CORRECTED);
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(result==GameplayCpuCommitResult::Ok&&
       !slippi_lobj_current_set_view_reset_after_restore(commit_.lobj_anchor)){
        phase_=Phase::Failed;
        return GameplayCpuCommitResult::UnexpectedWriterFailure;
    }
#endif
    if(result==GameplayCpuCommitResult::Ok&&
       !slippi_particle_source_reset_after_restore(commit_.particle_anchor)){
        phase_=Phase::Failed;
        return GameplayCpuCommitResult::UnexpectedWriterFailure;
    }
    phase_=result==GameplayCpuCommitResult::Ok?Phase::Correcting:Phase::Failed;
    return result;
}
bool GameplayCpuCheckpoint::finish_corrected(const GameplayMatchSession& session) noexcept {
    comparison_={};checked_domains_=matched_domains_=0;
    auto refuse=[this](CpuComparisonDomain domain,std::uint32_t detail=0){
        comparison_={static_cast<std::uint32_t>(domain),1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};
        phase_=Phase::Failed;
        return false;
    };
    if(phase_!=Phase::Correcting||!same_owner(session)||
       session.source_frames()!=post_frame_||
       !slippi_lobj_anchor_admit(commit_.lobj_anchor)||
       !slippi_tobj_anchor_admit(commit_.tobj_anchor)||
       !slippi_particle_presentation_anchor_admit(commit_.particle_anchor))
        return refuse(CpuComparisonDomain::Owner,
                      (slippi_lobj_failure()|(slippi_tobj_failure()<<16)));
    if(!melee_web_cpu_interval_admit(commit_.effect_token))
        return refuse(CpuComparisonDomain::Effects,melee_web_cpu_interval_failure());
    if(!melee_web_cpu_interval_motor_selected_finish(commit_.effect_token,session.source_frames()))
        return refuse(CpuComparisonDomain::Effects,melee_web_cpu_interval_failure());
    const auto action_failure=melee_web_action_interval_active_failure(commit_.actions);
    if(action_failure)
        return refuse(CpuComparisonDomain::Actions,action_failure);
    if(melee_web_action_interval_seal_corrected(commit_.actions)!=1)
        return refuse(CpuComparisonDomain::Actions,
                      melee_web_action_interval_failure_code(commit_.actions));
    phase_=Phase::CorrectedSealed;
    return true;
}
const MeleeWebCpuMotorEvidence* GameplayCpuCheckpoint::motor_evidence(
    const GameplayMatchSession& session) const noexcept {
    if(!reusable_loop_||!motor_token_||!owner_.context||!owner_.world||
       !owner_.context_epoch||commit_.effect_token!=motor_token_||
       melee_web_cpu_interval_token()!=motor_token_||!same_owner(session))return nullptr;
    return melee_web_cpu_interval_motor_evidence_for(motor_token_,owner_.world,
        owner_.context,owner_.context_epoch,owner_.frame,span_);
}
bool GameplayCpuCheckpoint::matches_post_bytes(const GameplayMatchSession&,const std::vector<PostBytes>& bytes,const AudioRanges& ranges,std::size_t count) noexcept {
    struct Visit {const AudioRanges* ranges;std::size_t count,n;bool failed;};Visit visit{&ranges,count,0,false};
    auto visitor=[](const MeleeWebAudioOwnedRange* row,void* data)->int {
        auto& v=*static_cast<Visit*>(data);
        if(!row||v.n>=v.count||!same_range(*row,(*v.ranges)[v.n++])){
            v.failed=true;return 0;
        }return 1;
    };
    if(melee_web_audio_visit_owned_ranges(owner_.audio,audio_generation_,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,
         visitor,&visit)!=MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE||visit.failed||visit.n!=count){
        checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Audio);
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Audio),1,UINT32_MAX,UINT32_MAX,0,0,0,0,1};return false;
    }
    const auto scalar=melee_web_audio_cpu_storage(owner_.audio,audio_generation_);
    if(bytes.size()<2||bytes[bytes.size()-2].address!=scalar.command_budget||
       bytes.back().address!=scalar.sound_mode){
        checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Audio);
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Audio),1,UINT32_MAX,UINT32_MAX,0,0,0,0,2};return false;
    }
    for(std::size_t i=0;i<bytes.size();++i){
        const auto& row=bytes[i];
        const auto domain=static_cast<std::uint32_t>(row.domain);checked_domains_|=1u<<domain;
        const auto* actual=static_cast<const unsigned char*>(row.address);
        for(std::uint32_t j=0;j<row.bytes.size();++j){
            const auto expected=static_cast<unsigned char>(row.bytes[j]);
            if(expected!=actual[j]){comparison_={domain,2,row.kind,j,expected,actual[j],
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(row.address)),
                static_cast<std::uint32_t>(row.bytes.size()),0};return false;}
        }
        if(i+1==bytes.size()||bytes[i+1].domain!=row.domain)matched_domains_|=1u<<domain;
    }
    return true;
}
bool GameplayCpuCheckpoint::finish_replay(const GameplayMatchSession& session) noexcept {
    comparison_={};checked_domains_=matched_domains_=0;
    auto check=[this](CpuComparisonDomain domain,bool okay,std::uint32_t detail=0){
        const auto id=static_cast<std::uint32_t>(domain);checked_domains_|=1u<<id;
        if(okay){matched_domains_|=1u<<id;return true;}
        comparison_={id,1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};phase_=Phase::Failed;return false;
    };
    if(!check(CpuComparisonDomain::Owner,phase_==Phase::Replaying&&same_owner(session)&&
        session.source_frames()==post_frame_&&slippi_lobj_anchor_admit(commit_.lobj_anchor)==1&&slippi_tobj_anchor_admit(commit_.tobj_anchor)==1,(slippi_lobj_failure()|(slippi_tobj_failure()<<16))))return false;
    if(!melee_web_cpu_interval_motor_selected_finish(commit_.effect_token,session.source_frames()))
        return check(CpuComparisonDomain::Effects,false,melee_web_cpu_interval_failure());
    if(!check(CpuComparisonDomain::Effects,melee_web_cpu_interval_admit(commit_.effect_token)==1,melee_web_cpu_interval_failure()))return false;
    const int action_result=melee_web_action_interval_finish_replay(commit_.actions);
    if(!check(CpuComparisonDomain::Actions,action_result==1,
              melee_web_action_interval_failure_code(commit_.actions)))return false;
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    const auto payload=melee_web_source_memory_payload_snapshot_compare_live(post_payload_);
    if(payload.status){
        // Read-only, synchronous cross-check of THIS original comparison.
        // Diagnostic refusal never changes the original comparison/1216.
        if(payload.status==2){
            melee_web_lobj_writer_finalize(commit_.payload,post_payload_,payload,class_piece_anchor_,lobj_writer_session_);
            melee_web_tobj_writer_finalize(commit_.payload,post_payload_,payload,class_piece_anchor_,tobj_writer_session_);
            payload_failure_witness_=melee_web_source_memory_payload_failure_witness(post_payload_,payload);
            allocation_origin_witness_=melee_web_source_memory_allocation_origin_witness(post_payload_,payload);
            class_piece_witness_=melee_web_source_memory_class_piece_witness(post_payload_,payload,class_piece_anchor_);
            class_piece_capture_witness_=melee_web_source_memory_class_piece_capture_witness(commit_.payload,post_payload_,payload,class_piece_anchor_);
        }
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        if(span_==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
            melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_REPLAY_FINAL);
#endif
        melee_web_source_memory_class_piece_slab_trace_finalize(commit_.payload,post_payload_,payload);
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Payload),payload.status,
            static_cast<std::uint32_t>(payload.allocation_index),static_cast<std::uint32_t>(payload.byte_offset),
            payload.expected_byte,payload.actual_byte,static_cast<std::uint32_t>(payload.host_address),
            payload.requested_bytes,payload.status==1?payload.admission_status:payload.source_address};phase_=Phase::Failed;return false;
    }
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    if(span_==static_cast<std::uint32_t>(CpuCheckpointSpan::TwoTicks))
        melee_web_lobj_slist_trace_sample(MELEE_WEB_SLIST_TRACE_REPLAY_FINAL);
#endif
    melee_web_source_memory_class_piece_slab_trace_finalize(commit_.payload,post_payload_,payload);
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    if(!check(CpuComparisonDomain::Flow,melee_web_match_flow_cpu_snapshot_matches(owner_.flow,post_flow_,nullptr,0)==1))return false;
    if(!matches_post_bytes(session,post_bytes_,audio_ranges_,audio_range_count_)){phase_=Phase::Failed;return false;}
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);comparison_=statics_->compare_post();
    if(comparison_.status){phase_=Phase::Failed;return false;}
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);
    phase_=Phase::Finished;return true;
}
bool GameplayCpuCheckpoint::seal_drawn_reference(const GameplayMatchSession& session) noexcept {
    comparison_={};checked_domains_=matched_domains_=0;
    auto refuse=[this](CpuComparisonDomain domain,std::uint32_t detail=0){
        comparison_={static_cast<std::uint32_t>(domain),1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};return false;
    };
    if(phase_!=Phase::Sealed||drawn_sealed_||drawn_payload_||drawn_flow_||!drawn_bytes_.empty()||
       !same_owner(session)||session.source_frames()!=post_frame_)return refuse(CpuComparisonDomain::Owner);
    try {
        // Capture has no source writes. The original precheckpoint remains the
        // sole restore authority; its full admission is repeated at commit.
        if((!slippi_lobj_anchor_admit(commit_.lobj_anchor)||!slippi_tobj_anchor_admit(commit_.tobj_anchor)))
            return refuse(CpuComparisonDomain::Owner,(slippi_lobj_failure()|(slippi_tobj_failure()<<16)));
        if(!melee_web_cpu_interval_admit(commit_.effect_token))
            return refuse(CpuComparisonDomain::Effects,melee_web_cpu_interval_failure());
        if(melee_web_action_interval_admit(commit_.actions)!=1)
            return refuse(CpuComparisonDomain::Actions,melee_web_action_interval_failure_code(commit_.actions));
        auto admission=melee_web_source_memory_payload_snapshot_admit(commit_.payload);
        if(admission!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
            return refuse(CpuComparisonDomain::Payload,static_cast<std::uint32_t>(admission));
        auto payload=melee_web_source_memory_payload_snapshot_capture();drawn_payload_=payload.snapshot;
        if(payload.status||!drawn_payload_)return refuse(CpuComparisonDomain::Payload,static_cast<std::uint32_t>(payload.status));
        drawn_flow_=melee_web_match_flow_cpu_snapshot_capture(owner_.flow,nullptr,0);
        if(!drawn_flow_)return refuse(CpuComparisonDomain::Flow,static_cast<std::uint32_t>(CpuCaptureRefusal::NullSnapshot));
        // Existing grouped helper is opaque on false: do not invent which of
        // Context/Bootstrap/Audio failed inside it. detail1 names that helper.
        if(!capture_post_bytes(session,drawn_bytes_,drawn_audio_ranges_,drawn_audio_range_count_))
            return refuse(CpuComparisonDomain::None,1);
        comparison_=statics_->capture_expected(drawn_statics_);
        if(comparison_.status)return false;
        melee_web_lobj_writer_mark(lobj_writer_session_,2);
        melee_web_tobj_writer_mark(tobj_writer_session_,2);
        drawn_sealed_=true;return true;
    }catch(const std::bad_alloc&){return refuse(CpuComparisonDomain::None,static_cast<std::uint32_t>(CpuCaptureRefusal::AllocationException));}
    catch(...){return refuse(CpuComparisonDomain::None,static_cast<std::uint32_t>(CpuCaptureRefusal::UnexpectedException));}
}
bool GameplayCpuCheckpoint::matches_drawn_reference(const GameplayMatchSession& session) noexcept {
    comparison_={};checked_domains_=matched_domains_=0;
    auto check=[this](CpuComparisonDomain domain,bool okay,std::uint32_t detail=0){
        auto bit=1u<<static_cast<unsigned>(domain);checked_domains_|=bit;
        if(okay){matched_domains_|=bit;return true;}
        comparison_={static_cast<std::uint32_t>(domain),1,UINT32_MAX,UINT32_MAX,0,0,0,0,detail};return false;
    };
    if(!check(CpuComparisonDomain::Owner,phase_==Phase::Finished&&drawn_sealed_&&same_owner(session)&&
        session.source_frames()==post_frame_&&slippi_lobj_anchor_admit(commit_.lobj_anchor)==1&&slippi_tobj_anchor_admit(commit_.tobj_anchor)==1,(slippi_lobj_failure()|(slippi_tobj_failure()<<16))))return false;
    if(!check(CpuComparisonDomain::Effects,melee_web_cpu_interval_admit(commit_.effect_token)==1,melee_web_cpu_interval_failure()))return false;
    // ActionStore full event/private-state equality was accepted by finish_replay
    // before present; no new action event may be accepted afterwards.
    if(!check(CpuComparisonDomain::Actions,melee_web_action_interval_matches_finished(commit_.actions)==1))return false;
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    const auto payload=melee_web_source_memory_payload_snapshot_compare_live(drawn_payload_);
    if(payload.status){comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Payload),payload.status,
        static_cast<std::uint32_t>(payload.allocation_index),static_cast<std::uint32_t>(payload.byte_offset),
        payload.expected_byte,payload.actual_byte,static_cast<std::uint32_t>(payload.host_address),payload.requested_bytes,
        payload.status==1?payload.admission_status:payload.source_address};return false;}
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    if(!check(CpuComparisonDomain::Flow,melee_web_match_flow_cpu_snapshot_matches(owner_.flow,drawn_flow_,nullptr,0)==1)||
       !matches_post_bytes(session,drawn_bytes_,drawn_audio_ranges_,drawn_audio_range_count_))return false;
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);comparison_=statics_->compare_expected(drawn_statics_);
    if(comparison_.status)return false;
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);return true;
}
bool GameplayCpuCheckpoint::retire_confirmed(
    const GameplayMatchSession& session) noexcept {
    // A host-side result is not restore or ownership authority. It must first
    // complete the CPU/DRAW/raw comparison and GPU/Asyncify idle boundary;
    // this method independently proves only live native release safety.
    // Exact-replay Finished carries its full accepted native comparison.
    // Sealed and CorrectedSealed carry no comparison claim and require zero
    // native comparison masks before release.
    constexpr std::uint32_t full_domain_mask=(1u<<
        (static_cast<std::uint32_t>(CpuComparisonDomain::Statics)+1u))-2u;
    const bool exact_terminal=phase_==Phase::Finished&&
                              checked_domains_==full_domain_mask&&
                              matched_domains_==full_domain_mask;
    const bool clean_uncompared_terminal=
                                  (phase_==Phase::Sealed||
                                   phase_==Phase::CorrectedSealed)&&
                                  checked_domains_==0&&matched_domains_==0;
    if((!exact_terminal&&!clean_uncompared_terminal)||!source_step_possible_||
       comparison_.status!=0)
        return false;

    // Verify the complete checkpoint owner tuple and endpoint before asking
    // the live session to check its exact source RuntimeFiles scope.
    const auto live=session.diagnostic_cpu_owners();
    if(!live.storage||live.storage!=owner_.storage||live.context!=owner_.context||
       live.flow!=owner_.flow||live.audio!=owner_.audio||live.world!=owner_.world||
       live.context_epoch!=owner_.context_epoch||live.frame!=post_frame_||
       session.source_frames()!=post_frame_)
        return false;
    if(!session.diagnostic_source_files_quiescent())
        return false;

    if(melee_web_cpu_interval_admit(commit_.effect_token)!=1)
        return false;
    if(!melee_web_cpu_interval_motor_commit_admit(commit_.effect_token,owner_.world,
       owner_.context,owner_.context_epoch,static_cast<std::uint32_t>(owner_.frame),span_))
        return false;
    if(slippi_lobj_anchor_admit(commit_.lobj_anchor)!=1||
       slippi_tobj_anchor_admit(commit_.tobj_anchor)!=1||
       slippi_particle_presentation_anchor_admit(commit_.particle_anchor)!=1)
        return false;

    // ActionStore first performs a pure, exact-catalog release-safety proof.
    // Its commit operation must leave all leases intact when it refuses.
    if(!commit_.actions||
       melee_web_action_interval_retire_admit(commit_.actions)!=1)
        return false;
    if(melee_web_action_interval_retire_confirmed(commit_.actions)!=1)
        return false;

    // The private reusable route captures only the already admitted selected
    // motor transcript. This bookkeeping never dispatches a hardware command.
    melee_web_cpu_interval_motor_capture_commit(commit_.effect_token);

    // All remaining operations revoke routing/anchors and are non-failing.
    // Keep this terminal distinct from source-owner teardown: the match stays
    // live and may begin a later, independently captured interval.
    disarm();
    confirmed_disarmed_=true;
    phase_=Phase::ConfirmedDetached;
    source_step_possible_=false;
    return true;
}
void GameplayCpuCheckpoint::disarm() noexcept {
    if(confirmed_disarmed_)return;
    melee_web_source_memory_class_piece_slab_trace_early_close();
    // Cleanup revokes host routing even after a failed interval. Its result is
    // not a source comparison or successful rollback assertion.
    if(commit_.effect_token)
        (void)slippi_particle_source_interval_end(owner_.world,commit_.effect_token);
    slippi_particle_presentation_anchor_disarm(commit_.particle_anchor);
    slippi_tobj_anchor_disarm(commit_.tobj_anchor);
    slippi_lobj_anchor_disarm(commit_.lobj_anchor);
    melee_web_lobj_writer_disarm(lobj_writer_session_);
    melee_web_tobj_writer_disarm(tobj_writer_session_);
    melee_web_class_piece_disarm(class_piece_anchor_);
    if(commit_.actions)melee_web_action_interval_disarm(commit_.actions);
    if(commit_.effect_token&&commit_.audio_snapshot)
        (void)melee_web_audio_snapshot_interval_end(owner_.audio,commit_.audio_snapshot,
                                                  commit_.effect_token);
    if(commit_.effect_token){melee_web_cpu_interval_end(commit_.effect_token);commit_.effect_token=0;}
}
bool GameplayCpuCheckpoint::source_owners_retired(const GameplayMatchSession* session) noexcept {
    /* No stale session is dereferenced: caller passes its current live owner,
     * or null after resetting it. Actual bootstrap must also be closed. */
    if(session&&session->diagnostic_cpu_owners().storage)return false;
    if(melee_web_gameplay_world_exists())return false;
    disarm();phase_=Phase::Retired;source_step_possible_=false;return true;
}
void GameplayCpuCheckpoint::release() noexcept {
    melee_web_action_interval_destroy(commit_.actions);
    melee_web_audio_snapshot_destroy(const_cast<MeleeWebAudioSnapshot*>(commit_.audio_snapshot));
    melee_web_match_flow_cpu_snapshot_destroy(const_cast<MeleeWebMatchFlowCpuSnapshot*>(commit_.flow_snapshot));
    melee_web_match_context_snapshot_destroy(const_cast<MeleeWebMatchContextSnapshot*>(commit_.context_snapshot));
    melee_web_gameplay_bootstrap_snapshot_destroy(const_cast<MeleeWebGameplayBootstrapSnapshot*>(commit_.bootstrap));
    melee_web_source_memory_payload_snapshot_destroy(const_cast<MeleeWebSourceMemoryPayloadSnapshot*>(commit_.payload));
    melee_web_source_memory_payload_snapshot_destroy(post_payload_);
    melee_web_match_flow_cpu_snapshot_destroy(post_flow_);
    melee_web_source_memory_payload_snapshot_destroy(drawn_payload_);
    melee_web_match_flow_cpu_snapshot_destroy(drawn_flow_);
}
GameplayCpuCheckpoint::~GameplayCpuCheckpoint(){
    /* Fail loudly rather than release a last animation lease under live or
     * partially replayed source borrowers. The adapter's normal cleanup must
     * disarm, retire source owners, mark retirement, then destroy this object. */
    if(source_step_possible_)std::abort();
    disarm();release();
}
}
