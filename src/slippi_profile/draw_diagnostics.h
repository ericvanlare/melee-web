/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_DRAW_DIAGNOSTICS_H
#define SLIPPI_PROFILE_DRAW_DIAGNOSTICS_H

#include <stdint.h>

#include <melee/gm/forward.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlippiProfileDrawDiagnosticStatus {
    SLIPPI_DRAW_DIAGNOSTIC_OK = 0,
    SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT = 1u << 0,
    SLIPPI_DRAW_DIAGNOSTIC_COUNTER_OVERFLOW = 1u << 1,
    SLIPPI_DRAW_DIAGNOSTIC_METADATA_CHANGED = 1u << 2,
} SlippiProfileDrawDiagnosticStatus;

typedef enum SlippiProfileMotionBoundary {
    SLIPPI_PROFILE_MOTION_INTERACTIVE = 0,
    SLIPPI_PROFILE_MOTION_ENTRY = 1,
    SLIPPI_PROFILE_MOTION_DEATH = 2,
    SLIPPI_PROFILE_MOTION_RESPAWN = 3,
    SLIPPI_PROFILE_MOTION_UNKNOWN = 4,
} SlippiProfileMotionBoundary;

/* Fixed read-only diagnostic ABI. These counters live outside the source
 * observation and are never reset by a frame callback. */
typedef struct SlippiProfileDrawDiagnostics {
    uint32_t slot_count;
    uint32_t status;
    uint32_t callback_entries[GM_MAX_PLAYERS];
    uint32_t model_transform_entries[GM_MAX_PLAYERS];
    uint32_t normal_model_dispatch_entries[GM_MAX_PLAYERS];
    int32_t processed_pad_slots[GM_MAX_PLAYERS];
    int32_t fighter_kinds[GM_MAX_PLAYERS];
    uint32_t metadata_seen_mask;
} SlippiProfileDrawDiagnostics;

void slippi_profile_draw_diagnostics_record_callback(int32_t player_id,
                                                      int32_t processed_pad_slot,
                                                      int32_t fighter_kind);
void slippi_profile_draw_diagnostics_record_model_transform(int32_t player_id,
                                                             int32_t processed_pad_slot,
                                                             int32_t fighter_kind);
void slippi_profile_draw_diagnostics_record_normal_model_dispatch(
    int32_t player_id, int32_t processed_pad_slot, int32_t fighter_kind);
const SlippiProfileDrawDiagnostics *slippi_profile_draw_diagnostics_snapshot(void);
SlippiProfileMotionBoundary slippi_profile_draw_motion_boundary(int32_t motion_id);
int slippi_profile_draw_is_mario_kind(int32_t fighter_kind);

#ifdef __cplusplus
}
#endif

#endif
