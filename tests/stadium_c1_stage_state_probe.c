#include "stadium_c1_stage_state_probe.h"

#include <melee/gr/grdatfiles.h>
#include <melee/gr/grpstadium.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/it/it_3F14.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebStadiumC1StageInfoSnapshot {
    struct StageInfo saved;
    int restored;
};

static struct MeleeWebStadiumC1StageInfoSnapshot* active_snapshot;

int melee_web_stadium_c1_item_runtime_globals_view(
    MeleeWebStadiumC1ItemRuntimeGlobalsView* view)
{
    if (!view) return 0;
    view->public_data = it_804D6D20;
    view->common_articles = it_804D6D24;
    view->common_data = it_804D6D28;
    view->pokemon_articles = it_804D6D30;
    view->character_articles = it_804D6D38;
    view->bounce_data = it_804D6D40;
    view->color_rows = it_804D6D04;
    return 1;
}

int melee_web_stadium_c1_item_public_data_view(
    void* public_data, MeleeWebStadiumC1ItemPublicDataView* view)
{
    if (!public_data || !view) return 0;
    const it_804D6D20_t* source = public_data;
    view->common_data = source->x0;
    view->common_articles = source->x4;
    view->character_articles = source->x8;
    view->pokemon_articles = source->xC;
    view->bounce_data = source->x10;
    view->color_rows = source->x14;
    return 1;
}

int melee_web_stadium_c1_random_article_state_row(
    void* value, uint32_t row, void** state_table, void** script)
{
    if (!value || !state_table || !script || row >= 8) return 0;
    Article* article = value;
    if (!article->xC_itemStates) return 0;
    *state_table = article->xC_itemStates;
    *script = article->xC_itemStates->x0_itemStateDesc[row].xC_script;
    return 1;
}

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

struct MeleeWebStadiumC1FtDeviceSnapshot {
    struct ftDeviceUnk3 first[1];
    struct ftDeviceUnk5 bury_things[2];
    struct ftDeviceUnk3 third[1];
    struct ftDeviceUnk4 fourth;
    int first_count;
    int bury_thing_count;
    const void* addresses[6];
};

size_t melee_web_stadium_c1_ground_map_slot_count(void)
{
    return sizeof(stage_info.map_gobjs) / sizeof(stage_info.map_gobjs[0]);
}

void* melee_web_stadium_c1_ground_map_slot(size_t index)
{
    if (index >= melee_web_stadium_c1_ground_map_slot_count()) return NULL;
    return stage_info.map_gobjs[index];
}

size_t melee_web_stadium_c1_ground_marker_slot_count(void)
{
    return sizeof(stage_info.x280) / sizeof(stage_info.x280[0]);
}

void* melee_web_stadium_c1_ground_marker_slot(size_t index)
{
    if (index >= melee_web_stadium_c1_ground_marker_slot_count()) return NULL;
    return stage_info.x280[index];
}

int melee_web_stadium_c1_ground_map_profile(
    int map_id, MeleeWebStadiumC1GroundStageProfile* profile)
{
    if (profile == NULL || map_id != 1 ||
        (size_t) map_id >= melee_web_stadium_c1_ground_map_slot_count())
        return 0;
    profile->grkind = (int32_t) grPs_StageData.grkind;
    profile->callback_row_present = grPs_StageData.callbacks != NULL;
    profile->callback_flags_b2 = profile->callback_row_present
                                     ? grPs_StageData.callbacks[map_id].flags_b2
                                     : 0;
    profile->joint_count = grPs_StageData.joint_count;
    profile->joint_table_present = profile->joint_count == 0 ||
                                   grPs_StageData.joints != NULL;
    profile->collision_row_present = 0;
    if (grPs_StageData.joints != NULL) {
        for (size_t i = 0; i < grPs_StageData.joint_count; ++i) {
            if (grPs_StageData.joints[i].y == map_id) {
                profile->collision_row_present = 1;
                break;
            }
        }
    }
    return 1;
}

void* melee_web_stadium_c1_ground_map_lookup(int map_id)
{
    return Ground_GetMapGObj(map_id);
}

