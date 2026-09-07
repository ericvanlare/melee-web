#include "dat_material.hpp"

#include <algorithm>
#include <cmath>

namespace melee_web {
namespace {

void descriptor(const DatArchive& archive, std::uint32_t offset, std::size_t size)
{
    if (offset % 4) throw DatError("Material descriptor is not four-byte aligned");
    (void) archive.range(offset, size);
    if (size > archive.next_target_offset(offset) - offset)
        throw DatError("Material descriptor crosses a referenced region boundary");
}

void absent(const DatArchive& archive, std::uint32_t slot, const char* message)
{
    if (archive.pointer(slot)) throw DatError(message);
}

} // namespace

DatMaterialPass read_dat_material_pass(const DatArchive& archive, std::uint32_t offset)
{
    descriptor(archive, offset, 24);
    absent(archive, offset, "Custom material classes are unsupported");
    // DObjLoad maps these source flags to OPA, TEXEDGE and XLU respectively.
    switch (archive.be32(offset + 4) & 0x60000000U) {
    case 0: return DatMaterialPass::Opaque;
    case 0x40000000U: return DatMaterialPass::TextureEdge;
    case 0x60000000U: return DatMaterialPass::Translucent;
    default: throw DatError("Material has an invalid source blending flag combination");
    }
}

DatMaterial read_dat_material(const DatArchive& archive, std::uint32_t offset)
{
    descriptor(archive, offset, 24);
    absent(archive, offset, "Custom material classes are unsupported");
    absent(archive, offset + 16, "Custom material render descriptors are unsupported");
    absent(archive, offset + 20, "Custom material pixel-engine state is unsupported");
    DatMaterial material;
    material.descriptor_offset = offset;
    material.render_mode = archive.be32(offset + 4);
    // Original HSD receives these bits unchanged. No translucent
    // sorting, toon/shadow services or special depth modes exist in this target.
    constexpr std::uint32_t supported = 0xfffU;
    if (material.render_mode & ~supported)
        throw DatError("Material requires unsupported transparency, toon, shadow or depth behavior");
    const auto data = archive.pointer(offset + 12, 20);
    if (!data) throw DatError("Required material color descriptor is null");
    material.material_offset = *data;
    descriptor(archive, *data, 20);
    const auto ambient = archive.range(*data, 4);
    const auto diffuse = archive.range(*data + 4, 4);
    const auto specular = archive.range(*data + 8, 4);
    std::copy(ambient.begin(), ambient.end(), material.ambient.begin());
    std::copy(diffuse.begin(), diffuse.end(), material.diffuse.begin());
    std::copy(specular.begin(), specular.end(), material.specular.begin());
    material.alpha = archive.f32(*data + 12);
    material.shininess = archive.f32(*data + 16);
    if (material.alpha != 1.F) throw DatError("Only opaque material alpha is supported");
    // Original HSD_LObjSetup uses shininess/2 and 1-shininess/2 without a
    // 128 cap. Finite nonnegative input keeps both derived coefficients finite.
    if (!std::isfinite(material.shininess) || material.shininess < 0)
        throw DatError("Material shininess is nonfinite or outside the supported range");
    if (const auto first = archive.pointer(offset + 8, 92))
        material.textures = read_dat_texture_chain(archive, *first);
    return material;
}

} // namespace melee_web
