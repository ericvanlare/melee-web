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

/* Original VS Team Battle (mncharsel.c). cycleTeam advances a door through
 * (team + 1) % 3: the three authored team colours whose CSS colour table
 * (mnCharSel_804D50E0) and costume selectors (gm_80169264 red,
 * gm_801692BC blue, gm_80169290 green) the source uses. fn_80262F44 hides
 * CSS Start until at least two active doors belong to different teams; it
 * places no other bound on the roster, so 1v1, 2v1, 1v1v1, 2v2, 3v1 and
 * 2v1v1 all start. The VS CSS owns four doors (mnCharSel_804D6CF5), and the
 * port admits two through four contiguous players. In-progress CSS may show
 * every active door on one team while the cursor configures colours; only
 * CSS exit and match admission require opposing teams. */
#define MELEE_WEB_VS_TEAM_COLORS 3
#define MELEE_WEB_VS_TEAM_MAX_PLAYERS 4
static inline int melee_web_team_setup_supported(const StartMeleeData* start,
                                                 int count,
                                                 int require_opposing_teams)
{
    int i;
    int opposing = 0;

    if (start == NULL || start->rules.is_teams > 1) return 0;
    if (!start->rules.is_teams) return 1;
    if (count < 2 || count > MELEE_WEB_VS_TEAM_MAX_PLAYERS) return 0;
    for (i = 0; i < count; ++i) {
        if (start->players[i].team >= MELEE_WEB_VS_TEAM_COLORS) return 0;
        if (start->players[i].team != start->players[0].team) opposing = 1;
    }
    return opposing || !require_opposing_teams;
}

/* The prepared match must preserve gm_LoadRumbleEnabled's CPU policy.
 * Do not apply this to an in-progress CSS payload before that routine runs. */
static inline int melee_web_match_player_supported(const PlayerInitData* player)
{
    return melee_web_player_selection_supported(player) &&
           (player->slot_type != Gm_PKind_Cpu || !player->rumble_enabled);
}

#endif
