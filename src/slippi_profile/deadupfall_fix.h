/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_DEADUPFALL_FIX_H
#define MELEE_WEB_SLIPPI_DEADUPFALL_FIX_H

union ftCommon_MotionVars;

#ifdef __cplusplus
extern "C" {
#endif

/* These helpers are only compiled into the opt-in v2 Slippi fixture. The
 * private velocity words use the pinned source union's otherwise-unused
 * DeadUpFall padding and phase-3 interpolation slot. */
int slippi_profile_deadupfall_initialize(
    union ftCommon_MotionVars *motion, float initial_y, float initial_z,
    float *self_vel_x, float *self_vel_y, float *self_vel_z);
int slippi_profile_deadupfall_advance(
    union ftCommon_MotionVars *motion, float gravity, float terminal_velocity,
    float *temp_y, float *temp_z);

#ifdef __cplusplus
}
#endif

#endif
