/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "profile.h"

const char *slippi_profile_identity_name(SlippiProfileIdentity identity)
{
    if (identity !=
        SLIPPI_PROFILE_ACTIVE_IDENTITY) {
        return 0;
    }
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    return SLIPPI_PROFILE_TOBJ_PRESENTATION_V9_ID;
#elif defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
    return SLIPPI_PROFILE_LOBJ_PRESENTATION_V7_ID;
#elif defined(MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK) && MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK
    return SLIPPI_PROFILE_LOGICAL_FRAME_V6_ID;
#else
    return SLIPPI_PROFILE_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5_ID;
#endif
}

SlippiSourceDrawDecision slippi_profile_source_draw_decision(
    SlippiProfileIdentity identity, SlippiTickKind tick_kind)
{
    if (identity !=
        SLIPPI_PROFILE_ACTIVE_IDENTITY) {
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
