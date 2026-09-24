#include "dat_trophy_data.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace melee_web {
namespace {

constexpr std::size_t kTrophyEntryBytes = 0x24;
constexpr std::size_t kNameEntryBytes = 0x0c;
constexpr std::size_t kDisplayEntryBytes = 0x10;

void require(bool condition, const char* message)
{
    if (!condition) throw DatError(message);
}

std::uint32_t root_offset(const DatArchive& archive, std::string_view name)
{
    for (const auto& symbol : archive.public_symbols()) {
        if (symbol.name == name) {
            require((symbol.data_offset & 3U) == 0,
                    "TyDatai public root is unaligned");
            return symbol.data_offset;
        }
    }
    throw DatError("TyDatai public root is missing");
}

std::uint32_t region_size(const DatArchive& archive, std::uint32_t root,
                          std::size_t entry_bytes, const char* description)
{
    const auto end = archive.next_target_offset(root);
    require(end >= root, "TyDatai public root region is inverted");
    const auto bytes = static_cast<std::size_t>(end - root);
    require(bytes != 0 && bytes % entry_bytes == 0, description);
    return static_cast<std::uint32_t>(bytes);
}

void reject_relocations(const DatArchive& archive, std::uint32_t root,
                        std::size_t bytes, const char* description)
{
    require((root & 3U) == 0, description);
    for (std::size_t at = 0; at < bytes; at += 4) {
        if (archive.has_relocation(root + static_cast<std::uint32_t>(at))) {
            throw DatError(description);
        }
    }
}

std::int16_t signed16(std::uint16_t value)
{
    const auto bits = static_cast<std::int32_t>(value);
    return static_cast<std::int16_t>(bits < 0x8000 ? bits : bits - 0x10000);
}

std::int32_t signed32(std::uint32_t value)
{
    return std::bit_cast<std::int32_t>(value);
}

DatTrophyEntry read_trophy_entry(const DatArchive& archive, std::uint32_t offset)
{
    DatTrophyEntry result{};
    result.id = signed32(archive.be32(offset));
    result.x04 = signed32(archive.be32(offset + 4));
    result.x08 = archive.f32(offset + 8);
    result.x0c = archive.f32(offset + 12);
    result.x10 = archive.f32(offset + 16);
    result.x14 = archive.f32(offset + 20);
    result.x18 = archive.f32(offset + 24);
    result.x1c = archive.f32(offset + 28);
    const auto bytes = archive.range(offset + 32, 4);
    result.x20 = static_cast<std::int8_t>(bytes[0]);
    result.x21 = static_cast<std::int8_t>(bytes[1]);
    result.x22 = static_cast<std::int8_t>(bytes[2]);
    result.x23 = static_cast<std::int8_t>(bytes[3]);
    return result;
}

std::vector<DatTrophyEntry> read_trophy_table(const DatArchive& archive,
                                                std::uint32_t root)
{
    const auto bytes = region_size(archive, root, kTrophyEntryBytes,
                                   "TyDatai TrophyData region is malformed");
    require(bytes / kTrophyEntryBytes <= kDatTrophyCount + 1,
            "TyDatai TrophyData table exceeds the source trophy domain");
    reject_relocations(archive, root, bytes,
                       "TyDatai TrophyData contains a pointer relocation");

    std::vector<DatTrophyEntry> result;
    result.reserve(bytes / kTrophyEntryBytes);
    std::unordered_set<std::int32_t> ids;
    bool terminated = false;
    for (std::size_t index = 0; index < bytes / kTrophyEntryBytes; ++index) {
        const auto offset = root + static_cast<std::uint32_t>(index * kTrophyEntryBytes);
        auto entry = read_trophy_entry(archive, offset);
        if (entry.id == -1) {
            require(index + 1 == bytes / kTrophyEntryBytes,
                    "TyDatai TrophyData has bytes after its terminator");
            terminated = true;
        } else {
            require(!terminated && entry.id >= 0 &&
                        entry.id < static_cast<std::int32_t>(kDatTrophyCount),
                    "TyDatai TrophyData id is outside the source domain");
            require(ids.insert(entry.id).second,
                    "TyDatai TrophyData repeats a source id");
        }
        result.push_back(entry);
    }
    require(terminated, "TyDatai TrophyData has no source terminator");
    return result;
}

DatTrophyNameEntry read_name_entry(const DatArchive& archive, std::uint32_t offset)
{
    DatTrophyNameEntry result{};
    result.x0 = signed16(archive.be16(offset));
    result.x2 = signed16(archive.be16(offset + 2));
    result.x4 = signed16(archive.be16(offset + 4));
    result.x6 = signed16(archive.be16(offset + 6));
    result.x8 = signed16(archive.be16(offset + 8));
    result.xa = signed16(archive.be16(offset + 10));
    return result;
}

std::vector<DatTrophyNameEntry> read_name_table(const DatArchive& archive,
                                                 std::uint32_t root)
{
    const auto bytes = region_size(archive, root, kNameEntryBytes,
                                   "TyDatai model-sort region is malformed");
    // Toy_803064B8 indexes 0..TY_TROPHY_COUNT-1 and the authored table has a
    // final sentinel row. This is a source bound, not a runtime asset guess.
    require(bytes / kNameEntryBytes == kDatTrophyCount + 1,
            "TyDatai model-sort table does not match TY_TROPHY_COUNT");
    reject_relocations(archive, root, bytes,
                       "TyDatai model-sort table contains a relocation");

    std::vector<DatTrophyNameEntry> result;
    result.reserve(bytes / kNameEntryBytes);
    for (std::size_t index = 0; index < bytes / kNameEntryBytes; ++index) {
        result.push_back(read_name_entry(
            archive, root + static_cast<std::uint32_t>(index * kNameEntryBytes)));
    }
    require(result.back().x0 == -1,
            "TyDatai model-sort table has no source terminator");
    return result;
}

std::vector<std::int16_t> read_s16_table(const DatArchive& archive,
                                          std::uint32_t root,
                                          const char* description)
{
    const auto bytes = region_size(archive, root, sizeof(std::int16_t), description);
    require(bytes / sizeof(std::int16_t) <= kDatTrophyCount + 1,
            "TyDatai signed table exceeds the source trophy domain");
    reject_relocations(archive, root, bytes, description);

    std::vector<std::int16_t> result;
    result.reserve(bytes / sizeof(std::int16_t));
    bool terminated = false;
    for (std::size_t index = 0; index < bytes / sizeof(std::int16_t); ++index) {
        const auto value = signed16(archive.be16(
            root + static_cast<std::uint32_t>(index * sizeof(std::int16_t))));
        if (value == -1) {
            require(index + 1 == bytes / sizeof(std::int16_t),
                    "TyDatai signed table has bytes after its terminator");
            terminated = true;
        } else {
            require(!terminated && value >= 0 &&
                        value < static_cast<std::int16_t>(kDatTrophyCount),
                    "TyDatai signed table value is outside the source domain");
        }
        result.push_back(value);
    }
    require(terminated, "TyDatai signed table has no source terminator");
    return result;
}

DatTrophyDisplayEntry read_display_entry(const DatArchive& archive,
                                          std::uint32_t offset)
{
    DatTrophyDisplayEntry result{};
    result.x00 = signed32(archive.be32(offset));
    const auto bytes = archive.range(offset + 4, 4);
    result.x04 = bytes[0];
    result.x05 = bytes[1];
    result.pad06 = bytes[2];
    result.pad07 = bytes[3];
    result.x08 = archive.f32(offset + 8);
    result.x0c = archive.f32(offset + 12);
    return result;
}

std::vector<DatTrophyDisplayEntry> read_display_table(const DatArchive& archive,
                                                       std::uint32_t root)
{
    const auto bytes = region_size(archive, root, kDisplayEntryBytes,
                                   "TyDatai display region is malformed");
    // Display tables are keyed by trophy id but are not one-row-per-id: the
    // authored JP table contains a repeated id for two display variants.
    // Their source bound is therefore the public-root region plus its required
    // terminator, rather than TY_TROPHY_COUNT.
    reject_relocations(archive, root, bytes,
                       "TyDatai display table contains a relocation");

    std::vector<DatTrophyDisplayEntry> result;
    result.reserve(bytes / kDisplayEntryBytes);
    bool terminated = false;
    for (std::size_t index = 0; index < bytes / kDisplayEntryBytes; ++index) {
        const auto entry = read_display_entry(
            archive, root + static_cast<std::uint32_t>(index * kDisplayEntryBytes));
        if (entry.x00 == -1) {
            require(index + 1 == bytes / kDisplayEntryBytes,
                    "TyDatai display table has bytes after its terminator");
            terminated = true;
        } else {
            require(!terminated && entry.x00 >= 0 &&
                        entry.x00 < static_cast<std::int32_t>(kDatTrophyCount),
                    "TyDatai display id is outside the source domain");
        }
        result.push_back(entry);
    }
    require(terminated, "TyDatai display table has no source terminator");
    return result;
}

} // namespace

