#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_H
#define MELEE_WEB_GAMEPLAY_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebGameplayStats {
    uint64_t ticks;
    uint32_t objects, processes, object_peak, process_peak;
    int32_t heap_free_bytes;
    uint64_t generation;
} MeleeWebGameplayStats;

int melee_web_gameplay_startup(size_t heap_bytes, char* error, size_t error_size);
int melee_web_gameplay_shutdown(char* error, size_t error_size);
MeleeWebGameplayStats melee_web_gameplay_stats(void);
int melee_web_gameplay_world_exists(void);

#ifdef __cplusplus
}
#endif

#endif
