#include "gameplay_cpu_endpoint.hpp"
#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_match_session.hpp"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <utility>

using namespace melee_web;

namespace {
bool watch_new=false;
std::uint32_t watched_new=0;
GameplayMatchCpuOwners live_owners{};
std::uint32_t live_frame=10;
std::uint32_t cache_next=21;
std::uint32_t action_view=MELEE_WEB_ACTION_ENDPOINT_PRE;
std::uint32_t action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
bool action_admitted=true;
std::uint32_t source_word=0x13579bdf;
std::uint64_t process_mask=0x123456789abcdef0ULL;
std::uint32_t profile_phase=6;
std::array<std::uint8_t,64> clock_record{};
std::array<std::uint8_t,128> cadence_record{};
std::array<std::uint8_t,24> alarm_bytes{};
std::uint32_t context_bytes[2]={0x10203040,0x50607080};
std::uint32_t bootstrap_ticks=37,bootstrap_disabled=0;
std::uint32_t command_budget=12,sound_mode=2;
std::uint32_t payload_data[2]={0xfeed1234,0x76543210};
std::uint8_t symbol_a[]={'a','t','k','1'};
std::uint8_t symbol_b[]={'a','t','k','2'};
std::uint8_t symbol_event[]={'c','u','r'};
std::uint32_t audio_bytes_a[2]={1,2},audio_bytes_b[2]={3,4};
MeleeWebAudioOwnedRange audio_ranges[2]{};
MeleeWebClockCpuEndpoint clock_values={1,1,2,3,4,5,6,7,8,9,10,11,
    0x1112131415161718ULL,0x2122232425262728ULL,12,13,14,15};
MeleeWebCadenceCpuEndpoint cadence_values={1,100,7,1,200,9,0,300,400,500,0,
    alarm_bytes.data(),alarm_bytes.size()};
std::uint8_t owner_token=0,context_token=0,flow_token=0,audio_token=0;
std::uint8_t snapshot_token=0,payload_token=0,bootstrap_token=0,action_token=0;
std::uint32_t marker=0;
std::uint8_t session_storage[256]{};
alignas(GameplayMatchSession) std::byte session_memory[sizeof(GameplayMatchSession)];
alignas(GameplayCpuCheckpoint) std::byte checkpoint_memory[4][sizeof(GameplayCpuCheckpoint)];

template<class T> T* handle(std::uintptr_t n)
{ return reinterpret_cast<T*>(n); }
template<class T> const T* const_handle(std::uintptr_t n)
{ return reinterpret_cast<const T*>(n); }
std::uint32_t address32(const void* p)
{
    const auto value=reinterpret_cast<std::uintptr_t>(p);
    assert(value<=UINT32_MAX);
    return static_cast<std::uint32_t>(value);
}

std::unique_ptr<GameplayCpuStatics> statics_at(CpuStaticEndpointPhase phase)
{
    const auto address=address32(&source_word);
    const CpuStaticRow row{address,sizeof(source_word),CpuStaticPolicy::Restore,
        CpuStaticDomain::OriginalSourceC};
    const CpuStaticBounds bounds{address,address+sizeof(source_word)};
    auto value=GameplayCpuStatics::configure(std::span<const CpuStaticRow>(&row,1),
        bounds,std::span<const CpuIntervalSpan>{});
    assert(value&&value->capture());
    if(phase==CpuStaticEndpointPhase::Sealed||phase==CpuStaticEndpointPhase::Restored)
        assert(value->seal_post());
    if(phase==CpuStaticEndpointPhase::Restored)
        assert(value->prepare_commit()&&value->commit_prevalidated());
    assert(value->endpoint_header().status==CpuStaticEndpointStatus::Ok);
    return value;
}
}

void* operator new(std::size_t size)
{
    if(watch_new)++watched_new;
    if(void* value=std::malloc(size?size:1))return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* value) noexcept{std::free(value);}
void operator delete[](void* value) noexcept{std::free(value);}
void operator delete(void* value,std::size_t) noexcept{std::free(value);}
void operator delete[](void* value,std::size_t) noexcept{std::free(value);}

