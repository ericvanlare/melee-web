#pragma once

#include "dat_native_stage.hpp"
#include "dat_stage.hpp"

#include <cstdint>
#include <vector>

namespace melee_web::test {

/* C0 parser/owner checks use the same immutable C profile data as the guarded
 * source-stage profile. This adapter owns the C++ strings/vectors required by
 * DatNativeMapContract; no second Stadium identity table lives in tests. */
inline const MeleeWebStageProfile& stadium_profile_data()
{
    const auto* profile = melee_web_stage_stadium_profile_data();
    if (!profile) throw DatError("Canonical Stadium C0 profile data is absent");
    return *profile;
}

inline DatNativeStageMapContractData stadium_contract_data(
    const DatArchive& archive, const DatStage& metadata)
{
    return dat_native_stage_map_contract_from_profile(
        stadium_profile_data(), archive, metadata);
}

/* Count authored per-entry Ground light-pointer slots, including aliases. Each
 * present table is bounded at the next authored target and must contain a null
 * pointer terminator before that boundary. This is distinct from the map-level
 * light-override table and from unique descriptor identities. */
inline std::vector<uint32_t> ground_authored_light_pointer_counts(
    const DatArchive& archive, const DatStage& metadata)
{
    std::vector<uint32_t> counts(metadata.entries.size(), 0);
    for (const auto& entry : metadata.entries) {
        if (!entry.light_table_offset) continue;
        const uint32_t root = *entry.light_table_offset;
        const uint32_t end = archive.next_target_offset(root);
        uint32_t count = 0;
        for (; root + count * 4 < end; ++count)
            if (!archive.pointer(root + count * 4, 4)) break;
        if (root + count * 4 >= end || archive.pointer(root + count * 4, 4))
            throw DatError("Ground authored light-pointer table lacks a bounded null terminator");
        counts.at(entry.index) = count;
    }
    return counts;
}

} // namespace melee_web::test
