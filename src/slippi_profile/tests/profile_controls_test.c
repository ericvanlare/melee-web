/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>

#include "slippi_profile/dynamics_fix.h"
#include "slippi_profile/deadupfall_fix.h"
#include "slippi_profile/draw_diagnostics.h"
#include "slippi_profile/offscreen_damage.h"
#include "slippi_profile/ps_camera_independent_monitor_fix.h"
#include "slippi_profile/whispy_direction_fix.h"
#include "slippi_profile/profile.h"

#include <melee/ft/kinds/ftCommon/types.h>
#include <melee/ft/kinds/ftMario/forward.h>
#include <melee/ft/types.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

_Static_assert(JOBJ_MTX_DIRTY == 0x40u, "pinned JObj dirty flag changed");
_Static_assert(JOBJ_USER_DEF_MTX == 0x800000u,
               "pinned JObj user-defined matrix flag changed");
_Static_assert(offsetof(union ftCommon_MotionVars,
                        unk_deadup.pad_x48) == 8,
               "pinned DeadUpFall private Y offset changed");
_Static_assert(offsetof(union ftCommon_MotionVars,
                        unk_deadup.x4C) == 12,
               "pinned DeadUpFall private Z offset changed");
_Static_assert(offsetof(union ftCommon_MotionVars,
                        unk_deadup.x50) == 16,
               "pinned DeadUpFall position offset changed");
_Static_assert(offsetof(union ftCommon_MotionVars,
                        unk_deadup.x5C) == 28,
               "pinned DeadUpFall temporary offset changed");
_Static_assert(GM_MAX_PLAYERS == Gm_Player_NumMax,
               "draw diagnostic slots follow the authored player bound");
_Static_assert(sizeof(SlippiProfileDrawDiagnostics) == 132,
               "draw diagnostic ABI width changed");
static HSD_JObj *matrix_calls[8];
static size_t matrix_call_count;

void HSD_JObjSetupMatrixSub(HSD_JObj *jobj)
{
    assert(matrix_call_count < sizeof(matrix_calls) / sizeof(matrix_calls[0]));
    matrix_calls[matrix_call_count++] = jobj;
}

static void test_draw_policy(void)
{
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    const SlippiProfileIdentity profile =
        SLIPPI_PROFILE_IDENTITY_CAMERA_HISTORY_V20;
    assert(strcmp(slippi_profile_identity_name(profile),
                  SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID) == 0);
    assert(slippi_profile_identity_name(SLIPPI_PROFILE_IDENTITY_TEV_STAGE_SCRATCH_V19) == NULL);
    assert(slippi_profile_source_draw_decision(SLIPPI_PROFILE_IDENTITY_TEV_STAGE_SCRATCH_V19,
               SLIPPI_TICK_CORRECTION) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_STABLE_ACTION_ASSETS_V18) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_STABLE_ACTION_ASSETS_V18,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_PARTICLE_MEASUREMENT_V17) == NULL);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_PARTICLE_PRESENTATION_V16) == NULL);
#elif defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    const SlippiProfileIdentity profile =
        SLIPPI_PROFILE_IDENTITY_LOBJ_CURRENT_SET_V15;
    assert(strcmp(slippi_profile_identity_name(profile),
                  SLIPPI_PROFILE_LOBJ_CURRENT_SET_V15_ID) == 0);
    assert(slippi_profile_identity_name(SLIPPI_PROFILE_IDENTITY_EFFECT_MATRIX_V14) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_EFFECT_MATRIX_V14,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(SLIPPI_PROFILE_IDENTITY_LOCAL_TLUT_V13) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_LOCAL_TLUT_V13,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_SHADOW_CAMERA_V12) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_SHADOW_CAMERA_V12,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_FIGHTER_PROJECTION_V11) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_FIGHTER_PROJECTION_V11,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name(
               SLIPPI_PROFILE_IDENTITY_TOBJ_PRESENTATION_STATELESS_V9) == NULL);
    assert(slippi_profile_source_draw_decision(
               SLIPPI_PROFILE_IDENTITY_TOBJ_PRESENTATION_STATELESS_V9,
               SLIPPI_TICK_PRESENT) == SLIPPI_SOURCE_DRAW_ERROR);
#else
    const SlippiProfileIdentity profile =
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5;
    assert(strcmp(slippi_profile_identity_name(profile),
                  "slippi-render-dependencies-dbooc-shield-drop-v5") == 0);
