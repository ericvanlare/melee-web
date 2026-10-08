#ifndef MELEE_WEB_GAMEPLAY_VS_SIS_H
#define MELEE_WEB_GAMEPLAY_VS_SIS_H

#include "gameplay_bootstrap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stateless original SIS boundary, supplied only by the full source owner. */
int melee_web_gameplay_vs_sis(MeleeWebGameplayVSSisOperation operation,
                              int font_slot, void* expected,
                              char* error, size_t error_size);

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* Same source SIS heap size as start_vs_scene_manager, without running that
 * manager or a camera. This token borrows one exact source allocation lease. */
enum { MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES = 0x4800 };
typedef struct MeleeWebDiagnosticSisOwner {
    void* heap;
    uint64_t world_generation;
    uint64_t allocation_generation;
    int source_heap_handle;
} MeleeWebDiagnosticSisOwner;

int melee_web_diagnostic_sis_begin(MeleeWebDiagnosticSisOwner* owner,
                                  char* error, size_t error_size);
int melee_web_diagnostic_sis_end(MeleeWebDiagnosticSisOwner* owner,
                                char* error, size_t error_size);
#endif

#ifdef __cplusplus
}
#endif
#endif
