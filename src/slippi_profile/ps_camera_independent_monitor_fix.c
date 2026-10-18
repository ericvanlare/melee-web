/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ps_camera_independent_monitor_fix.h"

int slippi_profile_ps_camera_independent_monitor_inside(float x, float y)
{
    if (x < -120.0f) {
        return 0;
    }
    if (x > 120.0f) {
        return 0;
    }
    if (y > 80.0f) {
        return 0;
    }
    if (y < -20.0f) {
        return 0;
    }
    return 1;
}
