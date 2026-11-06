#include "gameplay_heap.h"
#include "gameplay_source_memory_runtime.h"

/* The isolated gameplay target substitutes this TU for Aurora's OSAlloc TU.
 * All SDK implementations remain original. Sharing their TU allows an explicit
 * ownership/reset boundary without exporting or guessing private heap layouts. */
#define OSAllocFromHeap melee_web_gameplay_original_OSAllocFromHeap
#define OSFreeToHeap melee_web_gameplay_original_OSFreeToHeap
#include "../.deps/aurora/lib/dolphin/os/OSAlloc.cpp"
#undef OSAllocFromHeap
#undef OSFreeToHeap

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace {
uint64_t gameplay_owner_epoch = 0;
void* gameplay_owned_arena = nullptr;
u8* gameplay_owned_start = nullptr;
u8* gameplay_owned_end = nullptr;
size_t gameplay_owned_bytes = 0;

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
           gameplay_owned_bytes == 0 && sNumHeaps == 0 &&
           sArenaStart == nullptr && sArenaEnd == nullptr && __OSCurrHeap == -1;
}

/* Track the original allocator's live main-heap requests in source-address
 * space. Host payloads remain ordinary Wasm pointers; only the shadow heap's
 * independently derived GALE01r2 addresses can become source identities. */
extern "C" void* melee_web_gameplay_original_OSAllocFromHeap(OSHeapHandle heap,
                                                              u32 size);
extern "C" void melee_web_gameplay_original_OSFreeToHeap(OSHeapHandle heap,
                                                          void* ptr);

extern "C" void* OSAllocFromHeap(OSHeapHandle heap, u32 size)
{
    void* result = melee_web_gameplay_original_OSAllocFromHeap(heap, size);
    if (result && !melee_web_source_memory_alloc(heap, result, size)) {
        melee_web_gameplay_original_OSFreeToHeap(heap, result);
        return nullptr;
    }
    return result;
}

extern "C" void OSFreeToHeap(OSHeapHandle heap, void* ptr)
{
    if (ptr) (void) melee_web_source_memory_free(heap, ptr);
    melee_web_gameplay_original_OSFreeToHeap(heap, ptr);
}

extern "C" int melee_web_gameplay_heap_owns(const void* expected_arena)
{
    if (!expected_arena || expected_arena != gameplay_owned_arena ||
        !gameplay_owned_bytes ||
        gameplay_owned_bytes > std::numeric_limits<uintptr_t>::max() -
            reinterpret_cast<uintptr_t>(expected_arena))
        return 0;
    const auto expected_end = reinterpret_cast<uintptr_t>(expected_arena) +
                              gameplay_owned_bytes;
    const auto rounded_end = expected_end & ~static_cast<uintptr_t>(31u);
    return sHeapArray == expected_arena && sNumHeaps == 1 &&
           sArenaStart == gameplay_owned_start && sArenaEnd == gameplay_owned_end &&
           reinterpret_cast<uintptr_t>(sArenaEnd) == rounded_end;
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
    if (++gameplay_owner_epoch == 0) ++gameplay_owner_epoch;
    gameplay_owned_arena = arena;
    gameplay_owned_bytes = bytes;
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
    gameplay_owned_bytes = 0;
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
    if (++gameplay_owner_epoch == 0) ++gameplay_owner_epoch;
    *out_heap = recreated;
    clear_error(error, error_size);
    return 1;
}


