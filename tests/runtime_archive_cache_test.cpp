#include "runtime_archive_cache.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {
void put32(std::vector<std::uint8_t>& bytes, std::size_t offset,
           std::uint32_t value)
{
    bytes[offset] = value >> 24;
    bytes[offset + 1] = value >> 16;
    bytes[offset + 2] = value >> 8;
    bytes[offset + 3] = value;
}
}

int main()
{
    melee_web::RuntimeFiles files;
    auto& bytes = files["empty.dat"];
    bytes.resize(32);
    put32(bytes, 0, bytes.size());

    melee_web::RuntimeArchiveCache cache(files);
    const auto first = cache.archive("empty.dat");
    const auto second = cache.archive("empty.dat");
    assert(first == second);
    cache.verify(first);

    const auto null_external = cache.archive(
        "empty.dat", melee_web::DatExternalPolicy::ResolveNull);
    assert(null_external != first);
    cache.verify(null_external);

    bytes.push_back(0);
    bool rejected = false;
    try {
        (void) cache.archive("empty.dat");
    } catch (const melee_web::DatError&) {
        rejected = true;
    }
    assert(rejected);
}
