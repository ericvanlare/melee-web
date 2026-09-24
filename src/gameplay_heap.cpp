#include "gameplay_heap.h"

/* The isolated gameplay target substitutes this TU for Aurora's OSAlloc TU.
 * All SDK implementations remain original. Sharing their TU allows an explicit
 * ownership/reset boundary without exporting or guessing private heap layouts. */
#include "../.deps/aurora/lib/dolphin/os/OSAlloc.cpp"

#include <cstdio>
#include <limits>

namespace {
void* gameplay_owned_arena = nullptr;
u8* gameplay_owned_start = nullptr;
u8* gameplay_owned_end = nullptr;

int heap_failure(char* error, size_t size, const char* reason)
{
    if (error && size) std::snprintf(error, size, "%s", reason);
    return 0;
}
void clear_error(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
}
}

extern "C" int melee_web_gameplay_heap_available(void)
{
    return gameplay_owned_arena == nullptr && sHeapArray == nullptr &&
           sNumHeaps == 0 && sArenaStart == nullptr && sArenaEnd == nullptr &&
           __OSCurrHeap == -1;
}

extern "C" int melee_web_gameplay_heap_owns(const void* expected_arena)
{
    return expected_arena && expected_arena == gameplay_owned_arena &&
           sHeapArray == expected_arena && sNumHeaps == 1 &&
           sArenaStart == gameplay_owned_start && sArenaEnd == gameplay_owned_end;
}

extern "C" void* melee_web_gameplay_heap_initialize(void* arena, size_t bytes,
                                                    char* error, size_t error_size)
{
    if (!melee_web_gameplay_heap_available()) {
        heap_failure(error, error_size, "SDK allocator already has arena metadata or a gameplay owner");
        return nullptr;
    }
    const auto address = reinterpret_cast<uintptr_t>(arena);
    if (!arena || address % alignof(HeapDesc) != 0 || bytes < 128 ||
        bytes > 64U * 1024U * 1024U || bytes > std::numeric_limits<uintptr_t>::max() - address) {
        heap_failure(error, error_size, "Gameplay arena must be aligned and between 128 bytes and 64 MiB");
        return nullptr;
    }
    void* start = OSInitAlloc(arena, reinterpret_cast<void*>(address + bytes), 1);
    if (!start) {
        heap_failure(error, error_size, "Original SDK allocator could not initialize the arena");
        return nullptr;
    }
    gameplay_owned_arena = arena;
    gameplay_owned_start = sArenaStart;
    gameplay_owned_end = sArenaEnd;
    clear_error(error, error_size);
    return start;
}

extern "C" int melee_web_gameplay_heap_release(void* expected_arena,
                                               char* error, size_t error_size)
{
    if (!expected_arena || expected_arena != gameplay_owned_arena)
        return heap_failure(error, error_size, "Gameplay does not own the expected SDK arena");
    // Compare identity before dereferencing SDK metadata, which may have been
    // replaced by an unauthorized external OSInitAlloc call.
    if (!melee_web_gameplay_heap_owns(expected_arena))
        return heap_failure(error, error_size, "SDK arena identity changed while gameplay owned it");
    for (int i = 0; i < sNumHeaps; ++i) {
        if (sHeapArray[i].size >= 0 || sHeapArray[i].freeList || sHeapArray[i].allocated)
            return heap_failure(error, error_size, "Destroy every SDK heap before releasing its arena");
    }
    if (__OSCurrHeap != -1)
        return heap_failure(error, error_size, "SDK still has a selected heap during arena release");
    // OSDestroyHeap intentionally leaves the allocator descriptor array alive.
    // This wrapper owns its containing allocation and must remove those roots
    // before the caller frees it. No remaining heap payload is traversed.
    sHeapArray = nullptr;
    sNumHeaps = 0;
    sArenaStart = sArenaEnd = nullptr;
    gameplay_owned_arena = nullptr;
    gameplay_owned_start = gameplay_owned_end = nullptr;
    clear_error(error, error_size);
    return 1;
}

extern "C" int melee_web_gameplay_heap_recreate(void* expected_arena,
                                                 int* out_heap, char* error,
                                                 size_t error_size)
{
    if (out_heap) *out_heap = -1;
    if (!expected_arena || expected_arena != gameplay_owned_arena)
        return heap_failure(error, error_size, "Gameplay does not own the expected SDK arena");
    // Compare identity before dereferencing SDK metadata, which may have been
    // replaced by an unauthorized external OSInitAlloc call.
    if (!melee_web_gameplay_heap_owns(expected_arena))
        return heap_failure(error, error_size, "SDK arena identity changed while gameplay owned it");
    if (__OSCurrHeap != -1)
        return heap_failure(error, error_size, "SDK still has a selected heap during arena reset");
    for (int i = 0; i < sNumHeaps; ++i) {
        if (sHeapArray[i].size >= 0 || sHeapArray[i].freeList || sHeapArray[i].allocated)
            return heap_failure(error, error_size, "Destroy every SDK heap before resetting its arena");
    }
    if (!out_heap)
        return heap_failure(error, error_size, "Arena reset requires an output heap handle");
    const OSHeapHandle recreated = OSCreateHeap(sArenaStart, sArenaEnd);
    if (recreated < 0)
        return heap_failure(error, error_size, "Original SDK allocator could not recreate the arena heap");
    *out_heap = recreated;
    clear_error(error, error_size);
    return 1;
}
