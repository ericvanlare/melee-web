#ifndef MELEE_WEB_GAMEPLAY_STAGE_DREAM_LAND_H
#define MELEE_WEB_GAMEPLAY_STAGE_DREAM_LAND_H

#include "native_dat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebDreamLandYakumono {
    int16_t bird_timer_min, bird_timer_max, bird_height, unused;
    int32_t tree_timer_max, tree_timer_min;
    float wind_speed;
    float wind_left_inner, wind_right_inner;
    float wind_left_outer, wind_right_outer;
    float wind_top, wind_bottom;
    float blink_timer_max, blink_timer_min;
} MeleeWebDreamLandYakumono;

void* melee_web_dream_land_yakumono_decode(const MeleeWebNativeDat*, uint32_t root);

#ifdef __cplusplus
}
#endif
#endif
