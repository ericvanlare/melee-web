#include "gameplay_compat.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <stdint.h>
#include <string.h>

/* Observation-only probe for the source grBattle_OnInit result. The map
 * object slots are queried after the stage owner has run the original callback;
 * this helper never creates, selects or mutates a stage object. */
int melee_web_test_stage_battlefield_state(uint32_t* map_mask, unsigned* count)
{
    uint32_t mask = 0;
    unsigned objects = 0;
    /* Map 1 is only the initial background.  grBattle_BG_Callback2 destroys
     * it after the first transition and replaces it with map 2 or 4.  The
     * stage's persistent objects are 0, 3 and 6; one of 1, 2 or 4 must be
     * live at every point in the source background lifecycle. */
    if (stage_info.grkind != Gr_Kind_Battle || !stage_info.map_gobjs[0] ||
        !stage_info.map_gobjs[3] || !stage_info.map_gobjs[6])
        return 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i) {
        if (stage_info.map_gobjs[i]) {
            ++objects;
            if (i < 32) mask |= UINT32_C(1) << i;
        }
    }
    if (map_mask) *map_mask = mask;
    if (count) *count = objects;
    return objects >= 4 && objects <= 7 &&
           (mask & (UINT32_C(1) << 1 | UINT32_C(1) << 2 |
                    UINT32_C(1) << 4)) != 0;
}

/* Read the original background state machine without changing its timer,
 * current/previous selection, or map registry.  grBattle stores this state on
 * the live map-3 Ground object, so this also proves the source callback owns
 * the object that the scheduler will update. */
int melee_web_test_stage_battlefield_background(int* state, int* current,
                                                int* previous, int* timer)
{
    if (stage_info.grkind != Gr_Kind_Battle || !stage_info.map_gobjs[3])
        return 0;
    Ground* ground = stage_info.map_gobjs[3]->user_data;
    if (!ground || ground->map_id != 3)
        return 0;
    if (state) *state = ground->u.battle_bg.state;
    if (current) *current = ground->u.battle_bg.curr;
    if (previous) *previous = ground->u.battle_bg.prev;
    if (timer) *timer = ground->u.battle_bg.timer;
    return ground->u.battle_bg.state >= 0 && ground->u.battle_bg.state <= 2;
}

/* Return source-derived camera/dead ranges after Ground_801C39C0 and
 * Ground_801C3BB4.  Battlefield's GroundParam::y is 0.8, so these values
 * catch a missing synthetic scale parent in the marker owner. */
int melee_web_test_stage_battlefield_bounds(float camera[4], float blast[4])
{
    if (stage_info.grkind != Gr_Kind_Battle || !stage_info.param)
        return 0;
    if (camera) {
        camera[0] = stage_info.cam_info.cam_bounds.left;
        camera[1] = stage_info.cam_info.cam_bounds.bottom;
        camera[2] = stage_info.cam_info.cam_bounds.right;
        camera[3] = stage_info.cam_info.cam_bounds.top;
    }
    if (blast) {
        blast[0] = stage_info.blast_zone.left;
        blast[1] = stage_info.blast_zone.bottom;
        blast[2] = stage_info.blast_zone.right;
        blast[3] = stage_info.blast_zone.top;
    }
    return stage_info.param->y == 0.8f;
}
