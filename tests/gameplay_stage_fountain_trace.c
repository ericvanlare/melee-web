#include "gameplay_compat.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* The reflection camera owns this source userdata shape in grIzumi.c. The
 * probe reads only the image identity; Ground->x18 remains the typed owner
 * field for the camera aliases. */
typedef struct FountainReflection {
    Mtx texture_matrix;
    HSD_ImageDesc* image;
} FountainReflection;

/* This is a read-only snapshot of the source light animation boundary. The
 * public map_plit records and the selected map light records are deliberately
 * reported separately: the former owns the source color table while the
 * latter owns the stage-selected path carrier used by the live WObj. */
typedef struct FountainLightAnimationSnapshot {
    uint32_t source_light_lists;
    uint32_t source_animation_tables;
    uint32_t source_animation_records;
    uint32_t source_active_records;
    uint32_t source_channel_mask;
    uint32_t live_light_count;
    uint32_t live_lobj_animations;
    uint32_t live_position_animations;
    uint32_t live_interest_animations;
    uint32_t lobj_flags[3];
    uint32_t position_flags[3];
    uint32_t interest_flags[3];
    uint32_t lobj_channel_mask[3];
    uint32_t position_channel_mask[3];
    uint32_t interest_channel_mask[3];
    float lobj_frames[3];
    float position_frames[3];
    float interest_frames[3];
    float lobj_end_frames[3];
    float position_end_frames[3];
    float interest_end_frames[3];
    uint8_t colors[12];
    float positions[9];
    uint32_t spline_carriers;
    uint32_t spline_flags[3];
    float spline_scales[9];
} FountainLightAnimationSnapshot;

/* These helpers observe the original Fountain callbacks after the host has
 * published their checked archive services. They never create, select, hide,
 * move, or repair a source object. */
int melee_web_test_stage_fountain_registry(uint32_t* mask, unsigned* count,
                                           unsigned* platform_count,
                                           unsigned* stage_gobj_count,
                                           unsigned* star_count)
{
    uint32_t registry_mask = 0;
    unsigned registry_count = 0;
    if (stage_info.grkind != Gr_Kind_Izumi)
        return 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i) {
        if (!stage_info.map_gobjs[i])
            continue;
        Ground* ground = stage_info.map_gobjs[i]->user_data;
        if (!ground || ground->map_id != (int)i)
            return 0;
        ++registry_count;
        if (i < 32)
            registry_mask |= UINT32_C(1) << i;
    }
    unsigned platforms = 0;
    unsigned stage_objects = 0;
    unsigned stars = 0;
    if (!HSD_GObj_Entities)
        return 0;
    /* Ground_GetStageGObj uses source p_link 5.  The map registry has one slot
     * per ID, but grIzumi_801CCBDC calls that constructor twice for map 4, so
     * the first platform is live only in this typed source list.  The complete
     * source count is maps 0,1,2,3 + two map-4 platforms + the map-id -1 star. */
    for (HSD_GObj* object = HSD_GObj_Entities->x14; object;
         object = object->next) {
        if (object->classifier != HSD_GOBJ_CLASS_STAGE || !object->user_data)
            continue;
        Ground* ground = object->user_data;
        ++stage_objects;
        if (ground->map_id == 4)
            ++platforms;
        if ((int)ground->map_id == -1)
            ++stars;
    }
    if (mask) *mask = registry_mask;
    if (count) *count = registry_count;
    if (platform_count) *platform_count = platforms;
    if (stage_gobj_count) *stage_gobj_count = stage_objects;
    if (star_count) *star_count = stars;
    return registry_mask == UINT32_C(0x1f) && registry_count == 5 &&
           platforms == 2 && stage_objects == 7 && stars == 1;
}

