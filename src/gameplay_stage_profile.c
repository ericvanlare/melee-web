#include "gameplay_stage_profile.h"
#include "gameplay_content.h"
#include <melee/gr/grbattle.h>
#include <melee/gr/grlast.h>
#include <melee/gr/grstory.h>
#include <melee/gr/groldpupupu.h>
#include <melee/gr/grshrine.h>
#include <melee/gr/grizumi.h>
#include <melee/gr/groldyoshi.h>
#include <melee/gr/grpstadium.h>
#include "gameplay_stage_story.h"
#include "gameplay_stage_dream_land.h"
#include "gameplay_stage_fountain.h"
#include "gameplay_stage_old_yoshi.h"
#include "gameplay_stage_stadium.h"

extern void* melee_web_grlast_exchange_yakumono(void*);
extern void* melee_web_grbattle_exchange_yakumono(void*);
extern void* melee_web_grstory_exchange_yakumono(void*);
extern void* melee_web_groldpupupu_exchange_yakumono(void*);
extern void* melee_web_grshrine_exchange_yakumono(void*);
extern void* melee_web_grizumi_exchange_yakumono(void*);
extern void* melee_web_groldyoshi_exchange_yakumono(void*);
extern void* melee_web_grpstadium_exchange_yakumono(void*);

/* The retained C0 map contract is source-owned here and shared with every
 * consumer through the C profile. Tests adapt this profile to the C++ map
 * contract instead of carrying a second Stadium row/flag registry. */
