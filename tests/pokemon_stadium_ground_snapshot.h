#ifndef MELEE_WEB_POKEMON_STADIUM_GROUND_SNAPSHOT_H
#define MELEE_WEB_POKEMON_STADIUM_GROUND_SNAPSHOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StadiumGroundSnapshot {
    float y;
    int32_t stage_param_count;
    uint32_t stage_ids[18];
    uint32_t row0_stkind;
    int16_t row0_x14;
} StadiumGroundSnapshot;

int stadium_ground_snapshot(const void* decoded, StadiumGroundSnapshot* out);

#ifdef __cplusplus
}
#endif

#endif
