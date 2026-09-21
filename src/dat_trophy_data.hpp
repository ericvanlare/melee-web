#pragma once

#include "dat_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace melee_web {

// TY_TROPHY_COUNT is the authored source-domain bound from
// .deps/melee/src/melee/ty/forward.h. The data tables below carry an explicit
// -1 terminator in addition to that domain where the original consumer scans
// until a sentinel.
inline constexpr std::size_t kDatTrophyCount = 293;

// Native layouts recovered from melee/ty/types.h. These are value types: the
// decoder preserves the source fields while keeping archive bytes and native
// consumers separate.
struct DatTrophyEntry {
    std::int32_t id;
    std::int32_t x04;
    float x08;
    float x0c;
    float x10;
    float x14;
    float x18;
    float x1c;
    std::int8_t x20;
    std::int8_t x21;
    std::int8_t x22;
    std::int8_t x23;
};
static_assert(sizeof(DatTrophyEntry) == 0x24);

struct DatTrophyNameEntry {
    std::int16_t x0;
    std::int16_t x2;
    std::int16_t x4;
    std::int16_t x6;
    std::int16_t x8;
    std::int16_t xa;
};
static_assert(sizeof(DatTrophyNameEntry) == 0x0c);

struct DatTrophyDisplayEntry {
    std::int32_t x00;
    std::uint8_t x04;
    std::uint8_t x05;
    std::uint8_t pad06;
    std::uint8_t pad07;
    float x08;
    float x0c;
};
static_assert(sizeof(DatTrophyDisplayEntry) == 0x10);

// Typed owner for the seven public roots passed to
// lbArchive_LoadSymbols("TyDatai.usd", ...). Every returned span includes
// the authored terminator row/value, matching the source pointers that scan
// these tables. The archive is retained for the owner's lifetime so callers
// may publish the copied descriptors without an accidental early release of
// the source archive.
class DatTrophyData {
public:
    explicit DatTrophyData(std::shared_ptr<const DatArchive> archive);
    ~DatTrophyData();
    DatTrophyData(const DatTrophyData&) = delete;
    DatTrophyData& operator=(const DatTrophyData&) = delete;

    [[nodiscard]] std::span<const DatTrophyEntry>
    init_model_table() const noexcept;
    [[nodiscard]] std::span<const DatTrophyEntry>
    init_model_d_table() const noexcept;
    [[nodiscard]] std::span<const DatTrophyNameEntry>
    model_sort_table() const noexcept;
    [[nodiscard]] std::span<const std::int16_t>
    exp_different_table() const noexcept;
    [[nodiscard]] std::span<const std::int16_t>
    no_get_us_table() const noexcept;
    [[nodiscard]] std::span<const DatTrophyDisplayEntry>
    display_model_table() const noexcept;
    [[nodiscard]] std::span<const DatTrophyDisplayEntry>
    display_model_us_table() const noexcept;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
