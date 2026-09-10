#include "gameplay_stage_profile.h"
#include "gameplay_content.h"
#include <melee/gr/grbattle.h>
#include <melee/gr/grlast.h>
#include <melee/gr/grstory.h>
#include <melee/gr/groldpupupu.h>
#include "gameplay_stage_story.h"
#include "gameplay_stage_dream_land.h"

extern void* melee_web_grlast_exchange_yakumono(void*);
extern void* melee_web_grbattle_exchange_yakumono(void*);
extern void* melee_web_grstory_exchange_yakumono(void*);
extern void* melee_web_groldpupupu_exchange_yakumono(void*);

static const uint8_t final_destination_map_ids[] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
/* grBattle_OnInit installs the floor and live background holders only. The
 * other map entries are created by the original background state machine when
 * it transitions; requiring them here would reject a valid normal match. */
static const uint8_t battlefield_map_ids[] = {0, 1, 3, 6};
static const MeleeWebStageProfile final_destination = {
    St_Kind_Last, Gr_Kind_Last, &grNLa_StageData,
    final_destination_map_ids, sizeof(final_destination_map_ids),
    melee_web_grlast_exchange_yakumono,
    NULL,
    4,
    10,
    (const uint8_t[]){1, 1, 1, 1, 11, 11, 11, 11, 11, 1},
    10,
};
static const MeleeWebStageProfile battlefield = {
    St_Kind_Battle, Gr_Kind_Battle, &grNBa_StageData,
    battlefield_map_ids, sizeof(battlefield_map_ids),
    melee_web_grbattle_exchange_yakumono,
    NULL,
    2,
    7,
    (const uint8_t[]){1, 1, 1, 1, 1, 1, 1},
    7,
};
static const uint8_t yoshis_story_map_ids[] = {0, 1, 2, 3};
static const MeleeWebStageProfile yoshis_story = {
    St_Kind_Story, Gr_Kind_Story, &grSt_StageData,
    yoshis_story_map_ids, sizeof(yoshis_story_map_ids),
    melee_web_grstory_exchange_yakumono,
    melee_web_story_yakumono_decode,
    0,
    4,
    (const uint8_t[]){1, 1, 1, 2},
    4,
};
static const uint8_t dream_land_map_ids[] = {0, 1, 3, 4, 5, 6, 7, 8};
static const MeleeWebStageProfile dream_land = {
    St_Kind_OldPupupu, Gr_Kind_OldPupupu, &grOp_StageData,
    dream_land_map_ids, sizeof(dream_land_map_ids),
    melee_web_groldpupupu_exchange_yakumono,
    melee_web_dream_land_yakumono_decode,
    0, 8,
    (const uint8_t[]){1, 6, 1, 1, 1, 1, 2, 6}, 8,
};

const MeleeWebStageProfile* melee_web_stage_profile(int stage_kind)
{
    const MeleeWebStageContent* content = melee_web_stage_content(stage_kind);
    if (!content) return NULL;
    switch (stage_kind) {
    case St_Kind_Last:
        return content->ground_kind == Gr_Kind_Last ? &final_destination : NULL;
    case St_Kind_Battle:
        return content->ground_kind == Gr_Kind_Battle ? &battlefield : NULL;
    case St_Kind_Story:
        return content->ground_kind == Gr_Kind_Story ? &yoshis_story : NULL;
    case St_Kind_OldPupupu:
        return content->ground_kind == Gr_Kind_OldPupupu ? &dream_land : NULL;
    default:
        return NULL;
    }
}
