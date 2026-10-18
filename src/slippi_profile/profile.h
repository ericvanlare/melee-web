/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_PROFILE_H
#define MELEE_WEB_SLIPPI_PROFILE_H

/* This is a deliberately incomplete diagnostic profile identity. */
#define SLIPPI_PROFILE_RENDER_DEPENDENCIES_V3_ID \
    "slippi-render-dependencies-v3"

typedef enum SlippiProfileIdentity {
    SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V3 = 3,
} SlippiProfileIdentity;

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
