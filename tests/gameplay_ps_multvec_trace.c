#include "gameplay_ps_math.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Provenance for the nine exact binary32 cases below:
 *   PSMTXMultVec: GALE01r2 0x80342AA8
 *   original main.dol SHA-256: dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646
 *   retail capture SHA-256: e1a0c367303b3d28dcd41cbd80f9a6d8f2a4d69bd3281d4a820d65878b5e426a
 *   scalar evidence SHA-256: 1b447e26a5e7014327992d81759db868e24de6fb379f763d3a1d9da70d2d0867
 */

typedef struct {
    uint32_t input[3];
    uint32_t output[3];
} MultVecCase;

static float from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static void load_matrix(Mtx matrix)
{
    static const uint32_t words[3][4] = {
        {0xbf333333, 0x3474e89d, 0x00000000, 0x4252b333},
        {0xb474e89d, 0xbf333333, 0x00000000, 0xc28e1999},
        {0x80000000, 0x00000000, 0x3f333333, 0x00000000},
    };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            matrix[row][column] = from_bits(words[row][column]);
        }
    }
}

static Vec input_vec(const MultVecCase* test_case)
{
    Vec result = {
        from_bits(test_case->input[0]),
        from_bits(test_case->input[1]),
        from_bits(test_case->input[2]),
    };
    return result;
}

static int matches(const Vec* value, const uint32_t expected[3])
{
    return bits(value->x) == expected[0] && bits(value->y) == expected[1] &&
           bits(value->z) == expected[2];
}

/* This is the ordinary multiply/add SDK fallback. It is intentionally kept
 * as a negative control: the tick-644 Y result rounds one ULP away from the
 * retail paired-single ps_madd/ps_sum0 sequence. */
static void unfused_negative_control(const Mtx matrix, const Vec* source,
                                     Vec* destination)
{
    float result[3];
    const float x = source->x;
    const float y = source->y;
    const float z = source->z;

    for (int row = 0; row < 3; ++row) {
        volatile float xy0 = matrix[row][0] * x;
        volatile float xy1 = matrix[row][1] * y;
        volatile float xy = xy0 + xy1;
        volatile float zterm = matrix[row][2] * z;
        volatile float xyz = zterm + xy;
        volatile float translation = matrix[row][3] * 1.0f;
        result[row] = translation + xyz;
    }
    destination->x = result[0];
    destination->y = result[1];
    destination->z = result[2];
}

int main(void)
{
    static const MultVecCase cases[] = {
        {{0xc2236173, 0xc294fffe, 0x00000000},
         {0x42a28880, 0xc1973331, 0x00000000}},
        {{0xc2215a3a, 0xc294fffe, 0x00000000},
         {0x42a1d2c5, 0xc1973331, 0x00000000}},
        {{0xc21f5346, 0xc294fffe, 0x00000000},
         {0x42a11d23, 0xc1973331, 0x00000000}},
        {{0xc21d4c51, 0xc294fffe, 0x00000000},
         {0x42a06780, 0xc1973331, 0x00000000}},
        {{0xc21b453a, 0xc294fffe, 0x00000000},
         {0x429fb1d2, 0xc1973331, 0x00000000}},
        {{0xc2193e23, 0xc294fffe, 0x00000000},
         {0x429efc24, 0xc1973331, 0x00000000}},
        {{0xc21736ea, 0xc294fffe, 0x00000000},
         {0x429e4669, 0xc1973331, 0x00000000}},
        {{0xc2152ff6, 0xc294fffe, 0x00000000},
         {0x429d90c7, 0xc1973332, 0x00000000}},
        {{0xc2132901, 0xc294fffe, 0x00000000},
         {0x429cdb24, 0xc1973332, 0x00000000}},
    };
    Mtx matrix;
    int direct_count = 0;
    int alias_count = 0;
    int mtx_alias_count = 0;
    int ps_alias_count = 0;

    load_matrix(matrix);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Vec source = input_vec(&cases[i]);
        Vec result;
        melee_web_ps_mtx_mult_vec(matrix, &source, &result);
        if (!matches(&result, cases[i].output)) {
            fprintf(stderr, "direct case %zu: %08x %08x %08x\n", i,
                    bits(result.x), bits(result.y), bits(result.z));
            return 1;
        }
        direct_count++;

        Vec in_place = source;
        melee_web_ps_mtx_mult_vec(matrix, &in_place, &in_place);
        if (!matches(&in_place, cases[i].output)) {
            fprintf(stderr, "in-place case %zu failed\n", i);
            return 2;
        }
        alias_count++;

        Vec mtx_alias = source;
        MTXMultVec(matrix, &mtx_alias, &mtx_alias);
        if (!matches(&mtx_alias, cases[i].output)) {
            fprintf(stderr, "MTXMultVec alias case %zu failed\n", i);
            return 3;
        }
        mtx_alias_count++;

        Vec ps_alias = source;
        PSMTXMultVec(matrix, &ps_alias, &ps_alias);
        if (!matches(&ps_alias, cases[i].output)) {
            fprintf(stderr, "PSMTXMultVec alias case %zu failed\n", i);
            return 4;
        }
        ps_alias_count++;
    }

    Vec negative_source = input_vec(&cases[4]);
    Vec negative_result;
    unfused_negative_control(matrix, &negative_source, &negative_result);
    if (bits(negative_result.y) == cases[4].output[1]) {
        fprintf(stderr, "unfused tick-644 control unexpectedly matched\n");
        return 5;
    }

    printf("multvec cases=%d direct=%d inplace=%d mtx=%d ps=%d negative=y-ulp\n",
           (int) (sizeof(cases) / sizeof(cases[0])), direct_count, alias_count,
           mtx_alias_count, ps_alias_count);
    return 0;
}
