#ifndef MELEE_WEB_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H
#define MELEE_WEB_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the checked GALE01r2 source-address shadow at the original VS main
 * HSD-heap replacement boundary. Bounds are derived from the pinned game-heap
 * descriptors and the source preload-3 retention transition. */
int melee_web_source_memory_begin(uint64_t world_generation, int source_heap);
int melee_web_source_memory_end(uint64_t world_generation);

/* Called only by the isolated gameplay OS allocator interposition. The source
 * heap receives the same request and validates allocation/free order. */
int melee_web_source_memory_alloc(int source_heap, void* host_payload,
                                  size_t requested_bytes);
int melee_web_source_memory_free(int source_heap, void* host_payload);
int melee_web_source_memory_healthy(void);

/* Pure observations of the active source-memory owner. These queries never
 * fail the owner, allocate a generation, or expose its allocation container. */
typedef enum MeleeWebSourceMemoryReadStatus {
    MELEE_WEB_SOURCE_MEMORY_READ_OK = 0,
    MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE = 1,
    MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY = 2,
    MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT = 3,
} MeleeWebSourceMemoryReadStatus;

typedef struct MeleeWebSourceMemoryContext {
    int source_heap_handle;
    uint64_t world_generation;
    uint64_t allocation_generation_watermark;
} MeleeWebSourceMemoryContext;

typedef struct MeleeWebSourceMemoryAllocation {
    int source_heap_handle;
    uint32_t requested_bytes;
    uint64_t world_generation;
    uint64_t allocation_generation;
    uint8_t live;
    uint8_t reserved[7];
} MeleeWebSourceMemoryAllocation;

MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(
    MeleeWebSourceMemoryContext* out);
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(
    const void* exact_host_payload, MeleeWebSourceMemoryAllocation* out);

/* Fighter_Create / ftDemo_CreateFighter and Fighter_Unload bind the allocation to its
 * live host owner. Reuse advances allocation_generation even if an HSD free
 * chain returns the same object address. */
typedef struct MeleeWebSourceFighterAddress {
    uint32_t source_address;
    uint64_t world_generation;
    uint64_t allocation_generation;
    uint8_t live;
    uint8_t reserved[7];
} MeleeWebSourceFighterAddress;

int melee_web_source_memory_fighter_acquire(void* host_fighter,
                                            size_t fighter_bytes,
                                            MeleeWebSourceFighterAddress* out);
int melee_web_source_memory_fighter_read(void* host_fighter,
                                         MeleeWebSourceFighterAddress* out);
int melee_web_source_memory_fighter_release(void* host_fighter);

#ifdef __cplusplus
}
#endif
#endif
