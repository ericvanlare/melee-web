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
bool source_files_quiet=true;
bool history_captured_action_ok=true,history_armed_action_ok=true,history_restored_action_ok=true;
bool history_corrected_tape_empty=true;
std::uint64_t history_action_incarnation=1;
std::uint32_t history_action_restore_count=1;
std::uint32_t history_action_role=0; // 1 captured PRE, 2 restored PRE, 3 selected END, 4 armed PRE
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
std::array<unsigned,4> strict_payload_calls{},current_payload_calls{};
bool payload_pre_topology_changed=false;
std::uint64_t current_payload_revision=3;
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
MeleeWebCpuMotorEvidence history_motor_evidence{};
alignas(GameplayMatchSession) std::byte session_memory[sizeof(GameplayMatchSession)];
alignas(GameplayCpuCheckpoint) std::byte checkpoint_memory[10][sizeof(GameplayCpuCheckpoint)];

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

std::unique_ptr<GameplayCpuStatics> history_statics_pre(std::uint32_t pre_frame)
{
    auto value=statics_at(CpuStaticEndpointPhase::Captured);
    assert(value->begin_history(pre_frame));
    return value;
}

std::unique_ptr<GameplayCpuStatics> history_statics_armed(std::uint32_t pre_frame)
{
    auto value=history_statics_pre(pre_frame);
    assert(value->history_begin_incarnation(1,pre_frame));
    return value;
}

std::unique_ptr<GameplayCpuStatics> history_statics_end(std::uint32_t pre_frame,
    std::uint64_t incarnation,std::uint32_t end_frame,std::uint32_t end_word)
{
    source_word=0x13579bdf;
    auto value=history_statics_pre(pre_frame);
    assert(value->history_begin_incarnation(incarnation,pre_frame));
    source_word=end_word;
    assert(value->history_seal_endpoint(incarnation,end_frame));
    return value;
}

std::unique_ptr<GameplayCpuStatics> history_statics_restored_pre(
    std::uint32_t pre_frame,std::uint64_t incarnation,std::uint32_t end_frame,
    std::uint32_t pre_word,std::uint32_t end_word)
{
    source_word=pre_word;
    auto value=history_statics_pre(pre_frame);
    assert(value->history_begin_incarnation(incarnation,pre_frame));
    source_word=end_word;
    assert(value->history_seal_endpoint(incarnation,end_frame));
    assert(value->history_close_endpoint(incarnation,end_frame));
    assert(value->history_prepare_commit(incarnation,end_frame));
    assert(value->history_admit_commit(incarnation,end_frame));
    value->history_commit_prevalidated();
    assert(source_word==pre_word);
    assert(value->history_restored_pre_admit(incarnation,1,pre_frame));
    return value;
}

std::unique_ptr<GameplayCpuStatics> history_statics_restored_second_incarnation(
    std::uint32_t pre_frame,std::uint32_t pre_word)
{
    source_word=pre_word;
    auto value=history_statics_pre(pre_frame);
    assert(value->history_begin_incarnation(1,pre_frame));
    source_word=0x2468ace0;
    assert(value->history_seal_endpoint(1,11));
    assert(value->history_close_endpoint(1,11));
    assert(value->history_begin_incarnation(2,11));
    source_word=0x369cf258;
    assert(value->history_seal_endpoint(2,12));
    assert(value->history_close_endpoint(2,12));
    assert(value->history_prepare_commit(2,12));
    assert(value->history_admit_commit(2,12));
    value->history_commit_prevalidated();
    assert(source_word==pre_word);
    assert(value->history_restored_pre_admit(2,1,pre_frame));
    return value;
}

