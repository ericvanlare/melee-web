#ifndef MELEE_WEB_TEST_STADIUM_C1_STAGE_STATE_PROBE_H
#define MELEE_WEB_TEST_STADIUM_C1_STAGE_STATE_PROBE_H

#include <stdint.h>

enum {
    MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE = 1u << 0,
    MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ = 1u << 1,
    MELEE_WEB_STADIUM_C1_STAGE_INSTANCE = 1u << 2,
    MELEE_WEB_STADIUM_C1_GROUND_GOBJ = 1u << 3,
    MELEE_WEB_STADIUM_C1_STAGE_ITEMS = 1u << 4,
    MELEE_WEB_STADIUM_C1_STAGE_LIGHTS = 1u << 5,
    MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT = 1u << 6,
};

#ifdef __cplusplus
extern "C" {
#endif

/* Read source stage registries without copying or mutating their state. */
uint32_t melee_web_stadium_c1_stage_state_failures(void);

#ifdef __cplusplus
}
#endif

#endif
