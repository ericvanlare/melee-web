/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_PS_CAMERA_INDEPENDENT_MONITOR_FIX_H
#define MELEE_WEB_SLIPPI_PS_CAMERA_INDEPENDENT_MONITOR_FIX_H

#ifdef __cplusplus
extern "C" {
#endif

/* Returns one inside the authored inclusive position rectangle, zero outside.
 * Sequential ordered comparisons preserve unordered NaN fallthrough. */
int slippi_profile_ps_camera_independent_monitor_inside(float x, float y);

#ifdef __cplusplus
}
#endif

#endif
