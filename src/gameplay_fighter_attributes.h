#ifndef MELEE_WEB_GAMEPLAY_FIGHTER_ATTRIBUTES_H
#define MELEE_WEB_GAMEPLAY_FIGHTER_ATTRIBUTES_H
#include "fighter_attributes.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Hydrate real original ftData scalar descriptors, call ftCo_800D0FA0 on a
 * scoped original Fighter/GObj, and return its copied values. No global tables,
 * heaps or native game object lifetimes are installed by this consumer check. */
int melee_web_fighter_copy_base_attributes(const MeleeWebFighterBaseAttributes* input,
                                           MeleeWebFighterBaseAttributes* output,
                                           char* error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
