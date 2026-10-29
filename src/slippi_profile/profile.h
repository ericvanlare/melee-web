/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_PROFILE_H
#define MELEE_WEB_SLIPPI_PROFILE_H

/* This is a deliberately incomplete diagnostic profile identity. */
#define SLIPPI_PROFILE_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5_ID \
    "slippi-render-dependencies-dbooc-shield-drop-v5"

#define SLIPPI_PROFILE_LOGICAL_FRAME_V6_ID "slippi-render-dependencies-logical-frame-v6"

#define SLIPPI_PROFILE_LOBJ_PRESENTATION_V7_ID "slippi-render-dependencies-logical-frame-lobj-v7"

/* V8 named the retired persistent TObj presentation owner. Keep its identity
 * value only so callers can explicitly refuse old manifests and snapshots. */
#define SLIPPI_PROFILE_TOBJ_PRESENTATION_V8_ID "slippi-render-dependencies-logical-frame-lobj-tobj-v8"
#define SLIPPI_PROFILE_TOBJ_PRESENTATION_V9_ID \
    "slippi-render-dependencies-logical-frame-lobj-tobj-stateless-v9"
#define SLIPPI_PROFILE_TOBJ_GROUND_MATRIX_V10_ID \
    "slippi-render-dependencies-logical-frame-lobj-tobj-ground-matrix-v10"

#define SLIPPI_PROFILE_FIGHTER_PROJECTION_V11_ID \
    "slippi-render-dependencies-logical-frame-lobj-tobj-fighter-projection-v11"

typedef enum SlippiProfileIdentity {
    SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5 = 5,
    SLIPPI_PROFILE_IDENTITY_LOGICAL_FRAME_V6 = 6,
    SLIPPI_PROFILE_IDENTITY_LOBJ_PRESENTATION_V7 = 7,
    SLIPPI_PROFILE_IDENTITY_TOBJ_PRESENTATION_V8 = 8,
    SLIPPI_PROFILE_IDENTITY_TOBJ_PRESENTATION_STATELESS_V9 = 9,
    SLIPPI_PROFILE_IDENTITY_TOBJ_GROUND_MATRIX_V10 = 10,
    SLIPPI_PROFILE_IDENTITY_FIGHTER_PROJECTION_V11 = 11,
} SlippiProfileIdentity;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#define SLIPPI_PROFILE_ACTIVE_IDENTITY SLIPPI_PROFILE_IDENTITY_FIGHTER_PROJECTION_V11
#elif defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
#define SLIPPI_PROFILE_ACTIVE_IDENTITY SLIPPI_PROFILE_IDENTITY_LOBJ_PRESENTATION_V7
#elif defined(MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK) && MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK
#define SLIPPI_PROFILE_ACTIVE_IDENTITY SLIPPI_PROFILE_IDENTITY_LOGICAL_FRAME_V6
#else
#define SLIPPI_PROFILE_ACTIVE_IDENTITY SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5
#endif

typedef enum SlippiTickKind {
    SLIPPI_TICK_PRESENT = 0,
    SLIPPI_TICK_CORRECTION = 1,
} SlippiTickKind;

typedef enum SlippiSourceDrawDecision {
    SLIPPI_SOURCE_DRAW_ERROR = -1,
    SLIPPI_SOURCE_DRAW_SKIP = 0,
    SLIPPI_SOURCE_DRAW_ALLOW = 1,
} SlippiSourceDrawDecision;

#ifdef __cplusplus
extern "C" {
#endif

const char *slippi_profile_identity_name(SlippiProfileIdentity identity);
SlippiSourceDrawDecision slippi_profile_source_draw_decision(
    SlippiProfileIdentity identity, SlippiTickKind tick_kind);

#ifdef __cplusplus
}
#endif

#endif
