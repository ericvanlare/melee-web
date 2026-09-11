/* Numeric oracle: read-only GALE01r2 PSMTXQuat observations.
 * Sidecar SHA256 ee1367ac89ba49e16da73baa69a239d33d226302635448ae5245ffaa2de287e5.
 * Fres edge values: Andrew Church's public-domain calc_fres hardware tests. */
#include "gameplay_ps_math.h"
#include "gameplay_fres.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct quat_case {
    uint32_t input[4];
    uint32_t expected[12];
};

static const struct quat_case quat_cases[] = {
    {
        {0xbd0e580e, 0xbeb4a342, 0xbd97a97a, 0x3f6e9ef7},
        {0x3f3d7636, 0x3e2679e8, 0xbf270ea1, 0x00000000,
         0xbde88320, 0x3f7c92e9, 0x3defb223, 0x00000000,
         0x3f29b143, 0xbc4d52fa, 0x3f3fa6b6, 0x00000000},
    },
    {
        {0x3ca32cfd, 0xbe8dad7f, 0x3d8fa102, 0x3f754b76},
        {0x3f56470c, 0xbe14e939, 0xbf0709b4, 0x00000000,
         0x3dfcab43, 0x3f7d4755, 0xbd9daa19, 0x00000000,
         0x3f0877e8, 0xba27f895, 0x3f5897b5, 0x00000000},
    },
    {
        {0xbe67f0a7, 0x3e8b3215, 0x3d88b51a, 0x3f6ed377},
        {0x3f57e043, 0xbe7da654, 0x3ef43b96, 0x00000000,
         0x3ab62025, 0x3f637373, 0x3eeaf695, 0x00000000,
         0xbf099949, 0xbec5cc0b, 0x3f3fe3c5, 0x00000000},
    },
    {
        {0x00000000, 0x00000000, 0x3f719f51, 0x3ea92ba9},
        {0xbf481aa9, 0xbf1fab5e, 0x00000000, 0x00000000,
         0x3f1fab5e, 0xbf481aa9, 0x80000000, 0x00000000,
         0x80000000, 0x00000000, 0x3f800000, 0x00000000},
    },
};

struct fres_case {
    uint32_t input;
    uint32_t expected;
};

static const struct fres_case fres_cases[] = {
    {0x40000000, 0x3efff800},
    {0xc0000000, 0xbefff800},
    {0x7f7fffff, 0x00200020},
    {0xff7fffff, 0x80200020},
    {0x00000001, 0x7f7fffff},
    {0x80000001, 0xff7fffff},
    {0x001fffff, 0x7f7fffff},
    {0x801fffff, 0xff7fffff},
    {0x00200000, 0x7f7ff800},
    {0x80200000, 0xff7ff800},
    {0x00000000, 0x7f800000},
    {0x80000000, 0xff800000},
    {0x7f800000, 0x00000000},
    {0xff800000, 0x80000000},
    {0x7fc12345, 0x7fc12345},
    {0x7fa12345, 0x7fe12345},
};

static int check_quat_case(const struct quat_case* item, unsigned index)
{
    Quaternion input;
    Mtx output;
    uint32_t actual[12];

    memcpy(&input, item->input, sizeof(input));
    melee_web_ps_mtx_quat(output, &input);
    memcpy(actual, output, sizeof(actual));
    if (memcmp(actual, item->expected, sizeof(actual)) == 0)
        return 0;
    fprintf(stderr, "PSMTXQuat case %u mismatch\n", index);
    for (unsigned i = 0; i < 12; ++i)
        fprintf(stderr, "%08x/%08x%c", item->expected[i], actual[i],
                i == 11 ? '\n' : ' ');
    return 1;
}

int main(void)
{
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(quat_cases) / sizeof(quat_cases[0]); ++i)
        failures += (unsigned) check_quat_case(&quat_cases[i], i);
    for (unsigned i = 0; i < sizeof(fres_cases) / sizeof(fres_cases[0]); ++i) {
        uint32_t actual = melee_web_fres_bits(fres_cases[i].input);
        if (actual != fres_cases[i].expected) {
            fprintf(stderr, "fres case %u: %08x/%08x\n", i,
                    fres_cases[i].expected, actual);
            ++failures;
        }
    }
    printf("psquat_cases %u fres_cases %u failures %u\n",
           (unsigned) (sizeof(quat_cases) / sizeof(quat_cases[0])),
           (unsigned) (sizeof(fres_cases) / sizeof(fres_cases[0])), failures);
    return failures != 0;
}
