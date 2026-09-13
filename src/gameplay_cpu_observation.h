#ifndef MELEE_WEB_CPU_OBSERVATION_H
#define MELEE_WEB_CPU_OBSERVATION_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only match diagnostics. Enabled only by an instrumented v3 recipe.
 * CPU outputs never feed the input recipe or the simulation. */
void melee_web_cpu_observation_begin(const uint8_t setup[0x138], size_t frames,
                                     int source_drawing);
/* One-shot native diagnostic opt-in. This only emits read-only HITLAG_AUDIT
 * lines; it is never part of the accepted input plan or simulation state. */
void melee_web_cpu_observation_enable_hitlag_audit(void);
void melee_web_cpu_observation_tick(size_t index);
void melee_web_cpu_observation_draw(size_t index);
void melee_web_cpu_observation_preparation_draw(void);
void melee_web_cpu_observation_end(size_t frames);
#ifdef __cplusplus
}
#endif
#endif