struct DatTrophyData::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::vector<DatTrophyEntry> init_model;
    std::vector<DatTrophyEntry> init_model_d;
    std::vector<DatTrophyNameEntry> model_sort;
    std::vector<std::int16_t> exp_different;
    std::vector<std::int16_t> no_get_us;
    std::vector<DatTrophyDisplayEntry> display_model;
    std::vector<DatTrophyDisplayEntry> display_model_us;
};

DatTrophyData::DatTrophyData(std::shared_ptr<const DatArchive> archive)
    : storage_(std::make_unique<Storage>())
{
    require(bool(archive), "Missing TyDatai archive");
    storage_->archive = std::move(archive);
    const auto& a = *storage_->archive;
    storage_->init_model = read_trophy_table(a, root_offset(a, "tyInitModelTbl"));
    storage_->init_model_d = read_trophy_table(a, root_offset(a, "tyInitModelDTbl"));
    storage_->model_sort = read_name_table(a, root_offset(a, "tyModelSortTbl"));
    storage_->exp_different =
        read_s16_table(a, root_offset(a, "tyExpDifferentTbl"),
                       "TyDatai expansion-different table is malformed");
    storage_->no_get_us =
        read_s16_table(a, root_offset(a, "tyNoGetUsTbl"),
                       "TyDatai no-get-US table is malformed");
    storage_->display_model =
        read_display_table(a, root_offset(a, "tyDisplayModelTbl"));
    storage_->display_model_us =
        read_display_table(a, root_offset(a, "tyDisplayModelUsTbl"));
}

DatTrophyData::~DatTrophyData() = default;

std::span<const DatTrophyEntry> DatTrophyData::init_model_table() const noexcept
{
    return storage_->init_model;
}

std::span<const DatTrophyEntry> DatTrophyData::init_model_d_table() const noexcept
{
    return storage_->init_model_d;
}

std::span<const DatTrophyNameEntry> DatTrophyData::model_sort_table() const noexcept
{
    return storage_->model_sort;
}

std::span<const std::int16_t> DatTrophyData::exp_different_table() const noexcept
{
    return storage_->exp_different;
}

std::span<const std::int16_t> DatTrophyData::no_get_us_table() const noexcept
{
    return storage_->no_get_us;
}

std::span<const DatTrophyDisplayEntry> DatTrophyData::display_model_table() const noexcept
{
    return storage_->display_model;
}

std::span<const DatTrophyDisplayEntry>
DatTrophyData::display_model_us_table() const noexcept
{
    return storage_->display_model_us;
}

} // namespace melee_web