std::unique_ptr<GameplayCpuStatics> history_statics_restored_twice(
    std::uint32_t pre_frame,std::uint32_t pre_word)
{
    source_word=pre_word;
    auto value=history_statics_pre(pre_frame);
    assert(value->history_begin_incarnation(1,pre_frame));
    source_word=0x2468ace0;
    assert(value->history_seal_endpoint(1,12));
    assert(value->history_close_endpoint(1,12));
    assert(value->history_prepare_commit(1,12));
    assert(value->history_admit_commit(1,12));
    value->history_commit_prevalidated();
    assert(source_word==pre_word);
    source_word=0x13572468;
    assert(value->history_seal_endpoint(1,12));
    assert(value->history_close_endpoint(1,12));
    assert(value->history_begin_incarnation(2,12));
    source_word=0x369cf258;
    assert(value->history_seal_endpoint(2,14));
    assert(value->history_close_endpoint(2,14));
    assert(value->history_prepare_commit(2,14));
    assert(value->history_admit_commit(2,14));
    value->history_commit_prevalidated();
    assert(source_word==pre_word);
    assert(value->history_restored_pre_admit(2,2,pre_frame));
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
bool GameplayMatchSession::diagnostic_source_files_quiescent() const noexcept
{ return source_files_quiet; }
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
    static GameplayCpuCheckpoint* history(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics,GameplayCpuCheckpoint::Phase phase,
        std::uint64_t incarnation,std::uint32_t end_frame,
        std::uint32_t restore_end,std::uint32_t restore_count)
    {
        auto* value=checkpoint(slot,std::move(statics),phase);
        value->history_mode_=true;
        value->history_incarnation_=incarnation;
        value->history_start_frame_=10;
        value->history_end_frame_=end_frame;
        value->history_restore_end_=restore_end;
        value->history_restore_count_=restore_count;
        value->commit_.effect_token=phase==GameplayCpuCheckpoint::Phase::HistoryReady?0:0x10b;
        value->history_actions_=GameplayActionStoreHistory::capture();
        assert(value->history_actions_);
        return value;
    }
    static GameplayCpuCheckpoint* history_ready(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics)
    { return history(slot,std::move(statics),GameplayCpuCheckpoint::Phase::HistoryReady,0,0,0,0); }
    static GameplayCpuCheckpoint* history_armed(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics)
    { return history(slot,std::move(statics),GameplayCpuCheckpoint::Phase::HistoryRecording,1,0,0,0); }
    static GameplayCpuCheckpoint* history_predicted(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics,std::uint32_t end_frame)
    { return history(slot,std::move(statics),GameplayCpuCheckpoint::Phase::HistorySealed,1,end_frame,0,0); }
    static GameplayCpuCheckpoint* history_correcting(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics,std::uint64_t incarnation,
        std::uint32_t end_frame,std::uint32_t restore_count)
    { return history(slot,std::move(statics),GameplayCpuCheckpoint::Phase::HistoryCorrecting,
        incarnation,end_frame,end_frame,restore_count); }
    static GameplayCpuCheckpoint* history_corrected(unsigned slot,
        std::unique_ptr<GameplayCpuStatics> statics,std::uint64_t incarnation,
        std::uint32_t end_frame,std::uint32_t restore_count)
    { return history(slot,std::move(statics),GameplayCpuCheckpoint::Phase::HistoryCorrectedSealed,
        incarnation,end_frame,end_frame,restore_count); }
    static GameplayCpuCheckpoint* captured(unsigned slot,std::unique_ptr<GameplayCpuStatics> statics)
    { return checkpoint(slot,std::move(statics),GameplayCpuCheckpoint::Phase::Captured); }
    static void release_statics(GameplayCpuCheckpoint& value)
    { value.commit_.statics=nullptr;value.statics_.reset(); }
    static void release_history(GameplayCpuCheckpoint& value)
    { value.history_actions_.reset(); }
    static void set_history_identity(GameplayCpuCheckpoint& value,
        GameplayCpuCheckpoint::Phase phase,std::uint64_t incarnation,
        std::uint32_t end_frame,std::uint32_t restore_end,std::uint32_t restore_count)
    {
        value.phase_=phase;value.history_incarnation_=incarnation;
        value.history_end_frame_=end_frame;value.history_restore_end_=restore_end;
        value.history_restore_count_=restore_count;
    }
    static void set_history_phase_recording(GameplayCpuCheckpoint& value)
    { value.phase_=GameplayCpuCheckpoint::Phase::HistoryRecording; }
    static void set_history_end(GameplayCpuCheckpoint& value,std::uint32_t end_frame)
    { value.history_end_frame_=end_frame; }
    static void set_history_restore_count(GameplayCpuCheckpoint& value,std::uint32_t count)
    { value.history_restore_count_=count; }
    static void set_history_effect_token(GameplayCpuCheckpoint& value,std::uint64_t token)
    { value.commit_.effect_token=token; }
    static void reset_history_ready(GameplayCpuCheckpoint& value)
    {
        value.phase_=GameplayCpuCheckpoint::Phase::HistoryReady;
        value.history_incarnation_=0;value.history_start_frame_=10;
        value.history_end_frame_=0;value.history_restore_end_=0;
        value.history_restore_count_=0;value.commit_.effect_token=0;
    }
};
}

namespace {
using D=GameplayCpuEndpointDomain;
using K=GameplayCpuEndpointRowKind;
using V=GameplayCpuEndpointView;
using P=GameplayCpuEndpointProvenance;

void hash_bytes(std::uint64_t& hash,const void* bytes,std::size_t count)
{
    const auto* values=static_cast<const std::uint8_t*>(bytes);
    for(std::size_t i=0;i<count;++i){hash^=values[i];hash*=1099511628211ULL;}
}

void digest_record(std::uint64_t& hash,const GameplayCpuEndpointRecord& row)
{
    hash_bytes(hash,&row.status,sizeof(row.status));
    hash_bytes(hash,&row.schema,sizeof(row.schema));
    hash_bytes(hash,&row.view,sizeof(row.view));
    hash_bytes(hash,&row.provenance,sizeof(row.provenance));
    hash_bytes(hash,&row.domain,sizeof(row.domain));
    hash_bytes(hash,&row.row_kind,sizeof(row.row_kind));
    hash_bytes(hash,&row.index,sizeof(row.index));
    hash_bytes(hash,&row.count,sizeof(row.count));
    hash_bytes(hash,&row.metadata_bytes,sizeof(row.metadata_bytes));
    hash_bytes(hash,&row.flags,sizeof(row.flags));
    hash_bytes(hash,row.fields,sizeof(row.fields));
    hash_bytes(hash,&row.borrowed_address,sizeof(row.borrowed_address));
    hash_bytes(hash,&row.borrowed_bytes,sizeof(row.borrowed_bytes));
    hash_bytes(hash,&row.reserved,sizeof(row.reserved));
    if(row.borrowed_bytes)
        hash_bytes(hash,reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(row.borrowed_address)),row.borrowed_bytes);
}

struct BatchResult {
    std::uint64_t cache_next{},clock_unk4{},cadence_x0_0_x8{},digest{},effect_token{};
    std::uint32_t rows{},payload_value{};
};

GameplayCpuEndpointRecord emit(GameplayCpuEndpointReader& reader,std::uint32_t& emitted,
          std::uint64_t& digest,
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
    digest_record(digest,row);
    ++emitted;
    return row;
}

