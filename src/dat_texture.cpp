#include "dat_texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace melee_web {
namespace {

constexpr std::uint32_t max_dimension = 1024;
constexpr std::size_t max_texture_bytes = 8U * 1024U * 1024U;

[[noreturn]] void reject(const char* message) { throw DatError(message); }

void region(const DatArchive& archive, std::uint32_t offset, std::size_t length)
{
    (void) archive.range(offset, length);
    if (length > archive.next_target_offset(offset) - offset)
        reject("Texture data crosses a referenced region boundary");
}

void descriptor(const DatArchive& archive, std::uint32_t offset, std::size_t length)
{
    if (offset % 4) reject("Texture descriptor is not four-byte aligned");
    region(archive, offset, length);
}

std::uint32_t required(const DatArchive& archive, std::uint32_t slot,
                       std::size_t length)
{
    const auto pointer = archive.pointer(slot, length);
    if (!pointer) reject("Required texture pointer is null");
    return *pointer;
}

void absent(const DatArchive& archive, std::uint32_t slot, const char* message)
{
    if (archive.pointer(slot)) reject(message);
}

struct Tile {
    std::uint32_t width, height, bytes;
};

Tile tile(std::uint32_t format)
{
    switch (format) {
    case 0: case 8: case 14: return {8, 8, 32}; // I4, C4, CMPR.
    case 1: case 2: case 9: return {8, 4, 32}; // I8, IA4, C8.
    case 3: case 4: case 5: case 10: return {4, 4, 32}; // 16-bit formats.
    case 6: return {4, 4, 64}; // RGBA8 has separate AR and GB planes per tile.
    default: reject("Unsupported GameCube image format");
    }
}

std::size_t level_bytes(std::uint32_t width, std::uint32_t height, Tile block)
{
    return std::size_t{(width + block.width - 1) / block.width} *
           ((height + block.height - 1) / block.height) * block.bytes;
}

bool indexed(std::uint32_t format) { return format == 8 || format == 9 || format == 10; }

DatTextureImage image(const DatArchive& archive, std::uint32_t offset)
{
    descriptor(archive, offset, 24);
    DatTextureImage result;
    result.descriptor_offset = offset;
    result.width = archive.be16(offset + 4);
    result.height = archive.be16(offset + 6);
    result.format = archive.be32(offset + 8);
    const auto block = tile(result.format);
    if (!result.width || !result.height || result.width > max_dimension || result.height > max_dimension)
        reject("Texture dimensions must be between 1 and 1024");
    const auto mipmap = archive.be32(offset + 12);
    if (mipmap > 1) reject("Invalid texture mipmap flag");
    result.mipmap = mipmap != 0;
    result.min_lod = archive.f32(offset + 16);
    result.max_lod = archive.f32(offset + 20);
    if (!std::isfinite(result.min_lod) || !std::isfinite(result.max_lod) ||
        result.min_lod < 0 || result.max_lod > 10 || result.min_lod > result.max_lod)
        reject("Invalid image LOD range");
    if (!result.mipmap && (result.min_lod != 0 || result.max_lod != 0))
        reject("Non-mipmapped images must use LOD zero in this reader");
    std::uint32_t available_levels = 1;
    for (auto size = std::max(result.width, result.height); size > 1; size /= 2)
        ++available_levels;
    // Aurora quantizes maxLOD to 1/16 then reads floor(maxLOD)+1 tiled levels.
    // The floor is unchanged by that quantization for our finite positive range.
    result.mip_levels = result.mipmap ? static_cast<std::uint32_t>(result.max_lod) + 1 : 1;
    if (result.mip_levels > available_levels)
        reject("Image LOD range exceeds the available dimension chain");
    std::size_t size = 0;
    for (std::uint32_t level = 0; level < result.mip_levels; ++level) {
        const auto width = std::max(std::uint32_t{result.width} >> level, 1U);
        const auto height = std::max(std::uint32_t{result.height} >> level, 1U);
        const auto bytes = level_bytes(width, height, block);
        if (bytes > max_texture_bytes - size) reject("Texture byte budget exceeded");
        size += bytes;
    }
    result.data_offset = required(archive, offset, size);
    if (result.data_offset % 32) reject("GameCube image data is not 32-byte aligned");
    region(archive, result.data_offset, size);
    result.bytes = archive.range(result.data_offset, size);
    return result;
}

DatTexturePalette palette(const DatArchive& archive, std::uint32_t offset,
                           const DatTextureImage& image)
{
    descriptor(archive, offset, 16);
    DatTexturePalette result;
    result.descriptor_offset = offset;
    result.format = archive.be32(offset + 4);
    result.source_name = archive.be32(offset + 8);
    result.entries = archive.be16(offset + 12);
    const std::uint32_t capacity = image.format == 8 ? 16 : image.format == 9 ? 256 : 16384;
    if (result.format > 2) reject("Unsupported TLUT palette format");
    if (!result.entries || result.entries > capacity) reject("TLUT entry count is invalid for its image format");
    result.data_offset = required(archive, offset, std::size_t{result.entries} * 2);
    if (result.data_offset % 32) reject("GameCube palette data is not 32-byte aligned");
    region(archive, result.data_offset, std::size_t{result.entries} * 2);
    result.bytes = archive.range(result.data_offset, std::size_t{result.entries} * 2);

    // Check referenced indices, not padding texels outside the dimensions.
    // All tiled source bytes are still preserved for Aurora's upload path.
    const auto block = tile(image.format);
    std::size_t level_start = 0;
    for (std::uint32_t level = 0; level < image.mip_levels; ++level) {
        const auto width = std::max(std::uint32_t{image.width} >> level, 1U);
        const auto height = std::max(std::uint32_t{image.height} >> level, 1U);
        const auto columns = (width + block.width - 1) / block.width;
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto tile_number = (y / block.height) * columns + x / block.width;
                const auto pixel = (y % block.height) * block.width + x % block.width;
                const auto start = level_start + std::size_t{tile_number} * block.bytes;
                std::uint32_t index;
                if (image.format == 8) {
                    const auto packed = image.bytes[start + pixel / 2];
                    index = pixel % 2 ? packed & 15U : packed >> 4;
                } else if (image.format == 9) {
                    index = image.bytes[start + pixel];
                } else {
                    index = ((std::uint32_t{image.bytes[start + pixel * 2]} << 8) |
                             image.bytes[start + pixel * 2 + 1]) & 0x3fffU;
                }
                if (index >= result.entries) reject("Image references an index outside its TLUT palette");
            }
        }
        level_start += level_bytes(width, height, block);
    }
    return result;
}

