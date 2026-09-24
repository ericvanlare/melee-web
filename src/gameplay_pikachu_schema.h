#ifndef MELEE_WEB_GAMEPLAY_PIKACHU_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_PIKACHU_SCHEMA_H

#include <stdint.h>

/* The source uses one ftPikachuAttributes record for Pikachu and for Pichu's
 * shared ftPk special routines.  Keep every scalarized vector component in
 * one table.  Rows carry the source container, the component offset within
 * that container, and the source scalar expression so C ABI checks can prove
 * both the byte location and the scalar category. */
#define MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES 0xF8u

#define MELEE_WEB_PIKACHU_TYPE_F32 float
#define MELEE_WEB_PIKACHU_TYPE_I32 int32_t
#define MELEE_WEB_PIKACHU_TYPE_U32 uint32_t
#define MELEE_WEB_PIKACHU_TYPE_ITEM int32_t

#define MELEE_WEB_PIKACHU_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, specialn_spawn_offset_x, specialn_spawn_offset, 0x0, specialn_spawn_offset.x) \
    X(0x004, F32, specialn_spawn_offset_y, specialn_spawn_offset, 0x4, specialn_spawn_offset.y) \
    X(0x008, F32, specialairn_spawn_offset_x, specialairn_spawn_offset, 0x0, specialairn_spawn_offset.x) \
    X(0x00c, F32, specialairn_spawn_offset_y, specialairn_spawn_offset, 0x4, specialairn_spawn_offset.y) \
    X(0x010, F32, specialairn_landing_lag, specialairn_landing_lag, 0x0, specialairn_landing_lag) \
    X(0x014, ITEM, specialn_itkind, specialn_itkind, 0x0, specialn_itkind) \
    X(0x018, ITEM, specialairn_itkind, specialairn_itkind, 0x0, specialairn_itkind) \
    X(0x01c, F32, x1C, x1C, 0x0, x1C) \
    X(0x020, F32, x20, x20, 0x0, x20) \
    X(0x024, F32, x24, x24, 0x0, x24) \
    X(0x028, F32, x28, x28, 0x0, x28) \
    X(0x02c, F32, x2C, x2C, 0x0, x2C) \
    X(0x030, F32, x30, x30, 0x0, x30) \
    X(0x034, F32, specials_start_friction, specials_start_friction, 0x0, specials_start_friction) \
    X(0x038, F32, specials_start_gravity, specials_start_gravity, 0x0, specials_start_gravity) \
    X(0x03c, F32, x3C, x3C, 0x0, x3C) \
    X(0x040, F32, x40, x40, 0x0, x40) \
    X(0x044, F32, x44, x44, 0x0, x44) \
    X(0x048, F32, x48, x48, 0x0, x48) \
    X(0x04c, F32, x4C, x4C, 0x0, x4C) \
    X(0x050, F32, x50, x50, 0x0, x50) \
    X(0x054, F32, x54, x54, 0x0, x54) \
    X(0x058, F32, x58, x58, 0x0, x58) \
    X(0x05c, I32, x5C, x5C, 0x0, x5C) \
    X(0x060, I32, x60, x60, 0x0, x60) \
    X(0x064, F32, x64, x64, 0x0, x64) \
    X(0x068, F32, x68, x68, 0x0, x68) \
    X(0x06c, F32, x6C_scale_x, x6C_scale, 0x0, x6C_scale.x) \
    X(0x070, F32, x6C_scale_y, x6C_scale, 0x4, x6C_scale.y) \
    X(0x074, F32, x6C_scale_z, x6C_scale, 0x8, x6C_scale.z) \
    X(0x078, F32, x78, x78, 0x0, x78) \
    X(0x07c, F32, x7C_scale_x, x7C_scale, 0x0, x7C_scale.x) \
    X(0x080, F32, x7C_scale_y, x7C_scale, 0x4, x7C_scale.y) \
    X(0x084, F32, x7C_scale_z, x7C_scale, 0x8, x7C_scale.z) \
    X(0x088, F32, x88, x88, 0x0, x88) \
    X(0x08c, F32, x8C, x8C, 0x0, x8C) \
    X(0x090, F32, x90, x90, 0x0, x90) \
    X(0x094, F32, x94, x94, 0x0, x94) \
    X(0x098, F32, x98, x98, 0x0, x98) \
    X(0x09c, F32, x9C, x9C, 0x0, x9C) \
    X(0x0a0, I32, xA0, xA0, 0x0, xA0) \
    X(0x0a4, F32, xA4, xA4, 0x0, xA4) \
    X(0x0a8, I32, xA8, xA8, 0x0, xA8) \
    X(0x0ac, F32, xAC, xAC, 0x0, xAC) \
    X(0x0b0, F32, xB0, xB0, 0x0, xB0) \
    X(0x0b4, F32, xB4, xB4, 0x0, xB4) \
    X(0x0b8, F32, xB8, xB8, 0x0, xB8) \
    X(0x0bc, F32, xBC, xBC, 0x0, xBC) \
    X(0x0c0, F32, xC0, xC0, 0x0, xC0) \
    X(0x0c4, F32, xC4, xC4, 0x0, xC4) \
    X(0x0c8, F32, xC8, xC8, 0x0, xC8) \
    X(0x0cc, F32, xCC, xCC, 0x0, xCC) \
    X(0x0d0, F32, xD0, xD0, 0x0, xD0) \
    X(0x0d4, I32, xD4, xD4, 0x0, xD4) \
    X(0x0d8, I32, xD8, xD8, 0x0, xD8) \
    X(0x0dc, U32, xDC, xDC, 0x0, xDC) \
    X(0x0e0, F32, height_top, height_attributes, 0x0, height_attributes.top) \
    X(0x0e4, F32, height_bottom, height_attributes, 0x4, height_attributes.bottom) \
    X(0x0e8, F32, height_left_x, height_attributes, 0x8, height_attributes.left.x) \
    X(0x0ec, F32, height_left_y, height_attributes, 0xc, height_attributes.left.y) \
    X(0x0f0, F32, height_right_x, height_attributes, 0x10, height_attributes.right.x) \
    X(0x0f4, F32, height_right_y, height_attributes, 0x14, height_attributes.right.y)

#define MELEE_WEB_PIKACHU_DECLARE(offset, type, name, original, component, source) \
    MELEE_WEB_PIKACHU_TYPE_##type name;
typedef struct MeleeWebPikachuAttributes {
    MELEE_WEB_PIKACHU_ATTRIBUTE_FIELDS(MELEE_WEB_PIKACHU_DECLARE)
} MeleeWebPikachuAttributes;
#undef MELEE_WEB_PIKACHU_DECLARE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebPikachuAttributes) == MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES,
              "Pikachu portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebPikachuAttributes) == MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES,
               "Pikachu portable extension ABI");
#endif

#endif