struct MeleeWebGameplayHeapGuard {
    uint64_t epoch;
    const void* owned_arena;
    u8 *owned_start, *owned_end;
    size_t owned_bytes;
    HeapDesc* heaps;
    int count;
    u8 *arena_start, *arena_end;
    OSHeapHandle selected;
};
extern "C" MeleeWebGameplayHeapGuard* melee_web_gameplay_heap_guard_capture(
    const void* expected_arena,char* error,size_t size)
{
    if (!melee_web_gameplay_heap_owns(expected_arena)) {
        heap_failure(error,size,"Heap guard requires the exact live owned arena");
        return nullptr;
    }
    auto* guard=new(std::nothrow) MeleeWebGameplayHeapGuard{
        gameplay_owner_epoch,gameplay_owned_arena,gameplay_owned_start,gameplay_owned_end,
        gameplay_owned_bytes,sHeapArray,sNumHeaps,sArenaStart,sArenaEnd,__OSCurrHeap};
    if (!guard) {heap_failure(error,size,"Cannot allocate SDK heap-root guard");return nullptr;}
    clear_error(error,size);
    return guard;
}
extern "C" int melee_web_gameplay_heap_guard_admit(
    const MeleeWebGameplayHeapGuard* guard,char* error,size_t size)
{
    if (!guard || guard->epoch!=gameplay_owner_epoch ||
        !melee_web_gameplay_heap_owns(guard->owned_arena) ||
        gameplay_owned_start!=guard->owned_start || gameplay_owned_end!=guard->owned_end ||
        gameplay_owned_bytes!=guard->owned_bytes || sHeapArray!=guard->heaps || sNumHeaps!=guard->count ||
        sArenaStart!=guard->arena_start || sArenaEnd!=guard->arena_end ||
        __OSCurrHeap!=guard->selected)
        return heap_failure(error,size,"SDK heap roots or selected heap changed after checkpoint");
    clear_error(error,size);
    return 1;
}
extern "C" void melee_web_gameplay_heap_guard_destroy(MeleeWebGameplayHeapGuard* guard)
{
    delete guard;
}


struct GameplayHeapHistoryCellRange { uint32_t offset, bytes; };

struct MeleeWebGameplayHeapHistorySnapshot {
    uint64_t epoch = 0;
    void* arena = nullptr;
    size_t arena_bytes = 0;
    u8* arena_start = nullptr;
    u8* arena_end = nullptr;
    HeapDesc* heaps = nullptr;
    int heap_count = 0;
    OSHeapHandle selected = -1;
    uint32_t heap_start_offset = 0;
    uint32_t heap_end_offset = 0;
    std::vector<u8> image;
    mutable std::vector<GameplayHeapHistoryCellRange> validation_scratch;
};

struct MeleeWebGameplayHeapHistoryPrepared {
    const MeleeWebGameplayHeapHistorySnapshot* snapshot = nullptr;
    uint64_t epoch = 0;
    void* arena = nullptr;
    size_t arena_bytes = 0;
    u8* arena_start = nullptr;
    u8* arena_end = nullptr;
    HeapDesc* heaps = nullptr;
    int heap_count = 0;
    OSHeapHandle selected = -1;
    std::vector<u8> live_fence;
    std::vector<GameplayHeapHistoryCellRange> validation_scratch;
    bool admitted = false;
    bool committed = false;
};

