#pragma once

#include "dat_archive.hpp"
#include "dat_material.hpp"
#include "hsd_pobj_bridge.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace melee_web {
struct RigidJoint {
    static constexpr uint32_t no_parent = UINT32_MAX;
    uint32_t descriptor_offset = 0, parent = no_parent, flags = 0;
    std::array<float, 3> rotation{}, scale{}, translation{};
};

struct RigidMesh {
    uint32_t joint_index = 0;
    std::array<float, 3> minimum{}, maximum{};
    std::shared_ptr<const DatMaterial> material;
    std::vector<MeleeWebPObjAttribute> attributes;
    const void* display = nullptr;
    uint32_t display_bytes = 0;
    uint16_t flags = 0;
};

// CPU-only decoded static graph. Archive-backed geometry/texture spans remain
// immutable; GPU resources and source HSD transform state belong to the renderer.
// Unsupported material, skinning and animation features are rejected whole.
class RigidModel {
public:
    RigidModel(std::shared_ptr<const DatArchive> archive, const std::string& symbol);
    std::shared_ptr<const DatArchive> archive;
    std::vector<RigidJoint> joints; // Parent precedes child; siblings keep their actual parent.
    std::vector<RigidMesh> meshes;
    std::array<float, 3> minimum, maximum;
    uint32_t draw_packets = 0;
    uint32_t submitted_vertices = 0;
    std::string symbol;
};
}
