/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ucf_dbooc_fix.h"

#include <math.h>
#include <string.h>

#include <gameplay_trig.h>

#define UCF_DBOOC_EPSILON_BITS UINT32_C(0x38D1B717)
#define UCF_DBOOC_OVERRIDE_THRESHOLD_BITS UINT32_C(0x3F170A3D)
#define UCF_DBOOC_RADIUS_LIMIT UINT32_C(6400)
#define UCF_DBOOC_SIGNED_MAX UINT32_C(0x80000000)

_Static_assert(sizeof(float) == sizeof(uint32_t),
               "DBOOC helper requires the source binary32 float type");

static float float_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t float_to_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint32_t ucf_dbooc_quantize(float axis, uint32_t *fmsubs_bits,
                                  uint32_t *conversion_word)
{
    const float epsilon = float_from_bits(UCF_DBOOC_EPSILON_BITS);
    const float scaled_minus_epsilon = fmaf(fabsf(axis), 80.0f, -epsilon);
    const int32_t converted = melee_web_trig_integer(scaled_minus_epsilon);
    const uint32_t converted_word = (uint32_t) converted;

    *fmsubs_bits = float_to_bits(scaled_minus_epsilon);
    *conversion_word = converted_word;
    return converted_word + UINT32_C(2);
}

uint32_t slippi_profile_ucf_quantized_radius_word(
    uint32_t stick_x_bits, uint32_t stick_y_bits,
    SlippiUcfQuantizedRadiusTrace *trace)
{
    SlippiUcfQuantizedRadiusTrace unused_trace;
    if (trace == NULL) {
        trace = &unused_trace;
    }
    memset(trace, 0, sizeof(*trace));

    const float stick_x = float_from_bits(stick_x_bits);
    const float stick_y = float_from_bits(stick_y_bits);
    const uint32_t x_q_word = ucf_dbooc_quantize(
        stick_x, &trace->x_fmsubs_bits, &trace->x_conversion_word);
    const uint32_t y_q_word = ucf_dbooc_quantize(
        stick_y, &trace->y_fmsubs_bits, &trace->y_conversion_word);
    const uint32_t x_square_low32 = x_q_word * x_q_word;
    const uint32_t y_square_low32 = y_q_word * y_q_word;

    trace->x_q_word = x_q_word;
    trace->y_q_word = y_q_word;
    trace->x_square_low32 = x_square_low32;
    trace->y_square_low32 = y_square_low32;
    trace->radius_sum_low32 = x_square_low32 + y_square_low32;
    return trace->radius_sum_low32;
}

int slippi_profile_ucf_dbooc_radius_exceeds_threshold(uint32_t radius_word)
{
    return radius_word > UCF_DBOOC_RADIUS_LIMIT &&
           radius_word < UCF_DBOOC_SIGNED_MAX;
}

uint32_t slippi_profile_ucf_dbooc_threshold_bits(
    uint32_t stick_x_bits, uint32_t stick_y_bits, uint8_t timer,
    uint32_t common_threshold_bits, SlippiUcfDboocTrace *trace)
{
    SlippiUcfDboocTrace unused_trace;
    uint32_t selected_threshold_bits = common_threshold_bits;

    if (trace == NULL) {
        trace = &unused_trace;
    }
    memset(trace, 0, sizeof(*trace));
    trace->timer = timer;
    trace->selected_threshold_bits = common_threshold_bits;

    /* lbz followed by cmpwi 1 admits only an unsigned timer byte equal to 0. */
    if (timer != 0) {
        return common_threshold_bits;
    }

    SlippiUcfQuantizedRadiusTrace radius_trace;
    const uint32_t radius_sum_low32 = slippi_profile_ucf_quantized_radius_word(
        stick_x_bits, stick_y_bits, &radius_trace);
    trace->x_fmsubs_bits = radius_trace.x_fmsubs_bits;
    trace->y_fmsubs_bits = radius_trace.y_fmsubs_bits;
    trace->x_conversion_word = radius_trace.x_conversion_word;
    trace->y_conversion_word = radius_trace.y_conversion_word;
    trace->x_q_word = radius_trace.x_q_word;
    trace->y_q_word = radius_trace.y_q_word;
    trace->x_square_low32 = radius_trace.x_square_low32;
    trace->y_square_low32 = radius_trace.y_square_low32;
    trace->radius_sum_low32 = radius_trace.radius_sum_low32;

    if (slippi_profile_ucf_dbooc_radius_exceeds_threshold(radius_sum_low32)) {
        selected_threshold_bits = UCF_DBOOC_OVERRIDE_THRESHOLD_BITS;
        trace->override_selected = 1;
    }
    trace->selected_threshold_bits = selected_threshold_bits;
    return selected_threshold_bits;
}

float slippi_profile_ucf_dbooc_squat_reversal_threshold(
    float stick_x, float stick_y, uint8_t timer, float common_threshold)
{
    uint32_t stick_x_bits;
    uint32_t stick_y_bits;
    uint32_t common_threshold_bits;
    uint32_t selected_threshold_bits;
    float selected_threshold;

    memcpy(&stick_x_bits, &stick_x, sizeof(stick_x_bits));
    memcpy(&stick_y_bits, &stick_y, sizeof(stick_y_bits));
    memcpy(&common_threshold_bits, &common_threshold,
           sizeof(common_threshold_bits));
    selected_threshold_bits = slippi_profile_ucf_dbooc_threshold_bits(
        stick_x_bits, stick_y_bits, timer, common_threshold_bits, NULL);
    memcpy(&selected_threshold, &selected_threshold_bits,
           sizeof(selected_threshold));
    return selected_threshold;
}