int melee_web_test_stage_fountain_platforms(int16_t state[2], int16_t timer[2],
                                             int16_t index[2], float current[2],
                                             float target[2], float positions[6],
                                             unsigned* count)
{
    if (stage_info.grkind != Gr_Kind_Izumi || !HSD_GObj_Entities)
        return 0;
    unsigned found = 0;
    int16_t slots[2] = {0};
    int16_t states[2] = {0}, timers[2] = {0};
    float currents[2] = {0}, targets[2] = {0}, world_positions[6] = {0};
    for (HSD_GObj* object = HSD_GObj_Entities->x14; object;
         object = object->next) {
        if (object->classifier != HSD_GOBJ_CLASS_STAGE || !object->user_data)
            continue;
        Ground* ground = object->user_data;
        if (ground->map_id != 4)
            continue;
        const int16_t slot = ground->u.izumi3.xC8;
        if (slot < 0 || slot > 1 || found >= 2)
            return 0;
        /* The source calls the constructor once for each slot. Reject a
         * duplicated slot instead of sorting guessed object order. */
        for (unsigned i = 0; i < found; ++i)
            if (slots[i] == slot)
                return 0;
        slots[found] = slot;
        states[found] = ground->u.izumi3.xC4;
        timers[found] = ground->u.izumi3.xC6;
        currents[found] = ground->u.izumi3.xD0;
        targets[found] = ground->u.izumi3.xD4;
        {
            Vec3 position;
            if (!object->hsd_obj)
                return 0;
            HSD_JObjGetTranslation((HSD_JObj*)object->hsd_obj, &position);
            world_positions[found * 3 + 0] = position.x;
            world_positions[found * 3 + 1] = position.y;
            world_positions[found * 3 + 2] = position.z;
        }
        ++found;
    }
    if (found == 2 && slots[0] > slots[1]) {
        int16_t slot = slots[0];
        slots[0] = slots[1]; slots[1] = slot;
        slot = states[0]; states[0] = states[1]; states[1] = slot;
        slot = timers[0]; timers[0] = timers[1]; timers[1] = slot;
        float value;
        value = currents[0]; currents[0] = currents[1]; currents[1] = value;
        value = targets[0]; targets[0] = targets[1]; targets[1] = value;
        for (unsigned i = 0; i < 3; ++i) {
            value = world_positions[i];
            world_positions[i] = world_positions[3 + i];
            world_positions[3 + i] = value;
        }
    }
    for (unsigned i = 0; i < found; ++i) {
        if (state) state[i] = states[i];
        if (timer) timer[i] = timers[i];
        if (index) index[i] = slots[i];
        if (current) current[i] = currents[i];
        if (target) target[i] = targets[i];
        if (positions) {
            positions[i * 3 + 0] = world_positions[i * 3 + 0];
            positions[i * 3 + 1] = world_positions[i * 3 + 1];
            positions[i * 3 + 2] = world_positions[i * 3 + 2];
        }
    }
    if (count) *count = found;
    return found == 2;
}

int melee_web_test_stage_fountain_dynamic_collision(float segments[12],
                                                     int* joint_ids,
                                                     unsigned* count)
{
    if (stage_info.grkind != Gr_Kind_Izumi)
        return 0;
    CollJoint* joints = mpGetGroundCollJoint();
    CollLine* lines = mpGetGroundCollLine();
    CollVtx* vertices = mpGetGroundCollVtx();
    if (!joints || !lines || !vertices)
        return 0;
    unsigned found = 0;
    /* grIz_803E0D60 binds source collision joints 0, 1 and 2 to map 3's
     * platform JObj at depths 1, 2 and 3. Their authored floor lines are
     * one-line ranges; observe the live vertices after mpLib updates them. */
    for (int i = 0; i < 3; ++i) {
        CollJoint* joint = &joints[i];
        if (!joint->x20 || !joint->inner || joint->inner->floor_count < 1)
            return 0;
        const int line_id = joint->inner->floor_start;
        if (line_id < 0 || line_id >= 34)
            return 0;
        MapLine* line = lines[line_id].x0;
        if (!line || line->v0_idx >= 2048 || line->v1_idx >= 2048)
            return 0;
        if (joint_ids) joint_ids[found] = i;
        if (segments) {
            segments[found * 4 + 0] = vertices[line->v0_idx].pos.x;
            segments[found * 4 + 1] = vertices[line->v0_idx].pos.y;
            segments[found * 4 + 2] = vertices[line->v1_idx].pos.x;
            segments[found * 4 + 3] = vertices[line->v1_idx].pos.y;
        }
        ++found;
    }
    if (count) *count = found;
    return found == 3;
}

