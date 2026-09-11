#include "gameplay_ps_math.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static void print_mtx(const Mtx m)
{
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            printf("%08x%c", bits(m[row][column]),
                   row == 2 && column == 3 ? '\n' : ' ');
}

static int same_mtx(const Mtx a, const Mtx b)
{
    return memcmp(a, b, sizeof(Mtx)) == 0;
}

static void unfused_negative_control(const Mtx a, const Mtx b, Mtx out)
{
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            float accum = b[0][column] * a[row][0];
            accum = accum + b[1][column] * a[row][1];
            accum = accum + b[2][column] * a[row][2];
            if (column >= 2)
                accum = accum + (column == 3 ? 1.0f : 0.0f) * a[row][3];
            out[row][column] = accum;
        }
    }
}

int main(void)
{
    /* These rows contain products whose exact sum needs the source's fused
     * middle and final operations. The fourth column also exercises Unit01's
     * zero lane for column two. */
    const Mtx a = {
        {0x1.000002p+0f, 0x1.000002p-1f, -0x1.000002p+1f, 3.25f},
        {-0x1.000002p+2f, 0x1.000002p-2f, 0x1.000002p+1f, -7.5f},
        {0x1.000002p-3f, -0x1.000002p+3f, 0x1.000002p-2f, 11.0f},
    };
    const Mtx b = {
        {0x1.fffffep-1f, -0x1.000002p+0f, 0x1.000002p-1f, -0x1.000002p+1f},
        {0x1.000002p+0f, 0x1.fffffep-2f, -0x1.000002p+1f, 0x1.000002p-2f},
        {0x1.000002p-1f, 0x1.000002p+1f, 0x1.fffffep-2f, -0x1.000002p+0f},
    };
    Mtx result;
    Mtx alias_a;
    Mtx alias_b;
    Mtx unfused;

    melee_web_ps_mtx_concat(a, b, result);
    print_mtx(result);
    unfused_negative_control(a, b, unfused);
    int fused_differs_from_unfused = !same_mtx(result, unfused);

    memcpy(alias_a, a, sizeof(alias_a));
    melee_web_ps_mtx_concat(alias_a, b, alias_a);
    if (!same_mtx(alias_a, result))
        return 2;

    memcpy(alias_b, b, sizeof(alias_b));
    melee_web_ps_mtx_concat(a, alias_b, alias_b);
    if (!same_mtx(alias_b, result))
        return 3;

    /* Signed zero is observable in the source's zero-lane fmaf and the
     * ordinary first multiply. */
    const Mtx zero_a = {
        {-0.0f, -0.0f, -0.0f, -0.0f},
        {-0.0f, -0.0f, -0.0f, -0.0f},
        {-0.0f, -0.0f, -0.0f, -0.0f},
    };
    const Mtx zero_b = {
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    };
    Mtx zero_result;
    melee_web_ps_mtx_concat(zero_a, zero_b, zero_result);
    printf("signed-zero %08x %08x %08x %08x\n",
           bits(zero_result[0][0]), bits(zero_result[0][2]),
           bits(zero_result[1][0]), bits(zero_result[2][2]));
    if (!fused_differs_from_unfused)
        return 4;
    return 0;
}
