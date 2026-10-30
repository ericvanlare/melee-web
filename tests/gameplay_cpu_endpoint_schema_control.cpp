#include "gameplay_cpu_endpoint.hpp"
#include "gameplay_match_session.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace melee_web;

namespace melee_web {
std::uint32_t GameplayMatchSession::source_frames() const { return 0; }
GameplayMatchCpuOwners GameplayMatchSession::diagnostic_cpu_owners() const noexcept { return {}; }
CpuStaticEndpointRow GameplayCpuStatics::endpoint_row(std::uint64_t) const noexcept { return {}; }
}

extern "C" {
MeleeWebActionEndpointBinding melee_web_action_interval_endpoint_binding(
    const MeleeWebActionInterval*,std::uint32_t) { return {}; }
MeleeWebActionEndpointSlot melee_web_action_interval_endpoint_slot(
    const MeleeWebActionInterval*,std::uint32_t,std::uint32_t) { return {}; }
MeleeWebActionEndpointCache melee_web_action_interval_endpoint_cache(
    const MeleeWebActionInterval*,std::uint32_t) { return {}; }
MeleeWebActionEndpointEvent melee_web_action_interval_endpoint_event(
    const MeleeWebActionInterval*,std::uint32_t) { return {}; }
MeleeWebSourcePayloadEndpointRow melee_web_source_memory_payload_endpoint_row(
    const MeleeWebSourceMemoryPayloadSnapshot*,std::uint64_t) { return {}; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_free_cell(
    const MeleeWebSourceMemoryPayloadSnapshot*,std::uint64_t) { return {}; }
MeleeWebSourcePayloadEndpointCell melee_web_source_memory_payload_endpoint_allocated_cell(
    const MeleeWebSourceMemoryPayloadSnapshot*,std::uint64_t) { return {}; }
}

int main()
{
    GameplayCpuEndpointRecord record{};
    assert(sizeof(record)==312);
    assert(offsetof(GameplayCpuEndpointRecord,fields)==40);
    assert(offsetof(GameplayCpuEndpointRecord,borrowed_address)==296);
    assert(offsetof(GameplayCpuEndpointRecord,borrowed_bytes)==304);
    assert(offsetof(GameplayCpuEndpointRecord,reserved)==308);
    assert(record.status==0&&record.schema==0&&record.flags==0&&record.reserved==0);
    for(const auto value:record.fields)assert(value==0);

    record.status=static_cast<std::uint32_t>(GameplayCpuEndpointStatus::Ok);
    record.schema=1;
    record.view=static_cast<std::uint32_t>(GameplayCpuEndpointView::CpuEnd);
    record.provenance=static_cast<std::uint32_t>(GameplayCpuEndpointProvenance::Corrected);
    record.domain=static_cast<std::uint32_t>(GameplayCpuEndpointDomain::Actions);
    record.row_kind=static_cast<std::uint32_t>(GameplayCpuEndpointRowKind::ActionEvent);
    record.index=6;record.count=7;
    record.metadata_bytes=GameplayCpuEndpointField::ActionEvent::Count*sizeof(std::uint64_t);
    record.fields[GameplayCpuEndpointField::ActionEvent::Motion]=0x12345678u;
    assert(record.metadata_bytes==168);
    assert(record.fields[GameplayCpuEndpointField::ActionEvent::Motion]==0x12345678u);
    for(std::size_t i=GameplayCpuEndpointField::ActionEvent::Count;i<32;++i)
        assert(record.fields[i]==0);
    assert(record.flags==0&&record.reserved==0);

    GameplayCpuEndpointReader reader;
    SlippiParticleCellEndpointHeader particle_header;
    SlippiParticleCellEndpointRow particle_row;
    std::memset(&particle_header,0xff,sizeof(particle_header));
    std::memset(&particle_row,0xff,sizeof(particle_row));
    assert(!reader.particle_header(particle_header));
    assert(!reader.particle_row(SLIPPI_PARTICLE_CELL_ROW_LIVE,0,particle_row));
    const auto zero=[](const auto& value){
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)assert(bytes[i]==0);
    };
    zero(particle_header);zero(particle_row);
    assert(sizeof(particle_header)==64&&sizeof(particle_row)==96);
    const auto refused=reader.record(GameplayCpuEndpointDomain::Actions,
        GameplayCpuEndpointRowKind::ActionEvent,0);
    assert(refused.status==static_cast<std::uint32_t>(GameplayCpuEndpointStatus::Incomplete));
    assert(refused.schema==1&&refused.view==0&&refused.provenance==0&&
        refused.domain==0&&refused.row_kind==0&&refused.index==0&&refused.count==0&&
        refused.metadata_bytes==0&&refused.flags==0&&refused.borrowed_address==0&&
        refused.borrowed_bytes==0&&refused.reserved==0);
    for(const auto value:refused.fields)assert(value==0);
    assert(!reader.active()&&reader.status()==GameplayCpuEndpointStatus::Invalid);
    puts("Fixed CPU endpoint schema offsets, named-slot bounds, and zeroed unused fields passed");
}
