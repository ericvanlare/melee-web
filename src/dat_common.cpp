#include "dat_common.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace melee_web {
namespace {
MELEE_WEB_COMMON_ASSERT_LAYOUT(MeleeWebCommonScalars)
void region(const DatArchive& archive, std::uint32_t offset, std::size_t size)
{
    if (offset % 4) throw DatError("Common-data descriptor is unaligned");
    (void) archive.range(offset, size);
    if (size > archive.next_target_offset(offset) - offset)
        throw DatError("Common-data descriptor crosses a referenced region");
}
float finite_scalar(const DatArchive& archive, std::uint32_t offset)
{
    const float value = archive.f32(offset);
    if (!std::isfinite(value)) throw DatError("Common-data float is nonfinite");
    return value;
}
}
DatCommon::DatCommon(const DatArchive& archive)
{
    const auto& symbols = archive.public_symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(),
        [](const auto& symbol) { return symbol.name == "ftLoadCommonData"; });
    if (found == symbols.end()) throw DatError("Common-data ftLoadCommonData root is missing");
    descriptor_offset = found->data_offset;
    region(archive, descriptor_offset, MELEE_WEB_COMMON_ROOT_COUNT * 4);
#define LOAD_ROOT(index, name) do { \
    const auto offset = archive.pointer(descriptor_offset + (index) * 4); \
    roots[index] = {index, #name, offset, offset ? DatCommonReadiness::Unresolved : DatCommonReadiness::Missing}; \
} while (false);
    MELEE_WEB_COMMON_ROOTS(LOAD_ROOT)
#undef LOAD_ROOT
    if (!roots[0].data_offset) throw DatError("Required ftCommonData scalar root is null");
    scalar_offset = *roots[0].data_offset;
    region(archive, scalar_offset, MELEE_WEB_COMMON_SCALAR_BYTES);
    for (std::uint32_t offset = 0; offset < MELEE_WEB_COMMON_SCALAR_BYTES; offset += 4)
        if (archive.has_relocation(scalar_offset + offset))
            throw DatError("Common scalar block has an unsupported internal relocation");
#define LOAD_F32(offset, member) scalars.member = finite_scalar(archive, scalar_offset + (offset));
#define LOAD_I32(offset, member) scalars.member = std::bit_cast<std::int32_t>(archive.be32(scalar_offset + (offset)));
#define LOAD_U32(offset, member) scalars.member = archive.be32(scalar_offset + (offset));
#define LOAD_OPAQUE32(offset, member) LOAD_U32(offset, member)
#define LOAD_BYTE(offset, member) scalars.member = archive.range(scalar_offset + (offset), 1)[0];
#define LOAD_FIELD(offset, kind, member) LOAD_##kind(offset, member)
    MELEE_WEB_COMMON_FIELDS(LOAD_FIELD)
#undef LOAD_FIELD
#undef LOAD_BYTE
#undef LOAD_OPAQUE32
#undef LOAD_U32
#undef LOAD_I32
#undef LOAD_F32
    roots[0].readiness = DatCommonReadiness::ScalarsDecoded;
}
} // namespace melee_web
