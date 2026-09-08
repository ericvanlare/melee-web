#pragma once
#include "animation_fixture.hpp"
#include "dat_fighter_runtime.hpp"

namespace fighter_runtime_test {
using namespace animation_test;
using namespace melee_web;
inline const FighterCostume& mario() { return resolve_fighter_costume("PlyMario5K_Share_joint"); }
inline Bytes pack(const Bytes& data, const std::vector<std::uint32_t>& slots,
                  std::string_view name, std::uint32_t root = 0)
{
    const auto publics = std::uint32_t(32 + data.size() + slots.size() * 4);
    Bytes bytes(publics + 8 + name.size() + 1);
    put32(bytes, 0, std::uint32_t(bytes.size())); put32(bytes, 4, std::uint32_t(data.size()));
    put32(bytes, 8, std::uint32_t(slots.size())); put32(bytes, 12, 1);
    std::copy(data.begin(), data.end(), bytes.begin() + 32);
    for (std::size_t i = 0; i < slots.size(); ++i) put32(bytes, 32 + data.size() + i * 4, slots[i]);
    put32(bytes, publics, root);
    std::copy(name.begin(), name.end(), bytes.begin() + publics + 8);
    return bytes;
}
struct FighterFixture {
    static constexpr std::uint32_t co = 128, ext = co + 0x184, pickup = ext + 0x84,
                                   vector = pickup + 0x30, table = vector + 8;
    const FighterCostume* costume;
    std::uint32_t blends, waits, command_a, command_b, name, dynamics_desc, hurt_desc, hurt_rows;
    Bytes data, container;
    std::vector<std::uint32_t> slots;
    explicit FighterFixture(const FighterCostume& selected = mario()) : costume(&selected)
    {
        blends = table + selected.motion_count * 24;
        waits = (blends + selected.motion_count * 2 + 3) & ~3U;
        command_a = waits + 24; command_b = command_a + 8; name = command_b + 8;
        dynamics_desc = name + 16; hurt_desc = dynamics_desc + 20; hurt_rows = hurt_desc + 8;
        data.resize(hurt_rows + 40);
        link(0, co); link(4, ext); link(12, table); link(16, blends); link(36, waits);
        link(64, pickup); link(80, vector);
        link(0x2c, dynamics_desc); link(0x30, hurt_desc);
        put32(data, hurt_desc, 1); link(hurt_desc + 4, hurt_rows);
        put32(data, hurt_rows, 1); put32(data, hurt_rows + 4, 2); put32(data, hurt_rows + 8, 1);
        put32(data, hurt_rows + 12, std::bit_cast<std::uint32_t>(-2.0f));
        put32(data, hurt_rows + 24, std::bit_cast<std::uint32_t>(3.0f));
        put32(data, hurt_rows + 36, std::bit_cast<std::uint32_t>(1.5f));
#define VALUE_F32(at) std::bit_cast<std::uint32_t>(float(at) + 0.25f)
#define VALUE_I32(at) std::uint32_t(std::int32_t(at) - 400)
#define VALUE_U32(at) (0x81234500U + at)
#define VALUE_U8(at) 0xa5000000U
#define CO(at, type, field, original) put32(data, co + at, VALUE_##type(at));
        MELEE_WEB_CO_ATTRIBUTE_FIELDS(CO)
#define EXT(at, type, field, original) put32(data, ext + at, VALUE_##type(at));
        MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(EXT)
#define PICKUP(at, type, field, original) put32(data, pickup + at, VALUE_##type(at));
        MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(PICKUP)
#undef CO
#undef EXT
#undef PICKUP
#undef VALUE_F32
#undef VALUE_I32
#undef VALUE_U32
#undef VALUE_U8
        put32(data, vector, std::bit_cast<std::uint32_t>(-3.5f));
        put32(data, vector + 4, std::bit_cast<std::uint32_t>(2.25f));
        put32(data, waits, 2); put32(data, waits + 4, 70);
        put32(data, waits + 8, 6); put32(data, waits + 12, 30);
        put32(data, waits + 16, 0xffffffff); put32(data, waits + 20, 0xffffffff);
        put32(data, command_a, 0xa0000123); link(command_a + 4, 0);
        put32(data, command_b, 0x04000018);
        data[name] = 'a';
        Fixture animation;
        const auto clip = pack(animation.data, animation.slots, "a", Fixture::root);
        container.resize(384);
        for (auto start : {0U, 128U, 256U}) std::copy(clip.begin(), clip.end(), container.begin() + start);
        for (auto id : {2U, 6U, 7U, 8U}) {
            link(row(id), name);
            put32(data, row(id) + 4, id == 7 ? 128 : id == 8 ? 256 : 0);
            put32(data, row(id) + 8, std::uint32_t(clip.size()));
            put32(data, row(id) + 16, id == 6 ? 0x80001234 : 0x1234);
            link(row(id) + 12, id == 6 ? command_b : command_a);
            data[blends + id * 2] = std::uint8_t(id);
            data[blends + id * 2 + 1] = std::uint8_t(id + 1);
        }
    }
    std::uint32_t row(std::uint32_t id) const { return table + id * 24; }
    void link(std::uint32_t slot, std::uint32_t target)
    {
        put32(data, slot, target);
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
    }
    void unlink(std::uint32_t slot) { put32(data, slot, 0); std::erase(slots, slot); }
    Bytes file() const { return pack(data, slots, costume->fighter_symbol); }
    std::shared_ptr<const DatFighterRuntime> read() const
    {
        return std::make_shared<const DatFighterRuntime>(std::make_shared<const DatArchive>(file()), *costume);
    }
};
}
