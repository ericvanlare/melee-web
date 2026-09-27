#ifndef MELEE_WEB_GAMEPLAY_RESULTS_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_RESULTS_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include <dolphin/pad.h>
#include "gameplay_compat.h"

struct ResultsMatchInfo;

#include "gameplay_audio.h"
#include "gameplay_pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebResultsContext MeleeWebResultsContext;

/* Read-only pointer snapshots around source Results OnEnter and native
 * collision adoption. These do not establish ownership or relax its guards. */
typedef struct MeleeWebResultsCameraEntrySnapshot {
    const void* saved_pool_at_context_begin;
    const void* source_pool_before_onenter;
    const void* owner_pool_before_onenter;
    const void* source_pool_after_onenter;
    const void* source_pool_after_collision_adoption;
    const void* context_pool_after_adoption;
    const void* owner_pool_after_adoption;
} MeleeWebResultsCameraEntrySnapshot;

/* Owns one original Results scene inside an already prepared Results world.
 * The caller retains the prepared C++ world/assets/audio owners through end(). */
MeleeWebResultsContext* melee_web_results_context_begin(
    const struct ResultsMatchInfo* match, uint32_t seed,
    const MeleeWebPadState* input, MeleeWebAudio* audio,
    char* error, size_t error_size);
int melee_web_results_context_tick(MeleeWebResultsContext*, const PADStatus raw[4],
    char* error, size_t error_size);
int melee_web_results_context_draw(MeleeWebResultsContext*, char* error, size_t error_size);
int melee_web_results_context_requested(const MeleeWebResultsContext*);
uint32_t melee_web_results_context_random_seed(const MeleeWebResultsContext*);
uint32_t melee_web_results_context_ticks(const MeleeWebResultsContext*);
int melee_web_results_context_camera_entry_snapshot(
    const MeleeWebResultsContext*, MeleeWebResultsCameraEntrySnapshot*);
/* Run scene OnExit before the enclosing mode OnExit. Keep scene assets/heap
 * resident until end(), matching the original scene-manager ordering. */
int melee_web_results_context_exit(MeleeWebResultsContext*, char*, size_t);
/* Read-only enclosing-mode handoff check after scene OnExit and before end().
 * Reuses the exact camera triple and reports the caller's boundary phase. */
int melee_web_results_context_check_handoff(const char* phase, char*, size_t);
int melee_web_results_context_end(MeleeWebResultsContext*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
