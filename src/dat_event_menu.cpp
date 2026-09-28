#include "dat_event_menu.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace melee_web {
namespace {

constexpr std::string_view kRootName = "sqEventInitDataLevelTbl";
constexpr std::size_t kRetailEventCount = 0x33;

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

struct EventPlayerInit {
    std::int8_t c_kind;
    std::uint8_t slot_type;
    std::uint8_t stocks;
    std::uint8_t color;
    std::uint8_t x5;
    std::uint8_t sub_color;
    std::uint8_t team;
    std::uint8_t xB;
    std::uint8_t flags;
    std::uint8_t xE;
    std::uint8_t cpu_level;
    std::uint8_t pad;
    std::uint16_t x12;
    std::uint16_t hp;
    float x18;
    float x1C;
    float x20;
};

struct EventInit {
    std::uint32_t x0_0 : 3;
    std::uint32_t x0_3 : 3;
    std::uint32_t x0_6 : 1;
    std::uint32_t x0_7 : 1;
    std::uint32_t x1_0 : 1;
    std::uint32_t x1_1 : 1;
    std::uint32_t x1_2 : 1;
    std::uint32_t x1_3 : 1;
    std::uint32_t x1_4 : 1;
    std::uint32_t x1_5 : 3;
    std::uint8_t unk2;
    std::int8_t unk3;
    std::int8_t unk4;
    std::uint8_t unk5;
    std::uint16_t unk6;
    std::int32_t unk8;
    std::uint8_t padC[4];
    std::uint64_t x10;
    std::int32_t x18;
    float x1C;
    float unk20;
    float unk24;
};

struct EventBonus {
    std::int8_t c_kind;
    std::uint8_t x1;
    std::uint8_t x2;
    std::uint8_t x3;
    std::uint8_t x4;
    std::uint8_t x5;
    std::uint8_t color;
    std::uint8_t pad7;
    float x8;
    float xC;
    float x10;
    std::uint8_t flags;
    std::uint8_t x15;
    std::uint8_t x16;
    std::uint8_t x17;
};

struct EventExtra {
    std::int32_t x0;
    std::intptr_t x4;
};

struct EventStageTable {
    std::uint8_t count;
    std::uint8_t pad1;
    std::uint16_t stage[7];
    EventPlayerInit* entries[5];
};

struct EventLevel {
    std::uint8_t kind;
    std::uint8_t flags;
    std::uint8_t pad2[2];
    EventExtra* x4;
    EventInit* x8;
    EventBonus* xC;
    EventStageTable* x10;
    EventPlayerInit* player_init[5];
};

#ifdef __wasm__
static_assert(sizeof(EventPlayerInit) == 28);
static_assert(sizeof(EventInit) == 40);
static_assert(offsetof(EventInit, unk2) == 2);
static_assert(offsetof(EventInit, unk6) == 6);
static_assert(offsetof(EventInit, x10) == 16);
static_assert(offsetof(EventInit, x18) == 24);
static_assert(sizeof(EventBonus) == 24);
static_assert(sizeof(EventExtra) == 8);
static_assert(sizeof(EventStageTable) == 36);
static_assert(sizeof(EventLevel) == 40);
static_assert(offsetof(EventLevel, player_init) == 20);
#endif

} // namespace

