#include "gameplay_cpu_endpoint.hpp"
#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_cpu_interval.h"
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace melee_web {
namespace {
using Status=GameplayCpuEndpointStatus;
using View=GameplayCpuEndpointView;
using Provenance=GameplayCpuEndpointProvenance;
using Domain=GameplayCpuEndpointDomain;
using Kind=GameplayCpuEndpointRowKind;

std::uint64_t address(const void* value) noexcept
{ return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value)); }

GameplayCpuEndpointRecord refusal(Status status) noexcept
{
    GameplayCpuEndpointRecord result{};
    result.status=static_cast<std::uint32_t>(status);
    result.schema=1;
    return result;
}

GameplayCpuEndpointRecord row(View view,Provenance provenance,Domain domain,
                              Kind kind,std::uint32_t index,std::uint32_t count,
                              std::initializer_list<std::uint64_t> fields) noexcept
{
    if(fields.size()>32||fields.size()>UINT32_MAX/sizeof(std::uint64_t)||index>=count)
        return refusal(fields.size()>32?Status::Overflow:Status::InvalidIndex);
    GameplayCpuEndpointRecord result{};
    result.status=static_cast<std::uint32_t>(Status::Ok);
    result.schema=1;
    result.view=static_cast<std::uint32_t>(view);
    result.provenance=static_cast<std::uint32_t>(provenance);
    result.domain=static_cast<std::uint32_t>(domain);
    result.row_kind=static_cast<std::uint32_t>(kind);
    result.index=index;
    result.count=count;
    result.metadata_bytes=static_cast<std::uint32_t>(fields.size()*sizeof(std::uint64_t));
    std::copy(fields.begin(),fields.end(),result.fields);
    return result;
}

bool set_borrowed(GameplayCpuEndpointRecord& record,const void* data,
                  std::size_t bytes) noexcept
{
    if(bytes>UINT32_MAX||(bytes&&!data))return false;
    record.borrowed_address=address(data);
    record.borrowed_bytes=static_cast<std::uint32_t>(bytes);
    return true;
}

bool equal_range(const MeleeWebAudioOwnedRange& a,
                 const MeleeWebAudioOwnedRange& b) noexcept
{
    return a.kind==b.kind&&a.address==b.address&&a.byte_extent==b.byte_extent&&
        a.owner_identity==b.owner_identity&&a.generation==b.generation;
}

bool equal_clock_endpoint(const MeleeWebClockCpuEndpoint& a,
                          const MeleeWebClockCpuEndpoint& b) noexcept
{
    return a.valid==b.valid&&a.unk_0==b.unk_0&&a.unk_4==b.unk_4&&
        a.unk_8==b.unk_8&&a.unk_C==b.unk_C&&
        a.callback_x0==b.callback_x0&&a.callback_x1==b.callback_x1&&
        a.callback_x2==b.callback_x2&&a.callback_x3==b.callback_x3&&
        a.callback_x4_0==b.callback_x4_0&&a.callback_x4_1==b.callback_x4_1&&
        a.unk_10_xC==b.unk_10_xC&&a.unk_10_x20==b.unk_10_x20&&
        a.unk_10_x28==b.unk_10_x28&&a.unk_10_x30==b.unk_10_x30&&
        a.unk_10_x34==b.unk_10_x34&&a.unk_10_x38_0==b.unk_10_x38_0&&
        a.unk_10_x38_1==b.unk_10_x38_1;
}

bool equal_cadence_endpoint(const MeleeWebCadenceCpuEndpoint& a,
                            const MeleeWebCadenceCpuEndpoint& b) noexcept
{
    return a.valid==b.valid&&a.x0_0_x0==b.x0_0_x0&&a.x0_0_x8==b.x0_0_x8&&
        a.x0_0_x10==b.x0_0_x10&&a.x0_1_x0==b.x0_1_x0&&
        a.x0_1_x8==b.x0_1_x8&&a.x0_1_x10==b.x0_1_x10&&a.x4==b.x4&&
        a.x38==b.x38&&a.x40==b.x40&&a.x48==b.x48&&
        a.alarm==b.alarm&&a.alarm_bytes==b.alarm_bytes;
}

