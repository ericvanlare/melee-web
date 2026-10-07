#include "dat_archive.hpp"
#include "gameplay_stage_stadium.h"
#include "native_dat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace melee_web;

namespace {

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void put_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value)
{
    check(offset <= bytes.size() && bytes.size() - offset >= 4,
          "synthetic word write escaped fixture");
    bytes[offset] = static_cast<std::uint8_t>(value >> 24);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

void put_u16(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint16_t value)
{
    check(offset <= bytes.size() && bytes.size() - offset >= 2,
          "synthetic halfword write escaped fixture");
    bytes[offset] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

std::shared_ptr<const DatArchive>
archive_with_data(const std::vector<std::uint8_t>& data)
{
    std::vector<std::uint8_t> file(0x20 + data.size(), 0);
    put_u32(file, 0, static_cast<std::uint32_t>(file.size()));
    put_u32(file, 4, static_cast<std::uint32_t>(data.size()));
    std::copy(data.begin(), data.end(), file.begin() + 0x20);
    return std::make_shared<DatArchive>(file);
}

std::vector<std::uint8_t> source_data()
{
    std::vector<std::uint8_t> data(0x58, 0xA5);
    constexpr std::array<std::int32_t, 7> signed_words = {
        INT32_MIN, -123456789, -1, 0, 1, 123456789, INT32_MAX,
    };
    constexpr std::array<std::uint32_t, 10> unsigned_words = {
        UINT32_C(0x00000000), UINT32_C(0xFFFFFFFF), UINT32_C(0x80000000),
        UINT32_C(0x7FFFFFFF), UINT32_C(0x01234567), UINT32_C(0x89ABCDEF),
        UINT32_C(0xFEDCBA98), UINT32_C(0x00000001), UINT32_C(0x40000000),
        UINT32_C(0xA5A5A5A5),
    };
    constexpr std::array<std::int16_t, 5> signed_halves = {
        INT16_MIN, -1, 0, 1, INT16_MAX,
    };
    for (std::size_t i = 0; i < signed_words.size(); ++i)
        put_u32(data, 4 * i, std::bit_cast<std::uint32_t>(signed_words[i]));
    data[0x1C] = 0x17;
    data[0x1D] = 0x8A;
    data[0x1E] = 0xE3;
    data[0x1F] = 0x6D; // Must not be read as part of RGB or the next word.
    for (std::size_t i = 0; i < unsigned_words.size(); ++i)
        put_u32(data, 0x20 + 4 * i, unsigned_words[i]);
    for (std::size_t i = 0; i < signed_halves.size(); ++i)
        put_u16(data, 0x48 + 2 * i,
                static_cast<std::uint16_t>(signed_halves[i]));
    data[0x52] = 0xB4; // Source tail padding is ignored and zeroed in the owner.
    data[0x53] = 0x29;
    put_u32(data, 0x54, UINT32_C(0xDECAFBAD)); // The next authored object.
    return data;
}

struct GuardedReader {
    const MeleeWebNativeDat* source;
    std::uint32_t root;
    bool fail_allocation;
    unsigned allocation_calls = 0;
    unsigned region_calls = 0;
    std::uint32_t region_root = 0;
    std::size_t region_size = 0;
    std::vector<std::pair<char, std::uint32_t>> reads;
    MeleeWebNativeDat api{};

    GuardedReader(const MeleeWebNativeDat* input, std::uint32_t source_root,
                   bool fail = false)
        : source(input), root(source_root), fail_allocation(fail)
    {
        api = *source;
        api.context = this;
        api.word = read_word;
        api.half = read_half;
        api.byte = read_byte;
        api.pointer = read_pointer;
        api.region = read_region;
        api.source_region = read_source_region;
        api.allocate = allocate;
        api.reject = reject;
        api.extent = extent;
    }

    void require_inside(std::uint32_t offset, std::size_t size)
    {
        const std::uint32_t end = root + 0x54;
        if (offset < root || offset > end || size > end - offset)
            throw std::runtime_error("decoder read entered padding or neighbor bytes");
    }

    static std::uint32_t read_word(void* context, std::uint32_t offset)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        self.require_inside(offset, 4);
        self.reads.emplace_back('w', offset);
        return self.source->word(self.source->context, offset);
    }

    static std::uint16_t read_half(void* context, std::uint32_t offset)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        self.require_inside(offset, 2);
        self.reads.emplace_back('h', offset);
        return self.source->half(self.source->context, offset);
    }

    static std::uint8_t read_byte(void* context, std::uint32_t offset)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        self.require_inside(offset, 1);
        self.reads.emplace_back('b', offset);
        return self.source->byte(self.source->context, offset);
    }

    static std::uint32_t read_pointer(void* context, std::uint32_t offset,
                                      std::size_t min_bytes)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        return self.source->pointer(self.source->context, offset, min_bytes);
    }

    static const void* read_region(void* context, std::uint32_t offset,
                                   std::size_t size)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        ++self.region_calls;
        self.region_root = offset;
        self.region_size = size;
        return self.source->region(self.source->context, offset, size);
    }

    static const void* read_source_region(void* context, std::uint32_t address,
                                          std::size_t size)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        return self.source->source_region(self.source->context, address, size);
    }

    static void* allocate(void* context, std::size_t count, std::size_t size)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        ++self.allocation_calls;
        if (self.fail_allocation) {
            self.fail_allocation = false;
            throw DatError("synthetic Stadium arena allocation failure");
        }
        return self.source->allocate(self.source->context, count, size);
    }

    static void reject(void* context, const char* message)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        self.source->reject(self.source->context, message);
    }

    static std::uint32_t extent(void* context, std::uint32_t offset)
    {
        auto& self = *static_cast<GuardedReader*>(context);
        return self.source->extent(self.source->context, offset);
    }
};