namespace melee_web {
struct GameplayMatchSession::Storage {};
GameplayMatchSession::GameplayMatchSession() noexcept = default;
GameplayMatchCpuOwners GameplayMatchSession::diagnostic_cpu_owners() const noexcept
{ return live_owners; }
std::uint32_t GameplayMatchSession::source_frames() const
{ return live_frame; }
}

namespace melee_web {
struct GameplayCpuEndpointTestFixture {
    static GameplayMatchSession* session()
    { return ::new(session_memory) GameplayMatchSession{}; }
    static GameplayCpuCheckpoint* checkpoint(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics,GameplayCpuCheckpoint::Phase phase)
    {
        auto* value=::new(checkpoint_memory[slot]) GameplayCpuCheckpoint(CpuCheckpointSpan::TwoTicks);
        value->owner_=live_owners;
        value->owner_.frame=10;
        value->commit_.actions=handle<MeleeWebActionInterval>(0x101);
        value->commit_.audio=handle<MeleeWebAudio>(0x102);
        value->commit_.audio_snapshot=const_handle<MeleeWebAudioSnapshot>(0x103);
        value->commit_.flow=handle<MeleeWebMatchFlow>(0x104);
        value->commit_.flow_snapshot=const_handle<MeleeWebMatchFlowCpuSnapshot>(0x105);
        value->post_flow_=handle<MeleeWebMatchFlowCpuSnapshot>(0x106);
        value->commit_.bootstrap=const_handle<MeleeWebGameplayBootstrapSnapshot>(0x107);
        value->commit_.context=handle<MeleeWebMatchContext>(0x108);
        value->commit_.context_snapshot=const_handle<MeleeWebMatchContextSnapshot>(0x109);
        value->commit_.payload=const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a);
        value->commit_.effect_token=0x10b;
        value->commit_.lobj_anchor={live_owners.world,1,2,3,0};
        value->commit_.tobj_anchor={live_owners.world,4,5,6,0};
        value->commit_.particle_anchor={live_owners.world,7,8,9,10};
        value->statics_=std::move(statics);
        value->commit_.statics=value->statics_.get();
        value->audio_generation_=11;
        value->post_frame_=12;
        value->phase_=phase;
        return value;
    }
    static GameplayCpuCheckpoint* recording(unsigned slot,std::unique_ptr<GameplayCpuStatics> statics)
    { return checkpoint(slot,std::move(statics),GameplayCpuCheckpoint::Phase::Recording); }
    static GameplayCpuCheckpoint* sealed(unsigned slot,std::unique_ptr<GameplayCpuStatics> statics)
    { return checkpoint(slot,std::move(statics),GameplayCpuCheckpoint::Phase::Sealed); }
    static GameplayCpuCheckpoint* corrected_sealed(unsigned slot,std::unique_ptr<GameplayCpuStatics> statics)
    { return checkpoint(slot,std::move(statics),GameplayCpuCheckpoint::Phase::CorrectedSealed); }
    static GameplayCpuCheckpoint* captured(unsigned slot,std::unique_ptr<GameplayCpuStatics> statics)
    { return checkpoint(slot,std::move(statics),GameplayCpuCheckpoint::Phase::Captured); }
    static void release_statics(GameplayCpuCheckpoint& value)
    { value.commit_.statics=nullptr;value.statics_.reset(); }
};
}

