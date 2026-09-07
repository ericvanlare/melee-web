#pragma once

#include "dat_archive.hpp"
#include "hsd_pobj_bridge.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace melee_web {
struct RigidMesh {
    std::vector<MeleeWebPObjAttribute> attributes;
    const void* display = nullptr;
    uint32_t display_bytes = 0;
    uint16_t flags = 0;
    std::array<uint8_t, 4> diffuse{};
};

// A deliberately restricted inspection path: one identity joint, opaque diffuse
// materials and rigid POS/NRM meshes. Unsupported features are rejected whole.
class RigidModel {
public:
    RigidModel(std::shared_ptr<const DatArchive> archive, const std::string& symbol);
    std::shared_ptr<const DatArchive> archive;
    std::vector<RigidMesh> meshes;
    std::array<float, 3> minimum, maximum;
    uint32_t draw_packets = 0;
    uint32_t submitted_vertices = 0;
    std::string symbol;
};
}
