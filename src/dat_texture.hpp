#pragma once

#include "dat_archive.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace melee_web {

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
    // Original source inputs. HSD applies its CI/non-mipmapped adjustments.
    std::uint32_t min_filter = 5, mag_filter = 0, anisotropy = 0;
    float lod_bias = 0;
    bool bias_clamp = false, edge_lod = false;
};

struct DatTexture {
    std::uint32_t descriptor_offset = 0, id = 0, source = 0, source_flags = 0;
    float blending = 0;
    std::array<float, 3> rotation{}, scale{}, translation{};
    std::uint8_t repeat_s = 0, repeat_t = 0;
    std::optional<std::uint32_t> lod_descriptor_offset;
    // Active-zero descriptors do not affect original HSD expression generation.
    std::optional<std::uint32_t> inactive_tev_descriptor_offset;
    DatTextureImage image;
    std::optional<DatTexturePalette> palette;
    DatTextureSampler sampler;
};

// Reads checked descriptors without converting the original GameCube tiles or
// palette words. Returned byte spans require the archive to remain alive.
// Supports up to eight ordinary UV/reflection TObjs with full finite SRT,
// repeat/wrap modes, standard HSD color/alpha operations and diffuse, specular,
// ambient or extension lightmaps. Raw flags/blend values are preserved for the
// original HSD expression compiler; no effective-operation approximation is made.
// Custom classes/active TEV, toon, bump, shadow and highlight coordinate modes remain
// unsupported. Referenced-region bounds are conservative, not allocation sizes.
// Source IDs are preserved, but HSD assigns actual texture resources at runtime.
[[nodiscard]] std::vector<DatTexture> read_dat_texture_chain(const DatArchive& archive,
                                                           std::uint32_t first_offset);

} // namespace melee_web
