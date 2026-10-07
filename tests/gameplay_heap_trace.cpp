#include "gameplay_heap.h"
#include "gameplay_source_memory_runtime.h"
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
        MeleeWebSourceMemoryContext context_before{};
        check(melee_web_source_memory_context_read(nullptr) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT,
              "null source-memory context output is rejected without changing ownership");
        check(melee_web_source_memory_context_read(&context_before) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE,
              "inactive source-memory context is reported without activation");
        check(melee_web_source_memory_begin(0x100u + cycle, heap),
              "checked source-memory context begins on the selected source heap");
        check(melee_web_source_memory_context_read(&context_before) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  context_before.source_heap_handle == heap &&
                  context_before.world_generation == 0x100u + cycle,
              "source-memory context reports its exact heap and world owner");
        MeleeWebSourceMemoryAllocation allocation_before{};
        check(melee_web_source_memory_allocation_read(nullptr, &allocation_before) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT,
              "null allocation payload is rejected without changing ownership");
        unsigned char foreign_payload[16]{};
        check(melee_web_source_memory_allocation_read(foreign_payload, nullptr) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT,
              "null allocation output is rejected without changing ownership");
        check(melee_web_source_memory_allocation_read(
                  foreign_payload, &allocation_before) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  !allocation_before.live &&
                  allocation_before.source_heap_handle == heap &&
                  allocation_before.world_generation == 0x100u + cycle,
              "foreign payload is reported as not-live in the valid context");
        void* allocation = OSAllocFromHeap(heap, 100);
        check(allocation && OSReferentSize(allocation) >= 100, "original allocation and cell metadata");
        MeleeWebSourceMemoryAllocation first_lease{};
        check(melee_web_source_memory_allocation_read(allocation, &first_lease) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK && first_lease.live &&
                  first_lease.requested_bytes == 100 &&
                  first_lease.source_heap_handle == heap &&
                  first_lease.world_generation == 0x100u + cycle &&
                  first_lease.allocation_generation >
                      context_before.allocation_generation_watermark,
              "exact payload observation reports requested bytes and fresh generation");
        MeleeWebSourceMemoryAllocation interior_lease{};
        check(melee_web_source_memory_allocation_read(
                  static_cast<unsigned char*>(allocation) + 1,
                  &interior_lease) == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  !interior_lease.live && !interior_lease.allocation_generation,
              "interior payload is not an exact allocation lease");
        const uintptr_t stale_address = reinterpret_cast<uintptr_t>(allocation);
        check(!melee_web_gameplay_heap_release(reinterpret_cast<void*>(end), error, sizeof(error)), "wrong arena blocks release");
        visits = 0; OSVisitAllocated(visitor); check(visits == 1, "live allocation visitor");
        OSFreeToHeap(heap, allocation);
        MeleeWebSourceMemoryAllocation stale_lease{};
        check(melee_web_source_memory_allocation_read(
                  reinterpret_cast<void*>(stale_address), &stale_lease) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK && !stale_lease.live &&
                  !stale_lease.requested_bytes &&
                  !stale_lease.allocation_generation,
              "freed exact payload is reported as not-live");
        MeleeWebSourceMemoryContext after_first_free{};
        check(melee_web_source_memory_context_read(&after_first_free) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  after_first_free.allocation_generation_watermark ==
                      first_lease.allocation_generation &&
                  melee_web_source_memory_healthy(),
              "read-only negative queries preserve health and generation watermark");
        void* reused = OSAllocFromHeap(heap, 100);
        check(reinterpret_cast<uintptr_t>(reused) == stale_address,
              "original SDK allocator reuses the exact freed host payload");
        MeleeWebSourceMemoryAllocation reused_lease{};
        check(melee_web_source_memory_allocation_read(reused, &reused_lease) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK && reused_lease.live &&
                  reused_lease.requested_bytes == 100 &&
                  reused_lease.allocation_generation >
                      first_lease.allocation_generation,
              "reused exact payload receives a newer allocation generation");
        OSFreeToHeap(heap, reused);
        MeleeWebSourceMemoryContext before_end{};
        check(melee_web_source_memory_context_read(&before_end) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  before_end.allocation_generation_watermark ==
                      reused_lease.allocation_generation,
              "freeing a reused payload preserves its latest generation watermark");
        check(melee_web_source_memory_end(0x100u + cycle),
              "source-memory context ends after exact payloads are gone");
        check(melee_web_source_memory_context_read(&after_first_free) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE,
              "ended source-memory context is reported as inactive");
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

void unhealthy_observer()
{
    alignas(32) static unsigned char arena[arena_bytes];
    void* start = melee_web_gameplay_heap_initialize(
        arena, arena_bytes, error, sizeof(error));
    check(start != nullptr, "claim arena for unhealthy observer control");
    const OSHeapHandle heap = OSCreateHeap(start, arena + arena_bytes);
    check(heap == 0, "create source heap for unhealthy observer control");
    OSSetCurrentHeap(heap);
    check(melee_web_source_memory_begin(0x200, heap),
          "begin source-memory context for unhealthy observer control");
    MeleeWebSourceMemoryContext before{};
    check(melee_web_source_memory_context_read(&before) ==
              MELEE_WEB_SOURCE_MEMORY_READ_OK,
          "read healthy source-memory control context");
    check(!melee_web_source_memory_alloc(heap, nullptr, 1),
          "invalid allocator event creates the unhealthy control");
    MeleeWebSourceMemoryContext rejected_context{};
    check(melee_web_source_memory_context_read(&rejected_context) ==
              MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY &&
              !melee_web_source_memory_healthy(),
          "unhealthy context observation reports status without healing or worsening it");
    unsigned char payload[8]{};
    MeleeWebSourceMemoryAllocation rejected_allocation{};
    check(melee_web_source_memory_allocation_read(payload,
                                                  &rejected_allocation) ==
              MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY,
          "allocation observation rejects an unhealthy context");
    check(!melee_web_source_memory_end(0x200),
          "unhealthy source-memory context remains failed through teardown");
    OSSetCurrentHeap(-1);
    OSDestroyHeap(heap);
    int recreated = -1;
    check(melee_web_gameplay_heap_recreate(arena, &recreated, error,
                                          sizeof(error)) && recreated == 0,
          "recreate empty heap for read-only generation-watermark check");
    OSSetCurrentHeap(recreated);
    check(melee_web_source_memory_begin(0x201, recreated),
          "restart source-memory context after unhealthy control");
    MeleeWebSourceMemoryContext after{};
    check(melee_web_source_memory_context_read(&after) ==
              MELEE_WEB_SOURCE_MEMORY_READ_OK &&
              after.allocation_generation_watermark ==
                  before.allocation_generation_watermark,
          "rejected observations do not consume an allocation generation");
    check(melee_web_source_memory_end(0x201),
          "end restarted source-memory context");
    OSSetCurrentHeap(-1);
    OSDestroyHeap(recreated);
    check(melee_web_gameplay_heap_release(arena, error, sizeof(error)),
          "release arena after unhealthy observer control");
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
    else if (!std::strcmp(argv[1], "unhealthy_observer")) unhealthy_observer();
    else check(false, "unknown lifetime case");
    std::puts("Original SDK allocator ownership trace: passed");
}