namespace {
using D=GameplayCpuEndpointDomain;
using K=GameplayCpuEndpointRowKind;
using V=GameplayCpuEndpointView;
using P=GameplayCpuEndpointProvenance;

void emit(GameplayCpuEndpointReader& reader,std::uint32_t& emitted,
          V view,P provenance,D domain,K kind,std::uint32_t index,
          std::uint32_t count,std::uint32_t fields,
          const void* borrowed=nullptr,std::uint32_t borrowed_bytes=0)
{
    const auto row=reader.record(domain,kind,index);
    assert(row.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::Ok));
    assert(row.schema==1&&row.view==static_cast<std::uint32_t>(view));
    assert(row.provenance==static_cast<std::uint32_t>(provenance));
    assert(row.domain==static_cast<std::uint32_t>(domain));
    assert(row.row_kind==static_cast<std::uint32_t>(kind));
    assert(row.index==index&&row.count==count);
    assert(row.metadata_bytes==fields*sizeof(std::uint64_t));
    assert(row.flags==0&&row.reserved==0);
    for(std::uint32_t slot=fields;slot<32;++slot)assert(row.fields[slot]==0);
    if(borrowed_bytes){
        assert(row.borrowed_address==reinterpret_cast<std::uintptr_t>(borrowed));
        assert(row.borrowed_bytes==borrowed_bytes);
    }else assert(row.borrowed_address==0&&row.borrowed_bytes==0);
    ++emitted;
}

