#ifndef MELEE_WEB_GAMEPLAY_KOOPA_SCHEMA_H
#define MELEE_WEB_GAMEPLAY_KOOPA_SCHEMA_H

#include <stdint.h>

/* Exact ftKoopaAttributes ABI from melee/ft/kinds/ftKoopa/types.h.
 * x4 and x20 are signed source words; x2c and unk50 are unsigned source
 * words. Every other word is an authored IEEE-754 float. Keep the fields as
 * one table so C/C++ portable layout, source offsets and scalar categories
 * cannot drift independently. */
#define MELEE_WEB_KOOPA_ATTRIBUTE_BYTES 0xa0u
#define MELEE_WEB_KOOPA_TYPE_F32 float
#define MELEE_WEB_KOOPA_TYPE_I32 int32_t
#define MELEE_WEB_KOOPA_TYPE_U32 uint32_t

#define MELEE_WEB_KOOPA_ATTRIBUTE_FIELDS(X) \
    X(0x000, F32, x0, x0) \
    X(0x004, I32, x4, x4) \
    X(0x008, F32, x8, x8) \
    X(0x00c, F32, xC, xC) \
    X(0x010, F32, x10, x10) \
    X(0x014, F32, x14, x14) \
    X(0x018, F32, x18, x18) \
    X(0x01c, F32, x1C, x1C) \
    X(0x020, I32, x20, x20) \
    X(0x024, F32, x24, x24) \
    X(0x028, F32, x28, x28) \
    X(0x02c, U32, x2C, x2C) \
    X(0x030, F32, x30, x30) \
    X(0x034, F32, x34, x34) \
    X(0x038, F32, x38, x38) \
    X(0x03c, F32, x3C, x3C) \
    X(0x040, F32, x40, x40) \
    X(0x044, F32, x44, x44) \
    X(0x048, F32, x48, x48) \
    X(0x04c, F32, x4C, x4C) \
    X(0x050, U32, unk50, unk50) \
    X(0x054, F32, x54, x54) \
    X(0x058, F32, x58, x58) \
    X(0x05c, F32, x5C, x5C) \
    X(0x060, F32, x60, x60) \
    X(0x064, F32, x64, x64) \
    X(0x068, F32, x68, x68) \
    X(0x06c, F32, x6C, x6C) \
    X(0x070, F32, x70, x70) \
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
    X(0x09c, F32, x9C, x9C)

#define MELEE_WEB_KOOPA_DECLARE_ATTRIBUTE(offset, type, name, original) \
    MELEE_WEB_KOOPA_TYPE_##type name;

typedef struct MeleeWebKoopaAttributes {
    MELEE_WEB_KOOPA_ATTRIBUTE_FIELDS(MELEE_WEB_KOOPA_DECLARE_ATTRIBUTE)
} MeleeWebKoopaAttributes;

#undef MELEE_WEB_KOOPA_DECLARE_ATTRIBUTE

#ifdef __cplusplus
static_assert(sizeof(MeleeWebKoopaAttributes) == MELEE_WEB_KOOPA_ATTRIBUTE_BYTES,
              "Koopa portable extension ABI");
#else
_Static_assert(sizeof(MeleeWebKoopaAttributes) == MELEE_WEB_KOOPA_ATTRIBUTE_BYTES,
               "Koopa portable extension ABI");
#endif

#endif
