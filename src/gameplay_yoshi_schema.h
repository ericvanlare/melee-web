#ifndef MELEE_WEB_GAMEPLAY_YOSHI_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_YOSHI_SCHEMA_H

#include <stdint.h>

/* ftYoshiAttributes is the source 0x138 extension. The original Yoshi special
 * callbacks also view portions of this allocation through ftYs_DatAttrs; keep
 * those alternate special-hi/star offsets as named scalar lanes. */
#define MELEE_WEB_YOSHI_ATTRIBUTE_BYTES 0x138u
#define MELEE_WEB_YOSHI_TYPE_F32 float
#define MELEE_WEB_YOSHI_TYPE_I32 int32_t
#define MELEE_WEB_YOSHI_TYPE_U32 uint32_t

#define MELEE_WEB_YOSHI_ATTRIBUTE_FIELDS(X) \
    X(0x000, I32, x0, x0) \
    X(0x004, F32, x4, x4) \
    X(0x008, F32, x8, x8) \
    X(0x00c, F32, xC, xC) \
    X(0x010, F32, x10, x10) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, F32, x20, x20) \
    X(0x024, F32, x24, x24) \
    X(0x028, F32, x28, x28) \
    X(0x02c, F32, x2C, x2C) \
    X(0x030, F32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, I32, x38, x38) \
    X(0x03c, F32, x3C_x, x3C.x) \
    X(0x040, F32, x3C_y, x3C.y) \
    X(0x044, F32, x44, x44) \
    X(0x048, I32, x48, x48) \
    X(0x04c, I32, x4C, x4C) \
    X(0x050, I32, x50, x50) \
    X(0x054, F32, x54, x54) \
    X(0x058, F32, x58, x58) \
    X(0x05c, F32, x5C, x5C) \
    X(0x060, F32, x60, x60) \
    X(0x064, F32, x64, x64) \
    X(0x068, F32, x68, x68) \
    X(0x06c, F32, specials_start_gravity, specials_start_gravity) \
    X(0x070, F32, specials_start_terminal_vel, specials_start_terminal_vel) \
    X(0x074, F32, x74, x74) \
    X(0x078, F32, x78, x78) \
    X(0x07c, F32, x7C, x7C) \
    X(0x080, F32, x80, x80) \
    X(0x084, F32, x84, x84) \
    X(0x088, F32, x88, x88) \
    X(0x08c, F32, x8C, x8C) \
    X(0x090, F32, x90, x90) \
    X(0x094, F32, x94, x94) \
    X(0x098, F32, x98, x98) \
    X(0x09c, F32, x9C, x9C) \
    X(0x0a0, F32, xA0, xA0) \
    X(0x0a4, I32, xA4, xA4) \
    X(0x0a8, F32, xA8, xA8) \
    X(0x0ac, F32, xAC, xAC) \
    X(0x0b0, F32, xB0, xB0) \
    X(0x0b4, F32, xB4, xB4) \
    X(0x0b8, F32, xB8, xB8) \
    X(0x0bc, F32, xBC, xBC) \
    X(0x0c0, F32, xC0, xC0) \
    X(0x0c4, F32, xC4, xC4) \
    X(0x0c8, F32, xC8, xC8) \
    X(0x0cc, F32, xCC, xCC) \
    X(0x0d0, F32, xD0, xD0) \
    X(0x0d4, F32, xD4, xD4) \
    X(0x0d8, F32, xD8, xD8) \
    X(0x0dc, I32, xDC, xDC) \
    X(0x0e0, F32, xE0, xE0) \
    X(0x0e4, F32, xE4, xE4) \
    X(0x0e8, F32, xE8, xE8) \
    X(0x0ec, F32, special_ec, special_ec) \
    X(0x0f0, F32, special_f0, special_f0) \
    X(0x0f4, F32, special_f4, special_f4) \
    X(0x0f8, F32, specialhi_base_angle, specialhi_base_angle) \
    X(0x0fc, F32, special_fc, special_fc) \
    X(0x100, F32, special_100, special_100) \
    X(0x104, F32, special_104, special_104) \
    X(0x108, F32, special_108, special_108) \
    X(0x10c, F32, special_10c, special_10c) \
    X(0x110, F32, special_110, special_110) \
    X(0x114, F32, x114, x114) \
    X(0x118, F32, speciallw_star_offset_x, speciallw_star_offset.x) \
    X(0x11c, F32, speciallw_star_offset_y, speciallw_star_offset.y) \
    X(0x120, F32, x120, x120) \
    X(0x124, F32, x124, x124) \
    X(0x128, F32, x128, x128) \
    X(0x12c, U32, padding_12c, padding_12C[0]) \
    X(0x130, U32, padding_130, padding_12C[1]) \
    X(0x134, U32, padding_134, padding_12C[2])

#define MELEE_WEB_YOSHI_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_YOSHI_TYPE_##type name;
typedef struct MeleeWebYoshiAttributes {
    MELEE_WEB_YOSHI_ATTRIBUTE_FIELDS(MELEE_WEB_YOSHI_DECLARE_ATTRIBUTE)
} MeleeWebYoshiAttributes;
#undef MELEE_WEB_YOSHI_DECLARE_ATTRIBUTE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebYoshiAttributes) == MELEE_WEB_YOSHI_ATTRIBUTE_BYTES,
              "Yoshi portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebYoshiAttributes) == MELEE_WEB_YOSHI_ATTRIBUTE_BYTES,
               "Yoshi portable extension ABI");
#endif

#endif
