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
    uint64_t generation; /* Changes when a fresh arena/world is created. */
} MeleeWebGameplayStats;

/* Owns a single isolated SDK heap and the HSD process tables. This nonrendering
 * bootstrap executes original allocation, object lifecycle and scheduling;
 * it does not initialize Fighter, stage, audio or graphics-object destructors.
 * GObj_Create/HSD_GObj_SetupProc remain the original APIs. Do not attach an
 * HSD render object until the full object-kind initialization is integrated.
 * Startup rejects an existing HSD object world or SDK heap. */
int melee_web_gameplay_startup(size_t heap_bytes, char* error, size_t error_size);
int melee_web_gameplay_step(char* error, size_t error_size);
int melee_web_gameplay_shutdown(char* error, size_t error_size);
MeleeWebGameplayStats melee_web_gameplay_stats(void);
/* Remains true during teardown, until the owned SDK arena is released. */
int melee_web_gameplay_world_exists(void);

/* Optional native HSD lifetime lane. Installs the original camera/light/joint/
 * fog destructor registry, without creating or rendering any such objects.
 * A single owner supplies the post-object class/ID cleanup before arena release.
 * Re-registering the same callback is idempotent within the current world. */
int melee_web_gameplay_enable_hsd_objects(void (*after_objects)(void),
                                         char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
