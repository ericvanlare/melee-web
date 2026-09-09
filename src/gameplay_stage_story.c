#include "gameplay_stage_story.h"
#include "gameplay_compat.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(MeleeWebStoryYakumono) == 0x24,
               "Yoshi's Story yakumono ABI");

void* melee_web_story_yakumono_decode(const MeleeWebNativeDat* r,
                                      uint32_t root)
{
    if (!r) return NULL;
    r->region(r->context, root, sizeof(MeleeWebStoryYakumono));
    MeleeWebStoryYakumono* out =
        r->allocate(r->context, 1, sizeof(MeleeWebStoryYakumono));
    for (unsigned i = 0; i < 9; ++i) {
        uint32_t bits = r->word(r->context, root + 4 * i);
        float value;
        memcpy(&value, &bits, sizeof(value));
        if (!isfinite(value))
            r->reject(r->context,
                      "Yoshi's Story yakumono parameter is nonfinite");
        memcpy((float*) out + i, &value, sizeof(value));
    }
    if (out->timer_min < 0 || out->timer_rand < 0 ||
        out->timer_min > 360000 || out->timer_rand > 360000 ||
        out->spawnmany_rarity < 1 || out->spawnmany_rarity > 2147483647.0f ||
        floorf(out->timer_min) != out->timer_min ||
        floorf(out->timer_rand) != out->timer_rand ||
        floorf(out->spawnmany_rarity) != out->spawnmany_rarity)
        r->reject(r->context,
                  "Yoshi's Story timer or RNG parameter is outside source bounds");
    return out;
}

int melee_web_story_state(uint32_t* map_mask, unsigned* count,
                          int* randall_timer, int* shy_timer,
                          int* shy_count, int* shy_pattern)
{
    uint32_t mask = 0;
    unsigned objects = 0;
    if (stage_info.grkind != Gr_Kind_Story)
        return 0;
    for (unsigned i = 0; i < sizeof(stage_info.map_gobjs) /
                                  sizeof(stage_info.map_gobjs[0]); ++i) {
        if (stage_info.map_gobjs[i]) {
            ++objects;
            if (i < 32) mask |= UINT32_C(1) << i;
        }
    }
    if ((mask & UINT32_C(0xf)) != UINT32_C(0xf))
        return 0;
    Ground* randall = stage_info.map_gobjs[2]->user_data;
    Ground* shy = stage_info.map_gobjs[3]->user_data;
    if (!randall || !shy || randall->map_id != 2 || shy->map_id != 3)
        return 0;
    if (map_mask) *map_mask = mask;
    if (count) *count = objects;
    if (randall_timer) *randall_timer = randall->u.randall.timer;
    if (shy_timer) *shy_timer = shy->u.shyguys.timer;
    if (shy_count) *shy_count = shy->u.shyguys.count;
    if (shy_pattern) *shy_pattern = shy->u.shyguys.pattern;
    return objects == 4;
}
