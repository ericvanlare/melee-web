/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slippi_profile/dynamics_fix.h"
#include "slippi_profile/deadupfall_fix.h"
#include "slippi_profile/offscreen_damage.h"
#include "slippi_profile/profile.h"

#include <melee/ft/kinds/ftCommon/types.h>

#include <assert.h>
#include <math.h>
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

static HSD_JObj *matrix_calls[8];
static size_t matrix_call_count;

void HSD_JObjSetupMatrixSub(HSD_JObj *jobj)
{
    assert(matrix_call_count < sizeof(matrix_calls) / sizeof(matrix_calls[0]));
    matrix_calls[matrix_call_count++] = jobj;
}

static void test_draw_policy(void)
{
    const SlippiProfileIdentity profile =
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2;
    assert(strcmp(slippi_profile_identity_name(profile),
                  "slippi-render-dependencies-v2") == 0);
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ALLOW);
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_CORRECTION) ==
           SLIPPI_SOURCE_DRAW_SKIP);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)99,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)99) == NULL);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)1) == NULL);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)1,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_source_draw_decision(profile, (SlippiTickKind)99) ==
           SLIPPI_SOURCE_DRAW_ERROR);
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
    test_offscreen_predicate();
    test_dirty_matrix_traversal();
    test_deadupfall_private_velocity();
    return 0;
}
