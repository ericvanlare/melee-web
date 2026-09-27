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

struct DatTextureTev { std::array<uint8_t,28> fields{}; uint32_t active=0; };
struct DatTexture {
    std::uint32_t descriptor_offset = 0, id = 0, source = 0, source_flags = 0;
    float blending = 0;
    std::array<float, 3> rotation{}, scale{}, translation{};
    std::uint8_t repeat_s = 0, repeat_t = 0;
    std::optional<std::uint32_t> lod_descriptor_offset;
    // Active-zero descriptors still have original HSD allocation/ownership.
    // The viewer records their identity; native_tev also retains their bytes.
    std::optional<std::uint32_t> inactive_tev_descriptor_offset;
    DatTextureImage image;
    std::optional<DatTexturePalette> palette;
    DatTextureSampler sampler;
    std::optional<DatTextureTev> native_tev;
};

// Reads checked descriptors without converting the original GameCube tiles or
// palette words. Returned byte spans require the archive to remain alive.
// Supports up to eight ordinary UV/reflection TObjs with full finite SRT,
// repeat/wrap modes, standard HSD color/alpha operations and diffuse, specular,
// ambient or extension lightmaps. Native descriptor reads also preserve the
// original TEX_BUMP flag for the HSD bump texgen/emboss path; the viewer policy
// continues to reject that source behavior. Raw flags/blend values are preserved
// for the original HSD expression compiler; no effective-operation approximation
// is made. Custom classes/active TEV, toon, shadow and highlight coordinate modes
// remain unsupported. Referenced-region bounds are conservative, not allocation
// sizes.
// Source IDs are preserved, but HSD assigns actual texture resources at runtime.
[[nodiscard]] DatTextureImage read_dat_texture_image(const DatArchive&, std::uint32_t);
[[nodiscard]] DatTexturePalette read_dat_texture_palette(const DatArchive&, std::uint32_t,
                                                        const DatTextureImage&);
// Descriptor-only palette read and exact maximum over visible indexed texels
// (including mips). Animated tables can validate each image once and compare
// that maximum against every palette, without rescanning the same tiled bytes.
[[nodiscard]] DatTexturePalette read_dat_texture_palette_descriptor(
    const DatArchive&, std::uint32_t, std::uint32_t image_format);
[[nodiscard]] std::uint32_t dat_texture_max_palette_index(const DatTextureImage&);
[[nodiscard]] std::vector<DatTexture> read_dat_texture_chain(const DatArchive& archive,
                                                           std::uint32_t first_offset,
                                                           bool native_descriptors = false);

} // namespace melee_web
