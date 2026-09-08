#ifndef MELEE_WEB_GAMEPLAY_MATCH_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_MATCH_CONTEXT_H
#include "gameplay_player_context.h"
#include "gameplay_collision.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchContext MeleeWebMatchContext;
typedef struct MeleeWebMatchSettings {
    MeleeWebPlayerSettings player; /* slot 0..3: source input indexes four PAD records. */
    uint32_t camera_subjects; /* Main source match uses 70. Explicit, nonzero. */
    uint32_t random_seed;
} MeleeWebMatchSettings;
typedef struct MeleeWebMatchEyeStats {
    uint32_t image_count, palette_count, image_index, palette_index;
    float animation_frame, animation_rate;
} MeleeWebMatchEyeStats;
typedef struct MeleeWebMatchStats {
    uint64_t ticks;
    uint32_t random_seed, live_fighters, camera_subjects;
    int32_t motion_id, ground_or_air;
    uint32_t extra_model_objects, eye_count;
    MeleeWebMatchEyeStats eyes[2];
    float position[3], animation_frame;
} MeleeWebMatchStats;
/* Requires the live bootstrap, native object destructors, initialized common,
 * published fighter/effect/stage assets and collision to remain owned through
 * end. Initializes actual camera subjects and shadow allocation, and a scoped
 * human player. Does not create a render camera or claim full stage readiness. */
MeleeWebMatchContext* melee_web_match_begin(const MeleeWebMatchSettings*,
    MeleeWebCollision*, char*, size_t);
/* Calls original Player_80031AD0 -> Fighter_Create. Ground/air and motion are
 * determined by original collision placement, not overwritten by the host. */
int melee_web_match_create_fighter(MeleeWebMatchContext*, char*, size_t);
/* Explicit neutral HSD_PadGameStatus fixture consumed by original Fighter input
 * processing. Runs original scheduler, including all registered fighter procs. */
int melee_web_match_step(MeleeWebMatchContext*, uint32_t ticks, char*, size_t);
int melee_web_match_stats(MeleeWebMatchContext*, MeleeWebMatchStats*, char*, size_t);
/* Original GObj disposal invokes registered Fighter_Unload, which clears the
 * player entity through Player_80031FB0, before source globals restore. */
int melee_web_match_end(MeleeWebMatchContext*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
