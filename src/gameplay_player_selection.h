#ifndef MELEE_WEB_GAMEPLAY_PLAYER_SELECTION_H
#define MELEE_WEB_GAMEPLAY_PLAYER_SELECTION_H

#include <melee/gm/types.h>

/* Ordinary VS CPU behavior is cpu_kind 4 (gm_InitPlayerData); the original
 * CSS difficulty slider selects 1..9. Event/training CPU modes are outside
 * this match boundary. Rumble is resolved later by original VS entry.
 * Keep menu, reference setup and original match initialization in sync. */
static inline int melee_web_player_selection_supported(const PlayerInitData* player)
{
    return player != NULL &&
           (player->slot_type == Gm_PKind_Human ||
            (player->slot_type == Gm_PKind_Cpu && player->cpu_kind == 4 &&
             player->cpu_level >= 1 && player->cpu_level <= 9));
}

/* The prepared match must preserve gm_LoadRumbleEnabled's CPU policy.
 * Do not apply this to an in-progress CSS payload before that routine runs. */
static inline int melee_web_match_player_supported(const PlayerInitData* player)
{
    return melee_web_player_selection_supported(player) &&
           (player->slot_type != Gm_PKind_Cpu || !player->rumble_enabled);
}

#endif