namespace {
using HistoryStatus = MeleeWebGameplayHeapHistoryStatus;
using HistorySnapshot = MeleeWebGameplayHeapHistorySnapshot;
using HistoryPrepared = MeleeWebGameplayHeapHistoryPrepared;
using CellRange = GameplayHeapHistoryCellRange;
static_assert(sizeof(MeleeWebGameplayHeapHistoryObservation) == 64,
              "SDK arena observation ABI must remain fixed at 64 bytes");
constexpr uint32_t kHistoryAlignment = 32;
constexpr uint32_t kHistoryMinCellBytes = 64;

bool history_owner_matches(const HistorySnapshot* snapshot)
{
    return snapshot && snapshot->epoch == gameplay_owner_epoch &&
        snapshot->arena == gameplay_owned_arena &&
        snapshot->arena_bytes == gameplay_owned_bytes &&
        snapshot->arena_start == gameplay_owned_start &&
        snapshot->arena_end == gameplay_owned_end &&
        snapshot->heaps == sHeapArray && snapshot->heap_count == sNumHeaps &&
        snapshot->selected == __OSCurrHeap && snapshot->selected == 0 &&
        melee_web_gameplay_heap_owns(snapshot->arena);
}

bool history_owner_matches(const HistoryPrepared* prepared)
{
    return prepared && prepared->epoch == gameplay_owner_epoch &&
        prepared->arena == gameplay_owned_arena &&
        prepared->arena_bytes == gameplay_owned_bytes &&
        prepared->arena_start == gameplay_owned_start &&
        prepared->arena_end == gameplay_owned_end &&
        prepared->heaps == sHeapArray && prepared->heap_count == sNumHeaps &&
        prepared->selected == __OSCurrHeap && prepared->selected == 0 &&
        melee_web_gameplay_heap_owns(prepared->arena) &&
        history_owner_matches(prepared->snapshot);
}

bool history_disjoint(const void* storage, size_t storage_bytes,
                      const void* arena, size_t arena_bytes)
{
    const auto s = reinterpret_cast<uintptr_t>(storage);
    const auto a = reinterpret_cast<uintptr_t>(arena);
    if (!storage || !arena || storage_bytes >
            std::numeric_limits<uintptr_t>::max() - s ||
        arena_bytes > std::numeric_limits<uintptr_t>::max() - a)
        return false;
    const auto se = s + storage_bytes;
    const auto ae = a + arena_bytes;
    return se <= a || ae <= s;
}

bool history_pointer_offset(uintptr_t pointer, uintptr_t arena_base,
                            size_t arena_bytes, uint32_t heap_start_offset,
                            uint32_t heap_end_offset, uint32_t* out)
{
    if (!out || pointer < arena_base || pointer - arena_base >= arena_bytes)
        return false;
    const auto offset = pointer - arena_base;
    if (offset < heap_start_offset || offset >= heap_end_offset ||
        (pointer & (kHistoryAlignment - 1u)) != 0 ||
        offset > std::numeric_limits<uint32_t>::max())
        return false;
    *out = static_cast<uint32_t>(offset);
    return true;
}

bool history_inspect_image(const u8* image, size_t arena_bytes,
                           uintptr_t arena_base, uint32_t heap_start_offset,
                           uint32_t heap_end_offset, OSHeapHandle selected,
                           CellRange* ranges, size_t range_capacity,
                           size_t* out_range_count, s32* out_free_bytes)
{
    if (!image || !ranges || !out_range_count || !out_free_bytes || selected != 0 ||
        arena_bytes > 64U * 1024U * 1024U || arena_bytes >
            std::numeric_limits<uintptr_t>::max() - arena_base ||
        heap_start_offset < sizeof(HeapDesc) ||
        heap_start_offset >= heap_end_offset || heap_end_offset > arena_bytes ||
        ((arena_base + heap_start_offset) & (kHistoryAlignment - 1u)) != 0 ||
        ((arena_base + heap_end_offset) & (kHistoryAlignment - 1u)) != 0 ||
        heap_end_offset - heap_start_offset < kHistoryMinCellBytes)
        return false;
    if (sizeof(HeapDesc) > arena_bytes) return false;
    HeapDesc descriptor{};
    std::memcpy(&descriptor, image, sizeof(descriptor));
    if (descriptor.size <= 0 ||
        static_cast<uint64_t>(descriptor.size) >
            static_cast<uint64_t>(heap_end_offset - heap_start_offset))
        return false;

    size_t count = 0;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    const uintptr_t descriptor_address = arena_base;
    auto walk = [&](Cell* head, bool allocated) -> bool {
        uintptr_t current = reinterpret_cast<uintptr_t>(head);
        uintptr_t expected_previous = 0;
        uint32_t previous_offset = 0;
        uint32_t previous_size = 0;
        size_t visits = 0;
        while (current) {
            if (++visits > range_capacity || count >= range_capacity) return false;
            uint32_t offset = 0;
            if (!history_pointer_offset(current, arena_base, arena_bytes,
                    heap_start_offset, heap_end_offset, &offset) ||
                sizeof(Cell) > arena_bytes - offset)
                return false;
            Cell cell{};
            // The links in a copied image are inert integer identities. Decode
            // their offsets, then read the cell bytes from `image`, never from
            // the copied physical pointer value.
            std::memcpy(&cell, image + offset, sizeof(cell));
            if (reinterpret_cast<uintptr_t>(cell.prev) != expected_previous ||
                cell.size < static_cast<s32>(kHistoryMinCellBytes) ||
                (static_cast<uint32_t>(cell.size) & (kHistoryAlignment - 1u)) != 0 ||
                static_cast<uint32_t>(cell.size) > heap_end_offset - offset ||
                reinterpret_cast<uintptr_t>(cell.owner) !=
                    (allocated ? descriptor_address : uintptr_t{0}))
                return false;
            if (!allocated && previous_offset &&
                (offset <= previous_offset ||
                 static_cast<uint64_t>(previous_offset) + previous_size > offset))
                return false;
            ranges[count++] = {offset, static_cast<uint32_t>(cell.size)};
            total_bytes += static_cast<uint32_t>(cell.size);
            if (allocated) {
                // OSAllocFromHeap returns the payload after this exact header.
                if (cell.size <= static_cast<s32>(sizeof(Cell))) return false;
            } else {
                free_bytes += static_cast<uint32_t>(cell.size) - sizeof(Cell);
            }
            if (total_bytes > static_cast<uint32_t>(descriptor.size) ||
                free_bytes > static_cast<uint32_t>(descriptor.size))
                return false;
            expected_previous = current;
            previous_offset = offset;
            previous_size = static_cast<uint32_t>(cell.size);
            current = reinterpret_cast<uintptr_t>(cell.next);
        }
        return true;
    };
    if (!walk(descriptor.allocated, true) || !walk(descriptor.freeList, false) ||
        total_bytes != static_cast<uint32_t>(descriptor.size) ||
        free_bytes > static_cast<uint64_t>(std::numeric_limits<s32>::max()))
        return false;

    std::sort(ranges, ranges + count, [](const CellRange& a, const CellRange& b) {
        return a.offset < b.offset;
    });
    uint64_t previous_end = 0;
    for (size_t i = 0; i < count; ++i) {
        if (ranges[i].offset < previous_end) return false;
        previous_end = static_cast<uint64_t>(ranges[i].offset) + ranges[i].bytes;
    }
    *out_range_count = count;
    *out_free_bytes = static_cast<s32>(free_bytes);
    return count != 0;
}

bool history_make_scratch(size_t arena_bytes, uint32_t start, uint32_t end,
                          std::vector<CellRange>* ranges)
{
    if (!ranges || end <= start || end > arena_bytes) return false;
    const size_t max_cells = (end - start) / kHistoryMinCellBytes;
    if (!max_cells) return false;
    ranges->resize(max_cells);
    return true;
}

bool history_validate_live(const void* arena, size_t arena_bytes, u8* start,
                           u8* end, OSHeapHandle selected,
                           std::vector<CellRange>* ranges)
{
    const auto base = reinterpret_cast<uintptr_t>(arena);
    const auto start_address = reinterpret_cast<uintptr_t>(start);
    const auto end_address = reinterpret_cast<uintptr_t>(end);
    if (!arena || !ranges || start_address < base || end_address <= start_address ||
        end_address - base > arena_bytes || start_address - base >
            std::numeric_limits<uint32_t>::max() || end_address - base >
            std::numeric_limits<uint32_t>::max())
        return false;
    size_t count = 0;
    s32 free_bytes = -1;
    if (!history_inspect_image(static_cast<const u8*>(arena), arena_bytes, base,
            static_cast<uint32_t>(start_address - base),
            static_cast<uint32_t>(end_address - base), selected,
            ranges->data(), ranges->size(), &count, &free_bytes))
        return false;
    // The checked walk above validates every pointer and bounds every chain;
    // only then invoke the original SDK invariant checker over that same graph.
    return OSCheckHeap(selected) == free_bytes;
}

HistoryStatus history_validate_snapshot_image(const HistorySnapshot* snapshot,
                                                std::vector<CellRange>* ranges)
{
    if (!snapshot || !ranges || snapshot->image.size() != snapshot->arena_bytes ||
        !history_disjoint(snapshot->image.data(), snapshot->image.size(),
                          snapshot->arena, snapshot->arena_bytes))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE;
    size_t count = 0;
    s32 free_bytes = -1;
    return history_inspect_image(snapshot->image.data(), snapshot->arena_bytes,
        reinterpret_cast<uintptr_t>(snapshot->arena), snapshot->heap_start_offset,
        snapshot->heap_end_offset, snapshot->selected, ranges->data(),
        ranges->size(), &count, &free_bytes)
        ? MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK
        : MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE;
}
}