void exact_scalar_abi()
{
    const auto data = source_data();
    auto archive = archive_with_data(data);
    NativeDatArena arena(archive);
    GuardedReader guarded(arena.reader(), 0);
    auto* decoded = static_cast<MeleeWebStadiumYakumono*>(
        melee_web_stadium_yakumono_decode(&guarded.api, 0));

    check(decoded != nullptr, "decoder returned no Stadium scalar object");
    check(guarded.region_calls == 1 && guarded.region_root == 0 &&
              guarded.region_size == 0x54,
          "decoder did not claim exactly the 0x54-byte source object");
    constexpr std::array<std::int32_t, 7> expected_s32 = {
        INT32_MIN, -123456789, -1, 0, 1, 123456789, INT32_MAX,
    };
    const std::array<std::int32_t, 7> actual_s32 = {
        decoded->x0, decoded->x4, decoded->x8, decoded->xC,
        decoded->x10, decoded->x14, decoded->x18,
    };
    check(actual_s32 == expected_s32,
          "signed 32-bit Stadium fields lost source values");
    constexpr std::array<std::uint32_t, 10> expected_u32 = {
        UINT32_C(0x00000000), UINT32_C(0xFFFFFFFF), UINT32_C(0x80000000),
        UINT32_C(0x7FFFFFFF), UINT32_C(0x01234567), UINT32_C(0x89ABCDEF),
        UINT32_C(0xFEDCBA98), UINT32_C(0x00000001), UINT32_C(0x40000000),
        UINT32_C(0xA5A5A5A5),
    };
    const std::array<std::uint32_t, 10> actual_u32 = {
        decoded->x20, decoded->x24, decoded->x28, decoded->x2C,
        decoded->x30, decoded->x34, decoded->x38, decoded->x3C,
        decoded->x40, decoded->x44,
    };
    check(actual_u32 == expected_u32,
          "unsigned 32-bit Stadium fields lost source bits");
    constexpr std::array<std::int16_t, 5> expected_s16 = {
        INT16_MIN, -1, 0, 1, INT16_MAX,
    };
    const std::array<std::int16_t, 5> actual_s16 = {
        decoded->x48, decoded->x4A, decoded->x4C, decoded->x4E,
        decoded->x50,
    };
    check(actual_s16 == expected_s16,
          "signed 16-bit Stadium fields lost source values");
    check(decoded->r == 0x17 && decoded->g == 0x8A && decoded->b == 0xE3,
          "Stadium RGB byte order changed");
    check(decoded->_rgb_padding == 0 && decoded->_final_padding == 0,
          "Stadium native padding was not normalized");

    std::vector<std::pair<char, std::uint32_t>> expected_reads;
    for (std::uint32_t offset = 0; offset <= 0x18; offset += 4)
        expected_reads.emplace_back('w', offset);
    for (std::uint32_t offset = 0x1C; offset <= 0x1E; ++offset)
        expected_reads.emplace_back('b', offset);
    for (std::uint32_t offset = 0x20; offset <= 0x44; offset += 4)
        expected_reads.emplace_back('w', offset);
    for (std::uint32_t offset = 0x48; offset <= 0x50; offset += 2)
        expected_reads.emplace_back('h', offset);
    check(guarded.reads == expected_reads,
          "decoder read padding, neighbor bytes, or changed authored field order");
    check(std::vector<std::uint8_t>(archive->data().begin(), archive->data().end()) == data,
          "decoder modified immutable archive data");
}

