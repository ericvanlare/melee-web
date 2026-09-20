#include "dat_archive.hpp"
#include "gameplay_stage_old_yoshi.h"
#include "native_dat.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

namespace {

struct Rejected : std::exception {
    const char* what() const noexcept override { return "decoder rejected"; }
};

struct SyntheticReader {
    std::vector<uint8_t> bytes;
    uint32_t root = 0x20;
    uint32_t region_root = 0;
    size_t region_size = 0;
    std::vector<void*> allocations;
    std::string rejection;

    static uint32_t word(void* context, uint32_t offset)
    {
        auto& self = *static_cast<SyntheticReader*>(context);
        if ((offset & 3) != 0 || offset < self.root ||
            offset + 4 > self.bytes.size())
            throw std::runtime_error("synthetic word read escaped fixture");
        return (uint32_t(self.bytes[offset]) << 24) |
               (uint32_t(self.bytes[offset + 1]) << 16) |
               (uint32_t(self.bytes[offset + 2]) << 8) |
               uint32_t(self.bytes[offset + 3]);
    }

    static uint16_t half(void* context, uint32_t offset)
    {
        auto& self = *static_cast<SyntheticReader*>(context);
        if ((offset & 1) != 0 || offset < self.root ||
            offset + 2 > self.bytes.size())
            throw std::runtime_error("synthetic half read escaped fixture");
        return (uint16_t(self.bytes[offset]) << 8) |
               uint16_t(self.bytes[offset + 1]);
    }

    static uint8_t byte(void*, uint32_t)
    {
        throw std::runtime_error("synthetic byte read was unexpected");
    }

    static uint32_t pointer(void*, uint32_t, size_t)
    {
        throw std::runtime_error("synthetic pointer read was unexpected");
    }

    static const void* region(void* context, uint32_t offset, size_t size)
    {
        auto& self = *static_cast<SyntheticReader*>(context);
        if (offset > self.bytes.size() || size > self.bytes.size() - offset)
            throw std::runtime_error("synthetic region escaped fixture");
        self.region_root = offset;
        self.region_size = size;
        return self.bytes.data() + offset;
    }

    static void* allocate(void* context, size_t count, size_t size)
    {
        auto& self = *static_cast<SyntheticReader*>(context);
        void* result = std::calloc(count, size);
        if (!result)
            throw std::bad_alloc();
        self.allocations.push_back(result);
        return result;
    }

    static void reject(void* context, const char* message)
    {
        auto& self = *static_cast<SyntheticReader*>(context);
        self.rejection = message ? message : "";
        throw Rejected();
    }

    MeleeWebNativeDat api()
    {
        return {this, word, half, byte, pointer, region, allocate, reject, nullptr};
    }

    void put_half(uint32_t offset, int16_t value)
    {
        const uint16_t bits = static_cast<uint16_t>(value);
        if (offset + 2 > bytes.size())
            throw std::runtime_error("synthetic half write escaped fixture");
        bytes[offset] = uint8_t(bits >> 8);
        bytes[offset + 1] = uint8_t(bits);
    }

    void put_word(uint32_t offset, uint32_t value)
    {
        if (offset + 4 > bytes.size())
            throw std::runtime_error("synthetic word write escaped fixture");
        bytes[offset] = uint8_t(value >> 24);
        bytes[offset + 1] = uint8_t(value >> 16);
        bytes[offset + 2] = uint8_t(value >> 8);
        bytes[offset + 3] = uint8_t(value);
    }