extern "C" MeleeWebGameplayHeapHistoryCaptureResult
melee_web_gameplay_heap_history_capture(void)
{
    auto result = MeleeWebGameplayHeapHistoryCaptureResult{
        MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INACTIVE, nullptr};
    if (!gameplay_owned_arena || !gameplay_owned_bytes) return result;
    if (!melee_web_gameplay_heap_owns(gameplay_owned_arena) ||
        sHeapArray != reinterpret_cast<HeapDesc*>(gameplay_owned_arena) ||
        sNumHeaps != 1 || __OSCurrHeap != 0)
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER, nullptr};
    try {
        auto snapshot = std::make_unique<HistorySnapshot>();
        snapshot->epoch = gameplay_owner_epoch;
        snapshot->arena = gameplay_owned_arena;
        snapshot->arena_bytes = gameplay_owned_bytes;
        snapshot->arena_start = gameplay_owned_start;
        snapshot->arena_end = gameplay_owned_end;
        snapshot->heaps = sHeapArray;
        snapshot->heap_count = sNumHeaps;
        snapshot->selected = __OSCurrHeap;
        const auto base = reinterpret_cast<uintptr_t>(gameplay_owned_arena);
        const auto start = reinterpret_cast<uintptr_t>(gameplay_owned_start);
        const auto end = reinterpret_cast<uintptr_t>(gameplay_owned_end);
        if (start < base || end <= start || end - base > gameplay_owned_bytes ||
            end - base > std::numeric_limits<uint32_t>::max())
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        snapshot->heap_start_offset = static_cast<uint32_t>(start - base);
        snapshot->heap_end_offset = static_cast<uint32_t>(end - base);
        std::vector<CellRange> ranges;
        if (!history_make_scratch(snapshot->arena_bytes,
                snapshot->heap_start_offset, snapshot->heap_end_offset, &ranges) ||
            !history_validate_live(snapshot->arena, snapshot->arena_bytes,
                snapshot->arena_start, snapshot->arena_end, snapshot->selected,
                &ranges))
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        snapshot->image.resize(snapshot->arena_bytes);
        std::memcpy(snapshot->image.data(), snapshot->arena, snapshot->arena_bytes);
        if (!history_disjoint(snapshot->image.data(), snapshot->image.size(),
                              snapshot->arena, snapshot->arena_bytes) ||
            !history_owner_matches(snapshot.get()) ||
            std::memcmp(snapshot->arena, snapshot->image.data(), snapshot->arena_bytes) != 0 ||
            history_validate_snapshot_image(snapshot.get(), &ranges) !=
                MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK)
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE, nullptr};
        snapshot->validation_scratch = std::move(ranges);
        result.status = MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK;
        result.snapshot = snapshot.release();
        return result;
    } catch (const std::bad_alloc&) {
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALLOCATION_FAILED, nullptr};
    }
}

