#include "gameplay_stage_old_yoshi.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(MeleeWebOldYoshiYakumono) == 0x1c,
               "Old Yoshi yakumono ABI size");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x0) == 0x00,
               "Old Yoshi x0 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x2) == 0x02,
               "Old Yoshi x2 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x4) == 0x04,
               "Old Yoshi x4 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x8) == 0x08,
               "Old Yoshi x8 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, xC) == 0x0c,
               "Old Yoshi xC ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x10) == 0x10,
               "Old Yoshi x10 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x12) == 0x12,
               "Old Yoshi x12 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x14) == 0x14,
               "Old Yoshi x14 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x16) == 0x16,
               "Old Yoshi x16 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, x18) == 0x18,
               "Old Yoshi x18 ABI offset");
_Static_assert(offsetof(MeleeWebOldYoshiYakumono, _final_padding) == 0x1a,
               "Old Yoshi final padding offset");

static int16_t old_yoshi_s16(const MeleeWebNativeDat* reader, uint32_t offset)
{
    const uint16_t bits = reader->half(reader->context, offset);
    int16_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static float old_yoshi_float(const MeleeWebNativeDat* reader, uint32_t offset)
{
    const uint32_t bits = reader->word(reader->context, offset);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        reader->reject(reader->context,
                       "Old Yoshi yakumono parameter is nonfinite");
    return value;
}

static void old_yoshi_validate(const MeleeWebNativeDat* reader,
                               const MeleeWebOldYoshiYakumono* value)
{
    /* These fields feed integer counters, HSD_Randi, or rand_range.  The
     * source helper accepts either argument order, so x14/x16 are checked for
     * nonnegativity without imposing an authored-order relationship. */
    if (value->x0 < 0 || value->x2 < 0 || value->x10 < 0 || value->x12 < 0 ||
        value->x14 < 0 || value->x16 < 0)
        reader->reject(reader->context,
                       "Old Yoshi yakumono counter or RNG bound is negative");

    /* grOldYoshi_8020F31C divides by x8 and clamps velocity with +/-x4. */
    if (value->x4 < 0.0f || value->x8 <= 0.0f || value->xC < 0.0f)
        reader->reject(reader->context,
                       "Old Yoshi yakumono motion bound is outside source bounds");
}

void* melee_web_old_yoshi_yakumono_decode(const MeleeWebNativeDat* reader,
                                          uint32_t root)
{
    if (!reader)
        return NULL;

    reader->region(reader->context, root, sizeof(MeleeWebOldYoshiYakumono));
    MeleeWebOldYoshiYakumono* out = reader->allocate(
        reader->context, 1, sizeof(MeleeWebOldYoshiYakumono));
    out->x0 = old_yoshi_s16(reader, root + 0x00);
    out->x2 = old_yoshi_s16(reader, root + 0x02);
    out->x4 = old_yoshi_float(reader, root + 0x04);
    out->x8 = old_yoshi_float(reader, root + 0x08);
    out->xC = old_yoshi_float(reader, root + 0x0c);
    out->x10 = old_yoshi_s16(reader, root + 0x10);
    out->x12 = old_yoshi_s16(reader, root + 0x12);
    out->x14 = old_yoshi_s16(reader, root + 0x14);
    out->x16 = old_yoshi_s16(reader, root + 0x16);
    out->x18 = old_yoshi_s16(reader, root + 0x18);
    out->_final_padding = 0;
    old_yoshi_validate(reader, out);
    return out;
}
