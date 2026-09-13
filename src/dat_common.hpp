#pragma once

#include "common_schema.h"
#include "common_tables.h"
#include "dat_archive.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace melee_web {
enum class DatCommonReadiness {
    Missing,
    Unresolved,
    ScalarsDecoded,
    StaticTablesDecoded,
    CpuDataDecoded,
};
struct DatCommonRoot {
    std::uint32_t index;
    std::string_view source_global; // Generated original Fighter_LoadCommonData assignment identity.
    std::optional<std::uint32_t> data_offset;
    DatCommonReadiness readiness;
};

// Owns the complete typed root0 scalar block and the checked 23-root inventory.
// UNK_T fields are opaque source uint32 words, never pointer fixups. Colors and
// raw u8 bytes retain channel order; numeric fields use host scalar values.
// Supported static tables own bounded decoded values with no borrowed pointers.
// Other roots remain offsets, not native graphs. This is hydration INPUT only:
// do not pass this mixed-readiness inventory to original Fighter_LoadCommonData
// or publish its addresses into all23 globals. An original consumer must own a
// scoped typed context and explicitly hydrate only its supported dependencies.
class DatCommon {
public:
    explicit DatCommon(const DatArchive&);
    DatCommon(const DatCommon&);
    DatCommon& operator=(const DatCommon&);
    DatCommon(DatCommon&&) noexcept;
    DatCommon& operator=(DatCommon&&) noexcept;
    ~DatCommon();
    std::uint32_t descriptor_offset = 0, scalar_offset = 0;
    std::array<DatCommonRoot, MELEE_WEB_COMMON_ROOT_COUNT> roots{};
    MeleeWebCommonScalars scalars{};
    MeleeWebCommonTables tables{};

private:
    MeleeWebCommonCpuData* cpu_data_ = nullptr;
};

} // namespace melee_web
