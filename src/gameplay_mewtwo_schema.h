#ifndef MELEE_WEB_GAMEPLAY_MEWTWO_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_MEWTWO_SCHEMA_H

#include <stdint.h>

/* Exact ftMewtwoAttributes ABI from melee/ft/kinds/ftMewtwo/types.h. The
 * Shadow Ball iteration and release-lag words, the Teleport duration and
 * angle clamp, and the Confusion reflection bone id are serialized integers;
 * every other serialized word is an IEEE-754 float. Source member designators
 * remain in each row so the C owner can assert the original nested layout. */
#define MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES 0x88u
#define MELEE_WEB_MEWTWO_TYPE_F32 float
#define MELEE_WEB_MEWTWO_TYPE_I32 int32_t
#define MELEE_WEB_MEWTWO_TYPE_U32 uint32_t
#define MELEE_WEB_MEWTWO_TYPE_U8 uint8_t

#define MELEE_WEB_MEWTWO_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, shadowball_charge_cycles, x0_MEWTWO_SHADOWBALL_CHARGE_CYCLES) \
    X(0x004, F32, shadowball_ground_recoil_x, x4_MEWTWO_SHADOWBALL_GROUND_RECOIL_X) \
    X(0x008, F32, shadowball_air_recoil_x, x8_MEWTWO_SHADOWBALL_AIR_RECOIL_X) \
    X(0x00c, I32, shadowball_charge_iterations, xC_MEWTWO_SHADOWBALL_CHARGE_ITERATIONS) \
    X(0x010, I32, shadowball_release_lag, x10_MEWTWO_SHADOWBALL_RELEASE_LAG) \
    X(0x014, F32, shadowball_landing_lag, x14_MEWTWO_SHADOWBALL_LANDING_LAG) \
    X(0x018, F32, confusion_air_boost, x18_MEWTWO_CONFUSION_AIR_BOOST) \
    X(0x01c, U32, confusion_bone_id, x1C_MEWTWO_CONFUSION_REFLECTION.x0_bone_id) \
    X(0x020, I32, confusion_max_damage, x1C_MEWTWO_CONFUSION_REFLECTION.x4_max_damage) \
    X(0x024, F32, confusion_offset_x, x1C_MEWTWO_CONFUSION_REFLECTION.x8_offset.x) \
    X(0x028, F32, confusion_offset_y, x1C_MEWTWO_CONFUSION_REFLECTION.x8_offset.y) \
    X(0x02c, F32, confusion_offset_z, x1C_MEWTWO_CONFUSION_REFLECTION.x8_offset.z) \
    X(0x030, F32, confusion_size, x1C_MEWTWO_CONFUSION_REFLECTION.x14_size) \
    X(0x034, F32, confusion_damage_multiplier, x1C_MEWTWO_CONFUSION_REFLECTION.x18_damage_mul) \
    X(0x038, F32, confusion_speed_multiplier, x1C_MEWTWO_CONFUSION_REFLECTION.x1C_speed_mul) \
    X(0x03c, U8, confusion_behavior, x1C_MEWTWO_CONFUSION_REFLECTION.x20_behavior) \
    X(0x040, F32, teleport_vel_div_x, x40_MEWTWO_TELEPORT_VEL_DIV_X) \
    X(0x044, F32, teleport_vel_div_y, x44_MEWTWO_TELEPORT_VEL_DIV_Y) \
    X(0x048, F32, teleport_gravity, x48_MEWTWO_TELEPORT_GRAVITY) \
    X(0x04c, F32, teleport_terminal_velocity, x4C_MEWTWO_TELEPORT_TERMINAL_VELOCITY) \
    X(0x050, I32, teleport_duration, x50_MEWTWO_TELEPORT_DURATION) \
    X(0x054, F32, teleport_x54_unk2, x54_MEWTWO_TELEPORT_UNK2) \
    X(0x058, F32, teleport_stick_range_min, x58_MEWTWO_TELEPORT_STICK_RANGE_MIN) \
    X(0x05c, F32, teleport_momentum, x5C_MEWTWO_TELEPORT_MOMENTUM) \
    X(0x060, F32, teleport_momentum_add, x60_MEWTWO_TELEPORT_MOMENTUM_ADD) \
    X(0x064, F32, teleport_drift, x64_MEWTWO_TELEPORT_DRIFT) \
    X(0x068, I32, teleport_angle_clamp, x68_MEWTWO_TELEPORT_ANGLE_CLAMP) \
    X(0x06c, F32, teleport_momentum_end_mul, x6C_MEWTWO_TELEPORT_MOMENTUM_END_MUL) \
    X(0x070, F32, teleport_freefall_mobility, x70_MEWTWO_TELEPORT_FREEFALL_MOBILITY) \
    X(0x074, F32, teleport_landing_lag, x74_MEWTWO_TELEPORT_LANDING_LAG) \
    X(0x078, F32, disable_gravity, x78_MEWTWO_DISABLE_GRAVITY) \
    X(0x07c, F32, disable_terminal_velocity, x7C_MEWTWO_DISABLE_TERMINAL_VELOCITY) \
    X(0x080, F32, disable_offset_x, x80_MEWTWO_DISABLE_OFFSET_X) \
    X(0x084, F32, disable_offset_y, x84_MEWTWO_DISABLE_OFFSET_Y)

#define MELEE_WEB_MEWTWO_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_MEWTWO_TYPE_##type name;

typedef struct MeleeWebMewtwoAttributes {
    MELEE_WEB_MEWTWO_ATTRIBUTE_FIELDS(MELEE_WEB_MEWTWO_DECLARE_ATTRIBUTE)
} MeleeWebMewtwoAttributes;

#undef MELEE_WEB_MEWTWO_DECLARE_ATTRIBUTE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebMewtwoAttributes) == MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES,
              "Mewtwo portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebMewtwoAttributes) == MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES,
               "Mewtwo portable extension ABI");
#endif

#endif
