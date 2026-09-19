#include "gameplay_audio_resample.h"

#include <stdint.h>

/* The port contract requires floor division, including negative products.
 * Use quotient/remainder so this does not depend on signed right shifts. */
static int64_t arithmetic_shift_right(int64_t value, unsigned shift)
{
    const int64_t quantum = (int64_t)1 << shift;

    return value / quantum - (value % quantum < 0);
}

static int16_t saturate_sample(int64_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)value;
}

static void consume_source(MeleeWebAudioResample* state,
                           uint32_t count,
                           MeleeWebAudioReadSample read_sample,
                           void* context)
{
    while (count != 0) {
        state->history[0] = state->history[1];
        state->history[1] = state->history[2];
        state->history[2] = state->history[3];
        state->history[3] = read_sample(context);
        --count;
    }
}

static int16_t render_polyphase(const MeleeWebAudioResample* state,
                                const int16_t* coefficients)
{
    /* DSPCode.c 0x060e-0x0615 forms ((phase >> 7) & 0x01fc) and adds the
     * selected DROM base.  The 0x01fc mask selects 128 rows of four words. */
    const unsigned coefficient_index = (state->fraction >> 7) & 0x01fcu;
    int64_t accumulator = 0;

    for (unsigned tap = 0; tap != 4; ++tap) {
        accumulator += (int64_t)state->history[tap] *
                       (int64_t)coefficients[coefficient_index + tap];
    }

    /* Preserve the port's Q15 floor and final signed-16-bit saturation.
     * Original DSP overflow/extraction equivalence remains unverified. */
    return saturate_sample(arithmetic_shift_right(accumulator, 15));
}

static int16_t render_linear(const MeleeWebAudioResample* state)
{
    const uint32_t fraction = state->fraction;
    const int64_t accumulator =
        (int64_t)state->history[0] * (int64_t)(0x10000u - fraction) +
        (int64_t)state->history[1] * (int64_t)fraction;

    return saturate_sample(arithmetic_shift_right(accumulator, 16));
}

int16_t melee_web_audio_resample(MeleeWebAudioResample* state,
                                 uint32_t ratio,
                                 unsigned select,
                                 const int16_t* coefficients,
                                 MeleeWebAudioReadSample read_sample,
                                 void* context)
{
    if (select == 2) {
        /* The port reads one direct sample and preserves fractional phase.
         * Equivalence to DSP block-boundary phase writeback remains unverified;
         * see docs/AUDIO_RESAMPLER_CONTRACT.md. */
        const int16_t output = read_sample(context);
        state->history[0] = state->history[1];
        state->history[1] = state->history[2];
        state->history[2] = state->history[3];
        state->history[3] = output;
        return output;
    }

    /* The DSP advances its source work ring before its final output loop.
     * Carry the integral phase into chronological history first, then render
     * from that state.  DSPCode.c persists only the low 16 bits at
     * 0x0634/0x06e0. */
    const uint32_t phase = state->fraction + ratio;
    const uint32_t consumed = phase >> 16;
    consume_source(state, consumed, read_sample, context);
    state->fraction = phase & 0xffffu;

    if (select == 0) {
        return render_polyphase(state, coefficients);
    }
    return render_linear(state);
}
