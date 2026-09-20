#include "gameplay_stage_fountain.h"
#include <math.h>
#include <string.h>

_Static_assert(sizeof(MeleeWebFountainYakumono) == 0x54,
               "Fountain of Dreams yakumono ABI");
_Static_assert(sizeof(int32_t) == 4, "Fountain yakumono x4 ABI");

static float fountain_float(const MeleeWebNativeDat* r, uint32_t at)
{
    uint32_t bits = r->word(r->context, at);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value))
        r->reject(r->context,
                  "Fountain of Dreams yakumono parameter is nonfinite");
    return value;
}

static int32_t fountain_int32(const MeleeWebNativeDat* r, uint32_t at)
{
    uint32_t bits = r->word(r->context, at);
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void* melee_web_fountain_yakumono_decode(const MeleeWebNativeDat* r,
                                         uint32_t root)
{
    if (!r)
        return NULL;
    r->region(r->context, root, sizeof(MeleeWebFountainYakumono));
    MeleeWebFountainYakumono* out =
        r->allocate(r->context, 1, sizeof(MeleeWebFountainYakumono));
    out->x0 = fountain_float(r, root + 0x00);
    out->x4 = fountain_int32(r, root + 0x04);
    out->x8 = fountain_float(r, root + 0x08);
    out->xC = fountain_float(r, root + 0x0C);
    out->x10 = fountain_float(r, root + 0x10);
    out->x14 = fountain_float(r, root + 0x14);
    out->x18 = fountain_float(r, root + 0x18);
    out->x1C = fountain_float(r, root + 0x1C);
    out->x20 = fountain_float(r, root + 0x20);
    out->x24 = fountain_float(r, root + 0x24);
    out->x28 = fountain_float(r, root + 0x28);
    out->x2C = fountain_float(r, root + 0x2C);
    out->x30 = fountain_float(r, root + 0x30);
    out->x34 = fountain_float(r, root + 0x34);
    out->x38 = fountain_float(r, root + 0x38);
    out->x3C = fountain_float(r, root + 0x3C);
    out->x40 = fountain_float(r, root + 0x40);
    out->x44 = fountain_float(r, root + 0x44);
    out->x48 = fountain_float(r, root + 0x48);
    out->x4C = fountain_float(r, root + 0x4C);
    out->x50 = fountain_float(r, root + 0x50);
    return out;
}