void* melee_web_stadium_c1_ground_map_create(int map_id)
{
    return Ground_GetStageGObj(map_id);
}

int melee_web_stadium_c1_ground_map_remove(void* object)
{
    if (object == NULL) return 0;
    Ground_801C4A08((HSD_GObj*) object);
    return 1;
}

void* melee_web_stadium_c1_ground_map_joint(void* object, int depth)
{
    if (object == NULL) return NULL;
    return Ground_801C3FA4((HSD_GObj*) object, depth);
}

int melee_web_stadium_c1_ground_map_object_view(
    void* user_data, MeleeWebStadiumC1GroundMapObjectView* view)
{
    if (user_data == NULL || view == NULL) return 0;
    Ground* ground = (Ground*) user_data;
    view->map_id = ground->map_id;
    view->gobj = ground->gobj;
    view->camera = ground->x18;
    return 1;
}

MeleeWebStadiumC1FtDeviceSnapshot*
melee_web_stadium_c1_ft_device_snapshot_create(void)
{
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot =
        malloc(sizeof(*snapshot));
    if (snapshot == NULL) return NULL;
    memcpy(snapshot->first, ft_80459A68, sizeof(snapshot->first));
    memcpy(snapshot->bury_things, ftDevice_BuryThings,
           sizeof(snapshot->bury_things));
    memcpy(snapshot->third, ft_80459A8C, sizeof(snapshot->third));
    memcpy(&snapshot->fourth, &ft_804D6578, sizeof(snapshot->fourth));
    snapshot->first_count = ft_804D6570;
    snapshot->bury_thing_count = ftDevice_BuryThingCount;
    snapshot->addresses[0] = ft_80459A68;
    snapshot->addresses[1] = ftDevice_BuryThings;
    snapshot->addresses[2] = ft_80459A8C;
    snapshot->addresses[3] = &ft_804D6578;
    snapshot->addresses[4] = &ft_804D6570;
    snapshot->addresses[5] = &ftDevice_BuryThingCount;
    return snapshot;
}

int melee_web_stadium_c1_ft_device_snapshot_restore(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    if (snapshot == NULL) return 0;
    memcpy(ft_80459A68, snapshot->first, sizeof(snapshot->first));
    memcpy(ftDevice_BuryThings, snapshot->bury_things,
           sizeof(snapshot->bury_things));
    memcpy(ft_80459A8C, snapshot->third, sizeof(snapshot->third));
    memcpy(&ft_804D6578, &snapshot->fourth, sizeof(snapshot->fourth));
    ft_804D6570 = snapshot->first_count;
    ftDevice_BuryThingCount = snapshot->bury_thing_count;
    return melee_web_stadium_c1_ft_device_snapshot_matches(snapshot);
}

int melee_web_stadium_c1_ft_device_snapshot_matches(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    return snapshot != NULL &&
           memcmp(ft_80459A68, snapshot->first,
                  sizeof(snapshot->first)) == 0 &&
           memcmp(ftDevice_BuryThings, snapshot->bury_things,
                  sizeof(snapshot->bury_things)) == 0 &&
           memcmp(ft_80459A8C, snapshot->third,
                  sizeof(snapshot->third)) == 0 &&
           memcmp(&ft_804D6578, &snapshot->fourth,
                  sizeof(snapshot->fourth)) == 0 &&
           ft_804D6570 == snapshot->first_count &&
           ftDevice_BuryThingCount == snapshot->bury_thing_count;
}

int melee_web_stadium_c1_ft_device_snapshot_release(
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot)
{
    if (snapshot == NULL) return 0;
    free(snapshot);
    return 1;
}

size_t melee_web_stadium_c1_ft_device_snapshot_addresses(
    const MeleeWebStadiumC1FtDeviceSnapshot* snapshot,
    const void** addresses, size_t capacity)
{
    if (snapshot == NULL || addresses == NULL ||
        capacity < sizeof(snapshot->addresses) / sizeof(snapshot->addresses[0]))
        return 0;
    memcpy(addresses, snapshot->addresses, sizeof(snapshot->addresses));
    return sizeof(snapshot->addresses) / sizeof(snapshot->addresses[0]);
}