struct BatchResult {std::uint64_t cache_next{},clock_unk4{},cadence_x0_0_x8{};std::uint32_t rows{};};
BatchResult complete_batch(GameplayMatchSession& session,GameplayCpuCheckpoint& checkpoint,
                           V view,P provenance,std::uint32_t frame)
{
    live_frame=frame;
    GameplayCpuEndpointReader reader;
    watch_new=true;watched_new=0;
    assert(reader.begin(session,checkpoint,view,provenance));
    assert(reader.status()==GameplayCpuEndpointStatus::Ok&&reader.active());
    std::uint32_t n=0;
    emit(reader,n,view,provenance,D::Owner,K::Owner,0,1,7);
    emit(reader,n,view,provenance,D::Effects,K::EffectToken,0,1,1);
    emit(reader,n,view,provenance,D::Effects,K::LObjAnchor,0,1,5);
    emit(reader,n,view,provenance,D::Effects,K::TObjAnchor,0,1,5);
    emit(reader,n,view,provenance,D::Effects,K::ParticleAnchor,0,1,5);
    emit(reader,n,view,provenance,D::Actions,K::Header,0,1,5);
    emit(reader,n,view,provenance,D::Actions,K::ActionBinding,0,1,7);
    emit(reader,n,view,provenance,D::Actions,K::ActionSlot,0,2,16,symbol_a,sizeof(symbol_a));
    emit(reader,n,view,provenance,D::Actions,K::ActionSlot,1,2,16,symbol_b,sizeof(symbol_b));
    BatchResult result{};
    for(std::uint32_t i=0;i<2;++i){
        const auto row=reader.record(D::Actions,K::ActionCacheEntry,i);
        assert(row.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::Ok));
        assert(row.metadata_bytes==GameplayCpuEndpointField::ActionCacheEntry::Count*8);
        assert(row.fields[GameplayCpuEndpointField::ActionCacheEntry::Next]==cache_next);
        assert(row.borrowed_address==reinterpret_cast<std::uintptr_t>(i?symbol_b:symbol_a));
        assert(row.borrowed_bytes==(i?sizeof(symbol_b):sizeof(symbol_a)));
        assert(row.flags==0&&row.reserved==0);
        for(std::uint32_t slot=GameplayCpuEndpointField::ActionCacheEntry::Count;slot<32;++slot)
            assert(row.fields[slot]==0);
        if(i==0)result.cache_next=row.fields[GameplayCpuEndpointField::ActionCacheEntry::Next];
        ++n;
    }
    emit(reader,n,view,provenance,D::Actions,K::ActionEvent,0,1,21,
        symbol_event,sizeof(symbol_event));
    emit(reader,n,view,provenance,D::Payload,K::Header,0,1,13);
    emit(reader,n,view,provenance,D::Payload,K::PayloadRow,0,1,5,payload_data,sizeof(std::uint32_t));
    emit(reader,n,view,provenance,D::Payload,K::PayloadFreeCell,0,1,4);
    emit(reader,n,view,provenance,D::Payload,K::PayloadAllocatedCell,0,1,4);
    emit(reader,n,view,provenance,D::Flow,K::Header,0,1,10);
    const auto clock=reader.record(D::Flow,K::FlowClockRecord,0);
    assert(clock.status==0&&clock.metadata_bytes==GameplayCpuEndpointField::FlowClock::Count*8);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::Unk4]==clock_values.unk_4);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::CallbackX4_0]==clock_values.callback_x4_0);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::Unk10X20]==clock_values.unk_10_x20);
    assert(clock.borrowed_address==0&&clock.borrowed_bytes==0&&clock.flags==0&&clock.reserved==0);
    for(std::uint32_t slot=GameplayCpuEndpointField::FlowClock::Count;slot<32;++slot)
        assert(clock.fields[slot]==0);
    result.clock_unk4=clock.fields[GameplayCpuEndpointField::FlowClock::Unk4];++n;
    emit(reader,n,view,provenance,D::Flow,K::FlowProcessMask,0,1,2,
        &process_mask,sizeof(process_mask));
    emit(reader,n,view,provenance,D::Flow,K::FlowProfilePhase,0,1,2,
        &profile_phase,sizeof(profile_phase));
    const auto cadence=reader.record(D::Flow,K::FlowCadenceRecord,0);
    assert(cadence.status==0&&cadence.metadata_bytes==GameplayCpuEndpointField::FlowCadence::Count*8);
    assert(cadence.fields[GameplayCpuEndpointField::FlowCadence::X0_0_X8]==
        static_cast<std::uint64_t>(cadence_values.x0_0_x8));
    assert(cadence.fields[GameplayCpuEndpointField::FlowCadence::AlarmAddress]==
        reinterpret_cast<std::uintptr_t>(alarm_bytes.data()));
    assert(cadence.borrowed_address==reinterpret_cast<std::uintptr_t>(alarm_bytes.data()));
    assert(cadence.borrowed_bytes==alarm_bytes.size()&&cadence.flags==0&&cadence.reserved==0);
    for(std::uint32_t slot=GameplayCpuEndpointField::FlowCadence::Count;slot<32;++slot)
        assert(cadence.fields[slot]==0);
    result.cadence_x0_0_x8=cadence.fields[GameplayCpuEndpointField::FlowCadence::X0_0_X8];++n;
    emit(reader,n,view,provenance,D::Context,K::ContextRange,0,1,4,context_bytes,sizeof(context_bytes));
    emit(reader,n,view,provenance,D::Bootstrap,K::Header,0,1,1);
    emit(reader,n,view,provenance,D::Bootstrap,K::BootstrapTicks,0,1,2,&bootstrap_ticks,sizeof(bootstrap_ticks));
    emit(reader,n,view,provenance,D::Bootstrap,K::BootstrapDisabledLinks,0,1,2,
        &bootstrap_disabled,sizeof(bootstrap_disabled));
    emit(reader,n,view,provenance,D::Audio,K::Header,0,1,4);
    emit(reader,n,view,provenance,D::Audio,K::AudioOwnedRange,0,2,5,audio_bytes_a,sizeof(audio_bytes_a));
    emit(reader,n,view,provenance,D::Audio,K::AudioOwnedRange,1,2,5,audio_bytes_b,sizeof(audio_bytes_b));
    emit(reader,n,view,provenance,D::Audio,K::AudioCommandBudget,0,1,2,&command_budget,sizeof(command_budget));
    emit(reader,n,view,provenance,D::Audio,K::AudioSoundMode,0,1,2,&sound_mode,sizeof(sound_mode));
    emit(reader,n,view,provenance,D::Statics,K::Header,0,1,2);
    emit(reader,n,view,provenance,D::Statics,K::StaticRow,0,1,5,&source_word,sizeof(source_word));
    assert(n==32);
    const auto invalid=reader.record(D::Actions,K::ActionBinding,1);
    assert(invalid.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::InvalidIndex));
    assert(invalid.flags==0&&invalid.reserved==0&&invalid.metadata_bytes==0);
    for(const auto value:invalid.fields)assert(value==0);
    assert(reader.revalidate()&&reader.status()==GameplayCpuEndpointStatus::Ok&&!reader.active());
    watch_new=false;
    assert(watched_new==0);
    result.rows=n;
    return result;
}
}

