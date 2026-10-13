#include "gameplay_fma_diagnostic.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t to_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int bits(void)
{
    const float a = from_bits(0x3f800001u);
    const float b = from_bits(0x3f7ffffeu);
    const float c = from_bits(0xbf800000u);
    volatile float rounded_product = a * b;
    const float split = rounded_product + c;
    const float candidate = MELEE_WEB_FMA_COLLISION_ADD(0, a, b, c);
    const float tie = MELEE_WEB_FMA_COLLISION_ADD(0,
        from_bits(0x42480000u), from_bits(0xbc88cc38u), from_bits(0x1b1c72a0u));
    const float negative_zero = MELEE_WEB_FMA_COLLISION_ADD(0,
        from_bits(0x80000000u), 1.0f, from_bits(0x80000000u));
    printf("candidate=%08x split=%08x tie=%08x negative_zero=%08x\n",
        to_bits(candidate), to_bits(split), to_bits(tie), to_bits(negative_zero));
    return 0;
}

#ifdef MELEE_WEB_FMA_DIAGNOSTIC
static int summary(int include_velocity_y)
{
    char output[8192];
    int prefix = snprintf(output, sizeof(output), "{\"control\":\"summary\"");
    if (prefix < 0 || (size_t) prefix >= sizeof(output))
        return 2;
    size_t used = (size_t) prefix;
    melee_web_fma_diagnostic_cursor(5239);
    (void) MELEE_WEB_FMA_COLLISION_ADD(MELEE_WEB_FMA_RADIUS,
        1.0f, 2.0f, 3.0f);
    if (include_velocity_y)
        (void) MELEE_WEB_FMA_VELOCITY_Y(0.5f, 2.0f, 1.0f);
    melee_web_fma_diagnostic_append_json(output, sizeof(output), &used);
    if (used + 2 > sizeof(output))
        return 2;
    output[used++] = '}';
    output[used] = 0;
    puts(output);
    melee_web_fma_diagnostic_finish_cursor();
    return 0;
}
#endif

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    if (strcmp(argv[1], "bits") == 0)
        return bits();
#ifdef MELEE_WEB_FMA_DIAGNOSTIC
    if (strcmp(argv[1], "summary") == 0)
        return summary(0);
    if (strcmp(argv[1], "summary-y") == 0)
        return summary(1);
    if (strcmp(argv[1], "reject-nan") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0, 1.0f, 1.0f,
            from_bits(0x7fc00000u));
    else if (strcmp(argv[1], "reject-infinity") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0,
            from_bits(0x7f800000u), 1.0f, 0.0f);
    else if (strcmp(argv[1], "reject-subnormal") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0,
            from_bits(0x00000001u), 1.0f, 0.0f);
    else if (strcmp(argv[1], "reject-ni-underflow") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0, FLT_MIN, 0.5f, 0.0f);
    else if (strcmp(argv[1], "reject-ni-boundary-positive") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0,
            from_bits(0x80800000u), from_bits(0x00800000u),
            from_bits(0x00800000u));
    else if (strcmp(argv[1], "reject-ni-boundary-negative") == 0)
        (void) MELEE_WEB_FMA_COLLISION_ADD(0,
            from_bits(0x00800000u), from_bits(0x00800000u),
            from_bits(0x80800000u));
    else
        return 2;
    return 3;
#else
    return 2;
#endif
}
