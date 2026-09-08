#ifndef MELEE_WEB_EFFECT_RUNTIME_H
#define MELEE_WEB_EFFECT_RUNTIME_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Initialize original EF/particle allocators, callbacks and scheduled procs
 * before publishing any effect bank. Owns the complete world's effect links. */
int melee_web_effect_runtime_active(void);
int melee_web_effect_runtime_begin(char*,size_t);
/* After stage/fighter destruction, before bank/descriptor release. */
int melee_web_effect_runtime_end(char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
