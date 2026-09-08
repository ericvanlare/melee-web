#ifndef MELEE_WEB_GAMEPLAY_FIGHTER_PROBE_H
#define MELEE_WEB_GAMEPLAY_FIGHTER_PROBE_H

#include "common_schema.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebFighterInputProbe MeleeWebFighterInputProbe;
typedef struct MeleeWebFighterInputResult {
    float stick_x, facing_direction, walk_threshold;
    uint32_t held_buttons, pressed_buttons, released_buttons;
    uint8_t stick_x_timer, stick_y_timer, trigger_timer;
    int can_walk;
} MeleeWebFighterInputResult;

/* A narrow consumer of typed common-data root0. Storage uses original ObjAlloc;
 * only original input reset and walk-threshold logic execute. This is not
 * Fighter_Create, complete input processing, or common-root initialization. */
MeleeWebFighterInputProbe* melee_web_fighter_input_probe_create(
    const MeleeWebCommonScalars* common, char* error, size_t error_size);
int melee_web_fighter_input_probe_reset(MeleeWebFighterInputProbe*, char* error, size_t error_size);
int melee_web_fighter_input_probe_read(MeleeWebFighterInputProbe*, MeleeWebFighterInputResult*,
                                      char* error, size_t error_size);
int melee_web_fighter_input_probe_sample(MeleeWebFighterInputProbe*, float stick_x, float facing_direction,
                                        MeleeWebFighterInputResult*, char* error, size_t error_size);
/* The handle survives world shutdown as an invalidated owner until destroyed. */
int melee_web_fighter_input_probe_destroy(MeleeWebFighterInputProbe*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
