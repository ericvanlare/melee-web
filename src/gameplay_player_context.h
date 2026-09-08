#ifndef MELEE_WEB_GAMEPLAY_PLAYER_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_PLAYER_CONTEXT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebPlayerContext MeleeWebPlayerContext;
typedef struct MeleeWebPlayerSettings {
    uint32_t slot, controller, stocks;
    float position[3], facing;
} MeleeWebPlayerSettings;
typedef struct MeleeWebPlayerStats {
    int32_t slot_type, character, controller, player_id, costume, stocks;
    int32_t cpu_type, cpu_level, state, damage;
    float position[3], facing, model_scale, attack_ratio, defense_ratio;
    uint32_t stale_index, live_entities;
} MeleeWebPlayerStats;
/* Scoped source StaticPlayer initialization for human neutral-costume Mario.
 * Uses original reset/setters/getters and stale-table reset. Owns no Fighter;
 * original Player_80031AD0(slot) can subsequently create and publish one.
 * Existing entity/state or overlapping context ownership rejects. */
MeleeWebPlayerContext* melee_web_player_context_begin(const MeleeWebPlayerSettings*, char*, size_t);
int melee_web_player_context_stats(const MeleeWebPlayerContext*, MeleeWebPlayerStats*, char*, size_t);
/* Requires original Fighter unload to clear both player entities first; restores
 * the complete prior StaticPlayer record, including statistics and flags. */
int melee_web_player_context_end(MeleeWebPlayerContext*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
