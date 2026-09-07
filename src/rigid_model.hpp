#pragma once

#include "dat_archive.hpp"
#include "dat_material.hpp"
#include "hsd_pobj_bridge.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace melee_web {
struct RigidJoint {
    static constexpr uint32_t no_parent = UINT32_MAX;
    uint32_t descriptor_offset = 0, parent = no_parent, flags = 0;
    std::array<float, 3> rotation{}, scale{}, translation{};
    std::optional<std::array<float, 12>> inverse_bind; // Row-major original 3x4 matrix.
};

struct RigidEnvelope {
    uint32_t descriptor_offset = 0;
    std::vector<MeleeWebSkinInfluence> influences; // Joint indices in model preorder.
};

struct RigidMesh {
    uint32_t descriptor_offset = 0;
    uint32_t joint_index = 0;
    uint32_t dobj_index = 0; // Preorder DObj occurrence, shared by its PObj chain.
    std::array<float, 3> minimum{}, maximum{};
    std::shared_ptr<const DatMaterial> material;
    std::vector<MeleeWebPObjAttribute> attributes;
    const void* display = nullptr;
    uint32_t display_bytes = 0;
    uint16_t flags = 0;
    std::vector<RigidEnvelope> envelopes; // Original ordered GX matrix palette.
    uint16_t palette_used_mask = 0;
    // Local submitted-position bounds for each referenced PN matrix slot.
    // Unused entries are zero; the renderer transforms only the used entries.
    std::array<std::array<float, 3>, MELEE_WEB_POBJ_MAX_PALETTE> palette_minimum{}, palette_maximum{};
};

// CPU-only decoded static graph. Archive-backed geometry/texture spans remain
// immutable; GPU resources and source HSD transform state belong to the renderer.
// Envelope weights and inverse binds stay unchanged for original HSD skinning.
// Shape animation, shared-joint skinning and unsupported material modes reject.
class RigidModel {
public:
    RigidModel(std::shared_ptr<const DatArchive> archive, const std::string& symbol);
    std::shared_ptr<const DatArchive> archive;
    std::vector<RigidJoint> joints; // Parent precedes child; siblings keep their actual parent.
    std::vector<RigidMesh> meshes;
    std::array<float, 3> minimum, maximum;
    uint32_t draw_packets = 0;
    uint32_t dobj_count = 0;
    uint32_t submitted_vertices = 0;
    std::string symbol;
};
}
