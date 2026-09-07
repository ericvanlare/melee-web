#pragma once

#include "dat_texture.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace melee_web {

// CPU metadata hydrated into the original HSD material engine by the renderer.
// The model shares const instances by descriptor offset. Image/palette spans
// remain owned by the model's DatArchive; this descriptor contains no GPU state.
struct DatMaterial {
    std::uint32_t descriptor_offset = 0, material_offset = 0, render_mode = 0;
    std::array<std::uint8_t, 4> ambient{}, diffuse{}, specular{};
    float alpha = 0, shininess = 0;
    std::vector<DatTexture> textures;
};

// Current original-HSD bridge subset: opaque constant/diffuse/specular materials
// and ordinary texture chains. Vertex color, custom class/render/PE state,
// translucency, toon/shadow and special depth flags are rejected explicitly.
[[nodiscard]] DatMaterial read_dat_material(const DatArchive& archive,
                                            std::uint32_t material_offset);

} // namespace melee_web
