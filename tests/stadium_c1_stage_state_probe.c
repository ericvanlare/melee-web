#include "stadium_c1_stage_state_probe.h"

#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebStadiumC1StageInfoSnapshot {
    struct StageInfo saved;
    int restored;
};

static struct MeleeWebStadiumC1StageInfoSnapshot* active_snapshot;

static int snapshot_fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

static void copy_stage_info_view(const struct StageInfo* source,
                                 MeleeWebStadiumC1StageInfoView* view)
{
    view->grkind = (int32_t) source->grkind;
    view->xA0 = source->xA0;
    view->x6E4[0] = source->x6E4[0];
    view->x6E4[1] = source->x6E4[1];
    view->itemdata = source->itemdata;
    view->coll_data = source->coll_data;
    view->param = source->param;
    view->ald_yaku_all = source->ald_yaku_all;
    view->map_ptcl = source->map_ptcl;
    view->map_texg = source->map_texg;
    view->yakumono_param = source->yakumono_param;
    view->map_plit = source->map_plit;
    view->quake_model_set = source->quake_model_set;
}

/* Source grDatFiles_8049EE10 is authored as UnkArchiveStruct[4]. */
enum {
    SOURCE_GRDATFILES_SLOT_COUNT = 4,
    /* The stage registry link walked by gameplay_stage_map.c. */
    SOURCE_STAGE_GOBJ_ENTITY_LINK = 5,
};

uint32_t melee_web_stadium_c1_stage_state_failures(void)
{
    uint32_t failures = melee_web_stadium_c1_stage_object_failures();
    UnkArchiveStruct* const ordinary = grDatFiles_GetArchive();
    if (ordinary == NULL) {
        failures |= MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT;
    } else {
        for (size_t i = 0; i < SOURCE_GRDATFILES_SLOT_COUNT; ++i) {
            if (ordinary[i].unk0 != NULL || ordinary[i].unk4 != NULL ||
                ordinary[i].unk8 != 0) {
                failures |= MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT;
                break;
            }
        }
    }
    return failures;
}

uint32_t melee_web_stadium_c1_stage_object_failures(void)
{
    uint32_t failures = 0;
    HSD_GObj** const entities = (HSD_GObj**) HSD_GObj_Entities;

    if (entities == NULL) {
        failures |= MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE;
    } else {
        for (size_t i = 0; i < sizeof(stage_info.map_gobjs) /
                                    sizeof(stage_info.map_gobjs[0]); ++i) {
            if (stage_info.map_gobjs[i] != NULL) {
                failures |= MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ;
                break;
            }
        }
        for (HSD_GObj* object = entities[SOURCE_STAGE_GOBJ_ENTITY_LINK]; object;
             object = object->next) {
            if (object->classifier == HSD_GOBJ_CLASS_STAGE) {
                failures |= MELEE_WEB_STADIUM_C1_STAGE_INSTANCE;
                break;
            }
        }
        if (Ground_801C498C() != NULL)
            failures |= MELEE_WEB_STADIUM_C1_GROUND_GOBJ;
    }

    if (stage_info.itemdata != NULL)
        failures |= MELEE_WEB_STADIUM_C1_STAGE_ITEMS;
    if (stage_info.map_plit != NULL)
        failures |= MELEE_WEB_STADIUM_C1_STAGE_LIGHTS;
    return failures;
}

MeleeWebStadiumC1StageInfoSnapshot*
melee_web_stadium_c1_stage_info_snapshot_begin(char* error,
                                                size_t error_size)
{
    if (active_snapshot) {
        snapshot_fail(error, error_size,
                      "A test StageInfo snapshot is already active");
        return NULL;
    }
    struct MeleeWebStadiumC1StageInfoSnapshot* snapshot =
        malloc(sizeof(*snapshot));
    if (!snapshot) {
        snapshot_fail(error, error_size,
                      "Cannot allocate the test StageInfo snapshot");
        return NULL;
    }
    memcpy(&snapshot->saved, &stage_info, sizeof(snapshot->saved));
    snapshot->restored = 0;
    active_snapshot = snapshot;
    if (error && error_size) error[0] = '\0';
    return snapshot;
}

int melee_web_stadium_c1_stage_info_snapshot_view(
    const MeleeWebStadiumC1StageInfoSnapshot* snapshot,
    MeleeWebStadiumC1StageInfoView* view)
{
    if (!snapshot || snapshot != active_snapshot || !view) return 0;
    copy_stage_info_view(&snapshot->saved, view);
    return 1;
}

int melee_web_stadium_c1_stage_info_current_view(
    MeleeWebStadiumC1StageInfoView* view)
{
    if (!view) return 0;
    copy_stage_info_view(&stage_info, view);
    return 1;
}

int melee_web_stadium_c1_stage_info_snapshot_restore(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size)
{
    if (!snapshot || snapshot != active_snapshot || snapshot->restored)
        return snapshot_fail(error, error_size,
                             "The test StageInfo snapshot is not restorable");
    memcpy(&stage_info, &snapshot->saved, sizeof(snapshot->saved));
    if (memcmp(&stage_info, &snapshot->saved, sizeof(snapshot->saved)) != 0)
        return snapshot_fail(error, error_size,
                             "Full source StageInfo restoration did not match its snapshot");
    snapshot->restored = 1;
    if (error && error_size) error[0] = '\0';
    return 1;
}

int melee_web_stadium_c1_stage_info_snapshot_release(
    MeleeWebStadiumC1StageInfoSnapshot* snapshot, char* error,
    size_t error_size)
{
    if (!snapshot || snapshot != active_snapshot || !snapshot->restored)
        return snapshot_fail(error, error_size,
                             "The test StageInfo snapshot must be restored before release");
    active_snapshot = NULL;
    free(snapshot);
    if (error && error_size) error[0] = '\0';
    return 1;
}
