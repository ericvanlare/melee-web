#include "stadium_c1_stage_state_probe.h"

#include <melee/gr/grdatfiles.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>

/* Source grDatFiles_8049EE10 is authored as UnkArchiveStruct[4]. */
enum {
    SOURCE_GRDATFILES_SLOT_COUNT = 4,
    /* The stage registry link walked by gameplay_stage_map.c. */
    SOURCE_STAGE_GOBJ_ENTITY_LINK = 5,
};

uint32_t melee_web_stadium_c1_stage_state_failures(void)
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
