/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "whispy_direction_fix.h"

#include <melee/ft/kinds/ftCommon/forward.h>

int slippi_profile_whispy_zero_direction_contribution(int32_t motion_id)
{
    return motion_id <= ftCo_MS_Sleep;
}
