#pragma once

#include "dat_archive.hpp"
#include <optional>
#include <string>
#include <vector>

namespace melee_web {

struct DatStageCountedTable {
    std::optional<uint32_t> data_offset;
    uint32_t count = 0;
    // Zero means the service's payload layout/count units are not yet decoded.
    // Otherwise the complete count * element_bytes region has been checked.
    uint32_t element_bytes = 0;
};

struct DatStageEntry {
    uint32_t index = 0, descriptor_offset = 0;
    std::optional<uint32_t> joint_offset;
    // These are references to unapplied services, not decoded runtime objects.
    // Animation tables have no count in the source map entry; no terminator or
    // animation count is inferred from nearby data or relocation targets.
    std::optional<uint32_t> joint_animation_table, material_animation_table, shape_animation_table;
    std::optional<uint32_t> camera_offset, unknown_14_offset, light_table_offset, fog_offset;
    DatStageCountedTable collision_bindings; // GrJoint records: three signed16 values.
    std::optional<uint32_t> animation_flags_offset;
    DatStageCountedTable joint_indices; // Signed16 source joint indices.
};

// Reads UnkStageDat and its explicit 0x34-byte map entries. No stage callbacks,
// animation, lights, camera, fog, collision or object flag mutation are applied.
// The selected joint tree is decoded separately by the model reader. Metadata
// is copied, and this object does not retain byte spans or archive ownership.
class DatStage {
public:
    static constexpr uint32_t max_entries = 256, max_service_count = 65536;
    explicit DatStage(const DatArchive& archive, const std::string& symbol = "map_head");
    uint32_t root_offset = 0;
    std::string symbol;
    DatStageCountedTable entry_table;
    std::vector<DatStageEntry> entries;
    DatStageCountedTable joint_reference_table, spline_table, light_override_table,
                         shadow_table, flagged_object_table;

    // Names of present services whose semantics this inspection viewer has not
    // applied. Throws for an invalid selection; does not imply missing services
    // have been emulated or that all entries are simultaneously stage-visible.
    [[nodiscard]] std::vector<std::string> unapplied_services(uint32_t entry_index) const;
};

} // namespace melee_web
