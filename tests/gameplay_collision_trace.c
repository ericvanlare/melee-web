#include "gameplay_collision.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static char error[256];
static void check(int value, const char* reason)
{
    if (!value) { fprintf(stderr, "%s: %s\n", reason, error); exit(1); }
}
static int near(float a, float b) { return fabsf(a - b) < 0.0003F; }
static MeleeWebCollisionVertex vertices[] = {{-10, 0}, {0, 0}, {0, 0}, {10, 5}};
static MeleeWebCollisionLine lines[] = {
    {0, 1, -1, 1, -1, 1, 1, 0x104},
    {1, 2, 0, 2, 0, 2, 1, 0x104},
    {2, 3, 1, -1, 1, -1, 1, 0x205},
    {3, 0, -1, -1, -1, -1, 2, 6},
};
static const MeleeWebCollisionJoint joint = {{{0, 3}, {3, 1}, {4, 0}, {4, 0}, {0, 0}}, -10, 0, 10, 5, {0, 4}};
static MeleeWebCollisionInput input = {
    vertices, 4, lines, 4, &joint, 1, {{0, 3}, {3, 1}, {4, 0}, {4, 0}, {0, 0}}, 0, Gr_Kind_Last, 2.0F};

int main(void)
{
    check(!melee_web_collision_create(&input, error, sizeof(error)), "uninitialized world rejects");
    check(melee_web_gameplay_startup(64 * 1024, error, sizeof(error)), "minimum valid HSD world starts");
    check(!melee_web_collision_create(&input, error, sizeof(error)), "undersized collision heap rejects before original allocation assertion");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "undersized collision attempt leaves world releasable");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "real original HSD heap startup");
    MeleeWebCollisionInput invalid = input;
    invalid.ranges[4].count = 1;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "dynamic ranges remain explicitly unsupported");
    invalid = input; invalid.stage_scale = INFINITY;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "invalid scale rejects");
    invalid = input; invalid.stage_scale = 1.0e-21F;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "SDK reciprocal-square-root arithmetic underflow rejects");
    invalid = input; invalid.stage_scale = 1.0e20F;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "SDK normal arithmetic overflow rejects");
    invalid = input; invalid.line_count = 1537;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "source line capacity enforced before reads");
    lines[2].next0 = 0;
    check(!melee_web_collision_create(&input, error, sizeof(error)), "cyclic island chains reject before original traversal");
    lines[2].next0 = -1;
    lines[0].prev0 = -2;
    check(!melee_web_collision_create(&input, error, sizeof(error)), "negative adjacency rejects before source dereference");
    lines[0].prev0 = -1;
    GroundParam saved = {0}; saved.y = 7;
    stage_info.param = &saved; stage_info.grkind = Gr_Kind_Pura;
    MeleeWebCollision* owner = melee_web_collision_create(&input, error, sizeof(error));
    check(owner != NULL, "original mpLibLoad and island initialization");
    check(stage_info.param == &saved && stage_info.grkind == Gr_Kind_Pura, "source stage context restored after scoped load");
    check(!melee_web_collision_create(&input, error, sizeof(error)), "exclusive original collision storage enforced");
    MeleeWebCollisionReadiness state;
    check(melee_web_collision_readiness(owner, &state, error, sizeof(error)), "readiness available");
    check(state.vertices == 4 && state.lines == 4 && state.joints == 1 && state.empty_lines == 1 &&
          state.floor_islands == 2 && state.ceiling_islands == 1 && state.storage_owned && state.original_indices_initialized &&
          !state.stage_joint_bindings_ready && !state.stage_callbacks_ready, "original index results and explicit pending stage services");
    MeleeWebCollisionLineResult line;
    check(melee_web_collision_line(owner, 0, &line, error, sizeof(error)), "original endpoint/adjacency query");
    check(line.v0[0] == -20 && line.v1[0] == 0 && line.next == 2 && line.previous == -1 &&
          line.joint == 0 && line.kind == 1 && line.material_flags == 0x104 &&
          (line.runtime_flags & 0x10000) && line.has_normal && near(line.normal[1], 1),
          "source scaling, enabled flags, material flags and pruning rewrites");
    check(melee_web_collision_line(owner, 1, &line, error, sizeof(error)), "empty line remains addressable");
    check((line.pruned_hi_flags & 0x80) && !line.has_normal && line.previous == -1 && line.next == -1,
          "original empty line marker, adjacency and absent normal");
    check(lines[1].hi_flags == 1 && lines[0].next0 == 1, "typed caller data remains immutable");
    MeleeWebCollisionFloorResult floor;
    check(melee_web_collision_floor(owner, 0, 10, 20, &floor, error, sizeof(error)), "original floor interpolation and traversal");
    check(floor.line == 2 && near(floor.displacement_y, -14.9999F) && floor.material_flags == 0x205 &&
          near(floor.normal[0], -0.4472136F) && near(floor.normal[1], 0.8944272F), "original floor displacement, normal and selected material");
    check(melee_web_collision_floor(owner, 0, 100, 20, &floor, error, sizeof(error)) && floor.line == -1,
          "original beyond-chain floor miss");
    check(!melee_web_collision_floor(owner, 1, 0, 0, &floor, error, sizeof(error)), "empty floor does not reach SDK zero-normal assertion");
    check(!melee_web_collision_line(owner, -2, &line, error, sizeof(error)), "negative public line index rejects");
    check(melee_web_collision_destroy(owner, error, sizeof(error)), "normal owner destruction");
    check(mpLib_8004D164() == NULL && mpGetGroundCollVtx() == NULL && mpGetGroundCollLine() == NULL &&
          mpGetGroundCollJoint() == NULL, "original private pointers cleared after storage release");
    const int free_before_reload = melee_web_gameplay_stats().heap_free_bytes;
    for (int i = 0; i < 3; ++i) {
        owner = melee_web_collision_create(&input, error, sizeof(error));
        check(owner != NULL && melee_web_collision_destroy(owner, error, sizeof(error)), "reload same original world");
        check(melee_web_gameplay_stats().heap_free_bytes == free_before_reload, "all collision arrays and original islands released on reload");
    }
    invalid = input; invalid.stage_kind = Gr_Kind_Pura;
    owner = melee_web_collision_create(&invalid, error, sizeof(error));
    check(owner && melee_web_collision_readiness(owner, &state, error, sizeof(error)) && state.empty_lines == 0,
          "original Poke Floats pruning exception uses supplied source stage kind");
    check(!melee_web_collision_floor(owner, 0, 0, 0, &floor, error, sizeof(error)), "retained degenerate chain rejects unsupported floor query");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "world shutdown invokes original GObj userdata cleanup");
    check(mpLib_8004D164() == NULL && mpGetGroundCollVtx() == NULL, "shutdown clears original collision pointers before heap destruction");
    check(!melee_web_collision_readiness(owner, &state, error, sizeof(error)), "stale owner rejects queries");
    check(melee_web_collision_destroy(owner, error, sizeof(error)), "release handle after automatic storage cleanup");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "fresh world starts after collision teardown");
    owner = melee_web_collision_create(&input, error, sizeof(error));
    check(owner && melee_web_collision_destroy(owner, error, sizeof(error)), "collision storage can be recreated in a fresh generation");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "final heap teardown");
    puts("Original mpLib collision initialization/query trace: passed");
    return 0;
}