int melee_web_test_stage_fountain_markers(float positions[27])
{
    if (stage_info.grkind != Gr_Kind_Izumi || !stage_info.param)
        return 0;
    const int ids[] = {0x7f, 0x80, 0x81, 0x82, 0x94, 0x95, 0x96,
                       0x97, 0x98};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        Vec3 position;
        if (!Ground_801C2D24(ids[i], &position) ||
            !isfinite(position.x) || !isfinite(position.y) ||
            !isfinite(position.z))
            return 0;
        if (positions) {
            positions[i * 3 + 0] = position.x;
            positions[i * 3 + 1] = position.y;
            positions[i * 3 + 2] = position.z;
        }
    }
    return stage_info.param->y == 0.75f;
}

int melee_web_test_stage_fountain_bounds(float camera[4], float blast[4])
{
    if (stage_info.grkind != Gr_Kind_Izumi || !stage_info.param ||
        stage_info.param->y != 0.75f)
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
        if (!isfinite(values[i]))
            return 0;
    if (camera) memcpy(camera, values, 4 * sizeof(float));
    if (blast) memcpy(blast, values + 4, 4 * sizeof(float));
    return 1;
}

static HSD_LObj* melee_web_stage_fountain_light_owner(uint16_t classifier)
{
    if (!HSD_GObj_Entities)
        return NULL;
    /* Source publishes two independent p_link-3 light owners. The global
     * fighter/map_plit owner is classifier 0xC; Ground_801C466C creates the
     * selected map light owner as classifier 0xD. Both have no user data. */
    for (HSD_GObj* object = HSD_GObj_Entities->xC; object;
         object = object->next) {
        if (object->classifier == classifier &&
            object->obj_kind == HSD_GObj_LightKind && object->hsd_obj &&
            object->user_data == NULL)
            return (HSD_LObj*)object->hsd_obj;
    }
    return NULL;
}

static HSD_LObj* melee_web_stage_fountain_scaled_lights(void)
{
    return melee_web_stage_fountain_light_owner(HSD_GOBJ_CLASS_GROUND);
}

static HSD_LObj* melee_web_stage_fountain_global_lights(void)
{
    return melee_web_stage_fountain_light_owner(0xC);
}

static uint32_t fountain_fobj_desc_mask(const HSD_FObjDesc* fobj,
                                        unsigned* count)
{
    uint32_t mask = 0;
    unsigned found = 0;
    for (unsigned guard = 0; fobj && guard < 32; ++guard, fobj = fobj->next) {
        if (fobj->type >= 32)
            return UINT32_MAX;
        mask |= UINT32_C(1) << fobj->type;
        ++found;
    }
    if (fobj != NULL)
        return UINT32_MAX;
    if (count)
        *count += found;
    return mask;
}

static uint32_t fountain_fobj_mask(const HSD_FObj* fobj)
{
    uint32_t mask = 0;
    for (unsigned guard = 0; fobj && guard < 32; ++guard, fobj = fobj->next) {
        if (fobj->obj_type >= 32)
            return UINT32_MAX;
        mask |= UINT32_C(1) << fobj->obj_type;
    }
    return fobj == NULL ? mask : UINT32_MAX;
}

static void fountain_snapshot_aobj(const HSD_AObj* aobj, uint32_t* flags,
                                   uint32_t* channels, float* frame,
                                   float* end_frame)
{
    if (!aobj)
        return;
    if (flags)
        *flags = aobj->flags;
    if (channels)
        *channels = fountain_fobj_mask(aobj->fobj);
    if (frame)
        *frame = aobj->curr_frame;
    if (end_frame)
        *end_frame = aobj->end_frame;
}