#endif
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ALLOW);
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_CORRECTION) ==
           SLIPPI_SOURCE_DRAW_SKIP);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)99,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)99) == NULL);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)1) == NULL);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)2) == NULL);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)3) == NULL);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)4) == NULL);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)1,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)2,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)3,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)4,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_source_draw_decision(profile, (SlippiTickKind)99) ==
           SLIPPI_SOURCE_DRAW_ERROR);
}


static void test_whispy_motion_predicate(void)
{
    assert(slippi_profile_whispy_zero_direction_contribution(-1));
    assert(slippi_profile_whispy_zero_direction_contribution(0));
    assert(slippi_profile_whispy_zero_direction_contribution(11));
    assert(!slippi_profile_whispy_zero_direction_contribution(12));
}

static void test_stadium_position_predicate(void)
{
    assert(slippi_profile_ps_camera_independent_monitor_inside(0.0f, 0.0f));
    assert(slippi_profile_ps_camera_independent_monitor_inside(-120.0f, -20.0f));
    assert(slippi_profile_ps_camera_independent_monitor_inside(120.0f, 80.0f));
    assert(slippi_profile_ps_camera_independent_monitor_inside(-119.999f, 79.999f));
    assert(!slippi_profile_ps_camera_independent_monitor_inside(-120.001f, 0.0f));
    assert(!slippi_profile_ps_camera_independent_monitor_inside(120.001f, 0.0f));
    assert(!slippi_profile_ps_camera_independent_monitor_inside(0.0f, 80.001f));
    assert(!slippi_profile_ps_camera_independent_monitor_inside(0.0f, -20.001f));
    assert(slippi_profile_ps_camera_independent_monitor_inside(NAN, 0.0f));
    assert(slippi_profile_ps_camera_independent_monitor_inside(0.0f, NAN));
    assert(!slippi_profile_ps_camera_independent_monitor_inside(NAN, 80.001f));
}

static void test_draw_diagnostics(void)
{
    const SlippiProfileDrawDiagnostics *initial =
        slippi_profile_draw_diagnostics_snapshot();
    assert(initial != NULL && initial->slot_count == GM_MAX_PLAYERS);
    assert(initial->status == SLIPPI_DRAW_DIAGNOSTIC_OK);
    assert(initial->metadata_seen_mask == 0);

    slippi_profile_draw_diagnostics_record_callback(0, 0, FTKIND_MARIO);
    slippi_profile_draw_diagnostics_record_model_transform(0, 0, FTKIND_MARIO);
    slippi_profile_draw_diagnostics_record_normal_model_dispatch(
        0, 0, FTKIND_MARIO);
    slippi_profile_draw_diagnostics_record_callback(1, 1, FTKIND_MARIO);
    slippi_profile_draw_diagnostics_record_model_transform(1, 1, FTKIND_MARIO);
    slippi_profile_draw_diagnostics_record_normal_model_dispatch(
        1, 1, FTKIND_MARIO);
    const SlippiProfileDrawDiagnostics *observed =
        slippi_profile_draw_diagnostics_snapshot();
    assert(observed->callback_entries[0] == 1);
    assert(observed->callback_entries[1] == 1);
    assert(observed->model_transform_entries[0] == 1);
    assert(observed->model_transform_entries[1] == 1);
    assert(observed->normal_model_dispatch_entries[0] == 1);
    assert(observed->normal_model_dispatch_entries[1] == 1);
    assert(observed->processed_pad_slots[0] == 0 &&
           observed->processed_pad_slots[1] == 1);
    assert(observed->fighter_kinds[0] == FTKIND_MARIO &&
           observed->fighter_kinds[1] == FTKIND_MARIO);
    assert(observed->metadata_seen_mask == 3u);
    assert(observed->status == SLIPPI_DRAW_DIAGNOSTIC_OK);

    slippi_profile_draw_diagnostics_record_callback(
        (int32_t) GM_MAX_PLAYERS, 0, FTKIND_MARIO);
    assert(observed->status & SLIPPI_DRAW_DIAGNOSTIC_INVALID_SOURCE_SLOT);
    slippi_profile_draw_diagnostics_record_callback(1, 0, FTKIND_MARIO);
    assert(observed->status & SLIPPI_DRAW_DIAGNOSTIC_METADATA_CHANGED);
    assert(observed->callback_entries[1] == 2);

    assert(slippi_profile_draw_motion_boundary(ftCo_MS_Wait) ==
           SLIPPI_PROFILE_MOTION_INTERACTIVE);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_Entry) ==
           SLIPPI_PROFILE_MOTION_ENTRY);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_EntryEnd) ==
           SLIPPI_PROFILE_MOTION_ENTRY);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_DeadDown) ==
           SLIPPI_PROFILE_MOTION_DEATH);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_DeadUpFallHitCameraIce) ==
           SLIPPI_PROFILE_MOTION_DEATH);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_Rebirth) ==
           SLIPPI_PROFILE_MOTION_RESPAWN);
    assert(slippi_profile_draw_motion_boundary(ftCo_MS_RebirthWait) ==
           SLIPPI_PROFILE_MOTION_RESPAWN);
    assert(slippi_profile_draw_motion_boundary(-1) ==
           SLIPPI_PROFILE_MOTION_UNKNOWN);
    assert(slippi_profile_draw_motion_boundary(ftMr_MS_Count) ==
           SLIPPI_PROFILE_MOTION_UNKNOWN);
    assert(slippi_profile_draw_is_mario_kind(FTKIND_MARIO));
    assert(!slippi_profile_draw_is_mario_kind(FTKIND_FOX));
}