extern "C" MeleeWebGameplayHeapHistoryPrepareResult
melee_web_gameplay_heap_history_prepare(const HistorySnapshot* snapshot)
{
    if (!snapshot)
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT, nullptr};
    if (!history_owner_matches(snapshot))
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER, nullptr};
    try {
        std::vector<CellRange> preflight;
        if (!history_make_scratch(snapshot->arena_bytes, snapshot->heap_start_offset,
                                  snapshot->heap_end_offset, &preflight) ||
            history_validate_snapshot_image(snapshot, &preflight) !=
                MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK)
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        std::vector<CellRange> live_preflight;
        if (!history_make_scratch(snapshot->arena_bytes, snapshot->heap_start_offset,
                                  snapshot->heap_end_offset, &live_preflight) ||
            !history_validate_live(snapshot->arena, snapshot->arena_bytes,
                snapshot->arena_start, snapshot->arena_end, snapshot->selected,
                &live_preflight))
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        auto prepared = std::make_unique<HistoryPrepared>();
        prepared->snapshot = snapshot;
        prepared->epoch = snapshot->epoch;
        prepared->arena = snapshot->arena;
        prepared->arena_bytes = snapshot->arena_bytes;
        prepared->arena_start = snapshot->arena_start;
        prepared->arena_end = snapshot->arena_end;
        prepared->heaps = snapshot->heaps;
        prepared->heap_count = snapshot->heap_count;
        prepared->selected = snapshot->selected;
        if (!history_make_scratch(prepared->arena_bytes, snapshot->heap_start_offset,
                                  snapshot->heap_end_offset,
                                  &prepared->validation_scratch))
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        prepared->live_fence.resize(prepared->arena_bytes);
        if (!history_disjoint(prepared->live_fence.data(), prepared->live_fence.size(),
                              prepared->arena, prepared->arena_bytes) ||
            !history_disjoint(prepared->validation_scratch.data(),
                prepared->validation_scratch.size() * sizeof(CellRange),
                prepared->arena, prepared->arena_bytes))
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE, nullptr};
        std::memcpy(prepared->live_fence.data(), prepared->arena,
                    prepared->arena_bytes);
        if (!history_owner_matches(prepared.get()) ||
            std::memcmp(prepared->arena, prepared->live_fence.data(),
                        prepared->arena_bytes) != 0 ||
            !history_validate_live(prepared->arena, prepared->arena_bytes,
                prepared->arena_start, prepared->arena_end, prepared->selected,
                &prepared->validation_scratch))
            return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE, nullptr};
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK, prepared.release()};
    } catch (const std::bad_alloc&) {
        return {MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALLOCATION_FAILED, nullptr};
    }
}

