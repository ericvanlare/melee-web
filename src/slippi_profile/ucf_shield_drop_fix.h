/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_UCF_SHIELD_DROP_FIX_H
#define MELEE_WEB_SLIPPI_UCF_SHIELD_DROP_FIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns nonzero exactly when the injected Shield Drop guards suppress the
 * caller's neutral-escape transition. Outer caller eligibility remains in the
 * original source functions. */
int slippi_profile_ucf_shield_drop_suppresses(
    float cstick_y, float common_cstick_threshold,
    uint8_t x_tilt_timer, int common_x_tilt_window,
    float lstick_x, float lstick_y, int floor_index, uint32_t floor_flags);

#ifdef __cplusplus
}
#endif

#endif
