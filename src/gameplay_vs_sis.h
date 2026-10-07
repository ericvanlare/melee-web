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

#ifdef __cplusplus
}
#endif
#endif
