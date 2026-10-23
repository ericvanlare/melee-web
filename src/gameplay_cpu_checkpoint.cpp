#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_cpu_interval.h"
#include <cstring>
#include <limits>
#include <new>
#include <cstdlib>
namespace melee_web {
namespace {
bool same_range(const MeleeWebAudioOwnedRange& a,const MeleeWebAudioOwnedRange& b) noexcept {
    return a.kind==b.kind&&a.address==b.address&&a.byte_extent==b.byte_extent&&
        a.owner_identity==b.owner_identity&&a.generation==b.generation;
}
}
std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture(
    const GameplayMatchSession& session,std::unique_ptr<GameplayCpuStatics> statics) noexcept {
    std::unique_ptr<GameplayCpuCheckpoint> c(new(std::nothrow) GameplayCpuCheckpoint());
    if(!c||!statics)return {};
    try {
        c->owner_=session.diagnostic_cpu_owners();
        if(!c->owner_.storage||!c->owner_.world||
           c->owner_.frame==std::numeric_limits<std::uint32_t>::max())return {};
        c->statics_=std::move(statics);c->commit_.statics=c->statics_.get();
        c->commit_.audio=c->owner_.audio;c->commit_.flow=c->owner_.flow;c->commit_.context=c->owner_.context;
        c->commit_.bootstrap=melee_web_gameplay_bootstrap_snapshot_capture(nullptr,0);
        c->commit_.flow_snapshot=melee_web_match_flow_cpu_snapshot_capture(c->owner_.flow,nullptr,0);
        c->commit_.context_snapshot=melee_web_match_context_snapshot_capture(c->owner_.context,nullptr,0);
        c->commit_.audio_snapshot=melee_web_audio_snapshot_capture(c->owner_.audio);
        auto payload=melee_web_source_memory_payload_snapshot_capture();
        c->commit_.payload=payload.snapshot;
        c->commit_.actions=melee_web_action_interval_capture();
        if(!c->commit_.bootstrap||!c->commit_.flow_snapshot||!c->commit_.context_snapshot||
           !c->commit_.audio_snapshot||payload.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||
           !c->commit_.payload||!c->commit_.actions||!c->statics_->capture())return {};
        c->audio_generation_=melee_web_audio_generation(c->owner_.audio);
        if(!c->audio_generation_)return {};
        return c;
    }catch(...){return {};}
}
bool GameplayCpuCheckpoint::same_owner(const GameplayMatchSession& session) const noexcept {
    /* Re-read from the current live session before using any captured handle;
     * typed snapshots then check epoch/generation before source dereferences. */
    const auto live=session.diagnostic_cpu_owners();
    return live.storage&&live.storage==owner_.storage&&live.context==owner_.context&&
        live.flow==owner_.flow&&live.audio==owner_.audio&&live.world==owner_.world&&live.context_epoch==owner_.context_epoch;
}
bool GameplayCpuCheckpoint::begin_record(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Captured||!same_owner(session)||
       session.source_frames()!=owner_.frame)return false;
    commit_.effect_token=melee_web_cpu_interval_begin();
    if(!commit_.effect_token||melee_web_action_interval_begin_record(commit_.actions)!=1){
        disarm();phase_=Phase::Failed;return false;
    }
    phase_=Phase::Recording;
    /* The next ordinary fixture step can replace source Clip borrowers. From
     * here on cleanup retains both pre and first-pass leases until teardown. */
    source_step_possible_=true;return true;
}
bool GameplayCpuCheckpoint::capture_post_bytes(const GameplayMatchSession&) {
    MeleeWebMatchContextRange context{};
    if(melee_web_match_context_range_read(owner_.context,&context,nullptr,0)!=
       MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE)return false;
    const auto boot=melee_web_gameplay_bootstrap_cpu_storage();
    if(!boot.ticks||!boot.disabled_links)return false;
    auto save=[this](const void* p,std::size_t n,CpuComparisonDomain domain,std::uint32_t kind){
        if(!p||!n)return false;
        PostBytes value;value.address=p;value.domain=domain;value.kind=kind;value.bytes.resize(n);
        std::memcpy(value.bytes.data(),p,n);post_bytes_.push_back(std::move(value));return true;
    };
    if(!save(context.address,context.byte_extent,CpuComparisonDomain::Context,0)||
       !save(boot.ticks,boot.ticks_bytes,CpuComparisonDomain::Bootstrap,0)||
       !save(boot.disabled_links,boot.disabled_links_bytes,CpuComparisonDomain::Bootstrap,1))return false;
    struct Visit {GameplayCpuCheckpoint* c;bool failed;};Visit visit{this,false};
    auto visitor=[](const MeleeWebAudioOwnedRange* range,void* data)->int {
        auto& v=*static_cast<Visit*>(data);auto& c=*v.c;
        if(!range||!range->address||!range->byte_extent||c.audio_range_count_>=c.audio_ranges_.size()){
            v.failed=true;return 0;
        }
        try {
            PostBytes value;value.address=range->address;value.domain=CpuComparisonDomain::Audio;value.kind=range->kind;value.bytes.resize(range->byte_extent);
            std::memcpy(value.bytes.data(),range->address,range->byte_extent);
            c.post_bytes_.push_back(std::move(value));c.audio_ranges_[c.audio_range_count_++]=*range;
            return 1;
        }catch(...){v.failed=true;return 0;}
    };
    const auto scalar=melee_web_audio_cpu_storage(owner_.audio,audio_generation_);
    return melee_web_audio_visit_owned_ranges(owner_.audio,audio_generation_,
        MELEE_WEB_AUDIO_OWNED_RANGE_MAX,visitor,&visit)==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE&&
        !visit.failed&&audio_range_count_&&
        save(scalar.command_budget,sizeof(uint32_t),CpuComparisonDomain::Audio,0x100)&&
        save(scalar.sound_mode,sizeof(uint32_t),CpuComparisonDomain::Audio,0x101);
}
bool GameplayCpuCheckpoint::seal_post(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Recording)return false;
    try {
        if(!same_owner(session)||session.source_frames()!=owner_.frame+1||
           !melee_web_cpu_interval_admit(commit_.effect_token)||
           melee_web_action_interval_seal_record(commit_.actions)!=1){disarm();phase_=Phase::Failed;return false;}
        auto payload=melee_web_source_memory_payload_snapshot_capture();post_payload_=payload.snapshot;
        post_flow_=melee_web_match_flow_cpu_snapshot_capture(owner_.flow,nullptr,0);
        if(payload.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||!post_payload_||!post_flow_||
           !capture_post_bytes(session)||!statics_->seal_post()){
            disarm();phase_=Phase::Failed;return false;
        }
        post_frame_=session.source_frames();phase_=Phase::Sealed;return true;
    }catch(...){disarm();phase_=Phase::Failed;return false;}
}
GameplayCpuCommitResult GameplayCpuCheckpoint::restore(const GameplayMatchSession& session) noexcept {
    if(phase_!=Phase::Sealed||!same_owner(session)||session.source_frames()!=post_frame_)
        return GameplayCpuCommitResult::MissingOwner;
    const auto result=gameplay_cpu_commit(commit_);
    phase_=result==GameplayCpuCommitResult::Ok?Phase::Replaying:Phase::Failed;
    return result;
}
bool GameplayCpuCheckpoint::matches_post_bytes(const GameplayMatchSession&) noexcept {
    struct Visit {const GameplayCpuCheckpoint* c;std::size_t n;bool failed;};Visit visit{this,0,false};
    auto visitor=[](const MeleeWebAudioOwnedRange* row,void* data)->int {
        auto& v=*static_cast<Visit*>(data);
        if(!row||v.n>=v.c->audio_range_count_||!same_range(*row,v.c->audio_ranges_[v.n++])){
            v.failed=true;return 0;
        }return 1;
    };
    if(melee_web_audio_visit_owned_ranges(owner_.audio,audio_generation_,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,
         visitor,&visit)!=MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE||visit.failed||visit.n!=audio_range_count_){
        checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Audio);
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Audio),1,UINT32_MAX,UINT32_MAX,0,0,0,0,1};return false;
    }
    const auto scalar=melee_web_audio_cpu_storage(owner_.audio,audio_generation_);
    if(post_bytes_.size()<2||post_bytes_[post_bytes_.size()-2].address!=scalar.command_budget||
       post_bytes_.back().address!=scalar.sound_mode){
        checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Audio);
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Audio),1,UINT32_MAX,UINT32_MAX,0,0,0,0,2};return false;
    }
    for(std::size_t i=0;i<post_bytes_.size();++i){
        const auto& row=post_bytes_[i];
        const auto domain=static_cast<std::uint32_t>(row.domain);checked_domains_|=1u<<domain;
        const auto* actual=static_cast<const unsigned char*>(row.address);
        for(std::uint32_t j=0;j<row.bytes.size();++j){
            const auto expected=static_cast<unsigned char>(row.bytes[j]);
            if(expected!=actual[j]){comparison_={domain,2,row.kind,j,expected,actual[j],
                static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(row.address)),
                static_cast<std::uint32_t>(row.bytes.size()),0};return false;}
        }
        if(i+1==post_bytes_.size()||post_bytes_[i+1].domain!=row.domain)matched_domains_|=1u<<domain;
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
    if(!check(CpuComparisonDomain::Owner,phase_==Phase::Replaying&&same_owner(session)&&session.source_frames()==post_frame_))return false;
    if(!check(CpuComparisonDomain::Effects,melee_web_cpu_interval_admit(commit_.effect_token)==1,melee_web_cpu_interval_failure()))return false;
    const int action_result=melee_web_action_interval_finish_replay(commit_.actions);
    if(!check(CpuComparisonDomain::Actions,action_result==1,
              melee_web_action_interval_failure_code(commit_.actions)))return false;
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    const auto payload=melee_web_source_memory_payload_snapshot_compare_live(post_payload_);
    if(payload.status){
        comparison_={static_cast<std::uint32_t>(CpuComparisonDomain::Payload),payload.status,
            static_cast<std::uint32_t>(payload.allocation_index),static_cast<std::uint32_t>(payload.byte_offset),
            payload.expected_byte,payload.actual_byte,static_cast<std::uint32_t>(payload.host_address),
            payload.requested_bytes,payload.status==1?payload.admission_status:payload.source_address};phase_=Phase::Failed;return false;
    }
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Payload);
    if(!check(CpuComparisonDomain::Flow,melee_web_match_flow_cpu_snapshot_matches(owner_.flow,post_flow_,nullptr,0)==1))return false;
    if(!matches_post_bytes(session)){phase_=Phase::Failed;return false;}
    checked_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);comparison_=statics_->compare_post();
    if(comparison_.status){phase_=Phase::Failed;return false;}
    matched_domains_|=1u<<static_cast<unsigned>(CpuComparisonDomain::Statics);
    phase_=Phase::Finished;return true;
}
void GameplayCpuCheckpoint::disarm() noexcept {
    if(commit_.actions)melee_web_action_interval_disarm(commit_.actions);
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
}
GameplayCpuCheckpoint::~GameplayCpuCheckpoint(){
    /* Fail loudly rather than release a last animation lease under live or
     * partially replayed source borrowers. The adapter's normal cleanup must
     * disarm, retire source owners, mark retirement, then destroy this object. */
    if(source_step_possible_)std::abort();
    disarm();release();
}
}