extern "C" {
uint32_t melee_web_action_interval_endpoint_admit(const MeleeWebActionInterval*)
{ return action_admitted?MELEE_WEB_ACTION_ENDPOINT_OK:MELEE_WEB_ACTION_ENDPOINT_OWNER; }
MeleeWebActionEndpointHeader melee_web_action_interval_endpoint_header(const MeleeWebActionInterval*)
{ return {MELEE_WEB_ACTION_ENDPOINT_OK,1,action_view,action_provenance,1,2,1,1}; }
MeleeWebActionEndpointBinding melee_web_action_interval_endpoint_binding(const MeleeWebActionInterval*,uint32_t index)
{ MeleeWebActionEndpointBinding x{};x.status=0;x.index=index;x.present=1;x.generation=9;x.fighter=0x201;x.store=0x202;x.select_callback=0x203;x.transfer_callback=0x204;x.table=0x205;return x; }
MeleeWebActionEndpointSlot melee_web_action_interval_endpoint_slot(const MeleeWebActionInterval*,uint32_t binding,uint32_t slot)
{ MeleeWebActionEndpointSlot x{};x.status=0;x.binding=binding;x.slot=slot;x.present=1;x.motion=slot+1;x.offset=0x300+slot;x.archive_bytes=16;x.symbol_bytes=slot?sizeof(symbol_b):sizeof(symbol_a);x.fighter=0x201;x.store=0x202;x.table=0x205;x.clip=0x206+slot;x.tree=0x207+slot;x.row_identity=0x208+slot;x.animation=0x209+slot;x.row_owner=0x20a;x.command_owner=0x20b;x.symbol_data=reinterpret_cast<uintptr_t>(slot?symbol_b:symbol_a);return x; }
MeleeWebActionEndpointCache melee_web_action_interval_endpoint_cache(const MeleeWebActionInterval*,uint32_t index)
{ MeleeWebActionEndpointCache x{};x.status=0;x.index=index;x.next=cache_next;x.root=0x301;x.owner=0x302;x.cache=0x303;x.runtime=0x304;x.interval_owner=0x305;x.hit_callback=0x306;x.miss_replay_callback=0x307;x.miss_record_callback=0x308;for(unsigned i=0;i<2;++i){auto& e=x.entries[i];e.offset=0x400+i;e.archive_bytes=16;e.native_action=1;e.symbol_bytes=i?sizeof(symbol_b):sizeof(symbol_a);e.animation=0x500+i;e.symbol_data=reinterpret_cast<uintptr_t>(i?symbol_b:symbol_a);}return x; }
MeleeWebActionEndpointEvent melee_web_action_interval_endpoint_event(const MeleeWebActionInterval*,uint32_t index)
{ MeleeWebActionEndpointEvent x{};x.status=0;x.index=index;x.kind=2;x.policy=1;x.cache_hit=0;x.motion=3;x.slot=1;x.result_domain=0;x.offset=0x600;x.archive_bytes=8;x.symbol_bytes=sizeof(symbol_event);x.cache=0x601;x.runtime=0x602;x.row=0x603;x.animation=0x604;x.destination=0x605;x.source=0x606;x.destination_generation=1;x.source_generation=2;x.source_table=0x607;x.row_owner=0x608;x.clip=0x609;x.command_owner=0x610;x.symbol_data=reinterpret_cast<uintptr_t>(symbol_event);return x; }
MeleeWebSourcePayloadEndpointHeader melee_web_source_memory_payload_endpoint_header(const MeleeWebSourceMemoryPayloadSnapshot*)
{ MeleeWebSourcePayloadEndpointHeader x{};x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);x.source_heap_handle=3;x.heap_bytes=16;x.owner_epoch=8;x.world_generation=55;x.allocation_watermark=7;x.topology_revision=2;x.arena_begin=0x700;x.arena_end=0x710;x.row_count=1;x.free_cell_count=1;x.allocated_cell_count=1;x.payload_bytes=sizeof(payload_data[0]);return x; }
MeleeWebSourcePayloadEndpointRow melee_web_source_memory_payload_endpoint_row(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ MeleeWebSourcePayloadEndpointRow x{};x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);x.source_address=0x701;x.requested_bytes=sizeof(payload_data[0]);x.index=index;x.host_address=reinterpret_cast<uintptr_t>(payload_data);x.allocation_generation=4;return x; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_free_cell(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,1,sizeof(MeleeWebSourcePayloadEndpointCell),MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL,0x702,8,index}; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_allocated_cell(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,1,sizeof(MeleeWebSourcePayloadEndpointCell),MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL,0x703,8,index}; }
int melee_web_match_flow_cpu_snapshot_admit(MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t){return 1;}
MeleeWebMatchFlowCpuEndpoint melee_web_match_flow_cpu_endpoint_read(MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*)
{ return {0,0x801,8,3,1,0,0}; }
int melee_web_match_context_snapshot_admit(MeleeWebMatchContext*,const MeleeWebMatchContextSnapshot*,char*,size_t){return 1;}
MeleeWebMatchContextRangeResult melee_web_match_context_range_read(MeleeWebMatchContext*,MeleeWebMatchContextRange* out,char*,size_t)
{ if(!out)return MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT;*out={context_bytes,sizeof(context_bytes),55,8};return MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE; }
int melee_web_gameplay_bootstrap_snapshot_admit(const MeleeWebGameplayBootstrapSnapshot*,char*,size_t){return 1;}
MeleeWebBootstrapCpuStorage melee_web_gameplay_bootstrap_cpu_storage(void)
{ return {&bootstrap_ticks,sizeof(bootstrap_ticks),&bootstrap_disabled,sizeof(bootstrap_disabled)}; }
int melee_web_audio_snapshot_admit(MeleeWebAudio*,const MeleeWebAudioSnapshot*){return 1;}
MeleeWebAudioCpuStorage melee_web_audio_cpu_storage(MeleeWebAudio*,uint64_t){return {&command_budget,&sound_mode};}
MeleeWebAudioRangeVisitResult melee_web_audio_visit_owned_ranges(MeleeWebAudio*,uint64_t,size_t capacity,MeleeWebAudioOwnedRangeVisitor visitor,void* context)
{ if(capacity<2)return MELEE_WEB_AUDIO_RANGE_VISIT_INSUFFICIENT_CAPACITY;for(const auto& row:audio_ranges)if(!visitor(&row,context))return MELEE_WEB_AUDIO_RANGE_VISIT_STOPPED;return MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE; }
MeleeWebClockCpuStorage melee_web_source_clock_cpu_storage(void)
{ return {clock_record.data(),clock_record.size(),&process_mask,sizeof(process_mask),&profile_phase,sizeof(profile_phase)}; }
MeleeWebClockCpuEndpoint melee_web_source_clock_cpu_endpoint(void){return clock_values;}
MeleeWebCadenceCpuStorage melee_web_source_cadence_cpu_storage(void)
{ return {cadence_record.data(),cadence_record.size()}; }
MeleeWebCadenceCpuEndpoint melee_web_source_cadence_cpu_endpoint(void){return cadence_values;}
int melee_web_cpu_interval_admit(uint64_t token){return token==0x10b;}
}