struct AudioCollector {
    std::array<MeleeWebAudioOwnedRange,MELEE_WEB_AUDIO_OWNED_RANGE_MAX>* rows{};
    std::uint32_t count{};
    bool failed{};
};
int collect_audio_range(const MeleeWebAudioOwnedRange* row,void* context)
{
    auto& collector=*static_cast<AudioCollector*>(context);
    if(!row||!collector.rows||collector.count>=collector.rows->size()){
        collector.failed=true;return 0;
    }
    (*collector.rows)[collector.count++]=*row;
    return 1;
}

}

bool GameplayCpuEndpointReader::collect(BatchData& out) const noexcept
{
    if(!session_||!checkpoint_)return false;
    const auto& checkpoint=*checkpoint_;
    const bool pre=view_==View::Pre;
    if((pre&&(checkpoint.phase_!=GameplayCpuCheckpoint::Phase::Recording||
              provenance_!=Provenance::SharedPre))||
       (!pre&&((provenance_==Provenance::Reference&&
                checkpoint.phase_!=GameplayCpuCheckpoint::Phase::Sealed)||
               (provenance_==Provenance::Corrected&&
                checkpoint.phase_!=GameplayCpuCheckpoint::Phase::CorrectedSealed)||
               (provenance_!=Provenance::Reference&&provenance_!=Provenance::Corrected))))
        return false;
    if(!checkpoint.same_owner(*session_)||
       !slippi_lobj_anchor_admit(checkpoint.commit_.lobj_anchor)||
       !slippi_tobj_anchor_admit(checkpoint.commit_.tobj_anchor)||
       !slippi_particle_presentation_anchor_admit(checkpoint.commit_.particle_anchor)||
       !checkpoint.commit_.effect_token||
       !melee_web_cpu_interval_admit(checkpoint.commit_.effect_token))return false;

    out.source_frame=session_->source_frames();
    const auto expected_frame=pre?checkpoint.owner_.frame:checkpoint.post_frame_;
    if(out.source_frame!=expected_frame)return false;

    if(melee_web_action_interval_endpoint_admit(checkpoint.commit_.actions)!=
       MELEE_WEB_ACTION_ENDPOINT_OK)return false;
    out.actions=melee_web_action_interval_endpoint_header(checkpoint.commit_.actions);
    const auto leaf_view=pre?MELEE_WEB_ACTION_ENDPOINT_PRE:MELEE_WEB_ACTION_ENDPOINT_END;
    const auto leaf_provenance=provenance_==Provenance::Corrected?
        MELEE_WEB_ACTION_ENDPOINT_CORRECTED:MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    if(out.actions.status!=MELEE_WEB_ACTION_ENDPOINT_OK||out.actions.schema!=1||
       out.actions.view!=leaf_view||out.actions.provenance!=leaf_provenance||
       out.actions.slots_per_binding!=2)return false;
    if(out.actions.binding_count>UINT32_MAX/2u||out.actions.cache_count>UINT32_MAX/2u)
        return false;

    out.payload=melee_web_source_memory_payload_endpoint_header(checkpoint.commit_.payload);
    if(out.payload.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||out.payload.schema!=1||
       out.payload.row_count>UINT32_MAX||out.payload.free_cell_count>UINT32_MAX||
       out.payload.allocated_cell_count>UINT32_MAX)return false;

    const auto* flow_snapshot=pre?checkpoint.commit_.flow_snapshot:checkpoint.post_flow_;
    out.flow=melee_web_match_flow_cpu_endpoint_read(checkpoint.owner_.flow,flow_snapshot);
    if(out.flow.status!=0)return false;
    if(!melee_web_match_context_snapshot_admit(checkpoint.owner_.context,
          checkpoint.commit_.context_snapshot,nullptr,0) ||
       melee_web_match_context_range_read(checkpoint.owner_.context,&out.context,nullptr,0)!=
          MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE ||
       out.context.byte_extent>UINT32_MAX)return false;
    if(!melee_web_gameplay_bootstrap_snapshot_admit(checkpoint.commit_.bootstrap,nullptr,0))
        return false;
    out.bootstrap=melee_web_gameplay_bootstrap_cpu_storage();
    if(!out.bootstrap.ticks||!out.bootstrap.disabled_links||
       out.bootstrap.ticks_bytes>UINT32_MAX||out.bootstrap.disabled_links_bytes>UINT32_MAX)
        return false;

    if(!melee_web_audio_snapshot_admit(checkpoint.owner_.audio,checkpoint.commit_.audio_snapshot))
        return false;
    out.audio=melee_web_audio_cpu_storage(checkpoint.owner_.audio,checkpoint.audio_generation_);
    if(!out.audio.command_budget||!out.audio.sound_mode)return false;
    AudioCollector audio{&out.audio_ranges,0,false};
    if(melee_web_audio_visit_owned_ranges(checkpoint.owner_.audio,checkpoint.audio_generation_,
          MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_audio_range,&audio)!=
          MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE||audio.failed||!audio.count)return false;
    out.audio_count=audio.count;

    const auto clock=melee_web_source_clock_cpu_storage();
    const auto clock_endpoint=melee_web_source_clock_cpu_endpoint();
    const auto cadence=melee_web_source_cadence_cpu_storage();
    const auto cadence_endpoint=melee_web_source_cadence_cpu_endpoint();
    if(!clock.record||!clock.record_bytes||!clock.process_mask||!clock.process_mask_bytes||
       ((clock.profile_phase==nullptr)!=(clock.profile_phase_bytes==0))||
       !cadence.record||!cadence.record_bytes||
       !clock_endpoint.valid||!cadence_endpoint.valid||
       !cadence_endpoint.alarm||!cadence_endpoint.alarm_bytes||
       clock.record_bytes>UINT32_MAX||clock.process_mask_bytes>UINT32_MAX||
       clock.profile_phase_bytes>UINT32_MAX||cadence.record_bytes>UINT32_MAX||
       cadence_endpoint.alarm_bytes>UINT32_MAX)return false;
    out.clock=clock;out.clock_endpoint=clock_endpoint;
    out.cadence=cadence;out.cadence_endpoint=cadence_endpoint;

    out.statics=checkpoint.statics_->endpoint_header();
    if(out.statics.status!=CpuStaticEndpointStatus::Ok||out.statics.schema!=1||
       out.statics.row_count>UINT32_MAX)return false;
    return true;
}

