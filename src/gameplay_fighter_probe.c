#include "gameplay_fighter_probe.h"
#include "gameplay_bootstrap.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ftwalkcommon.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/objalloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

MELEE_WEB_COMMON_ASSERT_LAYOUT(struct ftCommonData);
_Static_assert(sizeof(Fighter) == 0x23ec, "Original Fighter storage size");

struct MeleeWebFighterInputProbe {
    struct MeleeWebFighterInputProbe* next;
    HSD_GObj* object;
    Fighter* fighter;
    struct ftCommonData common;
    uint64_t generation;
};
static HSD_ObjAllocData fighter_pool;
static uint64_t pool_generation;
static MeleeWebFighterInputProbe* owners;

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
static int live(MeleeWebFighterInputProbe* probe, char* error, size_t size)
{
    if (!probe || !probe->object || !probe->fighter ||
        probe->generation != melee_web_gameplay_stats().generation)
        return fail(error, size, "Fighter-input probe has no live gameplay world");
    return 1;
}
static void release_fighter(void* data)
{
    MeleeWebFighterInputProbe* probe = owners;
    while (probe && probe->fighter != data) probe = probe->next;
    if (!probe) { fputs("Fighter-input owner is missing during destruction\n", stderr); abort(); }
    HSD_ObjFree(&fighter_pool, probe->fighter);
    probe->fighter = NULL;
    probe->object = NULL;
}

MeleeWebFighterInputProbe* melee_web_fighter_input_probe_create(
    const MeleeWebCommonScalars* common, char* error, size_t error_size)
{
    const uint64_t world = melee_web_gameplay_stats().generation;
    if (!world || !common || !isfinite(common->walk_stick_threshold)) {
        fail(error, error_size, "Fighter-input probe requires a live world and finite typed walk threshold");
        return NULL;
    }
    MeleeWebFighterInputProbe* probe = calloc(1, sizeof(*probe));
    if (!probe) { fail(error, error_size, "Unable to allocate fighter-input owner"); return NULL; }
    if (pool_generation != world) {
        HSD_ObjAllocInit(&fighter_pool, sizeof(Fighter), 4);
        pool_generation = world;
    }
    probe->fighter = HSD_ObjAlloc(&fighter_pool);
    if (!probe->fighter) {
        free(probe); fail(error, error_size, "Original Fighter storage allocation failed"); return NULL;
    }
    probe->object = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, 8, 0);
    if (!probe->object) {
        HSD_ObjFree(&fighter_pool, probe->fighter); free(probe);
        fail(error, error_size, "Original Fighter GObj allocation failed"); return NULL;
    }
    probe->generation = world;
    /* This is the entire read set of ftWalkCommon_800DFC70. Unknown scalar
     * slots are not converted into native pointers, and no full root table is
     * published. A complete Fighter initialization needs the other roots. */
    probe->common.walk_stick_threshold = common->walk_stick_threshold;
    probe->next = owners;
    owners = probe;
    GObj_InitUserData(probe->object, 0, release_fighter, probe->fighter);
    Fighter_ResetInputData_80068854(probe->object);
    probe->fighter->facing_dir = 1.0F;
    success(error, error_size);
    return probe;
}

int melee_web_fighter_input_probe_reset(MeleeWebFighterInputProbe* probe, char* error, size_t error_size)
{
    if (!live(probe, error, error_size)) return 0;
    Fighter_ResetInputData_80068854(probe->object);
    return success(error, error_size);
}

int melee_web_fighter_input_probe_read(MeleeWebFighterInputProbe* probe, MeleeWebFighterInputResult* result,
                                      char* error, size_t error_size)
{
    if (!live(probe, error, error_size)) return 0;
    if (!result) return fail(error, error_size, "Fighter-input result storage is missing");
    Fighter* fighter = probe->fighter;
    struct ftCommonData* saved = p_ftCommonData;
    p_ftCommonData = &probe->common;
    const int can_walk = ftWalkCommon_800DFC70(probe->object);
    p_ftCommonData = saved;
    *result = (MeleeWebFighterInputResult) {
        fighter->input.lstick[0].x, fighter->facing_dir, probe->common.walk_stick_threshold,
        fighter->input.held_buttons[0], fighter->input.pressed_buttons, fighter->input.released_buttons,
        fighter->x670_timer_lstick_tilt_x, fighter->x671_timer_lstick_tilt_y,
        fighter->trigger_analog_timer, can_walk};
    return success(error, error_size);
}

int melee_web_fighter_input_probe_sample(MeleeWebFighterInputProbe* probe, float stick_x, float facing,
                                        MeleeWebFighterInputResult* result, char* error, size_t error_size)
{
    if (!live(probe, error, error_size)) return 0;
    if (!result || !isfinite(stick_x) || stick_x < -1.0F || stick_x > 1.0F ||
        (facing != -1.0F && facing != 1.0F))
        return fail(error, error_size, "Fighter-input probe requires a normalized stick and facing direction ±1");
    probe->fighter->input.lstick[0].x = stick_x;
    probe->fighter->facing_dir = facing;
    return melee_web_fighter_input_probe_read(probe, result, error, error_size);
}

int melee_web_fighter_input_probe_destroy(MeleeWebFighterInputProbe* probe, char* error, size_t error_size)
{
    if (!probe) return success(error, error_size);
    if (probe->object) {
        if (!live(probe, error, error_size)) return 0;
        if (probe->object == HSD_GObj_804D781C)
            return fail(error, error_size, "Cannot destroy a fighter-input owner inside its current callback");
        HSD_GObjPLink_80390228(probe->object);
    }
    MeleeWebFighterInputProbe** cursor = &owners;
    while (*cursor != probe) cursor = &(*cursor)->next;
    *cursor = probe->next;
    free(probe);
    return success(error, error_size);
}