extern "C" MeleeWebGameplayHeapHistoryStatus
melee_web_gameplay_heap_history_admit(HistoryPrepared* prepared)
{
    if (!prepared) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT;
    if (prepared->committed) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALREADY_COMMITTED;
    prepared->admitted = false;
    if (!history_owner_matches(prepared))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER;
    if (!history_disjoint(prepared->live_fence.data(), prepared->live_fence.size(),
                          prepared->arena, prepared->arena_bytes) ||
        std::memcmp(prepared->arena, prepared->live_fence.data(),
                    prepared->arena_bytes) != 0)
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE;
    if (!history_validate_live(prepared->arena, prepared->arena_bytes,
            prepared->arena_start, prepared->arena_end, prepared->selected,
            &prepared->validation_scratch))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE;
    prepared->admitted = true;
    return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK;
}

extern "C" MeleeWebGameplayHeapHistoryStatus
melee_web_gameplay_heap_history_current_admit(const HistorySnapshot* snapshot)
{
    if (!snapshot) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT;
    if (!history_owner_matches(snapshot))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER;
    if (!history_validate_live(snapshot->arena, snapshot->arena_bytes,
            snapshot->arena_start, snapshot->arena_end, snapshot->selected,
            &snapshot->validation_scratch))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE;
    return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK;
}

