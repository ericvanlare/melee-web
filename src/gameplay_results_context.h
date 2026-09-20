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
/* Run scene OnExit before the enclosing mode OnExit. Keep scene assets/heap
 * resident until end(), matching the original scene-manager ordering. */
int melee_web_results_context_exit(MeleeWebResultsContext*, char*, size_t);
int melee_web_results_context_exit_ready(void);
int melee_web_results_context_end(MeleeWebResultsContext*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
