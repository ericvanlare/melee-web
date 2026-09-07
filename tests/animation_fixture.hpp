#pragma once
#include "dat_animation.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace animation_test {
using Bytes = std::vector<std::uint8_t>;
inline void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
template <typename F> void rejects(F operation)
{
    try { operation(); }
    catch (const melee_web::DatError&) { return; }
    throw std::runtime_error("expected DatError");
}
inline void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) bytes.at(offset + i) = std::uint8_t(value >> (24 - i * 8));
}
inline void little_float(Bytes& bytes, float value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(std::uint8_t(bits >> (i * 8)));
}
inline Bytes segment(unsigned opcode, float first, float last, unsigned duration,
                     float first_slope = 0, float last_slope = 0)
{
    Bytes stream{std::uint8_t(0x10 | opcode)};
    little_float(stream, first);
    if (opcode == 4) little_float(stream, first_slope);
    stream.push_back(std::uint8_t(duration));
    little_float(stream, last);
    if (opcode == 4) little_float(stream, last_slope);
    return stream;
}

struct Fixture {
    static constexpr std::uint32_t root = 32, nodes = 52, track = 56;
    Bytes data = Bytes(68, 0);
    std::vector<std::uint32_t> slots{root + 12, root + 16, track + 8};

    explicit Fixture(Bytes stream = segment(2, 0, 10, 10), std::uint8_t type = 1)
    {
        check(stream.size() <= 32, "fixture stream must fit before root");
        std::copy(stream.begin(), stream.end(), data.begin());
        put32(data, root, 1);
        put32(data, root + 8, std::bit_cast<std::uint32_t>(10.f));
        put32(data, root + 12, nodes);
        put32(data, root + 16, track);
        data[nodes] = 0; data[nodes + 1] = 1; data[nodes + 2] = 255;
        data[track] = std::uint8_t(stream.size() >> 8);
        data[track + 1] = std::uint8_t(stream.size());
        data[track + 4] = type;
        // The relocated stream pointer deliberately targets body offset zero.
    }

    melee_web::DatArchive archive() const
    {
        const auto publics = 32 + data.size() + slots.size() * 4;
        Bytes file(publics + 10, 0);
        put32(file, 0, std::uint32_t(file.size()));
        put32(file, 4, std::uint32_t(data.size()));
        put32(file, 8, std::uint32_t(slots.size()));
        put32(file, 12, 1);
        std::copy(data.begin(), data.end(), file.begin() + 32);
        for (std::size_t i = 0; i < slots.size(); ++i) put32(file, 32 + data.size() + i * 4, slots[i]);
        put32(file, publics, root);
        file[publics + 8] = 'a';
        return melee_web::DatArchive(file);
    }
    melee_web::DatAnimation animation() const { return {archive(), root}; }
};
} // namespace animation_test