extern "C" MeleeWebGameplayHeapHistoryObservation
melee_web_gameplay_heap_history_observe(const HistorySnapshot* snapshot)
{
    MeleeWebGameplayHeapHistoryObservation observation{};
    if (!snapshot) {
        observation.status = MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT;
        return observation;
    }
    observation.status = melee_web_gameplay_heap_history_current_admit(snapshot);
    if (observation.status != MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK) return observation;
    observation.schema = 1;
    observation.bytes = sizeof(observation);
    observation.selected_heap = snapshot->selected;
    observation.owner_epoch = snapshot->epoch;
    observation.arena_base = reinterpret_cast<uintptr_t>(snapshot->arena);
    observation.arena_bytes = snapshot->arena_bytes;
    observation.compared_bytes = 0;
    observation.first_difference = UINT64_MAX;
    const auto* live = static_cast<const u8*>(snapshot->arena);
    observation.exact_equal = 1;
    // Use the optimized exact compare for the common equal case. Keep the byte
    // walk below for the original precise first-difference receipt on mismatch.
    if (std::memcmp(live, snapshot->image.data(), snapshot->arena_bytes) == 0) {
        observation.compared_bytes = snapshot->arena_bytes;
        return observation;
    }
    for (size_t i=0;i<snapshot->arena_bytes;++i) {
        ++observation.compared_bytes;
        if (live[i] != snapshot->image[i]) {
            observation.exact_equal = 0;
            observation.first_difference = i;
            break;
        }
    }
    return observation;
}

extern "C" MeleeWebGameplayHeapHistoryStatus
melee_web_gameplay_heap_history_visit_allocations(
    const HistorySnapshot* snapshot,
    MeleeWebGameplayHeapHistoryAllocationVisitor visitor, void* user,
    uint64_t* out_count)
{
    if (out_count) *out_count = 0;
    if (!snapshot || !visitor || !out_count)
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT;
    const auto admission = melee_web_gameplay_heap_history_current_admit(snapshot);
    if (admission != MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK) return admission;
    uint64_t ordinal = 0;
    Cell* cell = sHeapArray[0].allocated;
    while (cell) {
        // The whole live graph was validated before iteration. Retain next and
        // the row values before the read-only observer callback.
        Cell* const next = cell->next;
        const void* const payload = reinterpret_cast<const u8*>(cell) + kHeaderSize;
        const uint32_t capacity = static_cast<uint32_t>(cell->size) - kHeaderSize;
        if (!visitor(user, ordinal, payload, capacity)) {
            *out_count = ordinal;
            return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_VISITOR_STOPPED;
        }
        ++ordinal;
        cell = next;
    }
    *out_count = ordinal;
    return melee_web_gameplay_heap_history_current_admit(snapshot);
}

extern "C" MeleeWebGameplayHeapHistoryStatus
melee_web_gameplay_heap_history_commit(HistoryPrepared* prepared)
{
    if (!prepared) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT;
    if (prepared->committed) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALREADY_COMMITTED;
    if (!prepared->admitted) return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_NOT_ADMITTED;
    // All failure checks precede the first write. The caller serializes the
    // owner between final admission and this allocation/callback-free copy.
    if (!history_owner_matches(prepared))
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER;
    if (!history_disjoint(prepared->snapshot->image.data(),
                          prepared->snapshot->image.size(), prepared->arena,
                          prepared->arena_bytes) ||
        std::memcmp(prepared->arena, prepared->live_fence.data(),
                    prepared->arena_bytes) != 0)
        return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE;
    std::memcpy(prepared->arena, prepared->snapshot->image.data(),
                prepared->arena_bytes);
    prepared->committed = true;
    prepared->admitted = false;
    return MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK;
}

extern "C" void melee_web_gameplay_heap_history_commit_prevalidated(
    HistoryPrepared* prepared)
{
    // Aggregate caller contract: this plan was admitted before the transaction's
    // first write, every companion owner passed admission, and the owner remains
    // serialized until this slot. Earlier leaves may have made writes that the
    // arena snapshot intentionally replaces; therefore this leaf must not repeat
    // the selected-END byte fence or introduce a recoverable late failure.
    std::memcpy(prepared->arena, prepared->snapshot->image.data(),
                prepared->arena_bytes);
    prepared->committed = true;
    prepared->admitted = false;
}

extern "C" void melee_web_gameplay_heap_history_snapshot_destroy(
    HistorySnapshot* snapshot)
{
    delete snapshot;
}
extern "C" void melee_web_gameplay_heap_history_prepared_destroy(
    HistoryPrepared* prepared)
{
    delete prepared;
}