bool GameplayCpuEndpointReader::same_batch(const BatchData& a,
                                           const BatchData& b) const noexcept
{
    const auto& x=a.payload;const auto& y=b.payload;
    const auto& xf=a.flow;const auto& yf=b.flow;
    const auto& xc=a.context;const auto& yc=b.context;
    const auto& xb=a.bootstrap;const auto& yb=b.bootstrap;
    const auto& xclock=a.clock;const auto& yclock=b.clock;
    const auto& xcadence=a.cadence;const auto& ycadence=b.cadence;
    const auto& xa=a.audio;const auto& ya=b.audio;
    const auto& xs=a.statics;const auto& ys=b.statics;
    if(a.actions.schema!=b.actions.schema||a.actions.view!=b.actions.view||
       a.actions.provenance!=b.actions.provenance||a.actions.binding_count!=b.actions.binding_count||
       a.actions.slots_per_binding!=b.actions.slots_per_binding||a.actions.cache_count!=b.actions.cache_count||
       a.actions.event_count!=b.actions.event_count||
       x.schema!=y.schema||x.source_heap_handle!=y.source_heap_handle||x.heap_bytes!=y.heap_bytes||
       x.owner_epoch!=y.owner_epoch||x.world_generation!=y.world_generation||
       x.allocation_watermark!=y.allocation_watermark||x.topology_revision!=y.topology_revision||
       x.arena_begin!=y.arena_begin||x.arena_end!=y.arena_end||x.row_count!=y.row_count||
       x.free_cell_count!=y.free_cell_count||x.allocated_cell_count!=y.allocated_cell_count||
       x.payload_bytes!=y.payload_bytes||
       xf.identity!=yf.identity||xf.epoch!=yf.epoch||xf.generation!=yf.generation||
       xf.source_valid!=yf.source_valid||xf.complete!=yf.complete||xf.opening_demo!=yf.opening_demo||
       xc.address!=yc.address||xc.byte_extent!=yc.byte_extent||xc.world_generation!=yc.world_generation||
       xc.owner_epoch!=yc.owner_epoch||
       xb.ticks!=yb.ticks||xb.ticks_bytes!=yb.ticks_bytes||
       xb.disabled_links!=yb.disabled_links||xb.disabled_links_bytes!=yb.disabled_links_bytes||
       xclock.record!=yclock.record||xclock.record_bytes!=yclock.record_bytes||
       xclock.process_mask!=yclock.process_mask||xclock.process_mask_bytes!=yclock.process_mask_bytes||
       xclock.profile_phase!=yclock.profile_phase||xclock.profile_phase_bytes!=yclock.profile_phase_bytes||
       xcadence.record!=ycadence.record||xcadence.record_bytes!=ycadence.record_bytes||
       !equal_clock_endpoint(a.clock_endpoint,b.clock_endpoint)||
       !equal_cadence_endpoint(a.cadence_endpoint,b.cadence_endpoint)||
       xa.command_budget!=ya.command_budget||xa.sound_mode!=ya.sound_mode||
       xs.status!=ys.status||xs.schema!=ys.schema||xs.phase!=ys.phase||
       xs.table_identity!=ys.table_identity||xs.row_count!=ys.row_count||
       a.audio_count!=b.audio_count||a.source_frame!=b.source_frame)return false;
    for(std::uint32_t i=0;i<a.audio_count;++i)
        if(!equal_range(a.audio_ranges[i],b.audio_ranges[i]))return false;
    return true;
}

