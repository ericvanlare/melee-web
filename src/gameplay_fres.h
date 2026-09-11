#ifndef MELEE_WEB_GAMEPLAY_FRES_H
#define MELEE_WEB_GAMEPLAY_FRES_H

/*
 * Scalar model of the GameCube/750CL ``fres`` estimate.
 *
 * The table and integer steps follow Andrew Church's public-domain
 * calc_fres hardware test (https://achurch.org/cpu-tests/ppc750cl.s).
 * This helper accepts binary32 operands and returns value bits only; it does
 * not implement FPSCR exception/status flags.  Keeping the operation in words is intentional: it
 * preserves PPC's signed zero, NaN quieting, subnormal saturation, and the
 * output-denormal truncation without asking the host libm for a reciprocal.
 */

#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t base;
    uint16_t delta;
} melee_web_fres_table_entry;

static const melee_web_fres_table_entry melee_web_fres_table[32] = {
    {0x3FFC, 0x3E1}, {0x3C1C, 0x3A7}, {0x3875, 0x371}, {0x3504, 0x340},
    {0x31C4, 0x313}, {0x2EB1, 0x2EA}, {0x2BC8, 0x2C4}, {0x2904, 0x2A0},
    {0x2664, 0x27F}, {0x23E5, 0x261}, {0x2184, 0x245}, {0x1F40, 0x22A},
    {0x1D16, 0x212}, {0x1B04, 0x1FB}, {0x190A, 0x1E5}, {0x1725, 0x1D1},
    {0x1554, 0x1BE}, {0x1396, 0x1AC}, {0x11EB, 0x19B}, {0x104F, 0x18B},
    {0x0EC4, 0x17C}, {0x0D48, 0x16E}, {0x0BD7, 0x15B}, {0x0A7C, 0x15B},
    {0x0922, 0x143}, {0x07DF, 0x143}, {0x069C, 0x12D}, {0x056F, 0x12D},
    {0x0442, 0x11A}, {0x0328, 0x11A}, {0x020E, 0x108}, {0x0106, 0x106},
};

static inline uint32_t melee_web_fres_bits(uint32_t input)
{
    const uint32_t sign = input & UINT32_C(0x80000000);
    uint32_t exponent = (input >> 23) & UINT32_C(0xff);
    uint32_t mantissa = input & UINT32_C(0x007fffff);

    if (exponent == UINT32_C(0xff)) {
        if (mantissa == 0) {
            /* ±infinity -> signed zero. */
            return sign;
        }
        /* Preserve the payload while applying PPC's quiet-NaN bit. */
        return input | UINT32_C(0x00400000);
    }

    int32_t input_exponent = (int32_t) exponent;
    if (exponent == 0) {
        /* calc_fres saturates the smallest subnormals instead of normalizing
         * them.  Zero is the sole input that produces infinity. */
        if (mantissa < UINT32_C(0x00200000)) {
            return sign | (mantissa == 0 ? UINT32_C(0x7f800000)
                                         : UINT32_C(0x7f7fffff));
        }

        /* Normalize a larger subnormal exactly as the two possible PPC
         * shifts do, retaining the adjusted signed exponent. */
        mantissa <<= 1;
        if ((mantissa & UINT32_C(0x00800000)) == 0) {
            mantissa <<= 1;
            input_exponent = -1;
        }
        mantissa &= UINT32_C(0x007fffff);
    }

    int32_t output_exponent = 253 - input_exponent;
    const melee_web_fres_table_entry entry =
        melee_web_fres_table[(mantissa >> 18) & 31];
    const uint32_t table_delta =
        (uint32_t) entry.delta * ((mantissa >> 8) & UINT32_C(0x3ff));
    uint32_t estimate = ((uint32_t) entry.base << 10) - table_delta;
    estimate >>= 1;

    /* A reciprocal of a large finite input can be subnormal.  The reference
     * uses two right shifts at most; the hidden bit is restored before them. */
    if (output_exponent <= 0) {
        const int32_t was_negative = output_exponent < 0;
        output_exponent = 0;
        estimate |= UINT32_C(0x00800000);
        estimate >>= 1;
        if (was_negative) {
            estimate >>= 1;
        }
    }

    return sign | estimate | ((uint32_t) output_exponent << 23);
}

static inline float melee_web_fres(float input)
{
    uint32_t input_bits;
    uint32_t output_bits;
    float output;

    memcpy(&input_bits, &input, sizeof(input_bits));
    output_bits = melee_web_fres_bits(input_bits);
    memcpy(&output, &output_bits, sizeof(output));
    return output;
}

#endif
