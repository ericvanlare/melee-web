/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keep the source-only fixture byte-identical: this wrapper adds only the
 * profile-policy exports needed by the bounded synctest runner. */
#include "../../../tests/gameplay_snapshot_probe.cpp"

#include "slippi_profile/profile.h"
#include "slippi_profile/draw_diagnostics.h"

static_assert(offsetof(MeleeWebMatchStats, player_slot) == 136,
              "v2 observation player-slot decoder offset changed");
static_assert(offsetof(MeleeWebMatchStats, stocks) == 148,
              "v2 observation stock decoder offset changed");
static_assert(offsetof(MeleeWebMatchStats, fighter_kind) == 152,
              "v2 observation fighter-kind decoder offset changed");

extern "C" {
EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_draw_diagnostics(void)
{
    return slippi_profile_draw_diagnostics_snapshot();
}

EMSCRIPTEN_KEEPALIVE unsigned melee_web_synctest_draw_diagnostics_size(void)
{
    return sizeof(SlippiProfileDrawDiagnostics);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_motion_boundary(int motion_id)
{
    return slippi_profile_draw_motion_boundary(motion_id);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_is_mario_kind(int fighter_kind)
{
    return slippi_profile_draw_is_mario_kind(fighter_kind);
}

EMSCRIPTEN_KEEPALIVE const char *melee_web_synctest_profile_identity(void)
{
    return slippi_profile_identity_name(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_decision(int tick_kind)
{
    return slippi_profile_source_draw_decision(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2,
        static_cast<SlippiTickKind>(tick_kind));
}
}
