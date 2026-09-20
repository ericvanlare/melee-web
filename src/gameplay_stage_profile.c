#include "gameplay_stage_profile.h"
#include "gameplay_content.h"
#include <melee/gr/grbattle.h>
#include <melee/gr/grlast.h>
#include <melee/gr/grstory.h>
#include <melee/gr/groldpupupu.h>
#include <melee/gr/grshrine.h>
#include <melee/gr/grizumi.h>
#include <melee/gr/groldyoshi.h>
#include "gameplay_stage_story.h"
#include "gameplay_stage_dream_land.h"
#include "gameplay_stage_fountain.h"
#include "gameplay_stage_old_yoshi.h"

extern void* melee_web_grlast_exchange_yakumono(void*);
extern void* melee_web_grbattle_exchange_yakumono(void*);
extern void* melee_web_grstory_exchange_yakumono(void*);
extern void* melee_web_groldpupupu_exchange_yakumono(void*);
extern void* melee_web_grshrine_exchange_yakumono(void*);
extern void* melee_web_grizumi_exchange_yakumono(void*);
extern void* melee_web_groldyoshi_exchange_yakumono(void*);

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
    0,
    0,
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
    0,
    0,
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
    0,
    0,
};
static const uint8_t dream_land_map_ids[] = {0, 1, 3, 4, 5, 6, 7, 8};
static const MeleeWebStageProfile dream_land = {
    St_Kind_OldPupupu, Gr_Kind_OldPupupu, &grOp_StageData,
    dream_land_map_ids, sizeof(dream_land_map_ids),
    melee_web_groldpupupu_exchange_yakumono,
    melee_web_dream_land_yakumono_decode,
    0, 8,
    (const uint8_t[]){1, 6, 1, 1, 1, 1, 2, 6}, 8,
    0,
    0,
};

static const uint8_t shrine_map_ids[] = {0, 1, 2};
static const MeleeWebStageProfile shrine = {
    St_Kind_Shrine, Gr_Kind_Shrine, &grSh_StageData,
    shrine_map_ids, sizeof(shrine_map_ids),
    melee_web_grshrine_exchange_yakumono,
    NULL,
    0,
    3,
    (const uint8_t[]){1, 1, 1},
    3,
    1,
    1,
};

static const uint8_t fountain_map_ids[] = {0, 1, 2, 3, 4};
static const MeleeWebStagePublic fountain_public[] = {
    {"GrdIzumi_cd_wt_GrdIzumiDummy1_1_image_desc", MELEE_WEB_STAGE_PUBLIC_IMAGE},
    {"GrdIzumiStar_TopN_joint", MELEE_WEB_STAGE_PUBLIC_JOINT},
};
static const MeleeWebStageProfile fountain = {
    St_Kind_Izumi, Gr_Kind_Izumi, &grIz_StageData,
    fountain_map_ids, sizeof(fountain_map_ids),
    melee_web_grizumi_exchange_yakumono,
    melee_web_fountain_yakumono_decode,
    0, 5, (const uint8_t[]){1, 1, 1, 1, 1}, 5,
    0, 0, fountain_public, sizeof(fountain_public)/sizeof(fountain_public[0]),
};

static const uint8_t old_yoshi_map_ids[] = {0, 1, 4, 5, 2, 3};
static const MeleeWebStageProfile old_yoshi = {
    St_Kind_OldYoshi, Gr_Kind_OldYoshi, &grOy_StageData,
    old_yoshi_map_ids, sizeof(old_yoshi_map_ids),
    melee_web_groldyoshi_exchange_yakumono,
    melee_web_old_yoshi_yakumono_decode,
    0, 6, (const uint8_t[]){1, 1, 3, 1, 1, 1}, 6,
    0, 0,
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
    case St_Kind_Shrine:
        return content->ground_kind == Gr_Kind_Shrine ? &shrine : NULL;
    case St_Kind_Izumi:
        return content->ground_kind == Gr_Kind_Izumi ? &fountain : NULL;
    case St_Kind_OldYoshi:
        return content->ground_kind == Gr_Kind_OldYoshi ? &old_yoshi : NULL;
    default:
        return NULL;
    }
}
