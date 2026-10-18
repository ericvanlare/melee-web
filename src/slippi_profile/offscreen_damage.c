/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "offscreen_damage.h"

#define ACTION_STATE_STAR_KO 4
#define ACTION_STATE_SCREEN_KO 6

int slippi_profile_offscreen_damage_zone(
    const SlippiOffscreenDamageInput *input)
{
    if (input == 0) {
        return -1;
    }

    if (input->is_home_run_in_game ||
        input->is_dead ||
        input->action_state == ACTION_STATE_STAR_KO ||
        input->action_state == ACTION_STATE_SCREEN_KO)
    {
        return 0;
    }

    return input->x < input->left || input->x > input->right ||
           input->y > input->top || input->y < input->bottom;
}
