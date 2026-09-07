#include "dat_stage.hpp"
#include <algorithm>

namespace melee_web {
namespace {
void region(const DatArchive& a, uint32_t offset, size_t bytes, uint32_t alignment = 4)
{
    if (offset % alignment) throw DatError("Stage descriptor or table is unaligned");
    (void) a.range(offset, bytes);
    if (bytes > a.next_target_offset(offset) - offset)
        throw DatError("Stage descriptor or table crosses a referenced region");
}

std::optional<uint32_t> reference(const DatArchive& a, uint32_t slot,
                                  uint32_t bytes = 4, uint32_t alignment = 4)
{
    const auto offset = a.pointer(slot, bytes);
    if (offset) region(a, *offset, bytes, alignment);
    return offset;
}

DatStageCountedTable table(const DatArchive& a, uint32_t slot, uint32_t element_bytes,
                           uint32_t maximum = DatStage::max_service_count,
                           uint32_t alignment = 4)
{
    DatStageCountedTable result;
    result.count = a.be32(slot + 4);
    result.element_bytes = element_bytes;
    // Source fields are signed32; their negative encodings also exceed this cap.
    if (result.count > maximum) throw DatError("Stage table count is negative or exceeds its resource budget");
    result.data_offset = reference(a, slot, 1, alignment);
    if (result.count && !result.data_offset) throw DatError("Nonempty stage table has a null pointer");
    if (result.data_offset && result.count && element_bytes)
        region(a, *result.data_offset, size_t(result.count) * element_bytes, alignment);
    return result;
}
}

DatStage::DatStage(const DatArchive& a, const std::string& root_name) : symbol(root_name)
{
    const auto& roots = a.public_symbols();
    const auto root = std::find_if(roots.begin(), roots.end(),
        [&](const auto& entry) { return entry.name == root_name; });
    if (root == roots.end()) throw DatError("Stage public map descriptor is missing");
    root_offset = root->data_offset;
    region(a, root_offset, 0x30);
    joint_reference_table = table(a, root_offset, 12);
    entry_table = table(a, root_offset + 8, 0x34, max_entries);
    spline_table = table(a, root_offset + 16, 4);
    // The pinned source calls this light-override metadata, but the actual
    // archives' raw counts do not establish the reconstructed record units.
    // Preserve the bounded pointer/count without guessing a payload layout.
    light_override_table = table(a, root_offset + 24, 0);
    shadow_table = table(a, root_offset + 32, 8);
    flagged_object_table = table(a, root_offset + 40, 4);
    for (uint32_t i = 0; i < entry_table.count; ++i) {
        DatStageEntry entry;
        entry.index = i;
        entry.descriptor_offset = *entry_table.data_offset + i * 0x34;
        const auto d = entry.descriptor_offset;
        entry.joint_offset = reference(a, d, 64);
        entry.joint_animation_table = reference(a, d + 4);
        entry.material_animation_table = reference(a, d + 8);
        entry.shape_animation_table = reference(a, d + 12);
        entry.camera_offset = reference(a, d + 16);
        entry.unknown_14_offset = reference(a, d + 20);
        entry.light_table_offset = reference(a, d + 24);
        entry.fog_offset = reference(a, d + 28);
        entry.collision_bindings = table(a, d + 32, 6, max_service_count, 2);
        entry.animation_flags_offset = reference(a, d + 40, 1, 1);
        entry.joint_indices = table(a, d + 44, 2, max_service_count, 2);
        entries.push_back(std::move(entry));
    }
}

std::vector<std::string> DatStage::unapplied_services(uint32_t index) const
{
    if (index >= entries.size()) throw DatError("Stage entry selection is outside the map table");
    const auto& entry = entries[index];
    std::vector<std::string> result;
    const auto add = [&](bool present, const char* label) { if (present) result.emplace_back(label); };
    add(bool(joint_reference_table.data_offset), "stage joint references");
    add(bool(spline_table.data_offset), "stage splines");
    add(bool(light_override_table.data_offset), "stage light overrides (payload not decoded)");
    add(bool(shadow_table.data_offset), "stage shadow metadata");
    add(bool(flagged_object_table.data_offset), "stage object flag updates");
    add(bool(entry.joint_animation_table), "joint animation");
    add(bool(entry.material_animation_table), "material animation");
    add(bool(entry.shape_animation_table), "shape animation");
    add(bool(entry.camera_offset), "stage camera");
    add(bool(entry.unknown_14_offset), "untyped stage service +0x14");
    add(bool(entry.light_table_offset), "stage lights");
    add(bool(entry.fog_offset), "stage fog");
    add(bool(entry.collision_bindings.data_offset), "collision bindings");
    add(bool(entry.animation_flags_offset), "animation flags");
    add(bool(entry.joint_indices.data_offset), "stage joint list");
    return result;
}

} // namespace melee_web