static const uint8_t stadium_animation_counts[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static const uint32_t stadium_resident_entry_ids[] = {0, 1, 2, 5};
static const uint32_t stadium_animation_flag_entry_ids[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
};
static const MeleeWebStageMapExternalReference stadium_external_references[] = {
    {3, 0, "GrdPStadiumFire_TopN_joint"},
    {3, 8, "GrdPStadiumFire_TopN_matanim_joint_list"},
    {3, 16, "GrdPStadium_Fire_SEDUNIQUEfire_cam_int1_camera"},
    {3, 24, "GrdPStadium_Fire_SEDUNIQUEfire_scene_lights"},
    {4, 0, "GrdPStadiumGrass_TopN_joint"},
    {4, 8, "GrdPStadiumGrass_TopN_matanim_joint_list"},
    {4, 16, "GrdPStadium_Grass_SEDUNIQUEgrass_cam_int1_camera"},
    {4, 24, "GrdPStadium_Grass_SEDUNIQUEgrass_scene_lights"},
    {6, 0, "GrdPStadiumRock_TopN_joint"},
    {6, 4, "GrdPStadiumRock_TopN_animjoint_list"},
    {6, 16, "GrdPStadium_Rock_SEDUNIQUErock_cam_int1_camera"},
    {6, 24, "GrdPStadium_Rock_SEDUNIQUErock_scene_lights"},
    {7, 0, "GrdPStadiumWaterFunsuiA_TopN_joint"},
    {7, 4, "GrdPStadiumWaterFunsuiA_TopN_animjoint_list"},
    {7, 8, "GrdPStadiumWaterFunsuiA_TopN_matanim_joint_list"},
    {7, 16, "GrdPStadium_Water_SEDUNIQUEfunsui_a_cam_int1_camera"},
    {7, 24, "GrdPStadium_Water_SEDUNIQUEfunsui_a_scene_lights"},
    {8, 0, "GrdPStadiumWaterFunsuiB_TopN_joint"},
    {8, 4, "GrdPStadiumWaterFunsuiB_TopN_animjoint_list"},
    {8, 8, "GrdPStadiumWaterFunsuiB_TopN_matanim_joint_list"},
    {8, 16, "GrdPStadium_Water_SEDUNIQUEfunsui_b_cam_int1_camera"},
    {8, 24, "GrdPStadium_Water_SEDUNIQUEfunsui_b_scene_lights"},
    {9, 0, "GrdPStadiumWater_TopN_joint"},
    {9, 8, "GrdPStadiumWater_TopN_matanim_joint_list"},
    {9, 16, "GrdPStadium_Water_SEDUNIQUEwater_cam_int1_camera"},
    {9, 24, "GrdPStadium_Water_SEDUNIQUEwater_scene_lights"},
};
static const MeleeWebStageMapFlagExpectation stadium_flag_expectations[] = {
    {0, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 95896, NULL},
    {1, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 96584, NULL},
    {2, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_ATree_FShadowmat3_mobjdesc"},
    {3, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_ATree_FShadowmat4_mobjdesc"},
    {4, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_ATree_FShadowmat5_mobjdesc"},
    {5, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_CTerrace_FShadowmat6_mobjdesc"},
    {6, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_CTerrace_FShadowmat7_mobjdesc"},
    {7, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_ZGround_FShadowmat1_mobjdesc"},
    {8, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumFire_ZGround_FShadowmat2_mobjdesc"},
    {9, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_SYaguraB_GShadowmat10_mobjdesc"},
    {10, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_SyaguraA_GShadowmat11_mobjdesc"},
    {11, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_TreeB_GShadowmat8_mobjdesc"},
    {12, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_TreeB_GShadowmat9_mobjdesc"},
    {13, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_UGround_GShadowmat12_mobjdesc"},
    {14, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_UGround_GShadowmat13_mobjdesc"},
    {15, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_UGround_GShadowmat14_mobjdesc"},
    {16, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_UGround_GShadowmat15_mobjdesc"},
    {17, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumGrass_UGround_GShadowmat16_mobjdesc"},
    {18, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 9564, NULL},
    {19, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 9404, NULL},
    {20, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 9856, NULL},
    {21, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 9696, NULL},
    {22, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 9272, NULL},
    {23, MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL, 8996, NULL},
    {24, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_BStandA_RShadowmat2_mobjdesc"},
    {25, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_BStandB_RShadowmat6_mobjdesc"},
    {26, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_BStandB_RShadowmat7_mobjdesc"},
    {27, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_YRock_RShadowmat3_mobjdesc"},
    {28, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_ZGround_RShadowmat4_mobjdesc"},
    {29, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_ZGround_RShadowmat5_mobjdesc"},
    {30, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumRock_ZGround_shadowmat1_mobjdesc"},
    {31, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_ABox_WShadowmat14_mobjdesc"},
    {32, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_BBox_WShadowmat15_mobjdesc"},
    {33, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_BStand_WShadowmat10_mobjdesc"},
    {34, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_BStand_WShadowmat8_mobjdesc"},
    {35, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_BStand_WShadowmat9_mobjdesc"},
    {36, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_CWing_WShadowmat3_mobjdesc"},
    {37, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_CWing_WShadowmat4_mobjdesc"},
    {38, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_CWing_WShadowmat5_mobjdesc"},
    {39, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_CWing_WShadowmat6_mobjdesc"},
    {40, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_CWing_WShadowmat7_mobjdesc"},
    {41, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_YGround_WShadowmat11_mobjdesc"},
    {42, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_YGround_WShadowmat12_mobjdesc"},
    {43, MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL, 0, "GrdPStadiumWater_YGround_WShadowmat13_mobjdesc"},
};
static const MeleeWebStageMapOwnership stadium_map_ownership = {
    stadium_resident_entry_ids,
    sizeof(stadium_resident_entry_ids) / sizeof(stadium_resident_entry_ids[0]),
    stadium_external_references,
    sizeof(stadium_external_references) / sizeof(stadium_external_references[0]),
    stadium_animation_flag_entry_ids,
    sizeof(stadium_animation_flag_entry_ids) / sizeof(stadium_animation_flag_entry_ids[0]),
    stadium_flag_expectations,
    sizeof(stadium_flag_expectations) / sizeof(stadium_flag_expectations[0]),
};
static const uint8_t stadium_map_ids[] = {0, 1, 2, 5};
static const MeleeWebStagePublic stadium_public[] = {
    {"GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc",
     MELEE_WEB_STAGE_PUBLIC_IMAGE},
    {"SIS_GrPStadiumData", MELEE_WEB_STAGE_PUBLIC_SIS},
};
static const MeleeWebStageProfile stadium = {
    .stage_kind = St_Kind_PStadium,
    .ground_kind = Gr_Kind_PStadium,
    .source = &grPs_StageData,
    .required_map_ids = stadium_map_ids,
    .required_map_count = sizeof(stadium_map_ids) / sizeof(stadium_map_ids[0]),
    .exchange_yakumono = melee_web_grpstadium_exchange_yakumono,
    .decode_yakumono = melee_web_stadium_yakumono_decode,
    .yakumono_program_count = 0,
    .entry_count = sizeof(stadium_animation_counts) / sizeof(stadium_animation_counts[0]),
    .animation_counts = stadium_animation_counts,
    .animation_count_count = sizeof(stadium_animation_counts) / sizeof(stadium_animation_counts[0]),
    .allow_absent_particle_bank = 0,
    .opaque_yakumono = 0,
    .public_symbols = stadium_public,
    .public_symbol_count = sizeof(stadium_public) / sizeof(stadium_public[0]),
    .map_ownership_policy = MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED,
    .map_ownership = &stadium_map_ownership,
    .diagnostic_only = 1,
};

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
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
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
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
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
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
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
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
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
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
    0,
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
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
    0,
};

static const uint8_t old_yoshi_map_ids[] = {0, 1, 4, 5, 2, 3};
static const MeleeWebStageProfile old_yoshi = {
    St_Kind_OldYoshi, Gr_Kind_OldYoshi, &grOy_StageData,
    old_yoshi_map_ids, sizeof(old_yoshi_map_ids),
    melee_web_groldyoshi_exchange_yakumono,
    melee_web_old_yoshi_yakumono_decode,
    0, 6, (const uint8_t[]){1, 1, 3, 1, 1, 1}, 6,
    0, 0,
    NULL,
    0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT,
    NULL,
    0,
};

const MeleeWebStageProfile* melee_web_stage_stadium_profile_data(void)
{
    return &stadium;
}

const MeleeWebStageProfile* melee_web_stage_profile(int stage_kind)
{
    const MeleeWebStageContent* content = melee_web_stage_content_for_profile(stage_kind);
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    case St_Kind_PStadium:
        return content->diagnostic_only &&
                       content->ground_kind == Gr_Kind_PStadium
                   ? &stadium
                   : NULL;
#endif
    default:
        return NULL;
    }
}
