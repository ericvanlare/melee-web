#ifndef MELEE_WEB_GAMEPLAY_CPU_INTERVAL_H
#define MELEE_WEB_GAMEPLAY_CPU_INTERVAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private diagnostic interval barrier. No restore or browser export. The
 * aggregate adapter proves the completed main-thread boundary before begin.
 * Every attempted nonlocal effect is latched BEFORE its provider mutation.
 * A refusal never means the source operation succeeded; the aggregate fails.
 * Cleanup disarms first, so ordinary teardown can still be attempted. */
enum MeleeWebCpuIntervalEffect {
 MELEE_WEB_CPU_EFFECT_NONE=0,
 MELEE_WEB_CPU_EFFECT_SOURCE_FILE=1,
 MELEE_WEB_CPU_EFFECT_SSM=2,
 MELEE_WEB_CPU_EFFECT_HPS=3,
 MELEE_WEB_CPU_EFFECT_MOTOR=4,
};
/* Non-diagnostic consumers retain their original transport linkage: the
 * inactive checks fold away and require no new gameplay library dependency. */
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) || defined(MELEE_WEB_CPU_INTERVAL_IMPLEMENTATION)
uint64_t melee_web_cpu_interval_begin(void);
int melee_web_cpu_interval_admit(uint64_t token);
/* Raw armed state, including an interval whose first effect already failed. */
int melee_web_cpu_interval_active(void);
int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect);
unsigned melee_web_cpu_interval_failure(void);
int melee_web_cpu_interval_active_failure(void);
int melee_web_cpu_interval_end(uint64_t token);
#else
static inline uint64_t melee_web_cpu_interval_begin(void){return 0;}
static inline int melee_web_cpu_interval_admit(uint64_t token){(void)token;return 0;}
static inline int melee_web_cpu_interval_active(void){return 0;}
static inline int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect effect){(void)effect;return 1;}
static inline unsigned melee_web_cpu_interval_failure(void){return 0;}
static inline int melee_web_cpu_interval_active_failure(void){return 0;}
static inline int melee_web_cpu_interval_end(uint64_t token){(void)token;return 0;}
#endif
void melee_web_cpu_interval_pad_motor(uint32_t channel,uint32_t command);
#ifdef __cplusplus
}
#endif
#endif
