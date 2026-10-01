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
// All remains strict: every referenced mesh must be supported. Opaque selects
// the source DObjLoad OPA class and reports omitted TEXEDGE/XLU occurrences.
enum class ModelRenderPass { All, Opaque };
struct RigidJoint {
    static constexpr uint32_t no_parent = UINT32_MAX;
    uint32_t descriptor_offset = 0, parent = no_parent, child = no_parent,
             next = no_parent, flags = 0,
             instance_target_source_offset = UINT32_MAX;
    std::array<float, 3> rotation{}, scale{}, translation{};
    std::optional<std::array<float, 12>> inverse_bind; // Row-major original 3x4 matrix.
};

struct RigidEnvelope {
    uint32_t descriptor_offset = 0;
    std::vector<MeleeWebSkinInfluence> influences; // Joint indices in model preorder.
};

struct RigidShape {
    uint16_t flags = 0, shape_count = 0;
    uint32_t vertex_index_count = 0, normal_index_count = 0;
    uint32_t vertex_attribute = UINT32_MAX, normal_attribute = UINT32_MAX;
    std::vector<const uint8_t*> vertex_index_lists;
    std::vector<const uint8_t*> normal_index_lists;
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
    std::optional<RigidShape> shape;
    /* POBJ_SKIN source joint identity; resolved by the native descriptor
     * owner after the complete joint preorder is known. */
    std::optional<uint32_t> shared_joint_offset;
    std::vector<RigidEnvelope> envelopes; // Original ordered GX matrix palette.
    uint16_t palette_used_mask = 0;
    // Local submitted-position bounds for each referenced PN matrix slot.
    // Unused entries are zero; the renderer transforms only the used entries.
    std::array<std::array<float, 3>, MELEE_WEB_POBJ_MAX_PALETTE> palette_minimum{}, palette_maximum{};
};

// CPU-only decoded static graph. Archive-backed geometry/texture spans remain
// immutable; GPU resources and source HSD transform state belong to the renderer.
// Envelope weights and inverse binds stay unchanged for original HSD skinning.
// Shape animation and unsupported material modes reject.
class RigidModel {
public:
    RigidModel(std::shared_ptr<const DatArchive> archive, const std::string& symbol);
    RigidModel(std::shared_ptr<const DatArchive> archive, uint32_t joint_offset,
               const std::string& label, ModelRenderPass pass = ModelRenderPass::All,
               DatMaterialPolicy materials = DatMaterialPolicy::ViewerOpaque);
    std::shared_ptr<const DatArchive> archive;
    std::vector<RigidJoint> joints; // Parent precedes child; siblings keep their actual parent.
    std::vector<RigidMesh> meshes;
    std::array<float, 3> minimum, maximum;
    uint32_t draw_packets = 0;
    uint32_t dobj_count = 0; // Original occurrence space, including omitted passes.
    uint32_t submitted_vertices = 0;
    uint32_t root_offset = 0;
    ModelRenderPass render_pass = ModelRenderPass::All;
    // Only Opaque prunes joints; owners, envelope bones and their ancestors stay.
    uint32_t omitted_dobjs = 0, omitted_joints = 0;
    uint32_t omitted_translucent_meshes = 0, omitted_texture_edge_meshes = 0;
    [[nodiscard]] uint32_t omitted_meshes() const noexcept {
        return omitted_translucent_meshes + omitted_texture_edge_meshes;
    }
    std::string symbol;
};
}
