#include "gameplay_audio_resample.h"
#include "gameplay_audio_itd.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct Input {
    const int16_t* samples;
    unsigned count;
    unsigned cursor;
} Input;

static int16_t read_sample(void* context)
{
    Input* input = context;
    assert(input->cursor < input->count);
    return input->samples[input->cursor++];
}

static void phase_and_rounding(void)
{
    int16_t bank[512] = {0};
    for (unsigned row = 0; row < 128; ++row) bank[4 * row] = row + 1;
    Input empty = {NULL, 0, 0};
    for (unsigned fraction = 0; fraction < 65536; ++fraction) {
        MeleeWebAudioResample state = {{32767, 0, 0, 0}, fraction};
        assert(melee_web_audio_resample(&state, 0, 0, bank, read_sample, &empty)
               == (int16_t)(fraction / 512));
        assert(state.fraction == fraction && state.history[0] == 32767);
        state.history[0] = -32768;
        assert(melee_web_audio_resample(&state, 0, 0, bank, read_sample, &empty)
               == -(int16_t)(fraction / 512 + 1));

        /* The line joining the two signed endpoints has this closed form. */
        state = (MeleeWebAudioResample){{-32768, 32767, 11, 22}, fraction};
        int expected = fraction ? (int)fraction - 32769 : -32768;
        assert(melee_web_audio_resample(&state, 0, 1, NULL, read_sample, &empty)
               == expected);
        assert(state.fraction == fraction);
    }
    assert(empty.cursor == 0);

    MeleeWebAudioResample state = {{-1, 0, 0, 0}, 0};
    assert(melee_web_audio_resample(&state, 0, 0, bank, read_sample, &empty) == -1);
    state = (MeleeWebAudioResample){{-3, 0, 4, 8}, 0};
    assert(melee_web_audio_resample(&state, 32768, 1, NULL, read_sample, &empty) == -2);

    for (unsigned tap = 0; tap < 4; ++tap) bank[tap] = -32768;
    state = (MeleeWebAudioResample){{-32768, -32768, -32768, -32768}, 0};
    assert(melee_web_audio_resample(&state, 0, 0, bank, read_sample, &empty) == INT16_MAX);
    for (unsigned tap = 0; tap < 4; ++tap) bank[tap] = 32767;
    assert(melee_web_audio_resample(&state, 0, 0, bank, read_sample, &empty) == INT16_MIN);
}

static void source_clock_and_modes(void)
{
    const int16_t samples[] = {50, 60, 70, 80, 90};
    /* Each row is the chronological four-word window after 0..4 reads. */
    const int16_t windows[][4] = {
        {-1, -2, -3, -4}, {-2, -3, -4, 50}, {-3, -4, 50, 60},
        {-4, 50, 60, 70}, {50, 60, 70, 80}
    };
    for (unsigned reads = 0; reads <= 4; ++reads) {
        Input input = {samples, 5, 0};
        MeleeWebAudioResample state = {{-1, -2, -3, -4}, 65535};
        (void)melee_web_audio_resample(&state, reads * 65536, 1, NULL, read_sample, &input);
        assert(input.cursor == reads && state.fraction == 65535);
        assert(memcmp(state.history, windows[reads], sizeof(state.history)) == 0);
    }

    Input input = {samples, 5, 0};
    MeleeWebAudioResample state = {{-1, -2, -3, -4}, 65535};
    assert(melee_web_audio_resample(&state, 1, 1, NULL, read_sample, &input) == -2);
    assert(input.cursor == 1 && state.fraction == 0);
    assert(memcmp(state.history, windows[1], sizeof(state.history)) == 0);

    /* Nearest reads once even at zero ratio and leaves fractional phase intact. */
    input.cursor = 0;
    state = (MeleeWebAudioResample){{-1, -2, -3, -4}, 32768};
    assert(melee_web_audio_resample(&state, 0, 2, NULL, read_sample, &input) == 50);
    assert(input.cursor == 1 && state.fraction == 32768);
    assert(memcmp(state.history, windows[1], sizeof(state.history)) == 0);
    assert(melee_web_audio_resample(&state, 0, 1, NULL, read_sample, &input) == -3);
    assert(input.cursor == 1 && state.fraction == 32768);
    assert(melee_web_audio_resample(&state, 0x40000, 2, NULL, read_sample, &input) == 60);
    assert(input.cursor == 2 && state.fraction == 32768);
    assert(memcmp(state.history, windows[2], sizeof(state.history)) == 0);
}

static void interaural_delay(void)
{
    uint16_t shift = 0;
    for (unsigned i = 0; i < 31; ++i) {
        assert(melee_web_audio_itd_delay(shift) == 32 - i);
        shift = melee_web_audio_itd_step(shift, 31);
    }
    assert(shift == 31 && melee_web_audio_itd_step(shift, 31) == 31);
    for (unsigned i = 0; i < 31; ++i) shift = melee_web_audio_itd_step(shift, 0);
    assert(shift == 0);
}

int main(void)
{
    phase_and_rounding();
    source_clock_and_modes();
    interaural_delay();
    puts("Four-tap phase/history/saturation, signed linear rounding and nearest passed");
}
