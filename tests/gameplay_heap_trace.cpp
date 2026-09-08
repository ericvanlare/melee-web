#include "gameplay_heap.h"
#include <aurora/aurora.h>
#include <dolphin/os/OSAlloc.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The real Aurora logger's process configuration; no allocator substitutes.
namespace aurora { AuroraConfig g_config{}; }
namespace {
char error[160];
unsigned visits;
constexpr size_t arena_bytes = 65536;
void check(bool condition, const char* description)
{
    if (!condition) { std::fprintf(stderr, "%s: %s\n", description, error); std::exit(1); }
}
void visitor(void*, u32) { ++visits; }
void owned_lifetime()
{
    check(melee_web_gameplay_heap_available(), "fresh SDK allocator is available");
    check(!melee_web_gameplay_heap_initialize(nullptr, arena_bytes, error, sizeof(error)), "null arena rejected");
    alignas(32) unsigned char small[128]{};
    check(!melee_web_gameplay_heap_initialize(small + 1, sizeof(small)-1, error, sizeof(error)), "misaligned arena rejected");
    check(!melee_web_gameplay_heap_initialize(small, 1, error, sizeof(error)), "undersized arena rejected");
    check(melee_web_gameplay_heap_available(), "failed claims preserve readiness");
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        void* arena = std::malloc(arena_bytes);
        check(arena != nullptr, "real arena allocation");
        const auto arena_address = reinterpret_cast<uintptr_t>(arena);
        const auto end = arena_address + arena_bytes;
        void* start = melee_web_gameplay_heap_initialize(arena, arena_bytes, error, sizeof(error));
        check(start && !melee_web_gameplay_heap_available(), "owned initialized allocator is occupied");
        check(melee_web_gameplay_heap_owns(arena), "claimed arena identity is recognized");
        check(!melee_web_gameplay_heap_initialize(arena, arena_bytes, error, sizeof(error)), "double initialization rejected");
        const OSHeapHandle heap = OSCreateHeap(start, reinterpret_cast<void*>(end));
        check(heap == 0 && __OSCurrHeap == -1, "original SDK creates an unselected heap");
        const s32 initial_free = OSCheckHeap(heap);
        check(initial_free > 0, "real heap initially validates");
        check(!melee_web_gameplay_heap_release(arena, error, sizeof(error)), "live unselected heap blocks release");
        check(OSCheckHeap(heap) == initial_free, "rejected release preserves the heap");
        OSSetCurrentHeap(heap);
        void* allocation = OSAllocFromHeap(heap, 100);
        check(allocation && OSReferentSize(allocation) >= 100, "original allocation and cell metadata");
        const uintptr_t stale_address = reinterpret_cast<uintptr_t>(allocation);
        check(!melee_web_gameplay_heap_release(reinterpret_cast<void*>(end), error, sizeof(error)), "wrong arena blocks release");
        visits = 0; OSVisitAllocated(visitor); check(visits == 1, "live allocation visitor");
        OSFreeToHeap(heap, allocation);
        check(OSCheckHeap(heap) == initial_free, "original free/coalescing restores the heap");
        OSDestroyHeap(heap);
        check(__OSCurrHeap == -1 && !melee_web_gameplay_heap_available(), "destroyed heap retains owned metadata until release");
        check(melee_web_gameplay_heap_release(arena, error, sizeof(error)), "destroyed owned arena releases");
        std::free(arena);
        check(melee_web_gameplay_heap_available(), "release clears SDK roots before arena free");
        check(!melee_web_gameplay_heap_owns(reinterpret_cast<void*>(arena_address)), "released arena has no owner");
        check(OSCheckHeap(heap) == -1 && OSAllocFromHeap(heap, 32) == nullptr, "stale heap handle is invalid after free");
        check(OSReferentSize(reinterpret_cast<void*>(stale_address)) == 0, "stale allocation query does not inspect freed metadata");
        OSFreeToHeap(heap, reinterpret_cast<void*>(stale_address));
        check(OSSetCurrentHeap(heap) == -1 && __OSCurrHeap == -1, "cannot select a stale heap");
        check(OSCreateHeap(reinterpret_cast<void*>(stale_address), reinterpret_cast<void*>(end)) == -1, "cannot create in a freed arena");
        check(OSAllocFixed(reinterpret_cast<void*>(stale_address), reinterpret_cast<void*>(end)) == nullptr, "fixed allocation rejects absent arena");
        visits = 0; OSVisitAllocated(visitor); check(visits == 0, "no heap visitor follows freed metadata");
        check(!melee_web_gameplay_heap_release(reinterpret_cast<void*>(stale_address), error, sizeof(error)), "unowned release rejected");
    }
}
void foreign_allocator(bool create_heap)
{
    alignas(32) static unsigned char foreign[arena_bytes], candidate[arena_bytes];
    void* start = OSInitAlloc(foreign, foreign + arena_bytes, 1);
    check(start != nullptr && __OSCurrHeap == -1, "external allocator initialized without selecting a heap");
    OSHeapHandle heap = create_heap ? OSCreateHeap(start, foreign + arena_bytes) : -1;
    check(!melee_web_gameplay_heap_available(), "external allocator metadata is occupied even without current heap");
    check(!melee_web_gameplay_heap_owns(foreign), "external arena is not owned");
    check(!melee_web_gameplay_heap_initialize(candidate, arena_bytes, error, sizeof(error)), "external allocator cannot be overwritten");
    check(!melee_web_gameplay_heap_release(foreign, error, sizeof(error)), "external allocator cannot be released by gameplay");
    if (!create_heap) heap = OSCreateHeap(start, foreign + arena_bytes);
    check(heap == 0 && OSCheckHeap(heap) > 0 && __OSCurrHeap == -1, "rejections preserve external unselected heap");
    OSDestroyHeap(heap);
    check(!melee_web_gameplay_heap_available(), "external allocator metadata remains externally owned after destroy");
}
void changed_identity()
{
    alignas(32) static unsigned char owned[arena_bytes], replacement[arena_bytes];
    check(melee_web_gameplay_heap_initialize(owned, arena_bytes, error, sizeof(error)), "claim initial arena");
    void* start = OSInitAlloc(replacement, replacement + arena_bytes, 1);
    check(start != nullptr, "simulate direct external SDK reinitialization");
    check(!melee_web_gameplay_heap_owns(owned), "identity query detects replacement before SDK operations");
    check(!melee_web_gameplay_heap_release(owned, error, sizeof(error)), "ownership identity loss refuses reset");
    check(OSCreateHeap(start, replacement + arena_bytes) == 0, "failed release does not clear replacement metadata");
    check(OSCheckHeap(0) > 0, "replacement heap remains valid");
}
}
int main(int argc, char** argv)
{
    check(argc == 2, "select one lifetime case");
    if (!std::strcmp(argv[1], "owned")) owned_lifetime();
    else if (!std::strcmp(argv[1], "foreign_initialized")) foreign_allocator(false);
    else if (!std::strcmp(argv[1], "foreign_heap")) foreign_allocator(true);
    else if (!std::strcmp(argv[1], "changed_identity")) changed_identity();
    else check(false, "unknown lifetime case");
    std::puts("Original SDK allocator ownership trace: passed");
}
