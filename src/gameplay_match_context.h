#ifndef MELEE_WEB_GAMEPLAY_MATCH_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_MATCH_CONTEXT_H
#include "gameplay_player_context.h"
#include "gameplay_collision.h"
#include "gameplay_pad_state.h"
#include <dolphin/pad.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMatchContext MeleeWebMatchContext;
int melee_web_match_restore_input(MeleeWebMatchContext*,const MeleeWebPadState*,char*,size_t);
/* Source scene phases run after PAD renewal and around the original scheduler. */
typedef int (*MeleeWebMatchTickPhase)(void*, char*, size_t);
int melee_web_match_step_raw_phased(MeleeWebMatchContext*, const PADStatus[4],
    MeleeWebMatchTickPhase renew, MeleeWebMatchTickPhase before,
    MeleeWebMatchTickPhase after, void*, char*, size_t);

typedef struct MeleeWebMatchSettings {
    MeleeWebPlayerSettings player; /* slot 0..3: source input indexes four PAD records. */
    uint32_t camera_subjects; /* Main source match uses 70. Explicit, nonzero. */
    uint32_t random_seed;
} MeleeWebMatchSettings;
#define MELEE_WEB_MATCH_MAX_PLAYERS 4u
typedef struct MeleeWebControllerSample {
    uint32_t buttons;
    float stick_x, stick_y, cstick_x, cstick_y, trigger_l, trigger_r;
} MeleeWebControllerSample;
typedef struct MeleeWebMatchEyeStats {
    uint32_t image_count, palette_count, image_index, palette_index;
    float animation_frame, animation_rate;
    /* HSD_TObjAddAnim starts from the retained costume descriptor image and
     * palette. Keep that state separate from a command-selected table entry;
     * UINT32_MAX is reserved for the base state. */
    uint8_t image_is_base, palette_is_base;
    uint16_t reserved;
} MeleeWebMatchEyeStats;
typedef struct MeleeWebMatchStats {
    uint64_t ticks;
    uint32_t random_seed, live_fighters, camera_subjects;
    int32_t motion_id, ground_or_air;
    uint32_t extra_model_objects, eye_count;
    MeleeWebMatchEyeStats eyes[2];
    float position[3], facing_direction, animation_frame;
    float source_stick[2], source_triggers;
    uint32_t held_buttons, pressed_buttons, released_buttons;
    uint32_t player_slot;
    float damage_percent, shield_health;
    int32_t stocks;
} MeleeWebMatchStats;
/* Requires the live bootstrap, native object destructors, initialized common,
 * published fighter/effect/stage assets and collision to remain owned through
 * end. Initializes actual camera subjects and shadow allocation, and a scoped
 * human player. Does not create a render camera or claim full stage readiness. */
MeleeWebMatchContext* melee_web_match_begin(const MeleeWebMatchSettings*,
    MeleeWebCollision*, char*, size_t);
/* Bounded ordinary-VS boundary for two through four active players. Distinct
 * slots and controller ports are required. samples[controller] is routed to
 * the configured player's original PlayerId PAD record; source Fighter
 * processing computes input transitions. */
MeleeWebMatchContext* melee_web_match_begin_players(const MeleeWebPlayerSettings*,
    uint32_t count,uint32_t camera_subjects,uint32_t seed,MeleeWebCollision*,char*,size_t);
int melee_web_match_create_fighters(MeleeWebMatchContext*,char*,size_t);
/* Preserve Fighter_Create's disabled input until original Ready completes. */
int melee_web_match_create_fighters_intro(MeleeWebMatchContext*,char*,size_t);
/* Browser entry: consume Snapshot.raw, never Snapshot.clamped. Uses original
 * HSD queue consumption, radial clamp, AD conversion, scale, and edge/repeat
 * history with source Melee calibration; exactly one original scheduler tick. */
int melee_web_match_step_raw(MeleeWebMatchContext*,const PADStatus raw[4],char*,size_t);
/* Explicit normalized fixture entry, useful for narrow deterministic tests. */
int melee_web_match_step_inputs(MeleeWebMatchContext*,const MeleeWebControllerSample samples[4],char*,size_t);
int melee_web_match_player_stats(MeleeWebMatchContext*,uint32_t player_index,MeleeWebMatchStats*,char*,size_t);
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
