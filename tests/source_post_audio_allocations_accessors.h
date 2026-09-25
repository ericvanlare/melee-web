/* Read-only snapshots appended to the original memory/heap translation units.
 *
 * The implementation is included after exactly one original source body.  It
 * may therefore name that body's private authored state without copying the
 * allocator or heap algorithm.  Snapshot fields are raw MEM1/ARAM bounds and
 * source-pool indices/relationships in the Wasm32 address space; private BSS
 * pointers are never serialized and the accessor does not mutate source state.
 */
#ifndef MELEE_WEB_SOURCE_POST_AUDIO_ALLOCATIONS_ACCESSORS_H
#define MELEE_WEB_SOURCE_POST_AUDIO_ALLOCATIONS_ACCESSORS_H

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum {
    MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT = 0x83,
    MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT = 6,
    MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT = 5,
};

typedef struct MeleeWebSourceLBMemorySnapshot {
    uint32_t arena_lo;
    uint32_t arena_hi;
    /* Handle-pool indices; these are never serialized BSS addresses. */
    uint32_t root_handle;
    uint32_t root_handle_pool;
    uint32_t root_lo;
    uint32_t root_hi;
    /* Relationship indices.  A null relationship has index 0 and pool NONE. */
    uint32_t root_next;
    uint32_t root_next_pool;
    uint32_t root_prev;
    uint32_t root_prev_pool;
    uint32_t root_span_bytes;
    uint32_t root_matches_arena;
    uint32_t root_handle_index;
    uint32_t free_mem_head;
    uint32_t free_mem_head_pool;
    uint32_t free_mem_head_index;
    uint32_t free_heap_head;
    uint32_t free_heap_head_pool;
    uint32_t free_heap_head_index;
    int32_t num_allocs;
    int32_t max_num_allocs;
    uint32_t manager_size;
    uint32_t mem_entry_count;
    uint32_t heap_handle_count;
} MeleeWebSourceLBMemorySnapshot;

typedef struct MeleeWebSourceLBHeapDescriptorSnapshot {
    uint32_t index;
    uint32_t type;
    uint32_t previous_index;
    uint32_t size;
} MeleeWebSourceLBHeapDescriptorSnapshot;

typedef struct MeleeWebSourceLBHeapEntrySnapshot {
    uint32_t index;
    int32_t id;
    /* Handle-pool index; UINT32_MAX means an unknown/non-source handle. */
    uint32_t handle;
    uint32_t handle_pool;
    uint32_t start;
    uint32_t size;
    int32_t type;
    int32_t transient;
    int32_t status;
} MeleeWebSourceLBHeapEntrySnapshot;

typedef struct MeleeWebSourceLBHeapSnapshot {
    uint32_t arena_lo;
    uint32_t arena_hi;
    uint32_t aram_lo;
    uint32_t aram_hi;
    uint32_t heap_count;
    uint32_t descriptor_count;
    MeleeWebSourceLBHeapEntrySnapshot heaps[MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT];
    MeleeWebSourceLBHeapDescriptorSnapshot
        descriptors[MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT];
} MeleeWebSourceLBHeapSnapshot;

unsigned melee_web_source_lbmemory_snapshot(
    MeleeWebSourceLBMemorySnapshot* out);
unsigned melee_web_source_lbheap_snapshot(MeleeWebSourceLBHeapSnapshot* out);

/* Maps a source Handle pointer without exposing the private Allocator layout.
 * pool is 0 for null, 1 for a MemEntry slot, 2 for a heap Handle slot, and
 * UINT32_MAX for an unrecognized non-null source pointer. */
unsigned melee_web_source_lbmemory_handle_location(
    const void* value, uint32_t* pool, uint32_t* index);

#if defined(__cplusplus)
}
#endif

/* These helpers are shared by both same-translation-unit implementations.
 * Keep them outside the per-unit guards: each generated wrapper includes this
 * header after one private original source body, and the heap snapshot also
 * serializes source-owned pointers. */
#if !defined(MELEE_WEB_SOURCE_POST_AUDIO_HELPERS)
#define MELEE_WEB_SOURCE_POST_AUDIO_HELPERS 1

enum {
    MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE = 0,
    MELEE_WEB_SOURCE_POST_AUDIO_POOL_MEM_ENTRY = 1,
    MELEE_WEB_SOURCE_POST_AUDIO_POOL_HEAP_HANDLE = 2,
    MELEE_WEB_SOURCE_POST_AUDIO_POOL_UNKNOWN = UINT32_MAX,
};