bool GameplayCpuEndpointReader::begin(const GameplayMatchSession& session,
    const GameplayCpuCheckpoint& checkpoint,View view,Provenance provenance) noexcept
{
    if(active_){status_=Status::Incomplete;return false;}
    session_=nullptr;checkpoint_=nullptr;data_={};
    status_=Status::Invalid;
    if(view!=View::Pre&&view!=View::CpuEnd&&view!=View::DrawEnd)return false;
    if((view==View::Pre&&provenance!=Provenance::SharedPre)||
       (view!=View::Pre&&provenance==Provenance::SharedPre)){
        status_=Status::WrongPhase;return false;
    }
    session_=&session;checkpoint_=&checkpoint;view_=view;provenance_=provenance;
    if(!collect(data_)){
        session_=nullptr;checkpoint_=nullptr;status_=Status::OwnerRefused;return false;
    }
    active_=true;status_=Status::Ok;return true;
}

bool GameplayCpuEndpointReader::particle_header(
    SlippiParticleCellEndpointHeader& out) const noexcept
{
    out={};
    if(!active_||!session_||!checkpoint_||
       session_->source_frames()!=data_.source_frame)return false;
    return slippi_particle_presentation_cell_endpoint_header(
        checkpoint_->commit_.particle_anchor,&out)==1;
}

bool GameplayCpuEndpointReader::particle_row(
    std::uint32_t kind,std::uint32_t index,
    SlippiParticleCellEndpointRow& out) const noexcept
{
    out={};
    if(!active_||!session_||!checkpoint_||
       session_->source_frames()!=data_.source_frame)return false;
    return slippi_particle_presentation_cell_endpoint_row(
        checkpoint_->commit_.particle_anchor,kind,index,&out)==1;
}

