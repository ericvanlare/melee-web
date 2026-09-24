#ifndef MELEE_WEB_GAMEPLAY_PURIN_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_PURIN_SCHEMA_H

#include <stdint.h>

/* Exact ftPurinAttributes ABI from melee/ft/kinds/ftPurin/types.h.
 *
 * Rows carry the serialized offset, scalar category, destination expression in
 * the portable record, destination container/component, source container and
 * source expression.  The two specialn_vel rows retain the source Vec2
 * container and make its component offsets explicit.  Padding is represented
 * as bytes so no guessed semantics leak into the portable ABI. */
#define MELEE_WEB_PURIN_ATTRIBUTE_BYTES 0x100u
#define MELEE_WEB_PURIN_TYPE_F32 float
#define MELEE_WEB_PURIN_TYPE_I32 int32_t
#define MELEE_WEB_PURIN_TYPE_OPAQUE32 uint32_t
#define MELEE_WEB_PURIN_TYPE_PAD4 uint8_t[4]
#define MELEE_WEB_PURIN_TYPE_PAD8 uint8_t[8]
#define MELEE_WEB_PURIN_TYPE_BYTES_F32 4u
#define MELEE_WEB_PURIN_TYPE_BYTES_I32 4u
#define MELEE_WEB_PURIN_TYPE_BYTES_OPAQUE32 4u
#define MELEE_WEB_PURIN_TYPE_BYTES_PAD4 4u
#define MELEE_WEB_PURIN_TYPE_BYTES_PAD8 8u

typedef struct MeleeWebPurinVec2 {
    float x;
    float y;
} MeleeWebPurinVec2;

