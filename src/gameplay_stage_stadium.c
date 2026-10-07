#include "gameplay_stage_stadium.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(int32_t) == 4, "Stadium signed word ABI");
_Static_assert(sizeof(uint32_t) == 4, "Stadium unsigned word ABI");
_Static_assert(sizeof(int16_t) == 2, "Stadium signed halfword ABI");
_Static_assert(sizeof(uint8_t) == 1, "Stadium RGB byte ABI");
_Static_assert(sizeof(MeleeWebStadiumYakumono) == 0x54,
               "Pokémon Stadium yakumono ABI size");

#define STADIUM_ASSERT_OFFSET(field, expected) \
    _Static_assert(offsetof(MeleeWebStadiumYakumono, field) == (expected), \
                   "Pokémon Stadium yakumono offset: " #field)

STADIUM_ASSERT_OFFSET(x0, 0x00);
STADIUM_ASSERT_OFFSET(x4, 0x04);
STADIUM_ASSERT_OFFSET(x8, 0x08);
STADIUM_ASSERT_OFFSET(xC, 0x0C);
STADIUM_ASSERT_OFFSET(x10, 0x10);
STADIUM_ASSERT_OFFSET(x14, 0x14);
STADIUM_ASSERT_OFFSET(x18, 0x18);
STADIUM_ASSERT_OFFSET(r, 0x1C);
STADIUM_ASSERT_OFFSET(g, 0x1D);
STADIUM_ASSERT_OFFSET(b, 0x1E);
STADIUM_ASSERT_OFFSET(_rgb_padding, 0x1F);
STADIUM_ASSERT_OFFSET(x20, 0x20);
STADIUM_ASSERT_OFFSET(x24, 0x24);
STADIUM_ASSERT_OFFSET(x28, 0x28);
STADIUM_ASSERT_OFFSET(x2C, 0x2C);
STADIUM_ASSERT_OFFSET(x30, 0x30);
STADIUM_ASSERT_OFFSET(x34, 0x34);
STADIUM_ASSERT_OFFSET(x38, 0x38);
STADIUM_ASSERT_OFFSET(x3C, 0x3C);
STADIUM_ASSERT_OFFSET(x40, 0x40);
STADIUM_ASSERT_OFFSET(x44, 0x44);
STADIUM_ASSERT_OFFSET(x48, 0x48);
STADIUM_ASSERT_OFFSET(x4A, 0x4A);
STADIUM_ASSERT_OFFSET(x4C, 0x4C);
STADIUM_ASSERT_OFFSET(x4E, 0x4E);
STADIUM_ASSERT_OFFSET(x50, 0x50);
STADIUM_ASSERT_OFFSET(_final_padding, 0x52);

#undef STADIUM_ASSERT_OFFSET

static int32_t stadium_s32(const MeleeWebNativeDat* reader, uint32_t offset)
{
    const uint32_t bits = reader->word(reader->context, offset);
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int16_t stadium_s16(const MeleeWebNativeDat* reader, uint32_t offset)
{
    const uint16_t bits = reader->half(reader->context, offset);
    int16_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void* melee_web_stadium_yakumono_decode(const MeleeWebNativeDat* reader,
                                        uint32_t root)
{
    if (!reader)
        return NULL;

    reader->region(reader->context, root, sizeof(MeleeWebStadiumYakumono));
    MeleeWebStadiumYakumono* out = reader->allocate(
        reader->context, 1, sizeof(MeleeWebStadiumYakumono));

    out->x0 = stadium_s32(reader, root + 0x00);
    out->x4 = stadium_s32(reader, root + 0x04);
    out->x8 = stadium_s32(reader, root + 0x08);
    out->xC = stadium_s32(reader, root + 0x0C);
    out->x10 = stadium_s32(reader, root + 0x10);
    out->x14 = stadium_s32(reader, root + 0x14);
    out->x18 = stadium_s32(reader, root + 0x18);
    out->r = reader->byte(reader->context, root + 0x1C);
    out->g = reader->byte(reader->context, root + 0x1D);
    out->b = reader->byte(reader->context, root + 0x1E);
    out->_rgb_padding = 0;
    out->x20 = reader->word(reader->context, root + 0x20);
    out->x24 = reader->word(reader->context, root + 0x24);
    out->x28 = reader->word(reader->context, root + 0x28);
    out->x2C = reader->word(reader->context, root + 0x2C);
    out->x30 = reader->word(reader->context, root + 0x30);
    out->x34 = reader->word(reader->context, root + 0x34);
    out->x38 = reader->word(reader->context, root + 0x38);
    out->x3C = reader->word(reader->context, root + 0x3C);
    out->x40 = reader->word(reader->context, root + 0x40);
    out->x44 = reader->word(reader->context, root + 0x44);
    out->x48 = stadium_s16(reader, root + 0x48);
    out->x4A = stadium_s16(reader, root + 0x4A);
    out->x4C = stadium_s16(reader, root + 0x4C);
    out->x4E = stadium_s16(reader, root + 0x4E);
    out->x50 = stadium_s16(reader, root + 0x50);
    out->_final_padding = 0;
    return out;
}