static void test_offscreen_predicate(void)
{
    const SlippiOffscreenDamageInput inside = {
        0, 0, 0, 0.0f, 0.0f, -10.0f, 10.0f, 10.0f, -10.0f
    };
    SlippiOffscreenDamageInput input = inside;
    assert(slippi_profile_offscreen_damage_zone(NULL) == -1);
    assert(slippi_profile_offscreen_damage_zone(&inside) == 0);

    input.x = -10.001f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 1);
    input = inside; input.x = 10.001f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 1);
    input = inside; input.y = 10.001f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 1);
    input = inside; input.y = -10.001f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 1);

    input = inside; input.x = -10.0f; input.y = 10.0f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);
    input = inside; input.is_home_run_in_game = 1; input.x = 11.0f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);
    input = inside; input.is_dead = 1; input.x = 11.0f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);
    input = inside; input.action_state = 4; input.x = 11.0f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);
    input = inside; input.action_state = 6; input.x = 11.0f;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);

    input = inside; input.x = NAN; input.y = NAN;
    assert(slippi_profile_offscreen_damage_zone(&input) == 0);
}

static void test_dirty_matrix_traversal(void)
{
    HSD_JObj root = {0};
    HSD_JObj child = {0};
    HSD_JObj child_next = {0};
    HSD_JObj root_next = {0};
    HSD_JObj descendant = {0};
    HSD_JObj final_sibling = {0};
    root.flags = JOBJ_MTX_DIRTY;
    child.flags = JOBJ_MTX_DIRTY;
    child_next.flags = JOBJ_MTX_DIRTY | JOBJ_USER_DEF_MTX;
    descendant.flags = JOBJ_MTX_DIRTY;
    final_sibling.flags = JOBJ_MTX_DIRTY;
    root.child = &child;
    child.next = &child_next;
    root.next = &root_next;
    root_next.child = &descendant;
    root_next.next = &final_sibling;

    matrix_call_count = 0;
    slippi_profile_refresh_fighter_matrices(NULL);
    assert(matrix_call_count == 0);
    slippi_profile_refresh_fighter_matrices(&root);
    assert(matrix_call_count == 4);
    assert(matrix_calls[0] == &root);
    assert(matrix_calls[1] == &child);
    assert(matrix_calls[2] == &descendant);
    assert(matrix_calls[3] == &final_sibling);
}

