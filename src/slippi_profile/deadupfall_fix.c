/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "deadupfall_fix.h"

#include <melee/ft/kinds/ftCommon/types.h>

#include <float.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "DeadUpFall private words require 8-bit bytes");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "DeadUpFall velocity storage requires IEEE binary32");
_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53,
               "DeadUpFall velocity update requires IEEE binary64");
_Static_assert(offsetof(union ftCommon_MotionVars, unk_deadup.pad_x48) == 8,
               "pinned DeadUpFall private Y slot moved");
_Static_assert(sizeof(((union ftCommon_MotionVars *) 0)
                          ->unk_deadup.pad_x48) == sizeof(float),
               "pinned DeadUpFall private Y slot is not one word");
_Static_assert(offsetof(union ftCommon_MotionVars, unk_deadup.x4C) == 12,
               "pinned DeadUpFall private Z slot moved");
_Static_assert(offsetof(union ftCommon_MotionVars, walk.slow_anim_frame) == 8,
               "pinned motion union alias changed at private Y");
_Static_assert(offsetof(union ftCommon_MotionVars, walk.middle_anim_frame) == 12,
               "pinned motion union alias changed at private Z");
_Static_assert(offsetof(union ftCommon_MotionVars, unk_deadup.x50) == 16,
               "pinned DeadUpFall position offset changed");
_Static_assert(offsetof(union ftCommon_MotionVars, walk.fast_anim_frame) == 16,
               "pinned walk alias changed at DeadUpFall position");
_Static_assert(offsetof(union ftCommon_MotionVars, unk_deadup.x5C) == 28,
               "pinned DeadUpFall accumulation offset changed");
_Static_assert(sizeof(union ftCommon_MotionVars) >= 40,
               "pinned common motion union is shorter than DeadUpFall state");

int slippi_profile_deadupfall_initialize(
    union ftCommon_MotionVars *motion, float initial_y, float initial_z,
    float *self_vel_x, float *self_vel_y, float *self_vel_z)
{
    if (motion == NULL || self_vel_x == NULL || self_vel_y == NULL ||
        self_vel_z == NULL) {
        return 0;
    }

    union ftCommon_MotionVars updated;
    memcpy(&updated, motion, sizeof(updated));
    unsigned char *bytes = (unsigned char *) &updated;
    memcpy(bytes + offsetof(union ftCommon_MotionVars, unk_deadup.pad_x48),
           &initial_y, sizeof(initial_y));
    memcpy(bytes + offsetof(union ftCommon_MotionVars, unk_deadup.x4C),
           &initial_z, sizeof(initial_z));
    memcpy(motion, &updated, sizeof(updated));

    *self_vel_x = 0.0f;
    *self_vel_y = 0.0f;
    *self_vel_z = 0.0f;
    return 1;
}

int slippi_profile_deadupfall_advance(
    union ftCommon_MotionVars *motion, float gravity, float terminal_velocity,
    float *temp_y, float *temp_z)
{
    if (motion == NULL || temp_y == NULL || temp_z == NULL) {
        return 0;
    }

    union ftCommon_MotionVars updated;
    memcpy(&updated, motion, sizeof(updated));
    const unsigned char *source_bytes = (const unsigned char *) &updated;
    float private_y;
    float private_z;
    memcpy(&private_y,
           source_bytes + offsetof(union ftCommon_MotionVars,
                                   unk_deadup.pad_x48),
           sizeof(private_y));
    memcpy(&private_z,
           source_bytes + offsetof(union ftCommon_MotionVars,
                                   unk_deadup.x4C),
           sizeof(private_z));

    const double candidate = (double) private_y - (double) gravity;
    const double terminal_limit = -(double) terminal_velocity;
    const double selected = candidate > terminal_limit ? candidate
                                                        : terminal_limit;
    const float stored_y = (float) selected;
    unsigned char *updated_bytes = (unsigned char *) &updated;
    memcpy(updated_bytes + offsetof(union ftCommon_MotionVars,
                                    unk_deadup.pad_x48),
           &stored_y, sizeof(stored_y));
    memcpy(motion, &updated, sizeof(updated));

    /* Reload through the complete source union object after its store; keep
     * this binary32 boundary before the original ordered vector additions. */
    float reloaded_y;
    memcpy(&reloaded_y,
           (const unsigned char *) motion +
               offsetof(union ftCommon_MotionVars, unk_deadup.pad_x48),
           sizeof(reloaded_y));
    *temp_y = *temp_y + reloaded_y;
    *temp_z = private_z + *temp_z;
    return 1;
}