int main()
{
    live_owners={&owner_token,handle<MeleeWebMatchContext>(0x901),
        handle<MeleeWebMatchFlow>(0x902),handle<MeleeWebAudio>(0x903),55,8,10};
    audio_ranges[0]={MELEE_WEB_AUDIO_RANGE_AUDIO_BINDINGS,audio_bytes_a,sizeof(audio_bytes_a),
        &audio_token,11};
    audio_ranges[1]={MELEE_WEB_AUDIO_RANGE_AUDIO_PROGRAMS,audio_bytes_b,sizeof(audio_bytes_b),
        &audio_token,11};
    auto* session=GameplayCpuEndpointTestFixture::session();

    auto* pre=GameplayCpuEndpointTestFixture::recording(0,
        statics_at(CpuStaticEndpointPhase::Captured));
    const auto pre_result=complete_batch(*session,*pre,V::Pre,P::SharedPre,10);
    assert(pre_result.rows==32&&pre_result.cache_next==cache_next);

    action_view=MELEE_WEB_ACTION_ENDPOINT_END;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    auto* reference=GameplayCpuEndpointTestFixture::sealed(1,
        statics_at(CpuStaticEndpointPhase::Sealed));
    const auto ref_result=complete_batch(*session,*reference,V::CpuEnd,P::Reference,12);
    assert(ref_result.rows==32);

    action_provenance=MELEE_WEB_ACTION_ENDPOINT_CORRECTED;
    auto* corrected=GameplayCpuEndpointTestFixture::corrected_sealed(2,
        statics_at(CpuStaticEndpointPhase::Restored));
    const auto corrected_result=complete_batch(*session,*corrected,V::CpuEnd,P::Corrected,12);
    assert(corrected_result.rows==32);

    cache_next=22;
    action_view=MELEE_WEB_ACTION_ENDPOINT_PRE;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    const auto changed_cache=complete_batch(*session,*pre,V::Pre,P::SharedPre,10);
    assert(changed_cache.cache_next==22&&changed_cache.cache_next!=pre_result.cache_next);
    cache_next=21;

    const auto old_clock=clock_values.unk_4;
    const auto old_cadence=cadence_values.x0_0_x8;
    clock_values.unk_4=0x1234;cadence_values.x0_0_x8=-7;
    const auto changed_named=complete_batch(*session,*pre,V::Pre,P::SharedPre,10);
    assert(changed_named.clock_unk4==0x1234&&changed_named.cadence_x0_0_x8==
        static_cast<std::uint64_t>(-7));
    clock_values.unk_4=old_clock;cadence_values.x0_0_x8=old_cadence;

    GameplayCpuEndpointReader stale;
    live_frame=10;
    assert(stale.begin(*session,*pre,V::Pre,P::SharedPre));
    live_frame=11;
    const auto refused=stale.record(D::Owner,K::Owner,0);
    assert(refused.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::OwnerRefused));
    assert(!stale.revalidate()&&stale.status()==GameplayCpuEndpointStatus::OwnerRefused);
    live_frame=10;

    GameplayCpuEndpointReader wrong_phase;
    assert(!wrong_phase.begin(*session,*pre,V::Pre,P::Corrected));
    assert(wrong_phase.status()==GameplayCpuEndpointStatus::WrongPhase);
    auto* unsealed=GameplayCpuEndpointTestFixture::captured(3,
        statics_at(CpuStaticEndpointPhase::Captured));
    GameplayCpuEndpointReader phase_refused;
    assert(!phase_refused.begin(*session,*unsealed,V::Pre,P::SharedPre));
    assert(phase_refused.status()==GameplayCpuEndpointStatus::OwnerRefused);

    GameplayCpuEndpointReader owner_refused;
    live_owners.world++;
    assert(!owner_refused.begin(*session,*pre,V::Pre,P::SharedPre));
    assert(owner_refused.status()==GameplayCpuEndpointStatus::OwnerRefused);
    live_owners.world--;

    GameplayCpuEndpointReader leaf_refused;
    action_admitted=false;
    assert(!leaf_refused.begin(*session,*pre,V::Pre,P::SharedPre));
    assert(leaf_refused.status()==GameplayCpuEndpointStatus::OwnerRefused);
    action_admitted=true;

    GameplayCpuEndpointReader changed_during_batch;
    assert(changed_during_batch.begin(*session,*pre,V::Pre,P::SharedPre));
    const auto before=changed_during_batch.record(D::Flow,K::FlowClockRecord,0);
    assert(before.status==0);
    clock_values.unk_4++;
    assert(!changed_during_batch.revalidate());
    clock_values.unk_4--;

    GameplayCpuEndpointTestFixture::release_statics(*pre);
    GameplayCpuEndpointTestFixture::release_statics(*reference);
    GameplayCpuEndpointTestFixture::release_statics(*corrected);
    GameplayCpuEndpointTestFixture::release_statics(*unsealed);
    puts("Admitted nine-domain PRE/reference/corrected batches, named flow fields, cache next, and read-only revalidation passed");
}
