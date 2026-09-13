/* Observed at the original air-dodge call sites on retail scene tick 155.
 * See docs/RETAIL_REPLAY_CAPTURE.md for capture and executable provenance. */
#include "gameplay_trig.h"
#include <stdio.h>

int main(void)
{
    const uint32_t input = 0xc01da0a6;
    float angle;
    memcpy(&angle, &input, sizeof(angle));
    printf("%08x %08x\n", melee_web_trig_bits(sinf(angle)),
           melee_web_trig_bits(cosf(angle)));
    angle = atan2f(melee_web_trig_from_bits(0xbf100000),
                   melee_web_trig_from_bits(0x3f500000));
    printf("%08x %08x %08x\n", melee_web_trig_bits(angle),
           melee_web_trig_bits(sinf(angle)), melee_web_trig_bits(cosf(angle)));
    if (melee_web_trig_bits(sinf(0.0f)) != 0 ||
        melee_web_trig_bits(cosf(0.0f)) != 0x3f800000 ||
        melee_web_trig_integer(INFINITY) != INT32_MAX ||
        melee_web_trig_integer(-INFINITY) != INT32_MIN ||
        melee_web_trig_integer(NAN) != INT32_MIN ||
        melee_web_trig_twice(INT32_MAX) != -2.0f ||
        melee_web_trig_twice(INT32_MIN) != 0.0f)
        return 1;
    return 0;
}