GameplayCpuEndpointRecord GameplayCpuEndpointReader::record(
    Domain domain,Kind kind,std::uint32_t index) const noexcept
{
    if(!active_||!session_||!checkpoint_)return refusal(Status::Incomplete);
    if(session_->source_frames()!=data_.source_frame)return refusal(Status::OwnerRefused);
    const auto& c=*checkpoint_;
    using namespace GameplayCpuEndpointField;
    const auto v=view_;const auto p=provenance_;
    auto make=[&](Kind row_kind,std::uint32_t count,std::initializer_list<std::uint64_t> fields){
        return row(v,p,domain,row_kind,index,count,fields);
    };
    auto borrowed=[&](GameplayCpuEndpointRecord result,const void* bytes,std::size_t size){
        if(result.status)return result;
        if(!set_borrowed(result,bytes,size))return refusal(Status::Overflow);
        return result;
    };
    const auto k=static_cast<std::uint32_t>(kind);

    switch(domain){
    case Domain::Owner:
        if(k!=static_cast<std::uint32_t>(Kind::Owner)||index)return refusal(Status::InvalidIndex);
        {
            const auto owners=session_->diagnostic_cpu_owners();
            return make(Kind::Owner,1,{address(owners.storage),address(owners.context),
                address(owners.flow),address(owners.audio),owners.world,owners.context_epoch,
                session_->source_frames()});
        }
    case Domain::Effects:
        if(index)return refusal(Status::InvalidIndex);
        switch(static_cast<Kind>(k)){
        case Kind::EffectToken:
            return make(kind,1,{c.commit_.effect_token});
        case Kind::LObjAnchor:
            return make(kind,1,{c.commit_.lobj_anchor.world,c.commit_.lobj_anchor.serial,
                c.commit_.lobj_anchor.token,c.commit_.lobj_anchor.count,c.commit_.lobj_anchor.reserved});
        case Kind::TObjAnchor:
            return make(kind,1,{c.commit_.tobj_anchor.world,c.commit_.tobj_anchor.serial,
                c.commit_.tobj_anchor.token,c.commit_.tobj_anchor.count,c.commit_.tobj_anchor.reserved});
        case Kind::ParticleAnchor:
            return make(kind,1,{c.commit_.particle_anchor.world,c.commit_.particle_anchor.serial,
                c.commit_.particle_anchor.token,c.commit_.particle_anchor.particle_count,
                c.commit_.particle_anchor.appsrt_count});
        default:return refusal(Status::InvalidIndex);
        }
    case Domain::Actions:
        if(kind==Kind::Header){
            if(index)return refusal(Status::InvalidIndex);
            return make(kind,1,{data_.actions.schema,data_.actions.binding_count,
                data_.actions.slots_per_binding,data_.actions.cache_count,data_.actions.event_count});
        }
        if(kind==Kind::ActionBinding){
            const auto x=melee_web_action_interval_endpoint_binding(c.commit_.actions,index);
            if(x.status!=MELEE_WEB_ACTION_ENDPOINT_OK)return refusal(Status::LeafRefused);
            return make(kind,data_.actions.binding_count,{x.present,x.generation,x.fighter,x.store,
                x.select_callback,x.transfer_callback,x.table});
        }
        if(kind==Kind::ActionSlot){
            const std::uint32_t count=data_.actions.binding_count*data_.actions.slots_per_binding;
            if(index>=count)return refusal(Status::InvalidIndex);
            const std::uint32_t binding=index/data_.actions.slots_per_binding;
            const std::uint32_t slot=index%data_.actions.slots_per_binding;
            const auto x=melee_web_action_interval_endpoint_slot(c.commit_.actions,binding,slot);
            if(x.status!=MELEE_WEB_ACTION_ENDPOINT_OK)return refusal(Status::LeafRefused);
            auto result=make(kind,count,{x.binding,x.slot,x.present,x.motion,x.offset,x.archive_bytes,
                x.symbol_bytes,x.fighter,x.store,x.table,x.clip,x.tree,x.row_identity,x.animation,
                x.row_owner,x.command_owner});
            return borrowed(result,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x.symbol_data)),x.symbol_bytes);
        }
        if(kind==Kind::ActionCacheEntry){
            const std::uint32_t count=data_.actions.cache_count*2u;
            if(index>=count)return refusal(Status::InvalidIndex);
            const std::uint32_t cache_index=index/2u,entry=index%2u;
            const auto x=melee_web_action_interval_endpoint_cache(c.commit_.actions,cache_index);
            if(x.status!=MELEE_WEB_ACTION_ENDPOINT_OK)return refusal(Status::LeafRefused);
            const auto& e=x.entries[entry];
            auto result=make(kind,count,{cache_index,entry,x.next,x.root,x.owner,x.cache,x.runtime,
                x.interval_owner,x.hit_callback,x.miss_replay_callback,x.miss_record_callback,
                e.offset,e.archive_bytes,e.native_action,e.symbol_bytes,e.animation});
            return borrowed(result,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e.symbol_data)),e.symbol_bytes);
        }
        if(kind==Kind::ActionEvent){
            const auto x=melee_web_action_interval_endpoint_event(c.commit_.actions,index);
            if(x.status!=MELEE_WEB_ACTION_ENDPOINT_OK)return refusal(Status::LeafRefused);
            auto result=make(kind,data_.actions.event_count,{x.kind,x.policy,x.cache_hit,x.motion,x.slot,
                x.result_domain,x.offset,x.archive_bytes,x.symbol_bytes,x.cache,x.runtime,x.row,
                x.animation,x.destination,x.source,x.destination_generation,x.source_generation,
                x.source_table,x.row_owner,x.clip,x.command_owner});
            return borrowed(result,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x.symbol_data)),x.symbol_bytes);
        }
        return refusal(Status::InvalidIndex);
    case Domain::Payload:
        if(kind==Kind::Header){
            if(index)return refusal(Status::InvalidIndex);
            const auto& x=data_.payload;
            return make(kind,1,{x.schema,static_cast<std::uint32_t>(x.source_heap_handle),x.heap_bytes,
                x.owner_epoch,x.world_generation,x.allocation_watermark,x.topology_revision,
                x.arena_begin,x.arena_end,x.row_count,x.free_cell_count,x.allocated_cell_count,
                x.payload_bytes});
        }
        if(kind==Kind::PayloadRow){
            if(index>=data_.payload.row_count)return refusal(Status::InvalidIndex);
            const auto x=melee_web_source_memory_payload_endpoint_row(c.commit_.payload,index);
            if(x.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
                return refusal(Status::LeafRefused);
            auto result=make(kind,static_cast<std::uint32_t>(data_.payload.row_count),
                {x.index,x.source_address,x.requested_bytes,x.host_address,x.allocation_generation});
            return borrowed(result,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x.host_address)),x.requested_bytes);
        }
        if(kind==Kind::PayloadFreeCell||kind==Kind::PayloadAllocatedCell){
            const bool free=kind==Kind::PayloadFreeCell;
            const auto count=static_cast<std::uint32_t>(free?data_.payload.free_cell_count:data_.payload.allocated_cell_count);
            if(index>=count)return refusal(Status::InvalidIndex);
            const auto x=free?melee_web_source_memory_payload_endpoint_free_cell(c.commit_.payload,index):
                melee_web_source_memory_payload_endpoint_allocated_cell(c.commit_.payload,index);
            if(x.status!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)return refusal(Status::LeafRefused);
            return make(kind,count,{x.kind,x.source_address,x.cell_bytes,x.index});
        }
        return refusal(Status::InvalidIndex);
    case Domain::Flow:
        if(kind==Kind::Header){
            if(index)return refusal(Status::InvalidIndex);
            return make(kind,1,{data_.flow.identity,data_.flow.epoch,data_.flow.generation,
                static_cast<std::uint32_t>(data_.flow.source_valid),
                static_cast<std::uint32_t>(data_.flow.complete),
                static_cast<std::uint32_t>(data_.flow.opening_demo),
                GameplayCpuEndpointField::FlowClock::Count,
                data_.clock.process_mask_bytes,data_.clock.profile_phase_bytes,
                GameplayCpuEndpointField::FlowCadence::Count});
        }
        if(index)return refusal(Status::InvalidIndex);
        if(kind==Kind::FlowClockRecord)
            return make(kind,1,{address(data_.clock.record),data_.clock.record_bytes,
                data_.clock_endpoint.unk_0,data_.clock_endpoint.unk_4,
                data_.clock_endpoint.unk_8,static_cast<std::uint32_t>(data_.clock_endpoint.unk_C),
                data_.clock_endpoint.callback_x0,data_.clock_endpoint.callback_x1,
                data_.clock_endpoint.callback_x2,data_.clock_endpoint.callback_x3,
                data_.clock_endpoint.callback_x4_0,data_.clock_endpoint.callback_x4_1,
                data_.clock_endpoint.unk_10_xC,data_.clock_endpoint.unk_10_x20,
                data_.clock_endpoint.unk_10_x28,data_.clock_endpoint.unk_10_x30,
                static_cast<std::uint32_t>(data_.clock_endpoint.unk_10_x34),
                data_.clock_endpoint.unk_10_x38_0,data_.clock_endpoint.unk_10_x38_1});
        if(kind==Kind::FlowProcessMask)
            return borrowed(make(kind,1,{address(data_.clock.process_mask),data_.clock.process_mask_bytes}),
                data_.clock.process_mask,data_.clock.process_mask_bytes);
        if(kind==Kind::FlowProfilePhase){
            if(!data_.clock.profile_phase)return refusal(Status::InvalidIndex);
            return borrowed(make(kind,1,{address(data_.clock.profile_phase),data_.clock.profile_phase_bytes}),
                data_.clock.profile_phase,data_.clock.profile_phase_bytes);
        }
        if(kind==Kind::FlowCadenceRecord)
            return borrowed(make(kind,1,{address(data_.cadence.record),data_.cadence.record_bytes,
                static_cast<std::uint64_t>(data_.cadence_endpoint.x0_0_x0),
                static_cast<std::uint64_t>(data_.cadence_endpoint.x0_0_x8),
                static_cast<std::uint32_t>(data_.cadence_endpoint.x0_0_x10),
                static_cast<std::uint64_t>(data_.cadence_endpoint.x0_1_x0),
                static_cast<std::uint64_t>(data_.cadence_endpoint.x0_1_x8),
                static_cast<std::uint32_t>(data_.cadence_endpoint.x0_1_x10),
                data_.cadence_endpoint.x4,data_.cadence_endpoint.x38,
                static_cast<std::uint64_t>(data_.cadence_endpoint.x40),
                static_cast<std::uint32_t>(data_.cadence_endpoint.x48),
                address(data_.cadence_endpoint.alarm),data_.cadence_endpoint.alarm_bytes}),
                data_.cadence_endpoint.alarm,data_.cadence_endpoint.alarm_bytes);
        return refusal(Status::InvalidIndex);
    case Domain::Context:
        if(kind!=Kind::ContextRange||index)return refusal(Status::InvalidIndex);
        return borrowed(make(kind,1,{address(data_.context.address),data_.context.byte_extent,
            data_.context.world_generation,data_.context.owner_epoch}),data_.context.address,
            data_.context.byte_extent);
    case Domain::Bootstrap:
        if(kind==Kind::Header){if(index)return refusal(Status::InvalidIndex);return make(kind,1,{2});}
        if(index)return refusal(Status::InvalidIndex);
        if(kind==Kind::BootstrapTicks)
            return borrowed(make(kind,1,{address(data_.bootstrap.ticks),data_.bootstrap.ticks_bytes}),
                data_.bootstrap.ticks,data_.bootstrap.ticks_bytes);
        if(kind==Kind::BootstrapDisabledLinks)
            return borrowed(make(kind,1,{address(data_.bootstrap.disabled_links),data_.bootstrap.disabled_links_bytes}),
                data_.bootstrap.disabled_links,data_.bootstrap.disabled_links_bytes);
        return refusal(Status::InvalidIndex);
    case Domain::Audio:
        if(kind==Kind::Header){
            if(index)return refusal(Status::InvalidIndex);
            return make(kind,1,{c.audio_generation_,data_.audio_count,address(data_.audio.command_budget),
                address(data_.audio.sound_mode)});
        }
        if(kind==Kind::AudioOwnedRange){
            if(index>=data_.audio_count)return refusal(Status::InvalidIndex);
            const auto& x=data_.audio_ranges[index];
            auto result=make(kind,data_.audio_count,{static_cast<std::uint32_t>(x.kind),address(x.address),
                x.byte_extent,address(x.owner_identity),x.generation});
            return borrowed(result,x.address,x.byte_extent);
        }
        if(index)return refusal(Status::InvalidIndex);
        if(kind==Kind::AudioCommandBudget)
            return borrowed(make(kind,1,{address(data_.audio.command_budget),sizeof(*data_.audio.command_budget)}),
                data_.audio.command_budget,sizeof(*data_.audio.command_budget));
        if(kind==Kind::AudioSoundMode)
            return borrowed(make(kind,1,{address(data_.audio.sound_mode),sizeof(*data_.audio.sound_mode)}),
                data_.audio.sound_mode,sizeof(*data_.audio.sound_mode));
        return refusal(Status::InvalidIndex);
    case Domain::Statics:
        if(kind==Kind::Header){
            if(index)return refusal(Status::InvalidIndex);
            return make(kind,1,{data_.statics.table_identity,data_.statics.row_count});
        }
        if(kind!=Kind::StaticRow||index>=data_.statics.row_count)return refusal(Status::InvalidIndex);
        {
            const auto x=c.statics_->endpoint_row(index);
            if(x.status!=CpuStaticEndpointStatus::Ok||x.schema!=1||x.index!=index)
                return refusal(Status::LeafRefused);
            const auto* bytes=reinterpret_cast<const void*>(static_cast<std::uintptr_t>(x.address));
            return borrowed(make(kind,static_cast<std::uint32_t>(data_.statics.row_count),
                {x.index,x.address,x.extent,x.policy,x.domain}),bytes,x.extent);
        }
    }
    return refusal(Status::InvalidIndex);
}

bool GameplayCpuEndpointReader::revalidate() noexcept
{
    if(!active_||!session_||!checkpoint_){status_=Status::Incomplete;return false;}
    BatchData after{};
    if(!collect(after)||!same_batch(data_,after)||
       session_->source_frames()!=data_.source_frame){
        active_=false;status_=Status::OwnerRefused;return false;
    }
    active_=false;status_=Status::Ok;session_=nullptr;checkpoint_=nullptr;
    return true;
}
}
