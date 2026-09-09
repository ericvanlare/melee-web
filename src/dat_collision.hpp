#pragma once

#include "dat_archive.hpp"
#include <array>
#include <string>
#include <vector>

namespace melee_web {
class DatStage;

// Decode only GroundParam.y, the first float of the named grGroundParam root,
// consumed by original Ground_801C0498. No default scale or stage identity is
// inferred, and the remaining GroundParam services stay undecoded.
[[nodiscard]] float read_dat_stage_scale(const DatArchive&);

enum class DatCollisionCategory : std::size_t { Floor, Ceiling, RightWall, LeftWall, Dynamic };
struct DatCollisionRange {
    std::int16_t start = 0, count = 0;
};
// Same order as MapCollData/MapJoint: floor, ceiling, right wall, left wall, dynamic.
using DatCollisionRanges = std::array<DatCollisionRange, 5>;
struct DatCollisionVertex { float x, y; };
struct DatCollisionLine {
    std::uint32_t descriptor_offset;
    std::uint16_t v0, v1;
    std::int16_t prev0, next0, prev1, next1; // -1 is the source missing-line sentinel.
    std::uint16_t hi_flags, lo_flags; // Keep both words; kind and terrain are not interchangeable.
    [[nodiscard]] std::uint16_t kind() const noexcept { return hi_flags & 0xf; }
    [[nodiscard]] std::uint8_t terrain() const noexcept { return std::uint8_t(lo_flags & 0xff); }
};
struct DatCollisionJoint {
    std::uint32_t descriptor_offset;
    DatCollisionRanges line_ranges;
    float left, bottom, right, top;
    DatCollisionRange vertices;
};

// Owns a typed copy of on-disc MapCollData. Original mpLibLoad needs mutable host
// arrays: mpPruneEmptyLines changes adjacency/flags. This decoder never prunes,
// scales, moves vertices, computes normals, or performs collision/physics.
// Original stage scale, joint binding/update, callbacks and ECB remain required
// runtime services even when the archive's dynamic-line range is empty.
class DatCollision {
public:
    static constexpr std::uint32_t max_vertices = 2048, max_lines = 1536, max_joints = 256;
    static constexpr std::uint32_t max_archive_bindings = 4096; // Decoder budget; bindings may repeat joints.
    explicit DatCollision(const DatArchive&, const std::string& symbol = "coll_data");
    std::string symbol;
    std::uint32_t root_offset = 0, vertex_offset = 0, line_offset = 0, joint_offset = 0;
    std::uint32_t source_reserved_2c = 0; // Optional trailing source word; retained, not interpreted.
    std::vector<DatCollisionVertex> vertices;
    std::vector<DatCollisionLine> lines;
    DatCollisionRanges line_ranges;
    std::vector<DatCollisionJoint> joints;
};

struct DatCollisionBinding {
    std::uint32_t descriptor_offset, stage_entry;
    std::int16_t collision_joint, source_stage_entry, render_joint;
};
// Reads only archive-owned map-entry GrJoint arrays. Ground_801C2ED0 uses the
// owning entry (not GrJoint.y) for those records. source_stage_entry preserves y
// unchanged. render_joint is an original index, not a compacted renderer index;
// resolving it requires the original unpruned tree and stage wrapper semantics.
// Separate StageData.joints C initializers must also be supplied by the caller;
// an empty result does not mean the stage has no collision-to-model bindings.
[[nodiscard]] std::vector<DatCollisionBinding> read_dat_collision_bindings(
    const DatArchive&, const DatStage&, const DatCollision&);

} // namespace melee_web
