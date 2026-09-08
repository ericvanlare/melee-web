#include "gameplay_crowd.h"
#include "gameplay_bootstrap.h"
#include <melee/sfx/crowdsfx.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <stdio.h>

extern void fn_803219AC(HSD_GObj*);

static uint64_t generation;
static HSD_GObj* owner;
static CrowdSFX_UnkStruct* previous_state;
static CrowdSFX_UnkStruct previous_value;
static int previous_static_state;

static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

static int success(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
    return 1;
}

static HSD_GObj* find_manager(void)
{
    if (!HSD_GObj_Entities) return NULL;
    HSD_GObj** links = (HSD_GObj**) HSD_GObj_Entities;
    for (HSD_GObj* gobj = links[0x17]; gobj; gobj = gobj->next) {
        for (HSD_GObjProc* proc = gobj->proc; proc; proc = proc->child) {
            if (proc->on_invoke == fn_803219AC) return gobj;
        }
    }
    return NULL;
}

int melee_web_crowd_active(void)
{
    return generation && generation == melee_web_gameplay_stats().generation && owner &&
           un_804D7050 == &un_804A2F08;
}

int melee_web_crowd_begin(char* error, size_t size)
{
    uint64_t current = melee_web_gameplay_stats().generation;
    if (!current || !HSD_GObj_Entities || !gCrowdConfig)
        return fail(error, size, "Original crowd startup requires a live common-configured world");
    if (generation || owner)
        return fail(error, size, "Original crowd manager is already owned");
    if (find_manager())
        return fail(error, size, "A pre-existing original crowd manager is active");
    previous_state = un_804D7050;
    previous_static_state = previous_state == &un_804A2F08;
    if (previous_static_state) previous_value = un_804A2F08;
    un_80321900();
    owner = find_manager();
    if (!owner || un_804D7050 != &un_804A2F08) {
        if (owner) HSD_GObjPLink_80390228(owner);
        owner = NULL;
        if (previous_static_state) un_804A2F08 = previous_value;
        un_804D7050 = previous_state;
        previous_state = NULL;
        previous_static_state = 0;
        return fail(error, size, "Original crowd startup did not publish its manager process");
    }
    generation = current;
    return success(error, size);
}

int melee_web_crowd_end(char* error, size_t size)
{
    if (!generation) return success(error, size);
    if (generation != melee_web_gameplay_stats().generation)
        return fail(error, size, "Original crowd manager source world changed");
    if (!owner || un_804D7050 != &un_804A2F08)
        return fail(error, size, "Original crowd manager ownership was lost");
    if (HSD_GObj_804D781C == owner)
        return fail(error, size, "Original crowd manager cannot be destroyed from its callback");
    /* These are the source's own voice cleanup paths; no audio operation is
     * replaced or suppressed by the host-owned lifetime wrapper. */
    un_80321C28();
    un_80321CE8();
    HSD_GObjPLink_80390228(owner);
    owner = NULL;
    generation = 0;
    if (previous_static_state) un_804A2F08 = previous_value;
    un_804D7050 = previous_state;
    previous_state = NULL;
    previous_static_state = 0;
    return success(error, size);
}