static uint32_t melee_web_source_post_audio_pointer(const void* value)
{
    return (uint32_t)(uintptr_t)value;
}

static uint32_t melee_web_source_post_audio_index(
    const void* value, const void* base, uint32_t count, uint32_t stride)
{
    uintptr_t address = (uintptr_t)value;
    uintptr_t first = (uintptr_t)base;
    uintptr_t delta;
    if (value == 0 || address < first || stride == 0) {
        return UINT32_MAX;
    }
    delta = address - first;
    if (delta % stride != 0 || delta / stride >= count) {
        return UINT32_MAX;
    }
    return (uint32_t)(delta / stride);
}

#endif

#if defined(MELEE_WEB_SOURCE_POST_AUDIO_MEMORY_ACCESSOR_IMPLEMENTATION)

_Static_assert(sizeof(void*) == 4, "post-audio snapshots require Wasm32 pointers");
_Static_assert(sizeof(struct Allocator) == 0x6F0,
               "source allocator layout changed");
_Static_assert(sizeof(lbMemory_804318B0.x8_mem) /
                       sizeof(lbMemory_804318B0.x8_mem[0]) ==
                   MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT,
               "source memory-entry count changed");
_Static_assert(sizeof(lbMemory_804318B0.x638_heap) /
                       sizeof(lbMemory_804318B0.x638_heap[0]) ==
                   MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT,
               "source heap-handle count changed");

unsigned melee_web_source_lbmemory_snapshot(
    MeleeWebSourceLBMemorySnapshot* out)
{
    Handle* root;
    uint32_t pool;
    uint32_t index;
    if (out == 0) {
        return 0;
    }

    out->arena_lo = melee_web_source_post_audio_pointer(
        lbMemory_804318B0.a_arenaLo);
    out->arena_hi = melee_web_source_post_audio_pointer(
        lbMemory_804318B0.a_arenaHi);
    root = lbMemory_804318B0.x69C;
    if (root == 0) {
        out->root_handle = 0;
        out->root_handle_pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE;
    } else if (melee_web_source_lbmemory_handle_location(
                   root, &out->root_handle_pool, &out->root_handle) == 0) {
        out->root_handle = UINT32_MAX;
    }
    out->root_lo = root == 0 ? 0 : melee_web_source_post_audio_pointer(root->x4_lo);
    out->root_hi = root == 0 ? 0 : melee_web_source_post_audio_pointer(root->x8_hi);
    if (root == 0 || melee_web_source_lbmemory_handle_location(
                         root->x0_next, &pool, &index) == 0) {
        out->root_next = root == 0 ? 0 : UINT32_MAX;
        out->root_next_pool = root == 0 ? MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE :
            MELEE_WEB_SOURCE_POST_AUDIO_POOL_UNKNOWN;
    } else {
        out->root_next = index;
        out->root_next_pool = pool;
    }
    if (root == 0 || melee_web_source_lbmemory_handle_location(
                         root->xC_prev, &pool, &index) == 0) {
        out->root_prev = root == 0 ? 0 : UINT32_MAX;
        out->root_prev_pool = root == 0 ? MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE :
            MELEE_WEB_SOURCE_POST_AUDIO_POOL_UNKNOWN;
    } else {
        out->root_prev = index;
        out->root_prev_pool = pool;
    }
    out->root_span_bytes = root == 0 ? 0 :
        (uint32_t)((uintptr_t)root->x8_hi - (uintptr_t)root->x4_lo);
    out->root_matches_arena = root != 0 && root->x4_lo == lbMemory_804318B0.a_arenaLo &&
                              root->x8_hi == lbMemory_804318B0.a_arenaHi;
    out->root_handle_index = melee_web_source_post_audio_index(
        root, &lbMemory_804318B0.x638_heap[0],
        MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT, sizeof(lbMemory_804318B0.x638_heap[0]));
    out->free_mem_head_index = melee_web_source_post_audio_index(
        lbMemory_804318B0.free_mem, &lbMemory_804318B0.x8_mem[0],
        MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT, sizeof(lbMemory_804318B0.x8_mem[0]));
    out->free_mem_head = out->free_mem_head_index == UINT32_MAX ? UINT32_MAX :
        out->free_mem_head_index;
    out->free_mem_head_pool = lbMemory_804318B0.free_mem == 0 ?
        MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE :
        MELEE_WEB_SOURCE_POST_AUDIO_POOL_MEM_ENTRY;
    out->free_heap_head_index = melee_web_source_post_audio_index(
        lbMemory_804318B0.free_heap, &lbMemory_804318B0.x638_heap[0],
        MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT, sizeof(lbMemory_804318B0.x638_heap[0]));
    out->free_heap_head = out->free_heap_head_index == UINT32_MAX ? UINT32_MAX :
        out->free_heap_head_index;
    out->free_heap_head_pool = lbMemory_804318B0.free_heap == 0 ?
        MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE :
        MELEE_WEB_SOURCE_POST_AUDIO_POOL_HEAP_HANDLE;
    out->num_allocs = lbMemory_804318B0.x630_num_allocs;
    out->max_num_allocs = lbMemory_804318B0.x634_max_num_allocs;
    out->manager_size = lbMemory_804318B0.x6A0_mgr.size;
    out->mem_entry_count = MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT;
    out->heap_handle_count = MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT;
    return 1;
}

