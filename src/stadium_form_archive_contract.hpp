#pragma once
#include "dat_archive.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
namespace melee_web::stadium_form_contract {
// Existing C0 authored ownership identities, shared rather than duplicated.
struct AnimationSlotExpectation {
    // -1 means absent in every archive; -2 means local in every archive.
    // Otherwise the value is the ordinal of the archive that owns the root.
    int owner;
    const char* external_identity;
    std::size_t target_bytes;
    bool pointer_list;
};

constexpr std::array<const char*, 10> joint_external_identities{
    nullptr,
    "GrdPStadiumBG_TopN_joint",
    "GrdPStadiumField_TopN_joint",
    "GrdPStadiumFire_TopN_joint",
    "GrdPStadiumGrass_TopN_joint",
    "GrdPStadiumNormal_TopN_joint",
    "GrdPStadiumRock_TopN_joint",
    "GrdPStadiumWaterFunsuiA_TopN_joint",
    "GrdPStadiumWaterFunsuiB_TopN_joint",
    "GrdPStadiumWater_TopN_joint",
};
constexpr std::array<int, 10> joint_owners{-2, 0, 0, 1, 2, 0, 4, 3, 3, 3};
constexpr std::array<const char*, 10> joint_animation_external_identities{
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    "GrdPStadiumRock_TopN_animjoint_list",
    "GrdPStadiumWaterFunsuiA_TopN_animjoint_list",
    "GrdPStadiumWaterFunsuiB_TopN_animjoint_list",
    nullptr,
};
constexpr std::array<int, 10> joint_animation_owners{
    -1, -1, -1, -1, -1, -1, 4, 3, 3, -1};
constexpr std::array<const char*, 10> material_animation_external_identities{
    nullptr,
    "GrdPStadiumBG_TopN_matanim_joint_list",
    "GrdPStadiumField_TopN_matanim_joint_list",
    "GrdPStadiumFire_TopN_matanim_joint_list",
    "GrdPStadiumGrass_TopN_matanim_joint_list",
    "GrdPStadiumNormal_TopN_matanim_joint_list",
    nullptr,
    "GrdPStadiumWaterFunsuiA_TopN_matanim_joint_list",
    "GrdPStadiumWaterFunsuiB_TopN_matanim_joint_list",
    "GrdPStadiumWater_TopN_matanim_joint_list",
};
constexpr std::array<int, 10> material_animation_owners{
    -1, 0, 0, 1, 2, 0, -1, 3, 3, 3};

inline AnimationSlotExpectation animation_expectation(std::size_t entry,
                                               std::uint32_t field_offset)
{
    if (field_offset == 0)
        return {joint_owners[entry], joint_external_identities[entry], 64, false};
    if (field_offset == 4)
        return {joint_animation_owners[entry], joint_animation_external_identities[entry],
                4, true};
    if (field_offset == 8)
        return {material_animation_owners[entry],
                material_animation_external_identities[entry], 4, true};
    if(field_offset!=12)throw DatError("Unknown Stadium animation slot offset");
    return {-1, nullptr, 0, false};
}

}
