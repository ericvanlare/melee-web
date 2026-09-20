#ifndef MELEE_WEB_GAMEPLAY_DONKEY_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_DONKEY_SCHEMA_H

#include <stdint.h>

/* Exact ftDonkeyAttributes ABI from melee/ft/kinds/ftDonkey/types.h.
 * The first two source states and the Giant Punch counters are signed s32;
 * every other serialized word is an IEEE-754 float.  Source member designators
 * remain in each row so the C owner can assert the original nested layout. */
#define MELEE_WEB_DONKEY_ATTRIBUTE_BYTES 0x74u
#define MELEE_WEB_DONKEY_TYPE_F32 float
#define MELEE_WEB_DONKEY_TYPE_I32 int32_t

#define MELEE_WEB_DONKEY_ATTRIBUTE_FIELDS(X) \
    X(0x000, I32, motion_state, motion_state) \
    X(0x004, I32, x4_motion_state, x4_motion_state) \
    X(0x008, F32, x8, x8) \
    X(0x00c, F32, xC, xC) \
    X(0x010, F32, x10, x10) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, F32, cargo_hold_x20_TURN_SPEED, cargo_hold.x20_TURN_SPEED) \
    X(0x024, F32, cargo_hold_x24_JUMP_STARTUP_LAG, cargo_hold.x24_JUMP_STARTUP_LAG) \
    X(0x028, F32, cargo_hold_x28_LANDING_LAG, cargo_hold.x28_LANDING_LAG) \
    X(0x02c, I32, specialn_x2C_MAX_ARM_SWINGS, SpecialN.x2C_MAX_ARM_SWINGS) \
    X(0x030, I32, specialn_x30_DAMAGE_PER_SWING, SpecialN.x30_DAMAGE_PER_SWING) \
    X(0x034, F32, specialn_x34_PUNCH_HORIZONTAL_VEL, SpecialN.x34_PUNCH_HORIZONTAL_VEL) \
    X(0x038, F32, specialn_x38_LANDING_LAG, SpecialN.x38_LANDING_LAG) \
    X(0x03c, F32, specials_x3C_MIN_STICK_X_MOMENTUM, SpecialS.x3C_MIN_STICK_X_MOMENTUM) \
    X(0x040, F32, specials_x40_MOMENTUM_TRANSITION_MODIFIER, SpecialS.x40_MOMENTUM_TRANSITION_MODIFIER) \
    X(0x044, F32, specials_x44_AERIAL_GRAVITY, SpecialS.x44_AERIAL_GRAVITY) \
    X(0x048, F32, x48_UNKNOWN, x48_UNKNOWN) \
    X(0x04c, F32, specialhi_x4C_AERIAL_VERTICAL_VELOCITY, SpecialHi.x4C_AERIAL_VERTICAL_VELOCITY) \
    X(0x050, F32, specialhi_x50_AERIAL_GRAVITY, SpecialHi.x50_AERIAL_GRAVITY) \
    X(0x054, F32, specialhi_x54_GROUNDED_HORIZONTAL_VELOCITY, SpecialHi.x54_GROUNDED_HORIZONTAL_VELOCITY) \
    X(0x058, F32, specialhi_x58_AERIAL_HORIZONTAL_VELOCITY, SpecialHi.x58_AERIAL_HORIZONTAL_VELOCITY) \
    X(0x05c, F32, specialhi_x5C_GROUNDED_MOBILITY, SpecialHi.x5C_GROUNDED_MOBILITY) \
    X(0x060, F32, specialhi_x60_AERIAL_MOBILITY, SpecialHi.x60_AERIAL_MOBILITY) \
    X(0x064, F32, specialhi_x64_LANDING_LAG, SpecialHi.x64_LANDING_LAG) \
    X(0x068, F32, speciallw_x68, SpecialLw.x68) \
    X(0x06c, F32, speciallw_x6C, SpecialLw.x6C) \
    X(0x070, F32, speciallw_x70, SpecialLw.x70)

#define MELEE_WEB_DONKEY_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_DONKEY_TYPE_##type name;

typedef struct MeleeWebDonkeyAttributes {
    MELEE_WEB_DONKEY_ATTRIBUTE_FIELDS(MELEE_WEB_DONKEY_DECLARE_ATTRIBUTE)
} MeleeWebDonkeyAttributes;

#undef MELEE_WEB_DONKEY_DECLARE_ATTRIBUTE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebDonkeyAttributes) == MELEE_WEB_DONKEY_ATTRIBUTE_BYTES,
              "Donkey portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebDonkeyAttributes) == MELEE_WEB_DONKEY_ATTRIBUTE_BYTES,
               "Donkey portable extension ABI");
#endif

#endif
