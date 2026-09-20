#ifndef MELEE_WEB_GAMEPLAY_LUIGI_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_LUIGI_SCHEMA_H

#include <stdint.h>

/* Exact scalar extension size from ftLuigi/types.h.  The source stores the
 * two Cyclone fields at 0x88 and 0x94 as signed 32-bit integers; they are not
 * floats merely because all neighboring fields are floats. */
#define MELEE_WEB_LUIGI_ATTRIBUTE_BYTES 0x98u

/* Keep this row shape aligned with fighter_attributes.h: offset, source scalar
 * kind, portable member, and original source member. The same rows derive the
 * portable record and the source/portable compiler checks. */
#define MELEE_WEB_LUIGI_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, greenmissile_unk1, x0_LUIGI_GREENMISSILE_UNK1) \
    X(0x004, F32, greenmissile_smash, x4_LUIGI_GREENMISSILE_SMASH) \
    X(0x008, F32, greenmissile_charge_rate, x8_LUIGI_GREENMISSILE_CHARGE_RATE) \
    X(0x00c, F32, greenmissile_max_charge_frames, xC_LUIGI_GREENMISSILE_MAX_CHARGE_FRAMES) \
    X(0x010, F32, greenmissile_damage_tilt, x10_LUIGI_GREENMISSILE_DAMAGE_TILT) \
    X(0x014, F32, greenmissile_damage_slope, x14_LUIGI_GREENMISSILE_DAMAGE_SLOPE) \
    X(0x018, F32, greenmissile_traction, x18_LUIGI_GREENMISSILE_TRACTION) \
    X(0x01c, F32, greenmissile_unk2, x1C_LUIGI_GREENMISSILE_UNK2) \
    X(0x020, F32, greenmissile_falling_speed, x20_LUIGI_GREENMISSILE_FALLING_SPEED) \
    X(0x024, F32, greenmissile_vel_x, x24_LUIGI_GREENMISSILE_VEL_X) \
    X(0x028, F32, greenmissile_mul_x, x28_LUIGI_GREENMISSILE_MUL_X) \
    X(0x02c, F32, greenmissile_vel_y, x2C_LUIGI_GREENMISSILE_VEL_Y) \
    X(0x030, F32, greenmissile_mul_y, x30_LUIGI_GREENMISSILE_MUL_Y) \
    X(0x034, F32, greenmissile_gravity_start, x34_LUIGI_GREENMISSILE_GRAVITY_START) \
    X(0x038, F32, greenmissile_friction_end, x38_LUIGI_GREENMISSILE_FRICTION_END) \
    X(0x03c, F32, greenmissile_x_decel, x3C_LUIGI_GREENMISSILE_X_DECEL) \
    X(0x040, F32, greenmissile_gravity_mul, x40_LUIGI_GREENMISSILE_GRAVITY_MUL) \
    X(0x044, F32, greenmissile_misfire_chance, x44_LUIGI_GREENMISSILE_MISFIRE_CHANCE) \
    X(0x048, F32, greenmissile_misfire_vel_x, x48_LUIGI_GREENMISSILE_MISFIRE_VEL_X) \
    X(0x04c, F32, greenmissile_misfire_vel_y, x4C_LUIGI_GREENMISSILE_MISFIRE_VEL_Y) \
    X(0x050, F32, superjump_freefall_mobility, x50_LUIGI_SUPERJUMP_FREEFALL_MOBILITY) \
    X(0x054, F32, superjump_landing_lag, x54_LUIGI_SUPERJUMP_LANDING_LAG) \
    X(0x058, F32, superjump_reverse_stick_range, x58_LUIGI_SUPERJUMP_REVERSE_STICK_RANGE) \
    X(0x05c, F32, superjump_momentum_stick_range, x5C_LUIGI_SUPERJUMP_MOMENTUM_STICK_RANGE) \
    X(0x060, F32, superjump_angle_diff, x60_LUIGI_SUPERJUMP_ANGLE_DIFF) \
    X(0x064, F32, superjump_vel_x, x64_LUIGI_SUPERJUMP_VEL_X) \
    X(0x068, F32, superjump_gravity_start, x68_LUIGI_SUPERJUMP_GRAVITY_START) \
    X(0x06c, F32, superjump_vel_y, x6C_LUIGI_SUPERJUMP_VEL_Y) \
    X(0x070, F32, cyclone_tap_momentum, x70_LUIGI_CYCLONE_TAP_MOMENTUM) \
    X(0x074, F32, cyclone_momentum_x_ground, x74_LUIGI_CYCLONE_MOMENTUM_X_GROUND) \
    X(0x078, F32, cyclone_momentum_x_air, x78_LUIGI_CYCLONE_MOMENTUM_X_AIR) \
    X(0x07c, F32, cyclone_momentum_x_mul_ground, x7C_LUIGI_CYCLONE_MOMENTUM_X_MUL_GROUND) \
    X(0x080, F32, cyclone_momentum_x_mul_air, x80_LUIGI_CYCLONE_MOMENTUM_X_MUL_AIR) \
    X(0x084, F32, cyclone_friction_end, x84_LUIGI_CYCLONE_FRICTION_END) \
    X(0x088, I32, cyclone_unk, x88_LUIGI_CYCLONE_UNK) \
    X(0x08c, F32, cyclone_tap_y_vel_max, x8C_LUIGI_CYCLONE_TAP_Y_VEL_MAX) \
    X(0x090, F32, cyclone_tap_gravity, x90_LUIGI_CYCLONE_TAP_GRAVITY) \
    X(0x094, I32, cyclone_landing_lag, x94_LUIGI_CYCLONE_LANDING_LAG)

#define MELEE_WEB_LUIGI_TYPE_F32 float
#define MELEE_WEB_LUIGI_TYPE_I32 int32_t
#define MELEE_WEB_LUIGI_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_LUIGI_TYPE_##type name;

typedef struct MeleeWebLuigiAttributes {
    MELEE_WEB_LUIGI_ATTRIBUTE_FIELDS(MELEE_WEB_LUIGI_DECLARE_ATTRIBUTE)
} MeleeWebLuigiAttributes;

#undef MELEE_WEB_LUIGI_DECLARE_ATTRIBUTE

#endif
