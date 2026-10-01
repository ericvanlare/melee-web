#ifndef MELEE_WEB_GAMEPLAY_SAMUS_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_SAMUS_SCHEMA_H

#include <stdint.h>

/* Exact 0xD4 ftSs_DatAttrs source layout. x20 and x9C..xC8 are signed
 * integer words, while xD0 is an uninterpreted 32-bit source word: GALE01r2
 * stores 0xB4 there without a DAT relocation, and no admitted source consumer
 * dereferences it. Keep vectors/collision boxes scalarized at their ABI offsets. */
#define MELEE_WEB_SAMUS_ATTRIBUTE_BYTES 0xD4u
#define MELEE_WEB_SAMUS_TYPE_F32 float
#define MELEE_WEB_SAMUS_TYPE_I32 int32_t
#define MELEE_WEB_SAMUS_TYPE_PTR32 uint32_t

#define MELEE_WEB_SAMUS_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, x0, x0) \
    X(0x004, F32, x4, x4) \
    X(0x008, F32, x8, x8) \
    X(0x00c, F32, xC, xC) \
    X(0x010, F32, x10, x10) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, I32, x20, x20) \
    X(0x024, F32, x24, x24) \
    X(0x028, F32, x28, x28) \
    X(0x02c, F32, x2C, x2C) \
    X(0x030, F32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, F32, x38, x38) \
    X(0x03c, F32, x3C, x3C) \
    X(0x040, F32, x40, x40) \
    X(0x044, F32, x44, x44) \
    X(0x048, F32, x48, x48) \
    X(0x04c, F32, x4C, x4C) \
    X(0x050, F32, x50, x50) \
    X(0x054, F32, x54, x54) \
    X(0x058, F32, x58, x58) \
    X(0x05c, F32, x5C, x5C) \
    X(0x060, F32, x60, x60) \
    X(0x064, F32, x64, x64) \
    X(0x068, F32, x68, x68) \
    X(0x06c, F32, x6C, x6C) \
    X(0x070, F32, x70, x70) \
    X(0x074, F32, x74_x, x74_vec.x) \
    X(0x078, F32, x74_y, x74_vec.y) \
    X(0x07c, F32, x74_z, x74_vec.z) \
    X(0x080, F32, x80, x80) \
    X(0x084, F32, height_top, height_attributes.top) \
    X(0x088, F32, height_bottom, height_attributes.bottom) \
    X(0x08c, F32, height_left_x, height_attributes.left.x) \
    X(0x090, F32, height_left_y, height_attributes.left.y) \
    X(0x094, F32, height_right_x, height_attributes.right.x) \
    X(0x098, F32, height_right_y, height_attributes.right.y) \
    X(0x09c, I32, x9C, x9C) \
    X(0x0a0, I32, xA0, xA0) \
    X(0x0a4, I32, xA4, xA4) \
    X(0x0a8, I32, xA8, xA8) \
    X(0x0ac, I32, xAC, xAC) \
    X(0x0b0, I32, xB0, xB0) \
    X(0x0b4, I32, xB4, xB4) \
    X(0x0b8, I32, xB8, xB8) \
    X(0x0bc, I32, xBC, xBC) \
    X(0x0c0, I32, xC0, xC0) \
    X(0x0c4, I32, xC4, xC4) \
    X(0x0c8, I32, xC8, xC8) \
    X(0x0cc, F32, xCC, xCC) \
    X(0x0d0, PTR32, xD0, xD0)

#define MELEE_WEB_SAMUS_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_SAMUS_TYPE_##type name;
typedef struct MeleeWebSamusAttributes {
    MELEE_WEB_SAMUS_ATTRIBUTE_FIELDS(MELEE_WEB_SAMUS_DECLARE_ATTRIBUTE)
} MeleeWebSamusAttributes;
#undef MELEE_WEB_SAMUS_DECLARE_ATTRIBUTE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebSamusAttributes) == MELEE_WEB_SAMUS_ATTRIBUTE_BYTES,
              "Samus portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebSamusAttributes) == MELEE_WEB_SAMUS_ATTRIBUTE_BYTES,
               "Samus portable extension ABI");
#endif

#endif
