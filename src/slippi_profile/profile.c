/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "profile.h"

const char *slippi_profile_identity_name(SlippiProfileIdentity identity)
{
    if (identity != SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_V4) {
        return 0;
    }
    return SLIPPI_PROFILE_RENDER_DEPENDENCIES_DBOOC_V4_ID;
}

SlippiSourceDrawDecision slippi_profile_source_draw_decision(
    SlippiProfileIdentity identity, SlippiTickKind tick_kind)
{
    if (identity != SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_V4) {
        return SLIPPI_SOURCE_DRAW_ERROR;
    }
    switch (tick_kind) {
    case SLIPPI_TICK_PRESENT:
        return SLIPPI_SOURCE_DRAW_ALLOW;
    case SLIPPI_TICK_CORRECTION:
        return SLIPPI_SOURCE_DRAW_SKIP;
    default:
        return SLIPPI_SOURCE_DRAW_ERROR;
    }
}
