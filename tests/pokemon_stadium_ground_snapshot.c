#include "gameplay_compat.h"
#include <melee/gr/types.h>

#include "pokemon_stadium_ground_snapshot.h"

#include <string.h>

int stadium_ground_snapshot(const void* decoded, StadiumGroundSnapshot* out)
{
    if (decoded == NULL || out == NULL)
        return 0;

    const struct GroundParam* ground = (const struct GroundParam*) decoded;
    if (ground->stage_param_count != 18 || ground->stage_params == NULL)
        return 0;

    StadiumGroundSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.y = ground->y;
    snapshot.stage_param_count = ground->stage_param_count;
    snapshot.row0_stkind = (uint32_t) ground->stage_params[0].stkind;
    snapshot.row0_x14 = ground->stage_params[0].x14;
    for (uint32_t i = 0; i < 18; ++i)
        snapshot.stage_ids[i] = (uint32_t) ground->stage_params[i].stkind;
    *out = snapshot;
    return 1;
}
