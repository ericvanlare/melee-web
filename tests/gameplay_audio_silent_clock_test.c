#include "gameplay_audio_silent_clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t run(uint32_t ratio, uint8_t selector, unsigned frames,
                    uint32_t* fraction) {
    uint32_t consumed = 0;
    for (unsigned i = 0; i < frames; ++i)
        consumed += melee_web_audio_silent_clock_advance(fraction, ratio, selector);
    return consumed;
}

int main(void) {
    uint32_t fraction = 0;
    assert(run(32768, 0, 8, &fraction) == 4 && fraction == 0);
    fraction = 0;
    assert(run(98304, 1, 8, &fraction) == 12 && fraction == 0);
    fraction = 0;
    assert(run(0, 0, 32, &fraction) == 0 && fraction == 0);
    fraction = 0x8000;
    assert(melee_web_audio_silent_clock_advance(&fraction, 0, 2) == 1);
    assert(fraction == 0x8000);

    uint32_t whole_fraction = 0;
    const uint32_t whole = run(0x14000, 0, 17, &whole_fraction);
    uint32_t partitioned_fraction = 0;
    const uint32_t partitioned = run(0x14000, 0, 5, &partitioned_fraction) +
                                 run(0x14000, 0, 12, &partitioned_fraction);
    assert(whole == partitioned && whole_fraction == partitioned_fraction);
    puts("Silent AX source clock: selector rates, fractional cursor, nearest and partition invariance passed");
    return 0;
}