static int melee_web_test_stage_fountain_light_animation_owner(
    HSD_LObj* lights, FountainLightAnimationSnapshot* snapshot)
{
    if (!snapshot || stage_info.grkind != Gr_Kind_Izumi ||
        !stage_info.map_plit)
        return 0;
    memset(snapshot, 0, sizeof(*snapshot));

    for (LightList** cursor = stage_info.map_plit; *cursor; ++cursor) {
        ++snapshot->source_light_lists;
        if (!(*cursor)->anims)
            continue;
        ++snapshot->source_animation_tables;
        HSD_LightAnim* animation = (*cursor)->anims[0];
        for (unsigned guard = 0; animation && guard < 32;
             ++guard, animation = animation->next) {
            ++snapshot->source_animation_records;
            if (animation->aobjdesc || animation->position_anim ||
                animation->interest_anim)
                ++snapshot->source_active_records;
            if (animation->aobjdesc) {
                const uint32_t mask = fountain_fobj_desc_mask(
                    animation->aobjdesc->fobjdesc, NULL);
                if (mask == UINT32_MAX)
                    return 0;
                snapshot->source_channel_mask |= mask;
            }
        }
        if (animation != NULL)
            return 0;
    }

    if (!lights)
        return 0;
    unsigned found = 0;
    for (HSD_LObj* cursor = lights; cursor && found < 3;
         cursor = cursor->next, ++found) {
        const unsigned i = found;
        ++snapshot->live_light_count;
        if (cursor->aobj && cursor->aobj->fobj)
            ++snapshot->live_lobj_animations;
        fountain_snapshot_aobj(cursor->aobj, &snapshot->lobj_flags[i],
                               &snapshot->lobj_channel_mask[i],
                               &snapshot->lobj_frames[i],
                               &snapshot->lobj_end_frames[i]);
        memcpy(snapshot->colors + i * 4, &cursor->color, 4);

        if (cursor->position) {
            if (cursor->position->aobj && cursor->position->aobj->fobj)
                ++snapshot->live_position_animations;
            fountain_snapshot_aobj(
                cursor->position->aobj, &snapshot->position_flags[i],
                &snapshot->position_channel_mask[i],
                &snapshot->position_frames[i],
                &snapshot->position_end_frames[i]);
            snapshot->positions[i * 3 + 0] = cursor->position->pos.x;
            snapshot->positions[i * 3 + 1] = cursor->position->pos.y;
            snapshot->positions[i * 3 + 2] = cursor->position->pos.z;

            /* HSD_AObjLoadDesc resolves the source WObj path's obj_id to a
             * live HSD_JObj. Inspect that typed owner passively; this is the
             * source spline carrier, not a fixture-created replacement. */
            HSD_AObj* position_aobj = cursor->position->aobj;
            if (position_aobj && position_aobj->hsd_obj) {
                HSD_JObj* carrier = (HSD_JObj*) position_aobj->hsd_obj;
                if (carrier->flags & JOBJ_SPLINE) {
                    if (snapshot->spline_carriers >= 3)
                        return 0;
                    const unsigned carrier_index =
                        snapshot->spline_carriers++;
                    snapshot->spline_flags[carrier_index] = carrier->flags;
                    snapshot->spline_scales[carrier_index * 3 + 0] =
                        carrier->scale.x;
                    snapshot->spline_scales[carrier_index * 3 + 1] =
                        carrier->scale.y;
                    snapshot->spline_scales[carrier_index * 3 + 2] =
                        carrier->scale.z;
                }
            }
        }
        if (cursor->interest) {
            if (cursor->interest->aobj && cursor->interest->aobj->fobj)
                ++snapshot->live_interest_animations;
            fountain_snapshot_aobj(
                cursor->interest->aobj, &snapshot->interest_flags[i],
                &snapshot->interest_channel_mask[i],
                &snapshot->interest_frames[i],
                &snapshot->interest_end_frames[i]);
        }
    }
    return found == 3 && snapshot->live_light_count == 3;
}

int melee_web_test_stage_fountain_light_animation(
    FountainLightAnimationSnapshot* snapshot)
{
    return melee_web_test_stage_fountain_light_animation_owner(
        melee_web_stage_fountain_scaled_lights(), snapshot);
}

int melee_web_test_stage_fountain_global_light_animation(
    FountainLightAnimationSnapshot* snapshot)
{
    return melee_web_test_stage_fountain_light_animation_owner(
        melee_web_stage_fountain_global_lights(), snapshot);
}

int melee_web_test_stage_fountain_lights(uint32_t* count, uint16_t flags[3],
                                         uint8_t rgba[12])
{
    if (stage_info.grkind != Gr_Kind_Izumi || !stage_info.map_plit)
        return 0;
    unsigned found = 0;
    for (LightList** cursor = stage_info.map_plit; *cursor; ++cursor) {
        if (found >= 3 || !(*cursor)->desc)
            return 0;
        if (flags) flags[found] = (*cursor)->desc->flags;
        if (rgba) memcpy(rgba + found * 4, &(*cursor)->desc->color, 4);
        ++found;
    }
    if (count) *count = found;
    return found == 3;
}