BatchResult complete_batch(GameplayMatchSession& session,GameplayCpuCheckpoint& checkpoint,
                           V view,P provenance,std::uint32_t frame,bool history=false)
{
    live_frame=frame;
    const auto strict_before=strict_payload_calls,current_before=current_payload_calls;
    GameplayCpuEndpointReader reader;
    watch_new=true;watched_new=0;
    assert(history?reader.begin_history(session,checkpoint,view,provenance):
                   reader.begin(session,checkpoint,view,provenance));
    assert(reader.status()==GameplayCpuEndpointStatus::Ok&&reader.active());
    std::uint32_t n=0;
    BatchResult result{};
    std::uint64_t digest=14695981039346656037ULL;
    emit(reader,n,digest,view,provenance,D::Owner,K::Owner,0,1,7);
    const auto effect=emit(reader,n,digest,view,provenance,D::Effects,K::EffectToken,0,1,1);
    result.effect_token=effect.fields[GameplayCpuEndpointField::Effects::Token];
    emit(reader,n,digest,view,provenance,D::Effects,K::LObjAnchor,0,1,5);
    emit(reader,n,digest,view,provenance,D::Effects,K::TObjAnchor,0,1,5);
    emit(reader,n,digest,view,provenance,D::Effects,K::ParticleAnchor,0,1,5);
    emit(reader,n,digest,view,provenance,D::Actions,K::Header,0,1,5);
    emit(reader,n,digest,view,provenance,D::Actions,K::ActionBinding,0,1,7);
    emit(reader,n,digest,view,provenance,D::Actions,K::ActionSlot,0,2,16,symbol_a,sizeof(symbol_a));
    emit(reader,n,digest,view,provenance,D::Actions,K::ActionSlot,1,2,16,symbol_b,sizeof(symbol_b));
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
        digest_record(digest,row);
        ++n;
    }
    if(!history||view!=V::Pre)
        emit(reader,n,digest,view,provenance,D::Actions,K::ActionEvent,0,1,21,
            symbol_event,sizeof(symbol_event));
    emit(reader,n,digest,view,provenance,D::Payload,K::Header,0,1,13);
    const unsigned payload_count=view==V::Pre?1:2;
    for(unsigned i=0;i<payload_count;++i){
        emit(reader,n,digest,view,provenance,D::Payload,K::PayloadRow,i,payload_count,5,
             &payload_data[i],sizeof(std::uint32_t));
        const auto row=reader.record(D::Payload,K::PayloadRow,i);
        result.payload_value=*reinterpret_cast<const std::uint32_t*>(row.borrowed_address);
        assert(result.payload_value==payload_data[i]);
        if(view!=V::Pre){
            assert(row.fields[GameplayCpuEndpointField::PayloadRow::SourceAddress]==0x700+i*4);
            assert(row.fields[GameplayCpuEndpointField::PayloadRow::AllocationGeneration]==40+i);
        }
        emit(reader,n,digest,view,provenance,D::Payload,K::PayloadFreeCell,i,payload_count,4);
        emit(reader,n,digest,view,provenance,D::Payload,K::PayloadAllocatedCell,i,payload_count,4);
    }
    emit(reader,n,digest,view,provenance,D::Flow,K::Header,0,1,10);
    const auto clock=reader.record(D::Flow,K::FlowClockRecord,0);
    assert(clock.status==0&&clock.metadata_bytes==GameplayCpuEndpointField::FlowClock::Count*8);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::Unk4]==clock_values.unk_4);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::CallbackX4_0]==clock_values.callback_x4_0);
    assert(clock.fields[GameplayCpuEndpointField::FlowClock::Unk10X20]==clock_values.unk_10_x20);
    assert(clock.borrowed_address==0&&clock.borrowed_bytes==0&&clock.flags==0&&clock.reserved==0);
    for(std::uint32_t slot=GameplayCpuEndpointField::FlowClock::Count;slot<32;++slot)
        assert(clock.fields[slot]==0);
    result.clock_unk4=clock.fields[GameplayCpuEndpointField::FlowClock::Unk4];
    digest_record(digest,clock);++n;
    emit(reader,n,digest,view,provenance,D::Flow,K::FlowProcessMask,0,1,2,
        &process_mask,sizeof(process_mask));
    emit(reader,n,digest,view,provenance,D::Flow,K::FlowProfilePhase,0,1,2,
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
    result.cadence_x0_0_x8=cadence.fields[GameplayCpuEndpointField::FlowCadence::X0_0_X8];
    digest_record(digest,cadence);++n;
    emit(reader,n,digest,view,provenance,D::Context,K::ContextRange,0,1,4,context_bytes,sizeof(context_bytes));
    emit(reader,n,digest,view,provenance,D::Bootstrap,K::Header,0,1,1);
    emit(reader,n,digest,view,provenance,D::Bootstrap,K::BootstrapTicks,0,1,2,&bootstrap_ticks,sizeof(bootstrap_ticks));
    emit(reader,n,digest,view,provenance,D::Bootstrap,K::BootstrapDisabledLinks,0,1,2,
        &bootstrap_disabled,sizeof(bootstrap_disabled));
    emit(reader,n,digest,view,provenance,D::Audio,K::Header,0,1,4);
    emit(reader,n,digest,view,provenance,D::Audio,K::AudioOwnedRange,0,2,5,audio_bytes_a,sizeof(audio_bytes_a));
    emit(reader,n,digest,view,provenance,D::Audio,K::AudioOwnedRange,1,2,5,audio_bytes_b,sizeof(audio_bytes_b));
    emit(reader,n,digest,view,provenance,D::Audio,K::AudioCommandBudget,0,1,2,&command_budget,sizeof(command_budget));
    emit(reader,n,digest,view,provenance,D::Audio,K::AudioSoundMode,0,1,2,&sound_mode,sizeof(sound_mode));
    emit(reader,n,digest,view,provenance,D::Statics,K::Header,0,1,2);
    emit(reader,n,digest,view,provenance,D::Statics,K::StaticRow,0,1,5,&source_word,sizeof(source_word));
    assert(n==(view==V::Pre?(history?31u:32u):35u));
    if(history&&view==V::Pre)
        assert(reader.record(D::Actions,K::ActionEvent,0).status==
            static_cast<std::uint32_t>(GameplayCpuEndpointStatus::LeafRefused));
    assert(reader.record(D::Payload,K::PayloadRow,payload_count).status==
        static_cast<std::uint32_t>(GameplayCpuEndpointStatus::InvalidIndex));
    const auto invalid=reader.record(D::Actions,K::ActionBinding,1);
    assert(invalid.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::InvalidIndex));
    assert(invalid.flags==0&&invalid.reserved==0&&invalid.metadata_bytes==0);
    for(const auto value:invalid.fields)assert(value==0);
    assert(reader.revalidate()&&reader.status()==GameplayCpuEndpointStatus::Ok&&!reader.active());
    watch_new=false;
    assert(watched_new==0);
    if(view==V::Pre){
        assert(current_payload_calls==current_before);
        for(unsigned i=0;i<4;++i)assert(strict_payload_calls[i]>strict_before[i]);
    }else{
        assert(strict_payload_calls==strict_before);
        for(unsigned i=0;i<4;++i)assert(current_payload_calls[i]>current_before[i]);
    }
    result.rows=n;
    result.digest=digest;
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
// Named payload providers model distinct captured PRE and grown current END
// ledgers. Actual ledger/allocator admission is covered by its leaf control.
MeleeWebSourcePayloadEndpointHeader payload_fixture_header(bool current)
{
 MeleeWebSourcePayloadEndpointHeader x{};
 x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);
 x.source_heap_handle=3;x.heap_bytes=32;x.owner_epoch=8;x.world_generation=55;
 x.allocation_watermark=current?9:7;x.topology_revision=current?current_payload_revision:2;
 x.arena_begin=0x700;x.arena_end=0x720;x.row_count=current?2:1;
 x.free_cell_count=current?2:1;x.allocated_cell_count=current?2:1;
 x.payload_bytes=x.row_count*sizeof(payload_data[0]);return x;
}
MeleeWebSourcePayloadEndpointHeader melee_web_source_memory_payload_endpoint_header(const MeleeWebSourceMemoryPayloadSnapshot* owner)
{
 ++strict_payload_calls[0];assert(owner==const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a));
 if(payload_pre_topology_changed){MeleeWebSourcePayloadEndpointHeader x{};x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;return x;}
 return payload_fixture_header(false);
}
MeleeWebSourcePayloadEndpointRow melee_web_source_memory_payload_endpoint_row(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ ++strict_payload_calls[1];assert(!payload_pre_topology_changed&&index==0);MeleeWebSourcePayloadEndpointRow x{};x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);x.source_address=0x701;x.requested_bytes=sizeof(payload_data[0]);x.index=index;x.host_address=reinterpret_cast<uintptr_t>(payload_data);x.allocation_generation=4;return x; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_free_cell(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ ++strict_payload_calls[2];assert(!payload_pre_topology_changed&&index==0);return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,1,sizeof(MeleeWebSourcePayloadEndpointCell),MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL,0x702,8,index}; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_allocated_cell(const MeleeWebSourceMemoryPayloadSnapshot*,uint64_t index)
{ ++strict_payload_calls[3];assert(!payload_pre_topology_changed&&index==0);return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,1,sizeof(MeleeWebSourcePayloadEndpointCell),MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL,0x703,8,index}; }
MeleeWebSourcePayloadEndpointHeader melee_web_source_memory_payload_current_endpoint_header(const MeleeWebSourceMemoryPayloadSnapshot* owner)
{ ++current_payload_calls[0];assert(owner==const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a));return payload_fixture_header(true); }
bool current_payload_header_matches(const MeleeWebSourcePayloadEndpointHeader* header)
{
 const auto live=payload_fixture_header(true);
 return header&&header->schema==live.schema&&header->source_heap_handle==live.source_heap_handle&&
  header->heap_bytes==live.heap_bytes&&header->owner_epoch==live.owner_epoch&&
  header->world_generation==live.world_generation&&header->allocation_watermark==live.allocation_watermark&&
  header->topology_revision==live.topology_revision&&header->arena_begin==live.arena_begin&&
  header->arena_end==live.arena_end&&header->row_count==live.row_count&&
  header->free_cell_count==live.free_cell_count&&header->allocated_cell_count==live.allocated_cell_count&&
  header->payload_bytes==live.payload_bytes;
}
MeleeWebSourcePayloadEndpointRow melee_web_source_memory_payload_current_endpoint_row(const MeleeWebSourceMemoryPayloadSnapshot* owner,const MeleeWebSourcePayloadEndpointHeader* header,uint64_t index)
{
 ++current_payload_calls[1];assert(owner==const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a));
 MeleeWebSourcePayloadEndpointRow x{};
 if(!current_payload_header_matches(header)){x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;return x;}
 assert(index<2);x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);
 x.index=index;x.source_address=0x700+index*4;x.requested_bytes=sizeof(payload_data[0]);
 x.host_address=reinterpret_cast<uintptr_t>(&payload_data[index]);x.allocation_generation=40+index;return x;
}
MeleeWebSourcePayloadEndpointCell payload_fixture_current_cell(const MeleeWebSourceMemoryPayloadSnapshot* owner,const MeleeWebSourcePayloadEndpointHeader* header,uint64_t index,bool free)
{
 assert(owner==const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a));
 MeleeWebSourcePayloadEndpointCell x{};
 if(!current_payload_header_matches(header)){x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;return x;}
 assert(index<2);x.status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;x.schema=1;x.bytes=sizeof(x);
 x.kind=free?MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_FREE_CELL:MELEE_WEB_SOURCE_PAYLOAD_ENDPOINT_ALLOCATED_CELL;
 x.source_address=(free?0x710:0x700)+index*4;x.cell_bytes=4;x.index=index;return x;
}
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_current_endpoint_free_cell(const MeleeWebSourceMemoryPayloadSnapshot* owner,const MeleeWebSourcePayloadEndpointHeader* header,uint64_t index)
{++current_payload_calls[2];return payload_fixture_current_cell(owner,header,index,true);}
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_current_endpoint_allocated_cell(const MeleeWebSourceMemoryPayloadSnapshot* owner,const MeleeWebSourcePayloadEndpointHeader* header,uint64_t index)
{++current_payload_calls[3];return payload_fixture_current_cell(owner,header,index,false);}
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
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_history_evidence_for(
    uint64_t token,uint64_t world,const void* context,uint64_t context_epoch,
    uint32_t pre_frame,uint64_t incarnation,uint32_t actual_end)
{ return token==0x10b&&world==55&&context==handle<MeleeWebMatchContext>(0x901)&&
    context_epoch==8&&pre_frame==10&&incarnation>0&&incarnation<=3&&
    actual_end>pre_frame&&actual_end-pre_frame<=7?&history_motor_evidence:nullptr; }
