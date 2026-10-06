#include "fixed_format_writer.hpp"

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr unsigned char kSentinel = 0xA5;
int failures = 0;

void expect(bool condition, const char* label, size_t size) {
    if (!condition) {
        std::fprintf(stderr, "FAIL %s at buffer size %zu\n", label, size);
        ++failures;
    }
}

// The observer shape: one fragment per field, with literal-only, string,
// signed, unsigned, 64-bit, size_t, fixed and general float conversions.
int observer_fragments(char* buffer, size_t size, unsigned frame, int delta, double ms,
                       float position, unsigned long long mask, size_t bytes, const char* flag) {
    melee_web::FixedFormatWriter out(buffer, size);
    out.add("{");
    out.add("\"frame\":%u", frame);
    out.add(",\"delta\":%d", delta);
    out.add(",\"ms\":%.3f", ms);
    out.add(",\"x\":%.9g", position);
    out.add(",\"mask_hex\":\"%016llx\"", mask);
    out.add(",\"bytes\":%zu", bytes);
    out.add(",\"pair\":[%d,%d]", delta, -delta);
    out.add(",\"ready\":%s", flag);
    out.add(",\"phases\":{");
    out.add("\"empty\":%s", "");
    out.add("}");
    out.add("}");
    return out.result();
}

int observer_single(char* buffer, size_t size, unsigned frame, int delta, double ms,
                    float position, unsigned long long mask, size_t bytes, const char* flag) {
    return std::snprintf(buffer, size,
        "{\"frame\":%u,\"delta\":%d,\"ms\":%.3f,\"x\":%.9g,\"mask_hex\":\"%016llx\","
        "\"bytes\":%zu,\"pair\":[%d,%d],\"ready\":%s,\"phases\":{\"empty\":%s}}",
        frame, delta, ms, position, mask, bytes, delta, -delta, flag, "");
}

void compare_all_sizes(unsigned frame, int delta, double ms, float position,
                       unsigned long long mask, size_t bytes, const char* flag) {
    char probe[1];
    const int length = observer_single(probe, 0, frame, delta, ms, position, mask, bytes, flag);
    expect(length > 0, "reference length", 0);
    // Every size from none through a little past the full text, so every
    // fragment boundary meets the end of the buffer at least once.
    for (size_t size = 0; size <= static_cast<size_t>(length) + 3; ++size) {
        std::vector<unsigned char> single(size + 8, kSentinel);
        std::vector<unsigned char> fragments(size + 8, kSentinel);
        char* single_buffer = size ? reinterpret_cast<char*>(single.data()) : nullptr;
        char* fragment_buffer = size ? reinterpret_cast<char*>(fragments.data()) : nullptr;
        const int single_result = observer_single(single_buffer, size, frame, delta, ms, position,
                                                  mask, bytes, flag);
        const int fragment_result = observer_fragments(fragment_buffer, size, frame, delta, ms,
                                                       position, mask, bytes, flag);
        expect(single_result == length, "single result", size);
        expect(fragment_result == single_result, "fragment result equals snprintf", size);
        // Compare every byte, including untouched bytes past the terminator.
        expect(single == fragments, "fragment bytes equal snprintf", size);
    }
}

void untouched_until_first_add() {
    std::array<char, 8> buffer;
    buffer.fill('x');
    melee_web::FixedFormatWriter out(buffer.data(), buffer.size());
    expect(buffer[0] == 'x' && out.result() == 0, "constructor leaves buffer untouched", buffer.size());
    out.add("%s", "");
    expect(buffer[0] == '\0' && buffer[1] == 'x', "empty fragment terminates like snprintf", buffer.size());
}

}  // namespace

int main() {
    compare_all_sizes(17, -3, 12.3456, 1.25f, 0x00ff00ff00ff00ffULL, 4096, "true");
    compare_all_sizes(4294967295u, -2147483647 - 1, -0.0005, -123456.789f, ~0ULL,
                      static_cast<size_t>(-1), "false");
    compare_all_sizes(0, 0, 0.0, 0.0f, 0, 0, "");
    untouched_until_first_add();
    if (failures) {
        std::fprintf(stderr, "%d fixed-format writer checks failed\n", failures);
        return 1;
    }
    std::puts("fixed-format writer matches single snprintf for every buffer size");
    return 0;
}
