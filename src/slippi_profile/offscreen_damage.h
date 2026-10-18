/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_OFFSCREEN_DAMAGE_H
#define MELEE_WEB_SLIPPI_OFFSCREEN_DAMAGE_H

typedef struct SlippiOffscreenDamageInput {
    int is_home_run_in_game;
    int is_dead;
    int action_state;
    float x;
    float y;
    float left;
    float right;
    float top;
    float bottom;
} SlippiOffscreenDamageInput;

#ifdef __cplusplus
extern "C" {
#endif

/* Returns -1 for a null input, otherwise the original 0/1 zone predicate. */
int slippi_profile_offscreen_damage_zone(
    const SlippiOffscreenDamageInput *input);

#ifdef __cplusplus
}
#endif

#endif
