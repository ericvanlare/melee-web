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

DatTexturePalette palette_descriptor(const DatArchive& archive, std::uint32_t offset,
                           std::uint32_t image_format)
{
    if (!indexed(image_format)) reject("Palette requires an indexed image format");
    descriptor(archive, offset, 16);
    DatTexturePalette result;
    result.descriptor_offset = offset;
    result.format = archive.be32(offset + 4);
    result.source_name = archive.be32(offset + 8);
    result.entries = archive.be16(offset + 12);
    const std::uint32_t capacity = image_format == 8 ? 16 : image_format == 9 ? 256 : 16384;
    if (result.format > 2) reject("Unsupported TLUT palette format");
    if (!result.entries || result.entries > capacity) reject("TLUT entry count is invalid for its image format");
    result.data_offset = required(archive, offset, std::size_t{result.entries} * 2);
    if (result.data_offset % 32) reject("GameCube palette data is not 32-byte aligned");
    region(archive, result.data_offset, std::size_t{result.entries} * 2);
    result.bytes = archive.range(result.data_offset, std::size_t{result.entries} * 2);

    return result;
}

std::uint32_t maximum_palette_index(const DatTextureImage& image)
{
    if (!indexed(image.format) || !image.width || !image.height ||
        image.width > max_dimension || image.height > max_dimension ||
        !image.mip_levels || image.mip_levels > 11)
        reject("Invalid indexed image dimensions or format");
    std::uint32_t maximum = 0;
    // Check referenced indices, not padding texels outside the dimensions.
    // All tiled source bytes are still preserved for Aurora's upload path.
    const auto block = tile(image.format);
    std::size_t level_start = 0;
    for (std::uint32_t level = 0; level < image.mip_levels; ++level) {
        const auto width = std::max(std::uint32_t{image.width} >> level, 1U);
        const auto height = std::max(std::uint32_t{image.height} >> level, 1U);
        const auto bytes = level_bytes(width, height, block);
        if (level_start > image.bytes.size() || bytes > image.bytes.size() - level_start)
            reject("Indexed image tile bytes are truncated");
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
                maximum = std::max(maximum, index);
            }
        }
        level_start += level_bytes(width, height, block);
    }
    return maximum;
}

DatTexturePalette palette(const DatArchive& archive, std::uint32_t offset,
                           const DatTextureImage& image)
{
    auto result = palette_descriptor(archive, offset, image.format);
    const auto maximum = maximum_palette_index(image);
    if (maximum >= result.entries)
        throw DatError("Image references an index outside its TLUT palette: image=" +
                       std::to_string(image.descriptor_offset) + " palette=" +
                       std::to_string(offset) + " maximum=" + std::to_string(maximum) +
                       " entries=" + std::to_string(result.entries));
    return result;
}