unsigned melee_web_source_lbmemory_handle_location(
    const void* value, uint32_t* pool, uint32_t* index)
{
    uint32_t candidate;
    if (pool == 0 || index == 0) {
        return 0;
    }
    if (value == 0) {
        *pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE;
        *index = 0;
        return 1;
    }
    candidate = melee_web_source_post_audio_index(
        value, &lbMemory_804318B0.x8_mem[0],
        MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT, sizeof(lbMemory_804318B0.x8_mem[0]));
    if (candidate != UINT32_MAX) {
        *pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_MEM_ENTRY;
        *index = candidate;
        return 1;
    }
    candidate = melee_web_source_post_audio_index(
        value, &lbMemory_804318B0.x638_heap[0],
        MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT, sizeof(lbMemory_804318B0.x638_heap[0]));
    if (candidate != UINT32_MAX) {
        *pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_HEAP_HANDLE;
        *index = candidate;
        return 1;
    }
    *pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_UNKNOWN;
    *index = UINT32_MAX;
    return 0;
}

#endif

#if defined(MELEE_WEB_SOURCE_POST_AUDIO_HEAP_ACCESSOR_IMPLEMENTATION)

_Static_assert(sizeof(void*) == 4, "post-audio snapshots require Wasm32 pointers");
_Static_assert(sizeof(struct lbHeap_HeapState) == 0xB8,
               "source heap-state layout changed");
_Static_assert(sizeof(lbHeap_803BA380) / sizeof(lbHeap_803BA380[0]) ==
                   MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT,
               "source heap-descriptor count changed");
_Static_assert(sizeof(lbHeap_80431FA0.heap_array) /
                       sizeof(lbHeap_80431FA0.heap_array[0]) ==
                   MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT,
               "source heap-entry count changed");

unsigned melee_web_source_lbheap_snapshot(MeleeWebSourceLBHeapSnapshot* out)
{
    unsigned i;
    if (out == 0) {
        return 0;
    }

    out->arena_lo = melee_web_source_post_audio_pointer(
        lbHeap_80431FA0.arena_lo);
    out->arena_hi = melee_web_source_post_audio_pointer(
        lbHeap_80431FA0.arena_hi);
    out->aram_lo = (uint32_t)lbHeap_80431FA0.aram_lo;
    out->aram_hi = (uint32_t)lbHeap_80431FA0.aram_hi;
    out->heap_count = MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT;
    out->descriptor_count = MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT;
    for (i = 0; i < MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT; i++) {
        const struct Heap* heap = &lbHeap_80431FA0.heap_array[i];
        out->heaps[i].index = i;
        out->heaps[i].id = heap->id;
        if (heap->handle == 0) {
            out->heaps[i].handle_pool = MELEE_WEB_SOURCE_POST_AUDIO_POOL_NONE;
            out->heaps[i].handle = UINT32_MAX;
        } else if (melee_web_source_lbmemory_handle_location(
                       heap->handle, &out->heaps[i].handle_pool,
                       &out->heaps[i].handle) == 0) {
            out->heaps[i].handle = UINT32_MAX;
        }
        out->heaps[i].start = (uint32_t)heap->start;
        out->heaps[i].size = heap->size;
        out->heaps[i].type = heap->type;
        out->heaps[i].transient = heap->transient;
        out->heaps[i].status = heap->status;
    }
    for (i = 0; i < MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT; i++) {
        out->descriptors[i].index = lbHeap_803BA380[i].idx;
        out->descriptors[i].type = lbHeap_803BA380[i].type;
        out->descriptors[i].previous_index = lbHeap_803BA380[i].prev_idx;
        out->descriptors[i].size = lbHeap_803BA380[i].size;
    }
    return 1;
}

#endif

#endif
