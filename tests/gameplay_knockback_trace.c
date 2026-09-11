#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef int32_t s32;

typedef struct ftCommonData {
    float xF4;
    float xF8;
    float x108;
    float x110;
    float x114;
    float x118;
    float x11C;
    float x120;
    int x6D4;
    int x6D8[1];
} ftCommonData;

typedef struct HitCapsule {
    u32 x24;
    u32 x28;
    u32 x2C;
} HitCapsule;

typedef struct FighterDamage {
    float x1830_percent;
    float x1838_percentTemp;
} FighterDamage;

typedef struct FighterAttributes {
    float weight;
} FighterAttributes;

typedef struct Fighter {
    unsigned char x2225_b7;
    unsigned char x2224_b2;
    FighterDamage dmg;
    FighterAttributes co_attrs;
} Fighter;

static ftCommonData common_data;
ftCommonData* p_ftCommonData = &common_data;

/* The test inserts these three definitions from the patched ftcoll.c. */
/* PATCHED_KNOCKBACK_FUNCTIONS */

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

/* Deliberately unfused control for the same diagnostic inputs. */
static float unfused_control(Fighter* fp, HitCapsule* hit, u32 damage,
                             float stage, float attack, float defense,
                             float weight)
{
    float w = weight * p_ftCommonData->xF4;
    float weighted = p_ftCommonData->xF8 -
                     (w * p_ftCommonData->xF8) / (1.0f + w);
    float set = p_ftCommonData->x118;
    float inner;
    if (hit->x28 != 0) {
        inner = set * p_ftCommonData->x110 +
                p_ftCommonData->x114 * (set * (float) hit->x28);
    } else {
        float total = (float) fp->dmg.x1830_percent +
                      fp->dmg.x1838_percentTemp;
        inner = p_ftCommonData->x110 * total +
                p_ftCommonData->x114 * ((float) damage * total);
    }
    {
        float scaled = p_ftCommonData->x11C * (weighted * inner) +
                       p_ftCommonData->x120;
        float result = (0.01f * (float) hit->x24) * scaled +
                       (float) hit->x2C;
        result = defense * (attack * (stage * result));
        if (result >= p_ftCommonData->x108)
            result = p_ftCommonData->x108;
        return result;
    }
}

int main(void)
{
    common_data.xF4 = 0x1.47ae14p-7f; /* .01 */
    common_data.xF8 = 2.0f;
    common_data.x108 = 2500.0f;
    common_data.x110 = 0x1.99999ap-4f; /* .1 */
    common_data.x114 = 0x1.99999ap-5f; /* .05 */
    common_data.x118 = 10.0f;
    common_data.x11C = 0x1.666666p+0f; /* 1.4 */
    common_data.x120 = 18.0f;
    common_data.x6D4 = 25;
    common_data.x6D8[0] = 25;

    Fighter fighter = {0};
    fighter.co_attrs.weight = 75.0f;
    fighter.dmg.x1830_percent = 25.0f;
    fighter.dmg.x1838_percentTemp = 2.0f;
    HitCapsule hit = {100, 5, 0};

    float fused = ftColl_80079AB0(&fighter, &hit, 3, 1.0f, 1.0f, 1.0f,
                                  fighter.co_attrs.weight);
    float unfused = unfused_control(&fighter, &hit, 3, 1.0f, 1.0f, 1.0f,
                                    fighter.co_attrs.weight);
    printf("set %08x %08x\n", bits(fused), bits(unfused));
    if (bits(fused) != 0x41bccccd || bits(unfused) != 0x41bccccc)
        return 1;

    hit.x28 = 0;
    hit.x24 = 10000;
    float capped = ftColl_80079AB0(&fighter, &hit, 3, 1.0f, 1.0f, 1.0f,
                                   fighter.co_attrs.weight);
    printf("ordinary-cap %08x\n", bits(capped));
    if (bits(capped) != bits(common_data.x108))
        return 2;

    hit.x28 = 5;
    hit.x24 = 100;
    float ratios = ftColl_80079AB0(&fighter, &hit, 3, 1.3f, 0.9f, 0.8f,
                                   fighter.co_attrs.weight);
    printf("ratios %08x\n", bits(ratios));
    if (bits(ratios) != 0x41b0b780)
        return 3;
    return 0;
}
