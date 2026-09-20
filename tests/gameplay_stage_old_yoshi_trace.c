#include "gameplay_compat.h"
#include "gameplay_stage_context.h"
#include <melee/gr/groldyoshi.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/lb/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* These probes observe the original grOy owner after the host has published
 * the decoded archive services. They do not create objects or repair source
 * state. */
extern StageCallbacks grOy_StageCallbacks[];
extern s16 grOy_803E6574[];
extern StageData grOy_StageData;

static Ground_GObj* old_yoshi_gobj(int map_id)
{
    if (map_id < 0 || map_id >= 6)
        return NULL;
    return stage_info.map_gobjs[map_id];
}

static int old_yoshi_registry_fail(const char* reason)
{
    fprintf(stderr, "Old Yoshi registry: %s\n", reason);
    return 0;
}

int melee_web_test_stage_old_yoshi_registry(uint32_t* map_mask, unsigned* count,
                                            int order[6])
{
    int valid = 1;
    if (stage_info.grkind != Gr_Kind_OldYoshi)
        valid = old_yoshi_registry_fail("stage kind mismatch");
    if (grOy_StageData.callbacks != grOy_StageCallbacks)
        valid = old_yoshi_registry_fail("StageData callback table mismatch");

    uint32_t mask = 0;
    unsigned objects = 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i) {
        Ground_GObj* gobj = stage_info.map_gobjs[i];
        if (!gobj) {
            if (i < 6) {
                fprintf(stderr, "Old Yoshi registry: map[%u] is missing\n", i);
                valid = 0;
            }
            continue;
        }
        if (i >= 6) {
            fprintf(stderr, "Old Yoshi registry: unexpected map[%u] is live\n", i);
            valid = 0;
            ++objects;
            continue;
        }
        ++objects;
        if (i < 32)
            mask |= UINT32_C(1) << i;
        Ground* ground = gobj->user_data;
        if (!ground) {
            fprintf(stderr, "Old Yoshi registry: map[%u] user data is missing\n", i);
            valid = 0;
            continue;
        }
        if (ground->map_id != (int)i) {
            fprintf(stderr, "Old Yoshi registry: map[%u] has map_id=%d\n", i,
                    ground->map_id);
            valid = 0;
        }
        if (ground->x1C_callback != grOy_StageCallbacks[i].callback3) {
            fprintf(stderr,
                    "Old Yoshi registry: map[%u] callback identity mismatch\n",
                    i);
            valid = 0;
        }
    }
    if (map_mask)
        *map_mask = mask;
    if (count)
        *count = objects;
    if (objects != 6 || mask != UINT32_C(0x3f)) {
        fprintf(stderr, "Old Yoshi registry: map count=%u mask=0x%08x\n",
                objects, (unsigned) mask);
        valid = 0;
    }

    /* Ground_GetStageGObj creates p-link 5 objects with equal priority.
     * GObj_Create selects gobj_first_lower_prio, whose equal-priority scan
     * stops at the existing tail; GObj_PReorder therefore appends each new
     * object and preserves grOldYoshi_8020E79C's source order 0,1,4,5,2,3. */
    int observed_order[6] = {};
    unsigned found = 0;
    HSD_GObj* cursor = HSD_GObj_Entities ? HSD_GObj_Entities->x14 : NULL;
    if (!HSD_GObj_Entities) {
        fprintf(stderr, "Old Yoshi registry: GObj entity lists are missing\n");
        valid = 0;
    }
    for (; cursor; cursor = cursor->next) {
        for (unsigned i = 0; i < 6; ++i) {
            if ((HSD_GObj*) old_yoshi_gobj((int)i) != cursor)
                continue;
            if (found >= 6)
                return 0;
            observed_order[found++] = (int)i;
            break;
        }
    }
    if (found != 6) {
        fprintf(stderr, "Old Yoshi registry: p-link 5 stage map count=%u\n",
                found);
        valid = 0;
    }
    const int expected_source_order[6] = {0, 1, 4, 5, 2, 3};
    if (found == 6) {
        for (unsigned i = 0; i < 6; ++i) {
            const int value = observed_order[i];
            if (order)
                order[i] = value;
            if (value != expected_source_order[i]) {
                fprintf(stderr,
                        "Old Yoshi registry: source order mismatch at %u: got=%d expected=%d\n",
                        i, value, expected_source_order[i]);
                valid = 0;
            }
        }
    }
    return valid;
}