#define MELEE_WEB_PURIN_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, x0, x0, 0x0, x0, x0) \
    X(0x004, F32, x4, x4, 0x0, x4, x4) \
    X(0x008, F32, x8, x8, 0x0, x8, x8) \
    X(0x00c, F32, xC, xC, 0x0, xC, xC) \
    X(0x010, F32, x10, x10, 0x0, x10, x10) \
    X(0x014, I32, x14, x14, 0x0, x14, x14) \
    X(0x018, F32, x18, x18, 0x0, x18, x18) \
    X(0x01c, I32, x1C, x1C, 0x0, x1C, x1C) \
    X(0x020, I32, x20, x20, 0x0, x20, x20) \
    X(0x024, I32, x24, x24, 0x0, x24, x24) \
    X(0x028, I32, x28, x28, 0x0, x28, x28) \
    X(0x02c, I32, x2C, x2C, 0x0, x2C, x2C) \
    X(0x030, I32, x30, x30, 0x0, x30, x30) \
    X(0x034, I32, x34, x34, 0x0, x34, x34) \
    X(0x038, I32, x38, x38, 0x0, x38, x38) \
    X(0x03c, F32, x3C, x3C, 0x0, x3C, x3C) \
    X(0x040, F32, x40, x40, 0x0, x40, x40) \
    X(0x044, F32, x44, x44, 0x0, x44, x44) \
    X(0x048, PAD4, _48, _48, 0x0, _48, _48) \
    X(0x04c, F32, x4C, x4C, 0x0, x4C, x4C) \
    X(0x050, F32, x50, x50, 0x0, x50, x50) \
    X(0x054, F32, x54, x54, 0x0, x54, x54) \
    X(0x058, F32, x58, x58, 0x0, x58, x58) \
    X(0x05c, F32, x5C, x5C, 0x0, x5C, x5C) \
    X(0x060, PAD8, _60, _60, 0x0, _60, _60) \
    X(0x068, F32, x68, x68, 0x0, x68, x68) \
    X(0x06c, F32, x6C, x6C, 0x0, x6C, x6C) \
    X(0x070, I32, x70, x70, 0x0, x70, x70) \
    X(0x074, F32, x74, x74, 0x0, x74, x74) \
    X(0x078, F32, x78, x78, 0x0, x78, x78) \
    X(0x07c, F32, x7C, x7C, 0x0, x7C, x7C) \
    X(0x080, F32, x80, x80, 0x0, x80, x80) \
    X(0x084, F32, x84, x84, 0x0, x84, x84) \
    X(0x088, F32, specialn_vel.x, specialn_vel, 0x0, specialn_vel, specialn_vel.x) \
    X(0x08c, F32, specialn_vel.y, specialn_vel, 0x4, specialn_vel, specialn_vel.y) \
    X(0x090, F32, x90, x90, 0x0, x90, x90) \
    X(0x094, F32, x94, x94, 0x0, x94, x94) \
    X(0x098, F32, x98, x98, 0x0, x98, x98) \
    X(0x09c, I32, x9C, x9C, 0x0, x9C, x9C) \
    X(0x0a0, F32, xA0, xA0, 0x0, xA0, xA0) \
    X(0x0a4, F32, xA4, xA4, 0x0, xA4, xA4) \
    X(0x0a8, F32, xA8, xA8, 0x0, xA8, xA8) \
    X(0x0ac, F32, xAC, xAC, 0x0, xAC, xAC) \
    X(0x0b0, PAD4, _B0, _B0, 0x0, _B0, _B0) \
    X(0x0b4, F32, xB4, xB4, 0x0, xB4, xB4) \
    X(0x0b8, F32, xB8, xB8, 0x0, xB8, xB8) \
    X(0x0bc, F32, xBC, xBC, 0x0, xBC, xBC) \
    X(0x0c0, F32, xC0, xC0, 0x0, xC0, xC0) \
    X(0x0c4, F32, xC4, xC4, 0x0, xC4, xC4) \
    X(0x0c8, F32, xC8, xC8, 0x0, xC8, xC8) \
    X(0x0cc, F32, xCC, xCC, 0x0, xCC, xCC) \
    X(0x0d0, F32, xD0, xD0, 0x0, xD0, xD0) \
    X(0x0d4, F32, xD4, xD4, 0x0, xD4, xD4) \
    X(0x0d8, F32, xD8, xD8, 0x0, xD8, xD8) \
    X(0x0dc, F32, xDC, xDC, 0x0, xDC, xDC) \
    X(0x0e0, F32, xE0, xE0, 0x0, xE0, xE0) \
    X(0x0e4, F32, xE4, xE4, 0x0, xE4, xE4) \
    X(0x0e8, OPAQUE32, xE8, xE8, 0x0, xE8, xE8) \
    X(0x0ec, OPAQUE32, xEC, xEC, 0x0, xEC, xEC) \
    X(0x0f0, F32, xF0, xF0, 0x0, xF0, xF0) \
    X(0x0f4, F32, xF4, xF4, 0x0, xF4, xF4) \
    X(0x0f8, PAD8, _F8, _F8, 0x0, _F8, _F8)

/* The source Vec2 is kept as a named container; all other rows are generated
 * from the table.  This layout is deliberately independent of host pointers. */
typedef struct MeleeWebPurinAttributes {
    float x0, x4, x8, xC, x10;
    int32_t x14;
    float x18;
    int32_t x1C, x20, x24, x28, x2C, x30, x34, x38;
    float x3C, x40, x44;
    uint8_t _48[4];
    float x4C, x50, x54, x58, x5C;
    uint8_t _60[8];
    float x68, x6C;
    int32_t x70;
    float x74, x78, x7C, x80, x84;
    MeleeWebPurinVec2 specialn_vel;
    float x90, x94, x98;
    int32_t x9C;
    float xA0, xA4, xA8, xAC;
    uint8_t _B0[4];
    float xB4, xB8, xBC, xC0, xC4, xC8, xCC, xD0, xD4, xD8, xDC, xE0, xE4;
    uint32_t xE8, xEC;
    float xF0, xF4;
    uint8_t _F8[8];
} MeleeWebPurinAttributes;

#ifdef __cplusplus
static_assert(sizeof(MeleeWebPurinVec2) == 8, "Purin Vec2 ABI");
static_assert(sizeof(MeleeWebPurinAttributes) == MELEE_WEB_PURIN_ATTRIBUTE_BYTES,
              "Purin portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebPurinVec2) == 8, "Purin Vec2 ABI");
_Static_assert(sizeof(MeleeWebPurinAttributes) == MELEE_WEB_PURIN_ATTRIBUTE_BYTES,
               "Purin portable extension ABI");
#endif

#endif