void short_extent_rejected_before_allocation()
{
    auto data = source_data();
    data.resize(0x53);
    auto archive = archive_with_data(data);
    NativeDatArena arena(archive);
    GuardedReader guarded(arena.reader(), 0);
    bool rejected = false;
    try {
        (void) melee_web_stadium_yakumono_decode(&guarded.api, 0);
    } catch (const DatError&) {
        rejected = true;
    }
    check(rejected, "decoder accepted a 0x53-byte source extent");
    check(guarded.region_calls == 1 && guarded.region_size == 0x54,
          "short extent was not checked against the exact source ABI size");
    check(guarded.allocation_calls == 0,
          "decoder allocated before rejecting a short source extent");
}

void allocation_failure_and_independent_recovery()
{
    auto archive = archive_with_data(source_data());
    NativeDatArena failed_arena(archive);
    GuardedReader failing(failed_arena.reader(), 0, true);
    bool rejected = false;
    try {
        (void) melee_web_stadium_yakumono_decode(&failing.api, 0);
    } catch (const DatError&) {
        rejected = true;
    }
    check(rejected && failing.allocation_calls == 1,
          "synthetic arena allocation failure was not propagated");
    check(failing.reads.empty(),
          "decoder read source fields before allocation succeeded");

    auto independent_archive = archive_with_data(source_data());
    NativeDatArena independent_arena(independent_archive);
    GuardedReader recovered(independent_arena.reader(), 0);
    auto* decoded = static_cast<MeleeWebStadiumYakumono*>(
        melee_web_stadium_yakumono_decode(&recovered.api, 0));
    check(decoded && decoded->x0 == INT32_MIN && decoded->x18 == INT32_MAX &&
              decoded->r == 0x17 && decoded->b == 0xE3 &&
              decoded->x48 == INT16_MIN && decoded->x50 == INT16_MAX,
          "independent arena did not recover after allocation failure");
}

} // namespace

int main()
{
    try {
        static_assert(sizeof(MeleeWebStadiumYakumono) == 0x54,
                      "trace expects the exact source struct size");
        exact_scalar_abi();
        short_extent_rejected_before_allocation();
        allocation_failure_and_independent_recovery();
        check(melee_web_stadium_yakumono_decode(nullptr, 0) == nullptr,
              "null reader was not rejected safely");
        std::cout << "Pokémon Stadium yakumono exact 0x54-byte source ABI, "
                     "signed fields, RGB, bounded reads, short extent and "
                     "independent arena recovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
