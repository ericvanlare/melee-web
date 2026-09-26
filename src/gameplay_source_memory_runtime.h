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

/* Fighter_Create and Fighter_Unload bind the source object allocation to its
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