static void test_deadupfall_private_velocity(void)
{
    union ftCommon_MotionVars motion;
    unsigned char before[sizeof(motion)];
    memset(&motion, 0xA5, sizeof(motion));
    memcpy(before, &motion, sizeof(motion));

    float self_x = 11.0f, self_y = 12.0f, self_z = 13.0f;
    assert(slippi_profile_deadupfall_initialize(
        &motion, 1.0f, 0.5f, &self_x, &self_y, &self_z));
    assert(self_x == 0.0f && self_y == 0.0f && self_z == 0.0f);
    for (size_t i = 0; i < sizeof(motion); ++i) {
        if (i < 8 || i >= 16) {
            assert(((const unsigned char *) &motion)[i] == before[i]);
        }
    }
    float stored_y, stored_z;
    memcpy(&stored_y, (const unsigned char *) &motion + 8, sizeof(stored_y));
    memcpy(&stored_z, (const unsigned char *) &motion + 12, sizeof(stored_z));
    assert(stored_y == 1.0f && stored_z == 0.5f);

    self_x = 0.25f;
    float temp_y = 3.0f, temp_z = 4.0f;
    assert(slippi_profile_deadupfall_advance(
        &motion, 0.25f, 1.5f, &temp_y, &temp_z));
    memcpy(&stored_y, (const unsigned char *) &motion + 8, sizeof(stored_y));
    assert(stored_y == 0.75f);
    assert(temp_y == 3.75f && temp_z == 4.5f);

    /* candidate == -terminal selects the terminal-limit arm because the
     * comparison is strict; the stored single is then reloaded for addition. */
    assert(slippi_profile_deadupfall_advance(
        &motion, 2.25f, 1.5f, &temp_y, &temp_z));
    memcpy(&stored_y, (const unsigned char *) &motion + 8, sizeof(stored_y));
    assert(stored_y == -1.5f);
    assert(temp_y == 2.25f && temp_z == 5.0f);

    /* Ordered comparison on an unordered candidate falls back to the same
     * terminal limit. This tests the source contract, not NaN payload parity. */
    const float nan = NAN;
    assert(slippi_profile_deadupfall_initialize(
        &motion, nan, 0.5f, &self_x, &self_y, &self_z));
    assert(slippi_profile_deadupfall_advance(
        &motion, 0.25f, 1.5f, &temp_y, &temp_z));
    memcpy(&stored_y, (const unsigned char *) &motion + 8, sizeof(stored_y));
    assert(stored_y == -1.5f);

    /* Literal binary32 oracles from the retained hook's ordered fallback and
     * stfs/lfs boundary, not another implementation of its formula. */
    assert(slippi_profile_deadupfall_initialize(
        &motion, 0.0f, 0.5f, &self_x, &self_y, &self_z));
    assert(slippi_profile_deadupfall_advance(
        &motion, 2.0f, 1.5f, &temp_y, &temp_z));
    memcpy(&stored_y, (const unsigned char *) &motion + 8, sizeof(stored_y));
    assert(stored_y == -1.5f); /* candidate -2 is below -1.5 */

    unsigned int bits;
    _Static_assert(sizeof(bits) == sizeof(float), "float-bit oracle width");
    assert(slippi_profile_deadupfall_initialize(
        &motion, 0.0f, 0.5f, &self_x, &self_y, &self_z));
    memcpy(&bits, &self_x, sizeof(bits)); assert(bits == 0x00000000u);
    memcpy(&bits, &self_y, sizeof(bits)); assert(bits == 0x00000000u);
    memcpy(&bits, &self_z, sizeof(bits)); assert(bits == 0x00000000u);
    assert(slippi_profile_deadupfall_advance(
        &motion, 0.0f, 0.0f, &temp_y, &temp_z));
    memcpy(&bits, (const unsigned char *) &motion + 8, sizeof(bits));
    assert(bits == 0x80000000u); /* +0 == -terminal: select -0 */
    assert(slippi_profile_deadupfall_initialize(
        &motion, -0.0f, 0.5f, &self_x, &self_y, &self_z));
    assert(slippi_profile_deadupfall_advance(
        &motion, 0.0f, -0.0f, &temp_y, &temp_z));
    memcpy(&bits, (const unsigned char *) &motion + 8, sizeof(bits));
    assert(bits == 0x00000000u); /* -0 == -terminal: select +0 */

    assert(slippi_profile_deadupfall_initialize(
        &motion, 1.0f, 0.5f, &self_x, &self_y, &self_z));
    temp_y = -1.0f;
    assert(slippi_profile_deadupfall_advance(
        &motion, -0x1p-24f, 4.0f, &temp_y, &temp_z));
    memcpy(&bits, (const unsigned char *) &motion + 8, sizeof(bits));
    assert(bits == 0x3F800000u); /* halfway rounds to even 1 before addition */
    memcpy(&bits, &temp_y, sizeof(bits));
    assert(bits == 0x00000000u); /* 1 + -1, not unrounded 2^-24 */

    assert(!slippi_profile_deadupfall_initialize(
        NULL, 0.0f, 0.0f, &self_x, &self_y, &self_z));
    assert(!slippi_profile_deadupfall_advance(
        &motion, 0.0f, 1.0f, NULL, &temp_z));
}

int main(void)
{
    test_draw_policy();
    test_whispy_motion_predicate();
    test_stadium_position_predicate();
    test_draw_diagnostics();
    test_offscreen_predicate();
    test_dirty_matrix_traversal();
    test_deadupfall_private_velocity();
    return 0;
}
