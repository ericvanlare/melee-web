#include "gameplay_compat.h"
#include "gameplay_stage_map.h"
#include "gameplay_stage_context.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* These probes observe the source grShrine callbacks after the host has
 * published the decoded archive services.  They do not create or repair
 * stage state. */
int melee_web_test_stage_temple_state(uint32_t* map_mask, unsigned* count)
{
    uint32_t mask = 0;
    unsigned objects = 0;
    if (stage_info.grkind != Gr_Kind_Shrine)
        return 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i) {
        if (!stage_info.map_gobjs[i])
            continue;
        ++objects;
        if (i < 32)
            mask |= UINT32_C(1) << i;
        Ground* ground = stage_info.map_gobjs[i]->user_data;
        if (!ground || ground->map_id != (int)i)
            return 0;
    }
    if (map_mask) *map_mask = mask;
    if (count) *count = objects;
    return objects == 3 && mask == UINT32_C(0x7) &&
           stage_info.map_gobjs[0] && stage_info.map_gobjs[1] &&
           stage_info.map_gobjs[2];
}

int melee_web_test_stage_temple_bounds(float camera[4], float blast[4])
{
    if (stage_info.grkind != Gr_Kind_Shrine || !stage_info.param ||
        stage_info.param->y != 0.9f)
        return 0;
    const float values[] = {
        stage_info.cam_info.cam_bounds.left,
        stage_info.cam_info.cam_bounds.bottom,
        stage_info.cam_info.cam_bounds.right,
        stage_info.cam_info.cam_bounds.top,
        stage_info.blast_zone.left,
        stage_info.blast_zone.bottom,
        stage_info.blast_zone.right,
        stage_info.blast_zone.top,
    };
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) return 0;
    if (values[0] > values[2] || values[1] > values[3] ||
        values[4] > values[6] || values[5] > values[7])
        return 0;
    if (camera) memcpy(camera, values, 4 * sizeof(float));
    if (blast) memcpy(blast, values + 4, 4 * sizeof(float));
    return 1;
}

int melee_web_test_stage_temple_lights(uint32_t* count, uint16_t flags[3],
                                       uint8_t rgba[12])
{
    if (stage_info.grkind != Gr_Kind_Shrine || !stage_info.map_plit)
        return 0;
    unsigned lights = 0;
    for (LightList** cursor = stage_info.map_plit; *cursor; ++cursor) {
        if (lights >= 3 || !(*cursor)->desc) {
            if (count) *count = lights + 1;
            return 0;
        }
        if (flags) flags[lights] = (*cursor)->desc->flags;
        if (rgba) memcpy(rgba + lights * 4, &(*cursor)->desc->color, 4);
        ++lights;
    }
    if (count) *count = lights;
    return lights == 3;
}

void* melee_web_test_stage_temple_yakumono(void)
{
    return stage_info.yakumono_param;
}

int melee_web_test_stage_temple_ground_kind(void)
{
    return stage_info.grkind;
}

static HSD_LObj* melee_web_stage_temple_scaled_lights(void)
{
    if (!HSD_GObj_Entities) return NULL;
    /* Fighter_FirstInitialize_80067A84 reaches the original
     * ftCo_8009F4A4, which creates the shared stage-light owner and calls
     * Ground_801C2374 on its live HSD_LObj chain. The later host-owned stage
     * light GObj carries non-null user data; select the earlier source owner
     * through the typed p-link field rather than guessing an array index. */
    for (HSD_GObj* object = HSD_GObj_Entities->xC; object;
         object = object->next) {
        if (object->obj_kind == HSD_GObj_LightKind && object->hsd_obj &&
            object->user_data == NULL)
            return (HSD_LObj*) object->hsd_obj;
    }
    return NULL;
}

int melee_web_test_stage_temple_light_positions(float positions[6])
{
    if (stage_info.grkind != Gr_Kind_Shrine) return 0;
    HSD_LObj* lights = melee_web_stage_temple_scaled_lights();
    if (!lights) return 0;
    /* Ground_801C2374 has already run at the source fighter light boundary.
     * The HSD_LightDesc positions above remain authored/raw coordinates. */
    unsigned positioned = 0;
    for (HSD_LObj* cursor = lights; cursor; cursor = cursor->next) {
        if (!cursor->position) continue;
        if (positioned >= 2) return 0;
        if (positions) {
            positions[positioned * 3 + 0] = cursor->position->pos.x;
            positions[positioned * 3 + 1] = cursor->position->pos.y;
            positions[positioned * 3 + 2] = cursor->position->pos.z;
        }
        ++positioned;
    }
    return positioned == 2;
}

int melee_web_test_stage_temple_music(uint32_t mask, int* music, int* alternate)
{
    s32 selected = -1;
    const bool is_alternate = Ground_801C28AC(St_Kind_Shrine, mask, &selected);
    if (music) *music = selected;
    if (alternate) *alternate = is_alternate;
    return selected >= 0;
}

int melee_web_test_stage_temple_overrides(uint8_t values[3], unsigned* count)
{
    if (stage_info.grkind != Gr_Kind_Shrine || !stage_info.map_plit)
        return 0;
    /* These are the three global map_plit descriptors, owned separately from
     * the per-map-entry light lists. Observe the same identity registry used
     * by Ground_801C20E0 for this source light chain. */
    unsigned found = 0;
    for (LightList** cursor = stage_info.map_plit; *cursor; ++cursor) {
        if (found >= 3 || !(*cursor)->desc) return 0;
        int has_override = 0;
        uint8_t flags = 0;
        if (!melee_web_stage_lights_lookup_override((*cursor)->desc,
                                                    &has_override, &flags) ||
            !has_override)
            return 0;
        if (values) values[found] = flags;
        ++found;
    }
    if (count) *count = found;
    return found == 3;
}

int melee_web_test_stage_temple_empty(unsigned* count)
{
    unsigned objects = 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i)
        if (stage_info.map_gobjs[i]) ++objects;
    if (count) *count = objects;
    return objects == 0;
}
