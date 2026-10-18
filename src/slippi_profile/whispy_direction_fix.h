/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_WHISPY_DIRECTION_FIX_H
#define MELEE_WEB_SLIPPI_WHISPY_DIRECTION_FIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The source query remains unconditional; this identifies a zero contribution
 * for the authored signed motion-state range through ftCo_MS_Sleep. */
int slippi_profile_whispy_zero_direction_contribution(int32_t motion_id);

#ifdef __cplusplus
}
#endif

#endif
