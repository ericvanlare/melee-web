#ifndef MELEE_WEB_GAMEPLAY_ACTION_TRACE_H
#define MELEE_WEB_GAMEPLAY_ACTION_TRACE_H
#include "gameplay_match_context.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum MeleeWebActionTraceKind {
    MeleeWebActionTrace_Jab,
    MeleeWebActionTrace_JumpLanding,
    MeleeWebActionTrace_Shield,
    MeleeWebActionTrace_ContactDamage
} MeleeWebActionTraceKind;
typedef struct MeleeWebActionTraceReport {
    uint32_t frames, settle_frames, peak_hitboxes, air_frames, hitlag_frames;
    uint32_t saw_jab, saw_jump, saw_landing, saw_shield, saw_damage;
    int32_t final_motion[2];
    float initial_percent[2], peak_percent[2], initial_shield, minimum_shield;
} MeleeWebActionTraceReport;
/* Actual source-input integration fixture. Caller owns the live world/assets
 * and supplies stage-derived spawns. ContactDamage requires close, inward-facing
 * fighters; this helper never moves their positions or assigns actions. */
int melee_web_match_action_trace(MeleeWebCollision*,const MeleeWebPlayerSettings[2],
    MeleeWebActionTraceKind,MeleeWebActionTraceReport*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
