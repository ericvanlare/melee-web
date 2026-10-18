/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keep the source-only fixture byte-identical: this wrapper adds only the
 * profile-policy exports needed by the bounded synctest runner. */
#include "../../../tests/gameplay_snapshot_probe.cpp"

#include "slippi_profile/profile.h"

extern "C" {
EMSCRIPTEN_KEEPALIVE const char *melee_web_synctest_profile_identity(void)
{
    return slippi_profile_identity_name(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V1);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_decision(int tick_kind)
{
    return slippi_profile_source_draw_decision(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V1,
        static_cast<SlippiTickKind>(tick_kind));
}
}
