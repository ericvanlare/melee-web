#ifndef MELEE_WEB_GAMEPLAY_STAGE_STORY_H
#define MELEE_WEB_GAMEPLAY_STAGE_STORY_H

#include "native_dat.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exact GrSt.dat yakumono_param payload consumed by grstory.c. */
typedef struct MeleeWebStoryYakumono {
    float timer_min;
    float timer_rand;
    float spawnmany_rarity;
    float vpos[6];
} MeleeWebStoryYakumono;

/* Decode the stage-specific scalar ABI into arena-owned native storage. */
void* melee_web_story_yakumono_decode(const MeleeWebNativeDat*, uint32_t root);

/* Observation-only source lifecycle probe. It never creates or mutates map
 * objects. Map 2 is Randall and map 3 owns the Shy Guy scheduler. */
int melee_web_story_state(uint32_t* map_mask, unsigned* count,
                          int* randall_timer, int* shy_timer,
                          int* shy_count, int* shy_pattern);

#ifdef __cplusplus
}
#endif

#endif
