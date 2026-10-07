#pragma once

#include "dat_native_stage.hpp"

#include <array>
#include <cstdint>

namespace melee_web::test {
constexpr std::array<uint8_t,10> stadium_animation_counts={1,1,1,1,1,1,1,1,1,1};
constexpr std::array<uint32_t,4> stadium_resident_ids={0,1,2,5};
constexpr std::array<uint32_t,10> stadium_animation_flag_consumers={0,1,2,3,4,5,6,7,8,9};
constexpr std::array<melee_web::DatNativeMapExternalReference,26> stadium_external_references={{
 {3,0,"GrdPStadiumFire_TopN_joint"},
 {3,8,"GrdPStadiumFire_TopN_matanim_joint_list"},
 {3,16,"GrdPStadium_Fire_SEDUNIQUEfire_cam_int1_camera"},
 {3,24,"GrdPStadium_Fire_SEDUNIQUEfire_scene_lights"},
 {4,0,"GrdPStadiumGrass_TopN_joint"},
 {4,8,"GrdPStadiumGrass_TopN_matanim_joint_list"},
 {4,16,"GrdPStadium_Grass_SEDUNIQUEgrass_cam_int1_camera"},
 {4,24,"GrdPStadium_Grass_SEDUNIQUEgrass_scene_lights"},
 {6,0,"GrdPStadiumRock_TopN_joint"},
 {6,4,"GrdPStadiumRock_TopN_animjoint_list"},
 {6,16,"GrdPStadium_Rock_SEDUNIQUErock_cam_int1_camera"},
 {6,24,"GrdPStadium_Rock_SEDUNIQUErock_scene_lights"},
 {7,0,"GrdPStadiumWaterFunsuiA_TopN_joint"},
 {7,4,"GrdPStadiumWaterFunsuiA_TopN_animjoint_list"},
 {7,8,"GrdPStadiumWaterFunsuiA_TopN_matanim_joint_list"},
 {7,16,"GrdPStadium_Water_SEDUNIQUEfunsui_a_cam_int1_camera"},
 {7,24,"GrdPStadium_Water_SEDUNIQUEfunsui_a_scene_lights"},
 {8,0,"GrdPStadiumWaterFunsuiB_TopN_joint"},
 {8,4,"GrdPStadiumWaterFunsuiB_TopN_animjoint_list"},
 {8,8,"GrdPStadiumWaterFunsuiB_TopN_matanim_joint_list"},
 {8,16,"GrdPStadium_Water_SEDUNIQUEfunsui_b_cam_int1_camera"},
 {8,24,"GrdPStadium_Water_SEDUNIQUEfunsui_b_scene_lights"},
 {9,0,"GrdPStadiumWater_TopN_joint"},
 {9,8,"GrdPStadiumWater_TopN_matanim_joint_list"},
 {9,16,"GrdPStadium_Water_SEDUNIQUEwater_cam_int1_camera"},
 {9,24,"GrdPStadium_Water_SEDUNIQUEwater_scene_lights"},
}};
constexpr auto local_flag=melee_web::DatNativeMapFlagKind::LocalMaterial;
constexpr auto external_flag=melee_web::DatNativeMapFlagKind::ExternalNull;
constexpr std::array<melee_web::DatNativeMapFlagExpectation,44> stadium_flag_expectations={{
 {0,local_flag,95896,{}},
 {1,local_flag,96584,{}},
 {2,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat3_mobjdesc"},
 {3,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat4_mobjdesc"},
 {4,external_flag,0,"GrdPStadiumFire_ATree_FShadowmat5_mobjdesc"},
 {5,external_flag,0,"GrdPStadiumFire_CTerrace_FShadowmat6_mobjdesc"},
 {6,external_flag,0,"GrdPStadiumFire_CTerrace_FShadowmat7_mobjdesc"},
 {7,external_flag,0,"GrdPStadiumFire_ZGround_FShadowmat1_mobjdesc"},
 {8,external_flag,0,"GrdPStadiumFire_ZGround_FShadowmat2_mobjdesc"},
 {9,external_flag,0,"GrdPStadiumGrass_SYaguraB_GShadowmat10_mobjdesc"},
 {10,external_flag,0,"GrdPStadiumGrass_SyaguraA_GShadowmat11_mobjdesc"},
 {11,external_flag,0,"GrdPStadiumGrass_TreeB_GShadowmat8_mobjdesc"},
 {12,external_flag,0,"GrdPStadiumGrass_TreeB_GShadowmat9_mobjdesc"},
 {13,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat12_mobjdesc"},
 {14,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat13_mobjdesc"},
 {15,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat14_mobjdesc"},
 {16,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat15_mobjdesc"},
 {17,external_flag,0,"GrdPStadiumGrass_UGround_GShadowmat16_mobjdesc"},
 {18,local_flag,9564,{}},
 {19,local_flag,9404,{}},
 {20,local_flag,9856,{}},
 {21,local_flag,9696,{}},
 {22,local_flag,9272,{}},
 {23,local_flag,8996,{}},
 {24,external_flag,0,"GrdPStadiumRock_BStandA_RShadowmat2_mobjdesc"},
 {25,external_flag,0,"GrdPStadiumRock_BStandB_RShadowmat6_mobjdesc"},
 {26,external_flag,0,"GrdPStadiumRock_BStandB_RShadowmat7_mobjdesc"},
 {27,external_flag,0,"GrdPStadiumRock_YRock_RShadowmat3_mobjdesc"},
 {28,external_flag,0,"GrdPStadiumRock_ZGround_RShadowmat4_mobjdesc"},
 {29,external_flag,0,"GrdPStadiumRock_ZGround_RShadowmat5_mobjdesc"},
 {30,external_flag,0,"GrdPStadiumRock_ZGround_shadowmat1_mobjdesc"},
 {31,external_flag,0,"GrdPStadiumWater_ABox_WShadowmat14_mobjdesc"},
 {32,external_flag,0,"GrdPStadiumWater_BBox_WShadowmat15_mobjdesc"},
 {33,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat10_mobjdesc"},
 {34,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat8_mobjdesc"},
 {35,external_flag,0,"GrdPStadiumWater_BStand_WShadowmat9_mobjdesc"},
 {36,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat3_mobjdesc"},
 {37,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat4_mobjdesc"},
 {38,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat5_mobjdesc"},
 {39,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat6_mobjdesc"},
 {40,external_flag,0,"GrdPStadiumWater_CWing_WShadowmat7_mobjdesc"},
 {41,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat11_mobjdesc"},
 {42,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat12_mobjdesc"},
 {43,external_flag,0,"GrdPStadiumWater_YGround_WShadowmat13_mobjdesc"},
}};
const melee_web::DatNativeMapContract stadium_contract{
 10,stadium_animation_counts,stadium_resident_ids,stadium_external_references,
 stadium_animation_flag_consumers,stadium_flag_expectations
};

} // namespace melee_web::test
