// Use the pinned Aurora source provider in this allocation-only executable.
// The fixture needs the GameCube 32-byte source arena invariant, while the
// generic Wasm32 calloc provider promises only the C ABI alignment.  Routing
// this one source call through an aligned zeroing helper keeps the source
// OSMemory.cpp control flow and symbols intact and leaves the shared Aurora
// provider untouched for every other target.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>

extern "C" void* melee_original_startup_calloc(std::size_t count, std::size_t size)
{
    if (count != 1 || size > std::numeric_limits<std::size_t>::max() - 31) {
        return nullptr;
    }
    void* raw = std::calloc(1, size + 31);
    if (raw == nullptr) {
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto aligned = (address + 31U) & ~static_cast<std::uintptr_t>(31U);
    return reinterpret_cast<void*>(aligned);
}

#define calloc melee_original_startup_calloc
#include "../.deps/aurora/lib/dolphin/os/OSMemory.cpp"
#undef calloc
