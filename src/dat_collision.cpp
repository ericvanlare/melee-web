#include "dat_collision.hpp"
#include "dat_stage.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace melee_web {
namespace {
void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}
void region(const DatArchive& archive, std::uint32_t offset, std::size_t bytes, std::uint32_t alignment = 4)
{
    require(offset % alignment == 0, "Collision descriptor or array is unaligned");
    (void) archive.range(offset, bytes);
    require(bytes <= archive.next_target_offset(offset) - offset,
            "Collision descriptor or array crosses a referenced region");
}
std::int16_t signed16(const DatArchive& archive, std::uint32_t offset)
{
    return std::bit_cast<std::int16_t>(archive.be16(offset));
}
std::uint32_t array(const DatArchive& archive, std::uint32_t slot,
                    std::uint32_t count, std::size_t width)
{
    const auto offset = archive.pointer(slot, std::size_t(count) * width);
    require(offset.has_value(), "Required collision array pointer is null");
    region(archive, *offset, std::size_t(count) * width);
    return *offset;
}
DatCollisionRange range(const DatArchive& archive, std::uint32_t offset, std::size_t limit)
{
    DatCollisionRange result{signed16(archive, offset), signed16(archive, offset + 2)};
    require(result.start >= 0 && result.count >= 0 && std::size_t(result.start) <= limit &&
                std::size_t(result.count) <= limit - std::size_t(result.start),
            "Collision start/count range is negative or outside its source array");
    return result;
}
DatCollisionRanges ranges(const DatArchive& archive, std::uint32_t offset, std::size_t limit)
{
    DatCollisionRanges result;
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = range(archive, offset + std::uint32_t(i * 4), limit);
    return result;
}
void line_reference(std::int16_t id, std::size_t count)
{
    require(id == -1 || (id >= 0 && std::size_t(id) < count), "Collision adjacency is outside the line array");
}
float finite(const DatArchive& archive, std::uint32_t offset)
{
    const float value = archive.f32(offset);
    require(std::isfinite(value), "Collision coordinate or joint bound is nonfinite");
    return value;
}
} // namespace

float read_dat_stage_scale(const DatArchive& archive)
{
    const auto& symbols = archive.public_symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(),
        [](const auto& symbol) { return symbol.name == "grGroundParam"; });
    require(found != symbols.end(), "Stage scale requires the grGroundParam public root");
    region(archive, found->data_offset, 4);
    require(!archive.has_relocation(found->data_offset), "Stage scale field is an unsupported relocated pointer");
    const float scale = archive.f32(found->data_offset);
    require(std::isfinite(scale) && scale > 0, "Stage scale must be positive and finite");
    return scale;
}