TextureOperation endpoint_blend(float blending)
{
    if (blending == 0) return TextureOperation::pass;
    if (blending == 1) return TextureOperation::replace;
    reject("Intermediate texture blend constants are unsupported");
}

TextureOperation color_operation(std::uint32_t operation, float blending)
{
    switch (operation) {
    case 0: case 6: return TextureOperation::pass;
    case 3: return endpoint_blend(blending);
    case 4: return TextureOperation::modulate;
    case 5: return TextureOperation::replace;
    default: reject("Unsupported texture color operation");
    }
}

TextureOperation alpha_operation(std::uint32_t operation, float blending)
{
    switch (operation) {
    case 0: case 5: return TextureOperation::pass;
    case 2: return endpoint_blend(blending);
    case 3: return TextureOperation::modulate;
    case 4: return TextureOperation::replace;
    default: reject("Unsupported texture alpha operation");
    }
}

} // namespace

DatTexture read_dat_texture(const DatArchive& archive, std::uint32_t offset)
{
    descriptor(archive, offset, 92);
    absent(archive, offset, "Custom texture classes are unsupported");
    absent(archive, offset + 4, "Multiple texture objects are unsupported");
    absent(archive, offset + 88, "Custom texture TEV expressions are unsupported");
    DatTexture result;
    result.descriptor_offset = offset;
    result.id = archive.be32(offset + 8);
    result.source = archive.be32(offset + 12);
    if (result.id != 0 || result.source != 4) reject("Only texture map zero with TEX0 UV coordinates is supported");
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        result.rotation[axis] = archive.f32(offset + 16 + axis * 4);
        result.scale[axis] = archive.f32(offset + 28 + axis * 4);
        result.translation[axis] = archive.f32(offset + 40 + axis * 4);
        if (result.rotation[axis] != 0 || result.scale[axis] != 1 || result.translation[axis] != 0)
            reject("Nonidentity texture transforms are unsupported");
    }
    auto& sampler = result.sampler;
    sampler.wrap_s = archive.be32(offset + 52);
    sampler.wrap_t = archive.be32(offset + 56);
    if (sampler.wrap_s > 2 || sampler.wrap_t > 2) reject("Invalid texture wrap mode");
    if (sampler.wrap_t == 2) reject("Mirror-T requires the HSD texture-matrix translation path");
    result.repeat_s = archive.range(offset + 60, 1)[0];
    result.repeat_t = archive.range(offset + 61, 1)[0];
    if (result.repeat_s != 1 || result.repeat_t != 1) reject("Repeated texture matrices are unsupported");
    result.source_flags = archive.be32(offset + 64);
    constexpr std::uint32_t allowed_flags = 0x80000000U | 0x00ff0000U | 0x10U;
    if ((result.source_flags & ~allowed_flags) || !(result.source_flags & 0x10U))
        reject("Only UV diffuse texture flags are supported");
    result.blending = archive.f32(offset + 68);
    if (!std::isfinite(result.blending) || result.blending < 0 || result.blending > 1)
        reject("Texture blending value must be finite and between zero and one");
    result.color_operation = color_operation((result.source_flags >> 16) & 15U, result.blending);
    result.alpha_operation = alpha_operation((result.source_flags >> 20) & 15U, result.blending);
    sampler.mag_filter = archive.be32(offset + 72);
    if (sampler.mag_filter > 1) reject("Invalid texture magnification filter");
    result.image = image(archive, required(archive, offset + 76, 24));
    const auto tlut = archive.pointer(offset + 80, 16);
    if (indexed(result.image.format)) {
        if (!tlut) reject("Indexed texture requires a TLUT palette");
        result.palette = palette(archive, *tlut, result.image);
    } else if (tlut) reject("A palette on a nonindexed texture is unsupported");

    // Preserve HSD's default and its CI/non-mipmap filter adjustments.
    sampler.min_filter = 5; // GX_LIN_MIP_LIN.
    if (const auto lod = archive.pointer(offset + 84, 16)) {
        descriptor(archive, *lod, 16);
        sampler.min_filter = archive.be32(*lod);
        sampler.lod_bias = archive.f32(*lod + 4);
        const auto clamp = archive.range(*lod + 8, 1)[0];
        const auto edge = archive.range(*lod + 9, 1)[0];
        sampler.anisotropy = archive.be32(*lod + 12);
        if (sampler.min_filter > 5 || clamp > 1 || edge > 1 || sampler.anisotropy > 2)
            reject("Invalid texture LOD descriptor");
        if (!std::isfinite(sampler.lod_bias) || sampler.lod_bias < -4 || sampler.lod_bias > 3.99F)
            reject("Texture LOD bias is outside the supported GX range");
        sampler.bias_clamp = clamp != 0;
        sampler.edge_lod = edge != 0;
    }
    if (result.palette && sampler.min_filter == 5) sampler.min_filter = 3;
    if (!result.image.mipmap) sampler.min_filter &= 1U;
    return result;
}

} // namespace melee_web