DatTexture read_texture(const DatArchive& archive, std::uint32_t offset, bool native_descriptors)
{
    descriptor(archive, offset, 92);
    absent(archive, offset, "Custom texture classes are unsupported");
    DatTexture result;
    if (const auto tev = archive.pointer(offset + 88, 32)) {
        descriptor(archive, *tev, 32);
        // HSD_TObjMakeTExp only enters custom expressions when active flags are
        // set. Preserve this descriptor's identity; its inactive byte fields
        // are intentionally not interpreted as GX commands.
        const auto active=archive.be32(*tev+28);
        if(active && !native_descriptors) reject("Active custom texture TEV expressions are unsupported");
        if(!active) result.inactive_tev_descriptor_offset=*tev;
        else {
            const auto fields=archive.range(*tev,28);
            if(active&~0xc0000fffU) reject("Native texture TEV active flags are unsupported");
            for(unsigned channel=0;channel<2;++channel) if(active&(1u<<(30+channel))) {
                if(fields[channel]>1||fields[2+channel]>2||fields[4+channel]>3||fields[6+channel]>1)
                    reject("Native texture TEV operation/bias/scale/clamp is unsupported");
                for(unsigned i=0;i<4;++i) {
                    const auto v=fields[8+4*channel+i];
                    const bool valid=channel ? (v==4||v==7||(v>=0x40&&v<=0x45)) :
                        (v==8||v==9||v==12||v==13||v==15||(v>=0x80&&v<=0x88));
                    if(!valid) reject("Native texture TEV input is unsupported by original expression compiler");
                }
            }
            DatTextureTev value;std::copy(fields.begin(),fields.end(),value.fields.begin());value.active=active;
            result.native_tev=value;
        }
    }
    result.descriptor_offset = offset;
    result.id = archive.be32(offset + 8);
    result.source = archive.be32(offset + 12);
    if ((result.id > 7 && result.id != 255) || result.source > 20)
        reject("Invalid source texture map or coordinate source");
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        result.rotation[axis] = archive.f32(offset + 16 + axis * 4);
        result.scale[axis] = archive.f32(offset + 28 + axis * 4);
        result.translation[axis] = archive.f32(offset + 40 + axis * 4);
        for (const auto value : {result.rotation[axis], result.scale[axis], result.translation[axis]})
            if (!std::isfinite(value) || std::abs(value) > 1000000.F)
                reject("Texture transform is nonfinite or exceeds the supported magnitude");
    }
    auto& sampler = result.sampler;
    sampler.wrap_s = archive.be32(offset + 52);
    sampler.wrap_t = archive.be32(offset + 56);
    if (sampler.wrap_s > 2 || sampler.wrap_t > 2) reject("Invalid texture wrap mode");
    result.repeat_s = archive.range(offset + 60, 1)[0];
    result.repeat_t = archive.range(offset + 61, 1)[0];
    if (!result.repeat_s || !result.repeat_t) reject("Texture repeat values must be nonzero");
    result.source_flags = archive.be32(offset + 64);
    // TEX_BUMP is consumed by the original TObj setup (including its second
    // BUMP texgen and volatile emboss TEV stages).  Keep it behind the native
    // descriptor policy: the viewer material path has no equivalent setup.
    constexpr std::uint32_t tex_bump = 1U << 24;
    const std::uint32_t allowed_flags = 0x80000000U | 0x00ff0000U | 0xf0U | 0xfU |
        (native_descriptors ? tex_bump : 0U);
    const auto coordinates = result.source_flags & 15U;
    // The PlCo EntryStart accessory uses a native bump-only TObj. HSD accepts
    // this descriptor without the ordinary color/alpha operation nibble; keep
    // that authored flag set intact while retaining the operation requirement
    // for every non-bump texture.
    const bool native_bump_only = native_descriptors &&
        (result.source_flags & tex_bump) && !(result.source_flags & 0xf0U);
    if ((result.source_flags & ~allowed_flags) ||
        (!(result.source_flags & 0xf0U) && !native_bump_only) || coordinates > 1)
        reject("Unsupported texture coordinate, lightmap or behavior flags");
    if (coordinates == 0 && (result.source < 4 || result.source > 11))
        reject("UV texture requires a TEX0 through TEX7 vertex source");
    if (((result.source_flags >> 16) & 15U) > 8 || ((result.source_flags >> 20) & 15U) > 7)
        reject("Unknown HSD texture color or alpha operation");
    result.blending = archive.f32(offset + 68);
    if (!std::isfinite(result.blending) || result.blending < 0 || result.blending > 1)
        reject("Texture blending value must be finite and between zero and one");
    sampler.mag_filter = archive.be32(offset + 72);
    if (sampler.mag_filter > 1) reject("Invalid texture magnification filter");
    result.image = image(archive, required(archive, offset + 76, 24));
    const auto tlut = archive.pointer(offset + 80, 16);
    if (indexed(result.image.format)) {
        if (!tlut) reject("Indexed texture requires a TLUT palette");
        result.palette = palette(archive, *tlut, result.image);
    } else if (tlut) reject("A palette on a nonindexed texture is unsupported");

    // Preserve the source default; the original HSD material engine applies
    // CI and non-mipmap filter adjustments when it sets up this texture.
    sampler.min_filter = 5; // GX_LIN_MIP_LIN.
    if (const auto lod = archive.pointer(offset + 84, 16)) {
        result.lod_descriptor_offset = *lod;
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
    return result;
}

} // namespace

DatTexturePalette read_dat_texture_palette_descriptor(const DatArchive& archive,
    std::uint32_t offset, std::uint32_t image_format)
{
    return palette_descriptor(archive, offset, image_format);
}
std::uint32_t dat_texture_max_palette_index(const DatTextureImage& image)
{
    return maximum_palette_index(image);
}

DatTextureImage read_dat_texture_image(const DatArchive& archive, std::uint32_t offset)
{
    return image(archive, offset);
}
DatTexturePalette read_dat_texture_palette(const DatArchive& archive, std::uint32_t offset,
                                           const DatTextureImage& source_image)
{
    return palette(archive, offset, source_image);
}

std::vector<DatTexture> read_dat_texture_chain(const DatArchive& archive,
                                               std::uint32_t first_offset, bool native_descriptors)
{
    std::vector<DatTexture> textures;
    std::optional<std::uint32_t> offset = first_offset;
    while (offset) {
        if (textures.size() >= 8) reject("Texture chain exceeds the eight-texture resource limit");
        if (std::any_of(textures.begin(), textures.end(), [&](const auto& texture) {
                return texture.descriptor_offset == *offset;
            })) reject("Texture descriptor chain is cyclic");
        textures.push_back(read_texture(archive, *offset, native_descriptors));
        offset = archive.pointer(*offset + 4, 92);
    }
    return textures;
}

} // namespace melee_web