DatCollision::DatCollision(const DatArchive& archive, const std::string& name) : symbol(name)
{
    const auto& symbols = archive.public_symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(), [&](const auto& s) { return s.name == name; });
    require(found != symbols.end(), "Collision public coll_data descriptor is missing");
    root_offset = found->data_offset;
    /* MapCollData's source consumers stop at the joint count at +0x28. Some
     * archives carry an additional word at +0x2c, while GrSt.dat ends exactly
     * at the next public root. Keep that optional word when its own referenced
     * region contains it; never borrow bytes from the following symbol. */
    region(archive, root_offset, 44);
    const auto vertex_count = archive.be32(root_offset + 4);
    const auto line_count = archive.be32(root_offset + 12);
    const auto joint_count = archive.be32(root_offset + 40);
    // mpLibLoad allocates these exact capacities and dereferences the final joint.
    // Negative signed32 encodings exceed the bounds as well.
    require(vertex_count > 0 && vertex_count <= max_vertices && line_count > 0 &&
                line_count <= max_lines && joint_count > 0 && joint_count <= max_joints,
            "Collision counts are empty, negative or exceed original mpLib capacities");
    vertex_offset = array(archive, root_offset, vertex_count, 8);
    line_offset = array(archive, root_offset + 8, line_count, 16);
    joint_offset = array(archive, root_offset + 36, joint_count, 40);
    if (archive.next_target_offset(root_offset) - root_offset >= 48)
        source_reserved_2c = archive.be32(root_offset + 44);
    line_ranges = ranges(archive, root_offset + 16, line_count);
    for (std::uint32_t i = 0; i < vertex_count; ++i)
        vertices.push_back({finite(archive, vertex_offset + i * 8), finite(archive, vertex_offset + i * 8 + 4)});
    for (std::uint32_t i = 0; i < line_count; ++i) {
        const auto d = line_offset + i * 16;
        DatCollisionLine line{d, archive.be16(d), archive.be16(d + 2),
            signed16(archive, d + 4), signed16(archive, d + 6), signed16(archive, d + 8),
            signed16(archive, d + 10), archive.be16(d + 12), archive.be16(d + 14)};
        require(line.v0 < vertex_count && line.v1 < vertex_count, "Collision line vertex index is outside the vertex array");
        for (const auto id : {line.prev0, line.next0, line.prev1, line.next1}) line_reference(id, line_count);
        lines.push_back(line);
    }
    // Every line passed through this subset must be initialized by mpLibLoad.
    // Preserve the source ordering; gaps/overlaps are unsupported instead of
    // silently constructing a different categorization or reading uninitialized state.
    std::vector<bool> initialized(line_count, false);
    for (std::size_t category = 0; category < line_ranges.size(); ++category) {
        const auto selected = line_ranges[category];
        for (int i = selected.start; i < selected.start + selected.count; ++i) {
            require(!initialized[i], "Collision source category ranges overlap");
            initialized[i] = true;
            if (category < 4)
                require(lines[i].kind() == (1U << category), "Collision line kind differs from its source static category");
        }
    }
    require(std::all_of(initialized.begin(), initialized.end(), [](bool value) { return value; }),
            "Collision source category ranges leave lines uninitialized");
    for (std::uint32_t i = 0; i < joint_count; ++i) {
        const auto d = joint_offset + i * 40;
        DatCollisionJoint joint{d, ranges(archive, d, line_count), finite(archive, d + 20),
            finite(archive, d + 24), finite(archive, d + 28), finite(archive, d + 32),
            range(archive, d + 36, vertex_count)};
        require(joint.left <= joint.right && joint.bottom <= joint.top, "Collision joint bounds are reversed");
        for (std::size_t category = 0; category < joint.line_ranges.size(); ++category) {
            const auto local = joint.line_ranges[category], global = line_ranges[category];
            if (local.count)
                require(local.start >= global.start && local.start + local.count <= global.start + global.count,
                        "Collision joint line range is outside its source category");
        }
        joints.push_back(joint);
    }
    // mpJointFromLine identifies an owner by the first endpoint's membership in
    // a MapJoint vertex range. mpIsland consumers dereference that joint index.
    // Preserve original first-match behavior if ranges overlap, but reject a
    // line for which the source would return the unsafe missing-joint sentinel.
    for (const auto& line : lines)
        require(std::any_of(joints.begin(), joints.end(), [&](const auto& joint) {
            return line.v0 >= joint.vertices.start && line.v0 < joint.vertices.start + joint.vertices.count;
        }), "Collision line has no source joint vertex-range owner");
}

std::vector<DatCollisionBinding> read_dat_collision_bindings(
    const DatArchive& archive, const DatStage& stage, const DatCollision& collision)
{
    std::vector<DatCollisionBinding> result;
    for (const auto& entry : stage.entries) {
        const auto& table = entry.collision_bindings;
        require(table.count <= DatCollision::max_archive_bindings && result.size() + table.count <= DatCollision::max_archive_bindings,
                "Archive collision bindings exceed the decoder record budget");
        if (!table.count) continue;
        require(table.data_offset.has_value(), "Collision binding array is null");
        region(archive, *table.data_offset, std::size_t(table.count) * 6, 2);
        for (std::uint32_t i = 0; i < table.count; ++i) {
            const auto d = *table.data_offset + i * 6;
            DatCollisionBinding binding{d, entry.index, signed16(archive, d), signed16(archive, d + 2), signed16(archive, d + 4)};
            require(binding.collision_joint >= 0 && std::size_t(binding.collision_joint) < collision.joints.size(),
                    "Collision binding joint index is outside MapCollData");
            require(binding.render_joint >= 0, "Collision binding render-joint index is negative");
            result.push_back(binding);
        }
    }
    return result;
}
} // namespace melee_web
