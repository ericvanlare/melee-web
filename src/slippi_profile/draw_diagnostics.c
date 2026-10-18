/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "draw_diagnostics.h"

#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftMario/forward.h>

#include <limits.h>

_Static_assert(GM_MAX_PLAYERS == Gm_Player_NumMax,
               "draw diagnostics must follow the authored player bound");
_Static_assert(sizeof(SlippiProfileDrawDiagnostics) == 132,
               "draw diagnostics ABI changed");

static SlippiProfileDrawDiagnostics diagnostics = {
    GM_MAX_PLAYERS, 0, {0}, {0}, {0}, {0}, {0}, 0,
};

static void record(int32_t player_id, int32_t processed_pad_slot,
                   int32_t fighter_kind, uint32_t *counter)
{
    uint32_t slot;
    uint32_t bit;

    if (player_id < 0 || player_id >= (int32_t) GM_MAX_PLAYERS) {
        diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT;
        return;
    }
    slot = (uint32_t) player_id;
    bit = 1u << slot;
    if (diagnostics.metadata_seen_mask & bit) {
        if (diagnostics.processed_pad_slots[slot] != processed_pad_slot ||
            diagnostics.fighter_kinds[slot] != fighter_kind) {
            diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_METADATA_CHANGED;
        }
    } else {
        diagnostics.processed_pad_slots[slot] = processed_pad_slot;
        diagnostics.fighter_kinds[slot] = fighter_kind;
        diagnostics.metadata_seen_mask |= bit;
    }
    if (*counter == UINT32_MAX) {
        diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_COUNTER_OVERFLOW;
        return;
    }
    ++*counter;
}

void slippi_profile_draw_diagnostics_record_callback(int32_t player_id,
                                                      int32_t processed_pad_slot,
                                                      int32_t fighter_kind)
{
    if (player_id < 0 || player_id >= (int32_t) GM_MAX_PLAYERS) {
        diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT;
        return;
    }
    record(player_id, processed_pad_slot, fighter_kind,
           &diagnostics.callback_entries[(uint32_t) player_id]);
}

void slippi_profile_draw_diagnostics_record_model_transform(int32_t player_id,
                                                             int32_t processed_pad_slot,
                                                             int32_t fighter_kind)
{
    if (player_id < 0 || player_id >= (int32_t) GM_MAX_PLAYERS) {
        diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT;
        return;
    }
    record(player_id, processed_pad_slot, fighter_kind,
           &diagnostics.model_transform_entries[(uint32_t) player_id]);
}

const SlippiProfileDrawDiagnostics *slippi_profile_draw_diagnostics_snapshot(void)
{
    return &diagnostics;
}

void slippi_profile_draw_diagnostics_record_normal_model_dispatch(
    int32_t player_id, int32_t processed_pad_slot, int32_t fighter_kind)
{
    if (player_id < 0 || player_id >= (int32_t) GM_MAX_PLAYERS) {
        diagnostics.status |= SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT;
        return;
    }
    record(player_id, processed_pad_slot, fighter_kind,
           &diagnostics.normal_model_dispatch_entries[(uint32_t) player_id]);
}

SlippiProfileMotionBoundary slippi_profile_draw_motion_boundary(int32_t motion_id)
{
    if (motion_id < 0 || motion_id >= ftMr_MS_Count) {
        return SLIPPI_PROFILE_MOTION_UNKNOWN;
    }
    if (motion_id >= ftCo_MS_Entry && motion_id <= ftCo_MS_EntryEnd) {
        return SLIPPI_PROFILE_MOTION_ENTRY;
    }
    if (motion_id >= ftCo_MS_DeadDown &&
        motion_id <= ftCo_MS_DeadUpFallHitCameraIce) {
        return SLIPPI_PROFILE_MOTION_DEATH;
    }
    if (motion_id >= ftCo_MS_Rebirth && motion_id <= ftCo_MS_RebirthWait) {
        return SLIPPI_PROFILE_MOTION_RESPAWN;
    }
    return SLIPPI_PROFILE_MOTION_INTERACTIVE;
}

int slippi_profile_draw_is_mario_kind(int32_t fighter_kind)
{
    return fighter_kind == FTKIND_MARIO;
}
