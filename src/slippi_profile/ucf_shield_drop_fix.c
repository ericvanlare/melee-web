/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ucf_shield_drop_fix.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ucf_dbooc_fix.h"

_Static_assert(sizeof(float) == sizeof(uint32_t),
               "Shield Drop helper requires binary32 floats");

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

int slippi_profile_ucf_shield_drop_suppresses(
    float cstick_y, float common_cstick_threshold,
    uint8_t x_tilt_timer, int common_x_tilt_window,
    float lstick_x, float lstick_y, int floor_index, uint32_t floor_flags)
{
    if (!(cstick_y > common_cstick_threshold)) {
        return 0;
    }
    if ((int) x_tilt_timer < common_x_tilt_window) {
        return 0;
    }
    if (!(lstick_y > -0.8f)) {
        return 0;
    }
    if (floor_index == -1) {
        return 0;
    }
    if ((floor_flags & UINT32_C(0x100)) == 0) {
        return 0;
    }

    const uint32_t radius = slippi_profile_ucf_quantized_radius_word(
        float_bits(lstick_x), float_bits(lstick_y), NULL);
    return slippi_profile_ucf_dbooc_radius_exceeds_threshold(radius);
}
