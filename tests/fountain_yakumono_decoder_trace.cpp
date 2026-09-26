#include "gameplay_stage_fountain.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Rejected : std::exception {
    const char* what() const noexcept override { return "decoder rejected"; }
};

struct Reader {
    std::vector<uint8_t> bytes;
    uint32_t root = 0x20;
    uint32_t region_root = 0;
    size_t region_size = 0;
    std::vector<void*> allocations;
    std::string rejection;

    static uint32_t word(void* context, uint32_t offset)
    {
        auto& self = *static_cast<Reader*>(context);
        if ((offset & 3) != 0 || offset < self.root ||
            offset + 4 > self.bytes.size())
            throw std::runtime_error("synthetic word read escaped fixture");
        return (uint32_t(self.bytes[offset]) << 24) |
               (uint32_t(self.bytes[offset + 1]) << 16) |
               (uint32_t(self.bytes[offset + 2]) << 8) |
               uint32_t(self.bytes[offset + 3]);
    }

    static uint16_t half(void*, uint32_t)
    {
        throw std::runtime_error("synthetic half read was unexpected");
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
        auto& self = *static_cast<Reader*>(context);
        if (offset > self.bytes.size() || size > self.bytes.size() - offset)
            throw std::runtime_error("synthetic region escaped fixture");
        self.region_root = offset;
        self.region_size = size;
        return self.bytes.data() + offset;
    }

    static void* allocate(void* context, size_t count, size_t size)
    {
        auto& self = *static_cast<Reader*>(context);
        void* result = std::calloc(count, size);
        if (!result)
            throw std::bad_alloc();
        self.allocations.push_back(result);
        return result;
    }

    static void reject(void* context, const char* message)
    {
        auto& self = *static_cast<Reader*>(context);
        self.rejection = message ? message : "";
        throw Rejected();
    }

    MeleeWebNativeDat api()
    {
        return MeleeWebNativeDat{
            .context = this,
            .word = word,
            .half = half,
            .byte = byte,
            .pointer = pointer,
            .region = region,
            .source_region = nullptr,
            .allocate = allocate,
            .reject = reject,
            .extent = nullptr,
        };
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

    ~Reader()
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

static void put_float(Reader& reader, uint32_t offset, float value)
{
    uint32_t bits = float_bits(value);
    reader.put_word(offset, bits);
}

static void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static Reader fixture()
{
    Reader reader;
    reader.bytes.resize(reader.root + 0x54);
    put_float(reader, reader.root + 0x00, 20.0f);
    reader.put_word(reader.root + 0x04, UINT32_C(0xffffffff));
    const float values[] = {28.0f, 25.0f, 25.0f, 25.0f, 5.0f,
                            10.0f, 35.0f, 15.0f, 0.15f, 0.1f,
                            0.25f, 0.25f, 600.0f, 1080.0f, 4.0f,
                            10.0f, 8.0f, 480.0f, 1680.0f};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        put_float(reader, reader.root + 0x08 + 4 * i, values[i]);
    return reader;
}

static void exact_payload()
{
    Reader reader = fixture();
    MeleeWebNativeDat api = reader.api();
    auto* decoded = static_cast<MeleeWebFountainYakumono*>(
        melee_web_fountain_yakumono_decode(&api, reader.root));
    check(decoded != nullptr, "decoder did not publish scalar payload");
    check(reader.region_root == reader.root && reader.region_size == 0x54,
          "decoder claimed the wrong source ABI region");
    check(decoded->x0 == 20.0f && decoded->x4 == -1 &&
              decoded->x8 == 28.0f && decoded->xC == 25.0f &&
              decoded->x10 == 25.0f && decoded->x14 == 25.0f &&
              decoded->x18 == 5.0f && decoded->x1C == 10.0f &&
              decoded->x20 == 35.0f && decoded->x24 == 15.0f &&
              decoded->x28 == 0.15f && decoded->x2C == 0.1f &&
              decoded->x30 == 0.25f && decoded->x34 == 0.25f &&
              decoded->x38 == 600.0f && decoded->x3C == 1080.0f &&
              decoded->x40 == 4.0f && decoded->x44 == 10.0f &&
              decoded->x48 == 8.0f && decoded->x4C == 480.0f &&
              decoded->x50 == 1680.0f,
          "decoder changed source scalar offsets or types");
}

static void nonfinite_rejected()
{
    Reader reader = fixture();
    reader.put_word(reader.root + 0x30, UINT32_C(0x7fc00001));
    MeleeWebNativeDat api = reader.api();
    bool rejected = false;
    try {
        (void) melee_web_fountain_yakumono_decode(&api, reader.root);
    } catch (const Rejected&) {
        rejected = true;
    }
    check(rejected && reader.rejection.find("nonfinite") != std::string::npos,
          "decoder accepted a nonfinite source float");
}

static void null_reader_is_safe()
{
    check(melee_web_fountain_yakumono_decode(nullptr, 0) == nullptr,
          "null reader was not rejected safely");
}

} // namespace

int main()
{
    try {
        static_assert(sizeof(MeleeWebFountainYakumono) == 0x54,
                      "test must use the checked source ABI size");
        exact_payload();
        nonfinite_rejected();
        null_reader_is_safe();
        std::cout << "Fountain of Dreams yakumono 0x54-byte source ABI, signed x4, "
                     "nonfinite rejection and null reader boundary passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