int melee_web_test_stage_old_yoshi_clouds(
    int16_t state[3], uint8_t touching[3], uint8_t counters[3],
    float displacement[3], float velocity[3], uint8_t hidden[3],
    uint8_t enabled[3])
{
    Ground_GObj* gobj = old_yoshi_gobj(2);
    if (stage_info.grkind != Gr_Kind_OldYoshi || !gobj || !gobj->user_data ||
        !gobj->hsd_obj)
        return 0;
    Ground* ground = gobj->user_data;
    CollJoint* joints = mpGetGroundCollJoint();
    if (!joints)
        return 0;
    for (unsigned i = 0; i < 3; ++i) {
        const struct grOldYoshi_Cloud* cloud = &ground->u.oldyoshicloud.cloud[i];
        if (state) state[i] = (int16_t) cloud->xC4_0123;
        if (touching) touching[i] = (uint8_t) cloud->xC4_4;
        if (counters) counters[i] = (uint8_t) cloud->xC4_567;
        if (displacement) displacement[i] = cloud->xD0;
        if (velocity) velocity[i] = cloud->xD4;
        if (!cloud->xC8 || !isfinite(cloud->xD0) || !isfinite(cloud->xD4))
            return 0;
        if (hidden)
            hidden[i] = (uint8_t) ((HSD_JObjGetFlags(cloud->xC8) & JOBJ_HIDDEN) != 0);
        if (enabled)
            enabled[i] = (uint8_t) ((joints[grOy_803E6574[i * 2]].flags &
                                     CollJoint_Enabled) != 0);
    }
    return 1;
}

int melee_web_test_stage_old_yoshi_cloud_callbacks(uint8_t installed[3])
{
    Ground_GObj* gobj = old_yoshi_gobj(2);
    if (stage_info.grkind != Gr_Kind_OldYoshi || !gobj || !gobj->user_data)
        return 0;
    for (unsigned i = 0; i < 3; ++i) {
        mpLib_JointCollisionCallback callback = NULL;
        void* user_data = NULL;
        mpJointGetCb1(grOy_803E6574[i * 2], &callback, &user_data);
        const uint8_t active =
            (uint8_t) (callback != NULL && user_data == gobj->user_data);
        if (installed) installed[i] = active;
        if (!active)
            return 0;
    }
    return 1;
}

int melee_web_test_stage_old_yoshi_trigger_contact(unsigned cloud_index)
{
    Ground_GObj* gobj = old_yoshi_gobj(2);
    if (stage_info.grkind != Gr_Kind_OldYoshi || cloud_index >= 3 ||
        !gobj || !gobj->user_data)
        return 0;
    mpLib_JointCollisionCallback callback = NULL;
    void* user_data = NULL;
    const int joint_id = grOy_803E6574[cloud_index * 2];
    mpJointGetCb1(joint_id, &callback, &user_data);
    if (!callback || user_data != gobj->user_data)
        return 0;
    CollData collision;
    memset(&collision, 0, sizeof(collision));
    collision.x34_flags.b1234 = 1;
    callback(user_data, joint_id, &collision, 0, mpLib_GroundEnum_Unk0, 0.0f);
    Ground* ground = gobj->user_data;
    return ground->u.oldyoshicloud.cloud[cloud_index].xC4_4 == 1;
}

int melee_web_test_stage_old_yoshi_guest(int16_t* timer, int16_t* child,
                                         uint8_t* root_hidden,
                                         uint8_t* child_visible)
{
    Ground_GObj* gobj = old_yoshi_gobj(3);
    if (stage_info.grkind != Gr_Kind_OldYoshi || !gobj || !gobj->user_data ||
        !gobj->hsd_obj)
        return 0;
    Ground* ground = gobj->user_data;
    const int selected = ground->u.oldyoshiguest.xC6;
    if (timer) *timer = ground->u.oldyoshiguest.xC4;
    if (child) *child = (int16_t) selected;
    if (root_hidden)
        *root_hidden = (uint8_t) ((HSD_JObjGetFlags(gobj->hsd_obj) &
                                   JOBJ_HIDDEN) != 0);
    if (child_visible) {
        uint8_t visible = 0;
        if (selected >= 1 && selected <= 5) {
            HSD_JObj* child_jobj = Ground_801C3FA4(gobj, selected);
            if (child_jobj)
                visible = (uint8_t) ((HSD_JObjGetFlags(child_jobj) &
                                     JOBJ_HIDDEN) == 0);
        }
        *child_visible = visible;
    }
    return 1;
}

void* melee_web_test_stage_old_yoshi_yakumono(void)
{
    return stage_info.yakumono_param;
}

int melee_web_test_stage_old_yoshi_empty(unsigned* objects)
{
    unsigned count = 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i)
        if (stage_info.map_gobjs[i])
            ++count;
    if (objects)
        *objects = count;
    return count == 0;
}