MeleeWebSourceMemoryPayloadSnapshotStatus melee_web_source_memory_payload_snapshot_admit(
    const MeleeWebSourceMemoryPayloadSnapshot* owner)
{ return owner==const_handle<MeleeWebSourceMemoryPayloadSnapshot>(0x10a)?
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK:MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER; }
}

namespace melee_web {
GameplayActionStoreHistory::GameplayActionStoreHistory(
    MeleeWebActionInterval* capsule) noexcept : capsule_(capsule) {}
GameplayActionStoreHistory::~GameplayActionStoreHistory() = default;
std::unique_ptr<GameplayActionStoreHistory> GameplayActionStoreHistory::capture()
{
    return std::unique_ptr<GameplayActionStoreHistory>(
        new GameplayActionStoreHistory(nullptr));
}
std::uint32_t GameplayActionStoreHistory::history_captured_pre_admit() const noexcept
{ return history_action_role==1u&&history_captured_action_ok?MELEE_WEB_ACTION_ENDPOINT_OK:MELEE_WEB_ACTION_ENDPOINT_OWNER; }
MeleeWebActionEndpointHeader GameplayActionStoreHistory::history_captured_pre_header() const noexcept
{
    if(history_captured_pre_admit()!=MELEE_WEB_ACTION_ENDPOINT_OK)
        return {MELEE_WEB_ACTION_ENDPOINT_OWNER};
    return {MELEE_WEB_ACTION_ENDPOINT_OK,1,MELEE_WEB_ACTION_ENDPOINT_PRE,
        MELEE_WEB_ACTION_ENDPOINT_REFERENCE,1,2,1,0};
}
MeleeWebActionEndpointBinding GameplayActionStoreHistory::history_captured_pre_binding(
    std::uint32_t index) const noexcept
{ return history_captured_pre_admit()==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_binding(nullptr,index):MeleeWebActionEndpointBinding{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointSlot GameplayActionStoreHistory::history_captured_pre_slot(
    std::uint32_t binding,std::uint32_t slot) const noexcept
{ return history_captured_pre_admit()==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_slot(nullptr,binding,slot):MeleeWebActionEndpointSlot{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointCache GameplayActionStoreHistory::history_captured_pre_cache(
    std::uint32_t index) const noexcept
{ return history_captured_pre_admit()==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_cache(nullptr,index):MeleeWebActionEndpointCache{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointEvent GameplayActionStoreHistory::history_captured_pre_event(
    std::uint32_t index) const noexcept
{ (void)index;return {history_captured_pre_admit()==MELEE_WEB_ACTION_ENDPOINT_OK?
    MELEE_WEB_ACTION_ENDPOINT_PHASE:MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
std::uint32_t GameplayActionStoreHistory::history_armed_pre_admit(
    std::uint64_t incarnation) const noexcept
{ return history_action_role==4u&&incarnation==1u&&
    incarnation==history_action_incarnation&&history_armed_action_ok&&
    history_corrected_tape_empty?MELEE_WEB_ACTION_ENDPOINT_OK:
    MELEE_WEB_ACTION_ENDPOINT_OWNER; }
MeleeWebActionEndpointHeader GameplayActionStoreHistory::history_armed_pre_header(
    std::uint64_t incarnation) const noexcept
{
    if(history_armed_pre_admit(incarnation)!=MELEE_WEB_ACTION_ENDPOINT_OK)
        return {MELEE_WEB_ACTION_ENDPOINT_OWNER};
    return {MELEE_WEB_ACTION_ENDPOINT_OK,1,MELEE_WEB_ACTION_ENDPOINT_PRE,
        MELEE_WEB_ACTION_ENDPOINT_REFERENCE,1,2,1,0};
}
MeleeWebActionEndpointBinding GameplayActionStoreHistory::history_armed_pre_binding(
    std::uint64_t incarnation,std::uint32_t index) const noexcept
{ return history_armed_pre_admit(incarnation)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_binding(nullptr,index):MeleeWebActionEndpointBinding{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointSlot GameplayActionStoreHistory::history_armed_pre_slot(
    std::uint64_t incarnation,std::uint32_t binding,std::uint32_t slot) const noexcept
{ return history_armed_pre_admit(incarnation)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_slot(nullptr,binding,slot):MeleeWebActionEndpointSlot{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointCache GameplayActionStoreHistory::history_armed_pre_cache(
    std::uint64_t incarnation,std::uint32_t index) const noexcept
{ return history_armed_pre_admit(incarnation)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_cache(nullptr,index):MeleeWebActionEndpointCache{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointEvent GameplayActionStoreHistory::history_armed_pre_event(
    std::uint64_t incarnation,std::uint32_t index) const noexcept
{ (void)index;return {history_armed_pre_admit(incarnation)==MELEE_WEB_ACTION_ENDPOINT_OK?
    MELEE_WEB_ACTION_ENDPOINT_PHASE:MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
std::uint32_t GameplayActionStoreHistory::history_restored_pre_admit(
    std::uint64_t incarnation,std::uint64_t restore_count) const noexcept
{ return history_action_role==2u&&incarnation==history_action_incarnation&&
    restore_count==history_action_restore_count&&restore_count>=1u&&restore_count<=2u&&
    history_restored_action_ok&&history_corrected_tape_empty?
    MELEE_WEB_ACTION_ENDPOINT_OK:MELEE_WEB_ACTION_ENDPOINT_OWNER; }
MeleeWebActionEndpointHeader GameplayActionStoreHistory::history_restored_pre_header(
    std::uint64_t incarnation,std::uint64_t restore_count) const noexcept
{
    if(history_restored_pre_admit(incarnation,restore_count)!=MELEE_WEB_ACTION_ENDPOINT_OK)
        return {MELEE_WEB_ACTION_ENDPOINT_OWNER};
    return {MELEE_WEB_ACTION_ENDPOINT_OK,1,MELEE_WEB_ACTION_ENDPOINT_PRE,
        MELEE_WEB_ACTION_ENDPOINT_REFERENCE,1,2,1,0};
}
MeleeWebActionEndpointBinding GameplayActionStoreHistory::history_restored_pre_binding(
    std::uint64_t incarnation,std::uint64_t restore_count,std::uint32_t index) const noexcept
{ return history_restored_pre_admit(incarnation,restore_count)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_binding(nullptr,index):MeleeWebActionEndpointBinding{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointSlot GameplayActionStoreHistory::history_restored_pre_slot(
    std::uint64_t incarnation,std::uint64_t restore_count,
    std::uint32_t binding,std::uint32_t slot) const noexcept
{ return history_restored_pre_admit(incarnation,restore_count)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_slot(nullptr,binding,slot):MeleeWebActionEndpointSlot{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointCache GameplayActionStoreHistory::history_restored_pre_cache(
    std::uint64_t incarnation,std::uint64_t restore_count,std::uint32_t index) const noexcept
{ return history_restored_pre_admit(incarnation,restore_count)==MELEE_WEB_ACTION_ENDPOINT_OK?
    melee_web_action_interval_endpoint_cache(nullptr,index):MeleeWebActionEndpointCache{MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
MeleeWebActionEndpointEvent GameplayActionStoreHistory::history_restored_pre_event(
    std::uint64_t incarnation,std::uint64_t restore_count,
    std::uint32_t index) const noexcept
{ (void)index;return {history_restored_pre_admit(incarnation,restore_count)==MELEE_WEB_ACTION_ENDPOINT_OK?
    MELEE_WEB_ACTION_ENDPOINT_PHASE:MELEE_WEB_ACTION_ENDPOINT_OWNER}; }
std::uint32_t GameplayActionStoreHistory::endpoint_admit() const noexcept
{ return history_action_role==3u&&action_admitted?MELEE_WEB_ACTION_ENDPOINT_OK:MELEE_WEB_ACTION_ENDPOINT_OWNER; }
MeleeWebActionEndpointHeader GameplayActionStoreHistory::endpoint_header() const noexcept
{ return melee_web_action_interval_endpoint_header(nullptr); }
MeleeWebActionEndpointBinding GameplayActionStoreHistory::endpoint_binding(
    std::uint32_t index) const noexcept
{ return melee_web_action_interval_endpoint_binding(nullptr,index); }
MeleeWebActionEndpointSlot GameplayActionStoreHistory::endpoint_slot(
    std::uint32_t binding,std::uint32_t slot) const noexcept
{ return melee_web_action_interval_endpoint_slot(nullptr,binding,slot); }
MeleeWebActionEndpointCache GameplayActionStoreHistory::endpoint_cache(
    std::uint32_t index) const noexcept
{ return melee_web_action_interval_endpoint_cache(nullptr,index); }
MeleeWebActionEndpointEvent GameplayActionStoreHistory::endpoint_event(
    std::uint32_t index) const noexcept
{ return melee_web_action_interval_endpoint_event(nullptr,index); }
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
    payload_pre_topology_changed=true;
    const auto ref_result=complete_batch(*session,*reference,V::CpuEnd,P::Reference,12);
    assert(ref_result.rows==35);
    assert(complete_batch(*session,*reference,V::DrawEnd,P::Reference,12).rows==35);

    action_provenance=MELEE_WEB_ACTION_ENDPOINT_CORRECTED;
    auto* corrected=GameplayCpuEndpointTestFixture::corrected_sealed(2,
        statics_at(CpuStaticEndpointPhase::Restored));
    const auto corrected_result=complete_batch(*session,*corrected,V::CpuEnd,P::Corrected,12);
    assert(corrected_result.rows==35);
    assert(complete_batch(*session,*corrected,V::DrawEnd,P::Corrected,12).rows==35);
    const auto original_payload=payload_data[1];
    payload_data[1]^=0x00010000;
    const auto changed_payload_bytes=complete_batch(*session,*corrected,V::CpuEnd,P::Corrected,12);
    assert(changed_payload_bytes.payload_value==payload_data[1]&&
           changed_payload_bytes.payload_value!=corrected_result.payload_value);
    payload_data[1]=original_payload;
    // A changed current topology invalidates both indexed reads and end-of-batch
    // validation, rather than being silently refreshed under an active batch.
    GameplayCpuEndpointReader changed_payload;
    assert(changed_payload.begin(*session,*corrected,V::CpuEnd,P::Corrected));
    ++current_payload_revision;
    const auto stale_payload=changed_payload.record(D::Payload,K::PayloadRow,1);
    assert(stale_payload.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::LeafRefused));
    assert(!stale_payload.metadata_bytes&&!stale_payload.borrowed_address&&!stale_payload.borrowed_bytes);
    assert(!changed_payload.revalidate());
    --current_payload_revision;
    payload_pre_topology_changed=false;

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

    // History readers reuse the same nine-domain encoders but authenticate a
    // distinct captured/restored PRE role and selected predicted/corrected END.
    const std::uint32_t history_pre_word=0x13579bdf;
    const std::uint32_t history_end_word=0x2468ace0;
    source_word=history_pre_word;live_frame=10;source_files_quiet=true;
    action_view=MELEE_WEB_ACTION_ENDPOINT_PRE;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    history_action_role=1;history_captured_action_ok=true;
    history_armed_action_ok=true;history_restored_action_ok=true;
    history_corrected_tape_empty=true;
    history_action_incarnation=1;
    history_action_restore_count=1;
    auto* history_pre=GameplayCpuEndpointTestFixture::history_ready(0,
        history_statics_pre(10));

    // Captured PRE authenticates exact Restore bytes; a corrupted source row
    // refuses before the reader exposes any receipt.
    source_word^=1;
    GameplayCpuEndpointReader corrupted_history_pre;
    assert(!corrupted_history_pre.begin_history(*session,*history_pre,V::Pre,P::SharedPre));
    assert(corrupted_history_pre.status()==GameplayCpuEndpointStatus::OwnerRefused);
    source_word=history_pre_word;
    const auto captured_history=complete_batch(*session,*history_pre,V::Pre,P::SharedPre,10,true);
    assert(captured_history.rows==31&&captured_history.effect_token==0);

    // The armed PRE is captured after incarnation 1 binds the live motor and
    // effect token but before the first source tick. It retains the same
    // source/slot/cache PRE bytes while the live token differs from pre-arm.
    history_action_role=4;
    auto* history_armed=GameplayCpuEndpointTestFixture::history_armed(7,
        history_statics_armed(10));
    source_word^=1;
    GameplayCpuEndpointReader corrupted_armed_history;
    assert(!corrupted_armed_history.begin_history(*session,*history_armed,V::Pre,P::SharedPre));
    assert(corrupted_armed_history.status()==GameplayCpuEndpointStatus::OwnerRefused);
    source_word=history_pre_word;
    history_corrected_tape_empty=false;
    GameplayCpuEndpointReader dirty_armed_history;
    assert(!dirty_armed_history.begin_history(*session,*history_armed,V::Pre,P::SharedPre));
    assert(dirty_armed_history.status()==GameplayCpuEndpointStatus::OwnerRefused);
    history_corrected_tape_empty=true;
    const auto armed_history=complete_batch(*session,*history_armed,V::Pre,P::SharedPre,10,true);
    assert(armed_history.rows==31&&armed_history.effect_token==0x10b&&
           armed_history.effect_token!=captured_history.effect_token);
    GameplayCpuEndpointReader changed_armed_token;
    assert(changed_armed_token.begin_history(*session,*history_armed,V::Pre,P::SharedPre));
    GameplayCpuEndpointTestFixture::set_history_effect_token(*history_armed,0);
    assert(changed_armed_token.record(D::Effects,K::EffectToken,0).status==
        static_cast<std::uint32_t>(GameplayCpuEndpointStatus::OwnerRefused));
    assert(!changed_armed_token.revalidate());
    GameplayCpuEndpointTestFixture::set_history_effect_token(*history_armed,0x10b);

    // Actual selected END is admitted through the checkpoint's session-derived
    // source frame, full owner guards, and history-only leaf/static endpoints.
    history_action_role=3;
    action_view=MELEE_WEB_ACTION_ENDPOINT_END;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    live_frame=12;
    auto* history_reference=GameplayCpuEndpointTestFixture::history_predicted(1,
        history_statics_end(10,1,12,history_end_word),12);
    assert(complete_batch(*session,*history_reference,V::CpuEnd,P::Reference,12,true).rows==35);
    assert(complete_batch(*session,*history_reference,V::DrawEnd,P::Reference,12,true).rows==35);

    // A restored PRE is only readable at the real PRE frame in Correcting,
    // with the exact restore incarnation and an empty corrected event tape.
    source_word=history_pre_word;live_frame=10;
    action_view=MELEE_WEB_ACTION_ENDPOINT_PRE;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    history_action_role=2;history_action_incarnation=1;history_action_restore_count=1;
    auto* history_restored=GameplayCpuEndpointTestFixture::history_correcting(2,
        history_statics_restored_pre(10,1,12,history_pre_word,history_end_word),1,12,1);
    const auto restored_history=complete_batch(*session,*history_restored,V::Pre,P::SharedPre,10,true);
    assert(restored_history.rows==31&&restored_history.effect_token==armed_history.effect_token&&
           restored_history.digest==armed_history.digest);
    history_corrected_tape_empty=false;
    GameplayCpuEndpointReader dirty_history_pre;
    assert(!dirty_history_pre.begin_history(*session,*history_restored,V::Pre,P::SharedPre));
    assert(dirty_history_pre.status()==GameplayCpuEndpointStatus::OwnerRefused&&
           source_word==history_pre_word);
    history_corrected_tape_empty=true;
    history_action_incarnation=2;history_action_restore_count=1;
    auto* stale_history=GameplayCpuEndpointTestFixture::history_correcting(3,
        history_statics_restored_pre(10,1,12,history_pre_word,history_end_word),2,12,1);
    GameplayCpuEndpointReader stale_history_incarnation;
    assert(!stale_history_incarnation.begin_history(*session,*stale_history,V::Pre,P::SharedPre));
    assert(stale_history_incarnation.status()==GameplayCpuEndpointStatus::OwnerRefused);

    // The first correction may follow a confirmed incarnation 1. Here the
    // latest restored incarnation is 2 while cumulative restore_count is 1.
    auto* history_restored_in_incarnation_two=GameplayCpuEndpointTestFixture::history_correcting(6,
        history_statics_restored_second_incarnation(10,history_pre_word),2,12,1);
    const auto restored_second_incarnation=complete_batch(*session,
        *history_restored_in_incarnation_two,V::Pre,P::SharedPre,10,true);
    assert(restored_second_incarnation.effect_token==armed_history.effect_token&&
           restored_second_incarnation.digest==armed_history.digest);

    // The second restore belongs to incarnation 2, with a cumulative restore
    // count of 2, and still admits the exact immutable PRE bytes.
    history_action_incarnation=2;
    history_action_restore_count=2;
    auto* history_restored_twice=GameplayCpuEndpointTestFixture::history_correcting(5,
        history_statics_restored_twice(10,history_pre_word),2,14,2);
    assert(complete_batch(*session,*history_restored_twice,V::Pre,P::SharedPre,10,true).rows==31);

    // Static admission binds the actual cumulative count separately from
    // the latest restored incarnation; mismatched counts refuse before rows.
    history_action_restore_count=1;
    auto* stale_restore_count=GameplayCpuEndpointTestFixture::history_correcting(8,
        history_statics_restored_twice(10,history_pre_word),2,14,1);
    GameplayCpuEndpointReader stale_history_count;
    assert(!stale_history_count.begin_history(*session,*stale_restore_count,V::Pre,P::SharedPre));
    assert(stale_history_count.status()==GameplayCpuEndpointStatus::OwnerRefused);

    // Corrected CPU and DRAW endpoints remain distinct views over the selected
    // corrected END and carry the corrected source provenance.
    history_action_role=3;
    action_view=MELEE_WEB_ACTION_ENDPOINT_END;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_CORRECTED;
    live_frame=12;
    auto* history_corrected=GameplayCpuEndpointTestFixture::history_corrected(4,
        history_statics_end(10,1,12,history_end_word+1),1,12,1);
    assert(complete_batch(*session,*history_corrected,V::CpuEnd,P::Corrected,12,true).rows==35);
    assert(complete_batch(*session,*history_corrected,V::DrawEnd,P::Corrected,12,true).rows==35);

    source_word=history_pre_word;
    history_action_role=1;
    action_view=MELEE_WEB_ACTION_ENDPOINT_PRE;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    live_frame=10;
    GameplayCpuEndpointReader changed_history_identity;
    assert(changed_history_identity.begin_history(*session,*history_pre,V::Pre,P::SharedPre));
    GameplayCpuEndpointTestFixture::set_history_restore_count(*history_pre,1);
    assert(changed_history_identity.record(D::Owner,K::Owner,0).status==
        static_cast<std::uint32_t>(GameplayCpuEndpointStatus::OwnerRefused));
    assert(!changed_history_identity.revalidate());
    GameplayCpuEndpointTestFixture::set_history_restore_count(*history_pre,0);

    GameplayCpuEndpointReader changed_history_frame;
    assert(changed_history_frame.begin_history(*session,*history_pre,V::Pre,P::SharedPre));
    live_frame=11;
    assert(changed_history_frame.record(D::Owner,K::Owner,0).status==
        static_cast<std::uint32_t>(GameplayCpuEndpointStatus::OwnerRefused));
    assert(!changed_history_frame.revalidate());
    live_frame=10;

    GameplayCpuEndpointTestFixture::set_history_phase_recording(*history_pre);
    GameplayCpuEndpointReader history_wrong_phase;
    assert(!history_wrong_phase.begin_history(*session,*history_pre,V::Pre,P::SharedPre));
    GameplayCpuEndpointTestFixture::reset_history_ready(*history_pre);
    source_files_quiet=false;
    GameplayCpuEndpointReader history_non_quiet;
    assert(!history_non_quiet.begin_history(*session,*history_pre,V::Pre,P::SharedPre));
    source_files_quiet=true;

    // The reader cannot accept a forged eight-frame endpoint; it rechecks the
    // CPU checkpoint's actual-end admission instead of taking caller input.
    action_view=MELEE_WEB_ACTION_ENDPOINT_END;
    action_provenance=MELEE_WEB_ACTION_ENDPOINT_REFERENCE;
    history_action_role=3;live_frame=18;
    GameplayCpuEndpointTestFixture::set_history_end(*history_reference,18);
    GameplayCpuEndpointReader overwide_history;
    assert(!overwide_history.begin_history(*session,*history_reference,V::CpuEnd,P::Reference));
    assert(overwide_history.status()==GameplayCpuEndpointStatus::OwnerRefused);
    GameplayCpuEndpointTestFixture::set_history_end(*history_reference,12);
    live_frame=10;

    GameplayCpuEndpointTestFixture::release_statics(*pre);
    GameplayCpuEndpointTestFixture::release_statics(*reference);
    GameplayCpuEndpointTestFixture::release_statics(*corrected);
    GameplayCpuEndpointTestFixture::release_statics(*unsealed);
    GameplayCpuEndpointTestFixture::release_statics(*history_pre);
    GameplayCpuEndpointTestFixture::release_history(*history_pre);
    GameplayCpuEndpointTestFixture::release_statics(*history_armed);
    GameplayCpuEndpointTestFixture::release_history(*history_armed);
    GameplayCpuEndpointTestFixture::release_statics(*history_reference);
    GameplayCpuEndpointTestFixture::release_history(*history_reference);
    GameplayCpuEndpointTestFixture::release_statics(*history_restored);
    GameplayCpuEndpointTestFixture::release_history(*history_restored);
    GameplayCpuEndpointTestFixture::release_statics(*stale_history);
    GameplayCpuEndpointTestFixture::release_history(*stale_history);
    GameplayCpuEndpointTestFixture::release_statics(*history_corrected);
    GameplayCpuEndpointTestFixture::release_history(*history_corrected);
    GameplayCpuEndpointTestFixture::release_statics(*history_restored_twice);
    GameplayCpuEndpointTestFixture::release_history(*history_restored_twice);
    GameplayCpuEndpointTestFixture::release_statics(*history_restored_in_incarnation_two);
    GameplayCpuEndpointTestFixture::release_history(*history_restored_in_incarnation_two);
    GameplayCpuEndpointTestFixture::release_statics(*stale_restore_count);
    GameplayCpuEndpointTestFixture::release_history(*stale_restore_count);
    puts("Retained-history captured/armed/restored PRE and predicted/corrected CPU/DRAW endpoint batches passed strict phase/incarnation/restore-count/static/event/source-frame admission");
    puts("Current END payload routing enumerates grown rows/cells for Reference and Corrected CPU/DRAW without allocation; PRE and stale-header refusals stay strict");
    puts("Admitted nine-domain PRE/reference/corrected batches, named flow fields, cache next, and read-only revalidation passed");
}