struct DatEventMenuData::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::map<std::uint32_t, std::unique_ptr<EventPlayerInit>> players;
    std::map<std::uint32_t, std::unique_ptr<EventInit>> inits;
    std::map<std::uint32_t, std::unique_ptr<EventBonus>> bonuses;
    std::map<std::uint32_t, std::unique_ptr<EventExtra>> extras;
    std::map<std::uint32_t, std::unique_ptr<EventStageTable>> stages;
    std::map<std::uint32_t, std::unique_ptr<EventLevel>> levels_by_offset;
    std::vector<EventLevel*> level_table;

    explicit Storage(std::shared_ptr<const DatArchive> input)
        : archive(std::move(input))
    {
        require(archive != nullptr, "Event menu archive is null");
        const auto root = std::find_if(
            archive->public_symbols().begin(), archive->public_symbols().end(),
            [](const DatPublicSymbol& symbol) { return symbol.name == kRootName; });
        require(root != archive->public_symbols().end(),
                "GmEvent.dat is missing sqEventInitDataLevelTbl");

        const std::uint32_t table_end = archive->next_target_offset(root->data_offset);
        require(table_end >= root->data_offset &&
                    table_end - root->data_offset == kRetailEventCount * 4,
                "sqEventInitDataLevelTbl differs from the retail 0x33-entry bound");
        level_table.reserve(kRetailEventCount);
        for (std::size_t index = 0; index < kRetailEventCount; ++index) {
            const std::uint32_t slot = root->data_offset +
                                       static_cast<std::uint32_t>(index * 4);
            const auto target = archive->pointer(slot, 40);
            require(target.has_value(), "sqEventInitDataLevelTbl contains a null level");
            level_table.push_back(level(*target));
        }
    }

    void record(std::uint32_t offset, std::size_t size, const char* message) const
    {
        (void) archive->range(offset, size);
        require(offset % 4 == 0, message);
    }

    std::uint32_t required_pointer(std::uint32_t slot, std::size_t minbytes,
                                   const char* message) const
    {
        const auto target = archive->pointer(slot, minbytes);
        require(target.has_value(), message);
        return *target;
    }

    EventPlayerInit* player(std::uint32_t offset)
    {
        if (auto found = players.find(offset); found != players.end())
            return found->second.get();
        record(offset, 28, "GmEvent player initialization record is truncated");
        auto result = std::make_unique<EventPlayerInit>();
        const auto bytes = archive->range(offset, 28);
        result->c_kind = static_cast<std::int8_t>(bytes[0]);
        result->slot_type = bytes[1];
        result->stocks = bytes[2];
        result->color = bytes[3];
        result->x5 = bytes[4];
        result->sub_color = bytes[5];
        result->team = bytes[6];
        result->xB = bytes[7];
        result->flags = bytes[8];
        result->xE = bytes[9];
        result->cpu_level = bytes[10];
        result->pad = bytes[11];
        result->x12 = archive->be16(offset + 12);
        result->hp = archive->be16(offset + 14);
        result->x18 = archive->f32(offset + 16);
        result->x1C = archive->f32(offset + 20);
        result->x20 = archive->f32(offset + 24);
        auto* value = result.get();
        players.emplace(offset, std::move(result));
        return value;
    }

    EventInit* init(std::uint32_t offset)
    {
        if (auto found = inits.find(offset); found != inits.end())
            return found->second.get();
        record(offset, 40, "GmEvent match initialization record is truncated");
        const auto bytes = archive->range(offset, 40);
        auto result = std::make_unique<EventInit>();
        // The DAT stores the original big-endian bitfield bytes. Assign each
        // source field by its documented bit position so the native ABI keeps
        // the retail values on the little-endian browser target.
        result->x0_0 = (bytes[0] >> 5) & 7;
        result->x0_3 = (bytes[0] >> 2) & 7;
        result->x0_6 = (bytes[0] >> 1) & 1;
        result->x0_7 = bytes[0] & 1;
        result->x1_0 = (bytes[1] >> 7) & 1;
        result->x1_1 = (bytes[1] >> 6) & 1;
        result->x1_2 = (bytes[1] >> 5) & 1;
        result->x1_3 = (bytes[1] >> 4) & 1;
        result->x1_4 = (bytes[1] >> 3) & 1;
        result->x1_5 = bytes[1] & 7;
        result->unk2 = bytes[2];
        result->unk3 = static_cast<std::int8_t>(bytes[3]);
        result->unk4 = static_cast<std::int8_t>(bytes[4]);
        result->unk5 = bytes[5];
        result->unk6 = archive->be16(offset + 6);
        result->unk8 = static_cast<std::int32_t>(archive->be32(offset + 8));
        std::copy_n(bytes.begin() + 12, 4, result->padC);
        result->x10 = (std::uint64_t{archive->be32(offset + 16)} << 32) |
                      archive->be32(offset + 20);
        result->x18 = static_cast<std::int32_t>(archive->be32(offset + 24));
        result->x1C = archive->f32(offset + 28);
        result->unk20 = archive->f32(offset + 32);
        result->unk24 = archive->f32(offset + 36);
        auto* value = result.get();
        inits.emplace(offset, std::move(result));
        return value;
    }

    EventBonus* bonus(std::uint32_t offset)
    {
        if (auto found = bonuses.find(offset); found != bonuses.end())
            return found->second.get();
        record(offset, 24, "GmEvent bonus record is truncated");
        const auto bytes = archive->range(offset, 24);
        auto result = std::make_unique<EventBonus>();
        result->c_kind = static_cast<std::int8_t>(bytes[0]);
        result->x1 = bytes[1];
        result->x2 = bytes[2];
        result->x3 = bytes[3];
        result->x4 = bytes[4];
        result->x5 = bytes[5];
        result->color = bytes[6];
        result->pad7 = bytes[7];
        result->x8 = archive->f32(offset + 8);
        result->xC = archive->f32(offset + 12);
        result->x10 = archive->f32(offset + 16);
        result->flags = bytes[20];
        result->x15 = bytes[21];
        result->x16 = bytes[22];
        result->x17 = bytes[23];
        auto* value = result.get();
        bonuses.emplace(offset, std::move(result));
        return value;
    }

    EventExtra* extra(std::uint32_t offset)
    {
        if (auto found = extras.find(offset); found != extras.end())
            return found->second.get();
        record(offset, 8, "GmEvent extra record is truncated");
        auto result = std::make_unique<EventExtra>();
        result->x0 = static_cast<std::int32_t>(archive->be32(offset));
        const std::uint32_t pointer_slot = offset + 4;
        if (archive->has_relocation(pointer_slot)) {
            const auto target = required_pointer(
                pointer_slot, 28,
                "GmEvent extra pointer does not reference a player initialization record");
            result->x4 = reinterpret_cast<std::intptr_t>(player(target));
        } else {
            result->x4 = static_cast<std::int32_t>(archive->be32(pointer_slot));
        }
        auto* value = result.get();
        extras.emplace(offset, std::move(result));
        return value;
    }

    EventStageTable* stage_table(std::uint32_t offset)
    {
        if (auto found = stages.find(offset); found != stages.end())
            return found->second.get();
        record(offset, 36, "GmEvent stage table is truncated");
        auto result = std::make_unique<EventStageTable>();
        const auto bytes = archive->range(offset, 36);
        result->count = bytes[0];
        result->pad1 = bytes[1];
        require(result->count <= 7, "GmEvent stage table exceeds its authored stage array");
        for (std::size_t index = 0; index < 7; ++index)
            result->stage[index] = archive->be16(offset + 2 +
                                                static_cast<std::uint32_t>(index * 2));
        for (std::size_t index = 0; index < 5; ++index) {
            const std::uint32_t slot = offset + 16 +
                                       static_cast<std::uint32_t>(index * 4);
            if (const auto target = archive->pointer(slot, 28))
                result->entries[index] = player(*target);
        }
        auto* value = result.get();
        stages.emplace(offset, std::move(result));
        return value;
    }

    EventLevel* level(std::uint32_t offset)
    {
        if (auto found = levels_by_offset.find(offset); found != levels_by_offset.end())
            return found->second.get();
        record(offset, 40, "GmEvent level record is truncated");
        const auto bytes = archive->range(offset, 40);
        auto result = std::make_unique<EventLevel>();
        result->kind = bytes[0];
        result->flags = bytes[1];
        result->pad2[0] = bytes[2];
        result->pad2[1] = bytes[3];
        if (const auto target = archive->pointer(offset + 4, 8))
            result->x4 = extra(*target);
        result->x8 = init(required_pointer(offset + 8, 40,
                                           "GmEvent level lacks match initialization data"));
        if (const auto target = archive->pointer(offset + 12, 24))
            result->xC = bonus(*target);
        if (const auto target = archive->pointer(offset + 16, 36))
            result->x10 = stage_table(*target);
        for (std::size_t index = 0; index < 5; ++index) {
            const std::uint32_t slot = offset + 20 +
                                       static_cast<std::uint32_t>(index * 4);
            if (const auto target = archive->pointer(slot, 28))
                result->player_init[index] = player(*target);
        }
        auto* value = result.get();
        levels_by_offset.emplace(offset, std::move(result));
        return value;
    }
};

DatEventMenuData::DatEventMenuData(std::shared_ptr<const DatArchive> archive)
    : storage_(std::make_unique<Storage>(std::move(archive)))
{}

DatEventMenuData::~DatEventMenuData() = default;

void* DatEventMenuData::descriptor() const noexcept
{
    return storage_->level_table.data();
}

std::size_t DatEventMenuData::level_count() const noexcept
{
    return storage_->level_table.size();
}

std::span<const std::uint8_t> DatEventMenuData::source_bytes() const noexcept
{
    return storage_->archive->data();
}

} // namespace melee_web