int melee_web_test_stage_fountain_light_positions(float positions[6])
{
    if (stage_info.grkind != Gr_Kind_Izumi)
        return 0;
    /* Ground_801C2374 is the source global/fighter-light boundary. The
     * stage-selected classifier-0xD chain is constructed separately by
     * Ground_801C466C and is sampled by the animation probe below. */
    HSD_LObj* lights = melee_web_stage_fountain_global_lights();
    if (!lights)
        return 0;
    unsigned found = 0;
    for (HSD_LObj* cursor = lights; cursor; cursor = cursor->next) {
        if (!cursor->position)
            continue;
        if (found >= 2)
            return 0;
        if (positions) {
            positions[found * 3 + 0] = cursor->position->pos.x;
            positions[found * 3 + 1] = cursor->position->pos.y;
            positions[found * 3 + 2] = cursor->position->pos.z;
        }
        ++found;
    }
    return found == 2;
}

int melee_web_test_stage_fountain_reflection(void)
{
    if (stage_info.grkind != Gr_Kind_Izumi || !stage_info.map_gobjs[2] ||
        !stage_info.map_gobjs[3])
        return 0;
    Ground* reflection_owner = stage_info.map_gobjs[2]->user_data;
    Ground* ground = stage_info.map_gobjs[3]->user_data;
    if (!reflection_owner || !reflection_owner->x18 || !ground ||
        !ground->u.izumi.xC8 || !ground->u.izumi.xC4 ||
        !ground->u.izumi.xCC || !HSD_GObj_Entities)
        return 0;
    /* grIzumi_801CBE64 writes the reflection camera through the source
     * IzumiUnkCC view of map-2's Ground->x18. Read the typed Ground field so
     * this probe follows the native pointer width and destructor owner. */
    if (reflection_owner->x18 != ground->u.izumi.xC8)
        return 0;
    FountainReflection* reflection =
        (FountainReflection*) HSD_GObjGetUserData(ground->u.izumi.xC8);
    if (!reflection || !reflection->image ||
        ((HSD_TObj*) ground->u.izumi.xC4)->imagedesc != reflection->image)
        return 0;
    unsigned platform_aliases = 0;
    for (HSD_GObj* object = HSD_GObj_Entities->x14; object;
         object = object->next) {
        if (object->classifier != HSD_GOBJ_CLASS_STAGE || !object->user_data)
            continue;
        Ground* platform = object->user_data;
        if (platform->map_id == 4) {
            if (platform->x18 != ground->u.izumi.xC8)
                return 0;
            ++platform_aliases;
        }
    }
    if (platform_aliases != 2)
        return 0;
    for (HSD_GObj* object = HSD_GObj_Entities->x48; object;
         object = object->next) {
        if (object == ground->u.izumi.xC8 &&
            object->obj_kind == HSD_GObj_CameraKind && object->user_data)
            return 1;
    }
    return 0;
}

void* melee_web_test_stage_fountain_yakumono(void)
{
    return stage_info.yakumono_param;
}

int melee_web_test_stage_fountain_music(uint32_t mask, int* music,
                                        int* alternate)
{
    s32 selected = -1;
    const bool is_alternate = Ground_801C28AC(St_Kind_Izumi, mask, &selected);
    if (music) *music = selected;
    if (alternate) *alternate = is_alternate;
    return selected >= 0;
}

int melee_web_test_stage_fountain_empty(unsigned* registry_count,
                                        unsigned* stage_gobj_count)
{
    unsigned registry = 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i)
        if (stage_info.map_gobjs[i])
            ++registry;
    unsigned stage_objects = 0;
    if (HSD_GObj_Entities) {
        for (HSD_GObj* object = HSD_GObj_Entities->x14; object;
             object = object->next)
            if (object->classifier == HSD_GOBJ_CLASS_STAGE)
                ++stage_objects;
    }
    if (registry_count) *registry_count = registry;
    if (stage_gobj_count) *stage_gobj_count = stage_objects;
    return registry == 0 && stage_objects == 0;
}
