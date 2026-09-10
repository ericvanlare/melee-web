#include "gameplay_stage_dream_land.h"
#include <math.h>
#include <string.h>

_Static_assert(sizeof(MeleeWebDreamLandYakumono) == 0x34, "Dream Land yakumono ABI");

static float floating(const MeleeWebNativeDat* r, uint32_t at)
{
    uint32_t bits = r->word(r->context, at);
    float value;
    memcpy(&value, &bits, sizeof(value));
    if (!isfinite(value)) r->reject(r->context, "Dream Land yakumono parameter is nonfinite");
    return value;
}

void* melee_web_dream_land_yakumono_decode(const MeleeWebNativeDat* r, uint32_t root)
{
    if (!r) return NULL;
    r->region(r->context, root, sizeof(MeleeWebDreamLandYakumono));
    MeleeWebDreamLandYakumono* out = r->allocate(r->context, 1, sizeof(*out));
    out->bird_timer_min = (int16_t) r->half(r->context, root);
    out->bird_timer_max = (int16_t) r->half(r->context, root + 2);
    out->bird_height = (int16_t) r->half(r->context, root + 4);
    out->unused = (int16_t) r->half(r->context, root + 6);
    out->tree_timer_max = (int32_t) r->word(r->context, root + 8);
    out->tree_timer_min = (int32_t) r->word(r->context, root + 12);
    float* values = &out->wind_speed;
    for (unsigned i = 0; i < 9; ++i) values[i] = floating(r, root + 16 + 4 * i);
    if (out->bird_timer_min < 0 || out->bird_timer_max < 0 ||
        out->tree_timer_min < 0 || out->tree_timer_max < 0 ||
        out->bird_height < 0 || out->wind_speed < 0 ||
        out->blink_timer_min < 0 || out->blink_timer_max < 0)
        r->reject(r->context, "Dream Land timer, height or wind parameter is negative");
    return out;
}
