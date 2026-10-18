/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slippi_profile/dynamics_fix.h"
#include "slippi_profile/offscreen_damage.h"
#include "slippi_profile/profile.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

_Static_assert(JOBJ_MTX_DIRTY == 0x40u, "pinned JObj dirty flag changed");
_Static_assert(JOBJ_USER_DEF_MTX == 0x800000u,
               "pinned JObj user-defined matrix flag changed");

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
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V1;
    assert(slippi_profile_identity_name(profile) != NULL);
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ALLOW);
    assert(slippi_profile_source_draw_decision(profile, SLIPPI_TICK_CORRECTION) ==
           SLIPPI_SOURCE_DRAW_SKIP);
    assert(slippi_profile_source_draw_decision((SlippiProfileIdentity)99,
                                                SLIPPI_TICK_PRESENT) ==
           SLIPPI_SOURCE_DRAW_ERROR);
    assert(slippi_profile_identity_name((SlippiProfileIdentity)99) == NULL);
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

int main(void)
{
    test_draw_policy();
    test_offscreen_predicate();
    test_dirty_matrix_traversal();
    return 0;
}