    ~SyntheticReader()
    {
        for (void* allocation : allocations)
            std::free(allocation);
    }
};

static uint32_t float_bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static void put_float(SyntheticReader& reader, uint32_t offset, float value)
{
    reader.put_word(offset, float_bits(value));
}

static void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static SyntheticReader fixture()
{
    SyntheticReader reader;
    reader.bytes.resize(reader.root + 0x1c);
    reader.put_half(reader.root + 0x00, 120);
    reader.put_half(reader.root + 0x02, 180);
    put_float(reader, reader.root + 0x04, 0.15f);
    put_float(reader, reader.root + 0x08, 0.15f);
    put_float(reader, reader.root + 0x0c, 6.0f);
    reader.put_half(reader.root + 0x10, 30);
    reader.put_half(reader.root + 0x12, 30);
    reader.put_half(reader.root + 0x14, 3000);
    reader.put_half(reader.root + 0x16, 4000);
    reader.put_half(reader.root + 0x18, 30);
    return reader;
}

static void exact_synthetic_payload()
{
    SyntheticReader reader = fixture();
    MeleeWebNativeDat api = reader.api();
    auto* decoded = static_cast<MeleeWebOldYoshiYakumono*>(
        melee_web_old_yoshi_yakumono_decode(&api, reader.root));
    check(decoded != nullptr, "decoder did not publish Old Yoshi payload");
    check(reader.region_root == reader.root && reader.region_size == 0x1c,
          "decoder claimed the wrong Old Yoshi source region");
    check(decoded->x0 == 120 && decoded->x2 == 180 &&
              decoded->x4 == 0.15f && decoded->x8 == 0.15f &&
              decoded->xC == 6.0f && decoded->x10 == 30 &&
              decoded->x12 == 30 && decoded->x14 == 3000 &&
              decoded->x16 == 4000 && decoded->x18 == 30 &&
              decoded->_final_padding == 0,
          "decoder changed Old Yoshi scalar offsets or types");
}

static void argument_order_is_not_constrained()
{
    SyntheticReader reader = fixture();
    reader.put_half(reader.root + 0x14, 4000);
    reader.put_half(reader.root + 0x16, 3000);
    MeleeWebNativeDat api = reader.api();
    auto* decoded = static_cast<MeleeWebOldYoshiYakumono*>(
        melee_web_old_yoshi_yakumono_decode(&api, reader.root));
    check(decoded && decoded->x14 == 4000 && decoded->x16 == 3000,
          "decoder imposed an authored order on source rand_range bounds");
}

static void source_bounds_reject()
{
    {
        SyntheticReader reader = fixture();
        reader.put_half(reader.root + 0x00, -1);
        MeleeWebNativeDat api = reader.api();
        bool rejected = false;
        try { (void) melee_web_old_yoshi_yakumono_decode(&api, reader.root); }
        catch (const Rejected&) { rejected = true; }
        check(rejected && reader.rejection.find("counter") != std::string::npos,
              "negative source counter was accepted");
    }
    {
        SyntheticReader reader = fixture();
        put_float(reader, reader.root + 0x08, 0.0f);
        MeleeWebNativeDat api = reader.api();
        bool rejected = false;
        try { (void) melee_web_old_yoshi_yakumono_decode(&api, reader.root); }
        catch (const Rejected&) { rejected = true; }
        check(rejected && reader.rejection.find("motion") != std::string::npos,
              "zero acceleration step was accepted");
    }
}

static void nonfinite_rejected()
{
    SyntheticReader reader = fixture();
    reader.put_word(reader.root + 0x04, UINT32_C(0x7fc00001));
    MeleeWebNativeDat api = reader.api();
    bool rejected = false;
    try { (void) melee_web_old_yoshi_yakumono_decode(&api, reader.root); }
    catch (const Rejected&) { rejected = true; }
    check(rejected && reader.rejection.find("nonfinite") != std::string::npos,
          "decoder accepted a nonfinite source float");
}

static uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& item : archive.public_symbols())
        if (item.name == name)
            return item.data_offset;
    throw std::runtime_error(std::string("missing symbol: ") + name);
}

static bool near(float actual, float expected)
{
    return std::isfinite(actual) && std::fabs(actual - expected) < 0.000001f;
}

static void real_archive(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "cannot open owned GrOy.dat");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    const auto original = bytes;
    auto archive = std::make_shared<DatArchive>(bytes);
    check(bytes == original, "DAT constructor changed caller-owned bytes");
    const uint32_t root = symbol(*archive, "yakumono_param");
    check(archive->next_target_offset(root) - root == 0x1c,
          "GrOy yakumono public extent changed");
    check(archive->range(root, 0x1c).size() == 0x1c,
          "GrOy yakumono source region is not readable");

    NativeDatArena arena(archive);
    auto* decoded = static_cast<MeleeWebOldYoshiYakumono*>(
        melee_web_old_yoshi_yakumono_decode(arena.reader(), root));
    check(decoded && decoded->x0 == 120 && decoded->x2 == 180 &&
              near(decoded->x4, 0.15f) && near(decoded->x8, 0.15f) &&
              near(decoded->xC, 6.0f) && decoded->x10 == 30 &&
              decoded->x12 == 30 && decoded->x14 == 3000 &&
              decoded->x16 == 4000 && decoded->x18 == 30 &&
              decoded->_final_padding == 0,
          "GrOy real yakumono values changed");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        static_assert(sizeof(MeleeWebOldYoshiYakumono) == 0x1c,
                      "test must use the checked Old Yoshi ABI size");
        exact_synthetic_payload();
        argument_order_is_not_constrained();
        source_bounds_reject();
        nonfinite_rejected();
        check(melee_web_old_yoshi_yakumono_decode(nullptr, 0) == nullptr,
              "null reader was not rejected safely");
        check(argc == 2, "expected owned GrOy.dat");
        real_archive(argv[1]);
        std::cout << "Yoshi's Island 64 exact 28-byte yakumono ABI, source bounds, "
                     "rand_range order, and real GrOy values passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
