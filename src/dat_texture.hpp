#pragma once

#include "dat_archive.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace melee_web {

// Effective operations for the initial single diffuse texture path. Original
// BLEND is lowered only at exact endpoints: 0 -> pass, 1 -> replace. Intermediate
// blend constants and custom TEV expressions require a fuller material path.
enum class TextureOperation { pass, modulate, replace };

struct DatTextureImage {
    std::uint32_t descriptor_offset = 0, data_offset = 0;
    std::uint16_t width = 0, height = 0;
    std::uint32_t format = 0, mip_levels = 0;
    bool mipmap = false;
    float min_lod = 0, max_lod = 0;
    std::span<const std::uint8_t> bytes;
};

struct DatTexturePalette {
    std::uint32_t descriptor_offset = 0, data_offset = 0;
    std::uint32_t format = 0, source_name = 0;
    std::uint16_t entries = 0;
    std::span<const std::uint8_t> bytes;
};

struct DatTextureSampler {
    std::uint32_t wrap_s = 0, wrap_t = 0;
    std::uint32_t min_filter = 0, mag_filter = 0, anisotropy = 0;
    float lod_bias = 0;
    bool bias_clamp = false, edge_lod = false;
};

struct DatTexture {
    std::uint32_t descriptor_offset = 0, id = 0, source = 0, source_flags = 0;
    float blending = 0;
    std::array<float, 3> rotation{}, scale{}, translation{};
    std::uint8_t repeat_s = 0, repeat_t = 0;
    TextureOperation color_operation = TextureOperation::pass;
    TextureOperation alpha_operation = TextureOperation::pass;
    DatTextureImage image;
    std::optional<DatTexturePalette> palette;
    DatTextureSampler sampler;
};

// Reads checked descriptors without converting the original GameCube tiles or
// palette words. Returned byte spans require the archive to remain alive.
// Accepts one identity-transformed UV/TEX0 diffuse TObj, ordinary GX formats,
// validated mip chains and optional CI palettes. Rejects custom classes/TEV,
// multiple TObjs, unsupported coordinate/lightmap/material operations and
// texture matrices. Referenced-region bounds are conservative, not inferred
// allocation sizes; aliases into a payload can therefore be rejected.
[[nodiscard]] DatTexture read_dat_texture(const DatArchive& archive,
                                          std::uint32_t tobj_offset);

} // namespace melee_web
