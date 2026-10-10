#include "gameplay_collision.h"
#include "gameplay_bootstrap.h"
#include "gameplay_source_context.h"
#include <sysdolphin/baselib/memory.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <melee/mp/mpisland.h>
#include <melee/gr/grdynamicattr.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error[256];
static void check(int value, const char* reason)
{
    if (!value) { fprintf(stderr, "%s: %s\n", reason, error); exit(1); }
}
static int error_contains(const char* text) { return strstr(error, text) != NULL; }
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
static Vec2 source_vertices[] = {{-10, 0}, {0, 0}, {0, 0}, {10, 5}};
static MapLine source_lines[] = {
    {0, 1, -1, 1, -1, 1, 1, 0x104},
    {1, 2, 0, 2, 0, 2, 1, 0x104},
    {2, 3, 1, -1, 1, -1, 1, 0x205},
    {3, 0, -1, -1, -1, -1, 2, 6},
};
static MapJoint source_joints[] = {
    {0, 3, 3, 1, 4, 0, 4, 0, 0, 0, -10, 0, 10, 5, 0, 4},
};
static MapCollData source_map = {
    source_vertices, 4, source_lines, 4, 0, 3, 3, 1, 4, 0, 4, 0, 0, 0,
    source_joints, 1, 0,
};

static void source_loaded_case(void)
{
    GroundParam saved = {0}; saved.y = 2.0F;
    GroundParam* prior_param = stage_info.param;
    MapCollData* prior_data = stage_info.coll_data;
    GrKind prior_kind = stage_info.grkind;
    stage_info.param = &saved;
    stage_info.grkind = Gr_Kind_Last;
    stage_info.coll_data = &source_map;

    /* Match retail Stage_8022524C: load the source descriptor and construct
     * the original updater before attaching the explicit storage owner. */
    mpLibLoad(&source_map);
    mpLib_80058820();
    MeleeWebCollision* owner = melee_web_collision_adopt_loaded(
        &input, error, sizeof(error));
    check(owner != NULL, "retail-loaded MapCollData and updater are adopted");
    check(melee_web_collision_retire_unadopted(&source_map,error,sizeof(error)),
          "stage retirement leaves successfully adopted collision alive");
    check(mpLib_8004D164() == &source_map &&
              ((HSD_GObj**) HSD_GObj_Entities)[6]->user_data == owner,
          "adopted owner retains the source descriptor and original updater");
    MeleeWebCollisionLineResult line;
    check(melee_web_collision_line(owner, 0, &line, error, sizeof(error)) &&
              line.v0[0] == -20 && line.v1[0] == 0 && line.next == 2,
          "adopted source lines retain original load/prune results");
    check(melee_web_collision_destroy(owner, error, sizeof(error)),
          "adopted source updater releases its loaded storage");
    check(mpLib_8004D164() == NULL && mpGetGroundCollVtx() == NULL &&
              mpGetGroundCollLine() == NULL && mpGetGroundCollJoint() == NULL,
          "adopted source teardown clears original collision globals");
    const MeleeWebGameplayStats before=melee_web_gameplay_stats();
    for (int lifetime = 0; lifetime < 2; ++lifetime) {
        mpLibLoad(&source_map);
        mpLib_80058820();
        mp_UnkStruct0* const floor = mpIsland_8005AB54(0);
        const int loaded_free = melee_web_gameplay_stats().heap_free_bytes;
        check(floor != NULL, "source floor island exists before joint disable");
        mpLib_80057BC0(0);
        check(mpIsland_80458E88.next == NULL && mpIsland_80458E88.x4 == NULL &&
                  mpIsland_80458E88.x18 != NULL && mpIsland_80458E88.x1C != NULL,
              "original joint disable moves floor and ceiling islands to side lists");
        mpJointListAdd(0);
        check(mpIsland_8005AB54(0) == floor &&
                  mpIsland_80458E88.x18 == NULL && mpIsland_80458E88.x1C == NULL &&
                  melee_web_gameplay_stats().heap_free_bytes == loaded_free,
              "original reenable reuses the disabled islands without allocation");
        CollVtx* const loaded_vertices = mpGetGroundCollVtx();
        loaded_vertices[floor->x4].pos.x += 7.0F;
        mpIsland_8005B334(0, source_joints[0].vtx_start,
                         source_joints[0].vtx_count, true);
        check(mpIsland_8005AB54(0) == floor &&
                  floor->x8.x == loaded_vertices[floor->x4].pos.x &&
                  melee_web_gameplay_stats().heap_free_bytes == loaded_free,
              "original island movement updates the same allocation");
        mpLib_80057BC0(0);
        MeleeWebCollisionInput invalid=input; invalid.stage_scale=INFINITY;
        check(!melee_web_collision_adopt_loaded(&invalid,error,sizeof(error)),
              "invalid input rejects after original source loading");
        check(!melee_web_collision_retire_unadopted(&input,error,sizeof(error)),
              "rollback rejects a different source map identity");
        check(melee_web_collision_retire_unadopted(&source_map,error,sizeof(error)),
              "failed source adoption rolls back original storage and updater");
        const int side_list_free_after = melee_web_gameplay_stats().heap_free_bytes;
        fprintf(stderr,
                "COLLISION_SIDE_LIST_RETIRE free_before=%d free_after=%d\n",
                before.heap_free_bytes, side_list_free_after);
        check(side_list_free_after == before.heap_free_bytes,
              "source collision retirement returns disabled island side-list storage");
        check(melee_web_collision_source_available()&&
                  melee_web_gameplay_stats().objects==before.objects&&
                  melee_web_gameplay_stats().processes==before.processes,
              "rollback leaves collision ready for another construction");
        check(melee_web_collision_retire_unadopted(&source_map,error,sizeof(error)),
              "already retired source collision rollback is idempotent");
        check(melee_web_gameplay_stats().heap_free_bytes == before.heap_free_bytes,
              "repeated retirement does not free collision storage twice");
    }
    stage_info.param = prior_param;
    stage_info.coll_data = prior_data;
    stage_info.grkind = prior_kind;
}

static DynamicsDesc* source_dynamic_touch_line(int line_id)
{
    (void) line_id;
    abort();
}

static DynamicsDesc* source_dynamic_touch_line_changed(int line_id)
{
    (void) line_id;
    return NULL;
}

/* The shared owner accepts a safely removed dynamic joint while preserving
 * its exact source descriptors, then revalidates original bind/update/remove
 * transitions before executing live floor queries. */
static void source_dynamic_case(void)
{
    GroundParam param = {0};
    param.y = 1.0F;
    Vec2 source_vertices[] = {
        {-10, 0}, {0, 0}, {0, 10}, {10, 10},
        {-2, -1}, {2, -1}, {2, 1}, {-2, 1},
    };
    MapLine source_lines[] = {
        {0, 1, -1, 1, -1, 1, 1, 0},
        {1, 3, 0, -1, 0, -1, 1, 0},
        {2, 3, -1, -1, -1, -1, 2, 0},
        {4, 5, 6, 4, 6, 4, 1, 0},
        {5, 6, 3, 5, 3, 5, 1, 0},
        {6, 7, 4, 6, 4, 6, 1, 0},
        {7, 4, 5, 3, 5, 3, 1, 0},
    };
    MapJoint source_joints[] = {
        {0, 2, 2, 1, 3, 0, 3, 0, 3, 4, -10, -1, 10, 10, 0, 8},
    };
    MapCollData source_map = {
        .verts = source_vertices,
        .vert_count = 8,
        .lines = source_lines,
        .line_count = 7,
        .floor_start = 0,
        .floor_count = 2,
        .ceiling_start = 2,
        .ceiling_count = 1,
        .right_wall_start = 3,
        .left_wall_start = 3,
        .dynamic_start = 3,
        .dynamic_count = 4,
        .joints = source_joints,
        .joint_count = 1,
    };
    const int original_dynamic_count = source_map.dynamic_count;
    const MeleeWebCollisionVertex input_vertices[] = {
        {-10, 0}, {0, 0}, {0, 10}, {10, 10},
        {-2, -1}, {2, -1}, {2, 1}, {-2, 1},
    };
    const MeleeWebCollisionLine input_lines[] = {
        {0, 1, -1, 1, -1, 1, 1, 0},
        {1, 3, 0, -1, 0, -1, 1, 0},
        {2, 3, -1, -1, -1, -1, 2, 0},
        {4, 5, 6, 4, 6, 4, 1, 0},
        {5, 6, 3, 5, 3, 5, 1, 0},
        {6, 7, 4, 6, 4, 6, 1, 0},
        {7, 4, 5, 3, 5, 3, 1, 0},
    };
    const MeleeWebCollisionJoint input_joints[] = {
        {{{0, 2}, {2, 1}, {3, 0}, {3, 0}, {3, 4}}, -10, -1, 10, 10, {0, 8}},
    };
    const MeleeWebCollisionInput dynamic_input = {
        .vertices = input_vertices,
        .vertex_count = 8,
        .lines = input_lines,
        .line_count = 7,
        .joints = input_joints,
        .joint_count = 1,
        .ranges = {{0, 2}, {2, 1}, {3, 0}, {3, 0}, {3, 4}},
        .stage_kind = Gr_Kind_Last,
        .stage_scale = 1.0F,
    };
    MapLine source_lines_before[7];
    Vec2 source_vertices_before[8];
    MapJoint source_joints_before[1];
    memcpy(source_lines_before, source_lines, sizeof(source_lines));
    memcpy(source_vertices_before, source_vertices, sizeof(source_vertices));
    memcpy(source_joints_before, source_joints, sizeof(source_joints));
    const MeleeWebGameplayStats before = melee_web_gameplay_stats();
    HSD_JObj root = {0}, bound_joint = {0};
    bound_joint.mtx[0][0] = 1.0F;
    bound_joint.mtx[1][1] = 1.0F;
    bound_joint.mtx[2][2] = 1.0F;
    root.child = &bound_joint;
    bound_joint.parent = &root;
    GroundParam* prior_param = stage_info.param;
    MapCollData* prior_data = stage_info.coll_data;
    GrKind prior_kind = stage_info.grkind;
    GrTouchLineCallback prior_touch_line = stage_info.on_touch_line;
    stage_info.param = &param;
    stage_info.grkind = Gr_Kind_Last;
    stage_info.coll_data = &source_map;
    stage_info.on_touch_line = source_dynamic_touch_line;

    check(!melee_web_collision_create(&dynamic_input, error, sizeof(error)) &&
              error_contains("Dynamic collision lines require pending stage bindings"),
          "synthetic collision construction still rejects a valid dynamic range");
    mpLibLoad(&source_map);
    mpLib_80058820();
    stage_info.on_touch_line = NULL;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("Source dynamic collision has no authored touch-line callback"),
          "source dynamic collision rejects a missing authored touch-line callback");
    stage_info.on_touch_line = source_dynamic_touch_line;
    source_map.dynamic_count = 0;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("Source dynamic collision lost its original loaded map descriptor"),
          "source dynamic collision rejects a changed loaded-map descriptor");
    source_map.dynamic_count = original_dynamic_count;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("Source dynamic collision joint is active without its authored stage JObj"),
          "source dynamic collision rejects an unbound authored joint");
    mpLib_80057BC0(0);
    CollJoint* const source_joint_bindings = mpGetGroundCollJoint();
    CollLine* const source_collision_lines = mpGetGroundCollLine();
    CollVtx* const source_collision_vertices = mpGetGroundCollVtx();
    check(source_joint_bindings && source_collision_lines && source_collision_vertices &&
              !(source_joint_bindings[0].flags & CollJoint_Enabled) &&
              !source_joint_bindings[0].x20 &&
              !(source_collision_lines[3].flags & LINE_FLAG_ENABLED),
          "original removal leaves the source dynamic descriptor unbound, unlinked and disabled");
    const int authored_dynamic_start = source_joints[0].dynamic_start;
    source_joints[0].dynamic_start = 4;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("Source dynamic collision joint descriptor or range changed"),
          "source dynamic collision rejects a changed loaded joint range");
    source_joints[0].dynamic_start = authored_dynamic_start;

    const uint32_t deferred_line_flags = source_collision_lines[3].flags;
    source_collision_lines[3].flags |= LINE_FLAG_ENABLED;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("Removed source dynamic collision joint retains an enabled owned line"),
          "deferred source dynamic adoption rejects a partially enabled owned range");
    source_collision_lines[3].flags = deferred_line_flags;

    mpJointListAdd(0);
    source_joint_bindings[0].flags &= ~CollJoint_Enabled;
    check(!melee_web_collision_adopt_loaded(&dynamic_input, error, sizeof(error)) &&
              error_contains("enabled flag disagrees with active-list membership"),
          "source dynamic adoption rejects a linked joint whose enabled flag was cleared");
    source_joint_bindings[0].flags |= CollJoint_Enabled;
    mpLib_80057BC0(0);

    /* The four authored-floor hints form a closed dynamic-only next0 cycle.
     * A static floor seed entering that same non-root cycle must still reject. */
    MeleeWebCollisionLine mixed_cycle_lines[7];
    memcpy(mixed_cycle_lines, input_lines, sizeof(mixed_cycle_lines));
    mixed_cycle_lines[1].next0 = 3;
    MeleeWebCollisionInput mixed_cycle = dynamic_input;
    mixed_cycle.lines = mixed_cycle_lines;
    check(!melee_web_collision_adopt_loaded(&mixed_cycle, error, sizeof(error)) &&
              error_contains("Cyclic static island chains"),
          "static floor seed reaching a non-root dynamic cycle still rejects before adoption");

    MeleeWebCollision* owner = melee_web_collision_adopt_loaded(
        &dynamic_input, error, sizeof(error));
    check(owner != NULL,
          "source dynamic lines adopt in the original removed and disabled state");
    MeleeWebCollisionReadiness readiness;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              !readiness.stage_joint_bindings_ready && readiness.stage_callbacks_ready,
          "deferred readiness reports the absent JObj and retained callback separately");
    MeleeWebCollisionLineResult dynamic_floor, dynamic_left_wall,
        dynamic_ceiling, dynamic_right_wall;
    check(melee_web_collision_line(owner, 3, &dynamic_floor, error, sizeof(error)) &&
              dynamic_floor.kind == CollLine_Floor &&
              !(dynamic_floor.runtime_flags & LINE_FLAG_ENABLED),
          "deferred source line remains available for disabled descriptor inspection");
    MeleeWebCollisionFloorResult floor_result;
    check(!melee_web_collision_floor(owner, 3, 0.0F, 0.0F, &floor_result,
                                     error, sizeof(error)) &&
              error_contains("Floor query crosses a disabled source floor line"),
          "floor execution refuses a deferred dynamic source line");

    mpLib_800552B0(0, &root, 0);
    check(source_joint_bindings[0].x20 == &bound_joint,
          "original joint traversal binds the deferred dynamic source JObj");
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              readiness.stage_joint_bindings_ready && readiness.stage_callbacks_ready,
          "readiness observes the original JObj binding before line activation");
    mpLib_80055E9C(0);
    mpJointListAdd(0);
    mpJointUpdateDynamics(0);
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              readiness.stage_joint_bindings_ready && readiness.stage_callbacks_ready,
          "readiness revalidates the original update and joint-enable transition");

    unsigned char readiness_before[sizeof(readiness)];
    MapJoint wrong_inner = source_joints[0];
    MapJoint* const original_inner = source_joint_bindings[0].inner;
    source_joint_bindings[0].inner = &wrong_inner;
    memcpy(readiness_before, &readiness, sizeof(readiness));
    check(!melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              error_contains("lost its loaded MapJoint owner") &&
              memcmp(readiness_before, &readiness, sizeof(readiness)) == 0,
          "readiness rejects a changed bound inner without changing output");
    source_joint_bindings[0].inner = original_inner;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)),
          "readiness recovers after restoring the original bound inner");

    GrTouchLineCallback const original_touch_line = stage_info.on_touch_line;
    stage_info.on_touch_line = source_dynamic_touch_line_changed;
    memcpy(readiness_before, &readiness, sizeof(readiness));
    check(!melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              error_contains("touch-line callback changed") &&
              memcmp(readiness_before, &readiness, sizeof(readiness)) == 0,
          "readiness rejects a changed callback identity without changing output");
    stage_info.on_touch_line = original_touch_line;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)),
          "readiness recovers after restoring the authored callback");

    MapJoint original_descriptor = source_joints[0];
    source_joints[0].dynamic_start = (s16) (original_descriptor.dynamic_start + 1);
    memcpy(readiness_before, &readiness, sizeof(readiness));
    check(!melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              error_contains("joint descriptor or range changed") &&
              memcmp(readiness_before, &readiness, sizeof(readiness)) == 0,
          "readiness rejects a changed source joint range without changing output");
    source_joints[0] = original_descriptor;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)),
          "readiness recovers after restoring the authored joint range");

    HSD_JObj* const original_jobj = source_joint_bindings[0].x20;
    source_joint_bindings[0].x20 = NULL;
    memcpy(readiness_before, &readiness, sizeof(readiness));
    check(!melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              error_contains("active without its authored stage JObj") &&
              memcmp(readiness_before, &readiness, sizeof(readiness)) == 0,
          "readiness rejects an active-unbound transition without changing output");
    source_joint_bindings[0].x20 = original_jobj;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)),
          "readiness recovers after restoring the bound JObj");

    const s16 original_next1 = source_lines[3].next_id1;
    source_lines[3].next_id1 = (s16) source_map.line_count;
    MeleeWebCollisionLineResult guarded_line;
    memset(&guarded_line, 0xA5, sizeof(guarded_line));
    unsigned char guarded_line_before[sizeof(guarded_line)];
    memcpy(guarded_line_before, &guarded_line, sizeof(guarded_line));
    check(!melee_web_collision_line(owner, 3, &guarded_line, error, sizeof(error)) &&
              error_contains("Retail line adjacency escaped the loaded source map") &&
              memcmp(guarded_line_before, &guarded_line, sizeof(guarded_line)) == 0,
          "line inspection rejects an out-of-range retail alternate before source dereference");
    MeleeWebCollisionFloorResult guarded_floor;
    memset(&guarded_floor, 0xA5, sizeof(guarded_floor));
    unsigned char guarded_floor_before[sizeof(guarded_floor)];
    memcpy(guarded_floor_before, &guarded_floor, sizeof(guarded_floor));
    check(!melee_web_collision_floor(owner, 3, 0.0F, 0.0F, &guarded_floor,
                                     error, sizeof(error)) &&
              error_contains("Retail line adjacency escaped the loaded source map") &&
              memcmp(guarded_floor_before, &guarded_floor, sizeof(guarded_floor)) == 0,
          "floor query rejects an out-of-range retail alternate before source dereference");
    source_lines[3].next_id1 = original_next1;
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              melee_web_collision_line(owner, 3, &guarded_line, error, sizeof(error)),
          "line selection recovers after restoring the authored alternate");

    check(memcmp(source_lines_before, source_lines, sizeof(source_lines)) == 0 &&
              memcmp(source_vertices_before, source_vertices, sizeof(source_vertices)) == 0 &&
              memcmp(source_joints_before, source_joints, sizeof(source_joints)) == 0,
          "original dynamic update preserves source map descriptors and authored bytes");
    mpLib_800575B0(3);
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              readiness.stage_joint_bindings_ready && readiness.stage_callbacks_ready,
          "active bound readiness permits an original per-line disable");
    check(!melee_web_collision_floor(owner, 3, 0.0F, 0.0F, &floor_result,
                                     error, sizeof(error)) &&
              error_contains("Floor query crosses a disabled source floor line"),
          "floor execution refuses an actually disabled source floor line");
    mpLib_80057528(3);
    check(melee_web_collision_line(owner, 3, &dynamic_floor, error, sizeof(error)) &&
              dynamic_floor.kind == CollLine_Floor &&
              dynamic_floor.v0[0] == source_collision_vertices[4].pos.x &&
              dynamic_floor.v1[0] == source_collision_vertices[5].pos.x,
          "original updater resolves and queries the authored-floor edge from live source vertices");
    check(melee_web_collision_line(owner, 4, &dynamic_left_wall, error, sizeof(error)) &&
              dynamic_left_wall.kind == CollLine_LeftWall,
          "original updater resolves the authored-floor vertical edge as a live left wall");
    check(melee_web_collision_line(owner, 5, &dynamic_ceiling, error, sizeof(error)) &&
              dynamic_ceiling.kind == CollLine_Ceiling,
          "original updater resolves the reversed authored-floor edge as a live ceiling");
    check(melee_web_collision_line(owner, 6, &dynamic_right_wall, error, sizeof(error)) &&
              dynamic_right_wall.kind == CollLine_RightWall,
          "original updater resolves the opposite vertical edge as a live right wall");
    check(melee_web_collision_floor(owner, 3, 0.0F, 0.0F, &floor_result,
                                    error, sizeof(error)) && floor_result.line == 3,
          "live floor query accepts the dynamically resolved floor edge");
    check(!melee_web_collision_floor(owner, 4, 0.0F, 0.0F, &floor_result,
                                     error, sizeof(error)),
          "live floor query refuses a dynamically resolved wall edge");
    check(!melee_web_collision_floor(owner, 5, 0.0F, 0.0F, &floor_result,
                                     error, sizeof(error)),
          "live floor query refuses a dynamically resolved ceiling edge");
    check(dynamic_floor.v1[0] == 2.0F && source_collision_vertices[5].pos.x == 2.0F,
          "dynamic endpoint starts at the original loaded vertex");
    bound_joint.mtx[0][3] = 1.0F;
    mpLib_80055E9C(0);
    MeleeWebCollisionLineResult moved_dynamic_floor;
    check(melee_web_collision_line(owner, 3, &moved_dynamic_floor,
                                   error, sizeof(error)) &&
              moved_dynamic_floor.kind == CollLine_Floor &&
              moved_dynamic_floor.v1[0] == 3.0F,
          "original JObj update and query read the moved live vertex array");
    check(memcmp(source_lines_before, source_lines, sizeof(source_lines)) == 0 &&
              memcmp(source_vertices_before, source_vertices, sizeof(source_vertices)) == 0 &&
              memcmp(source_joints_before, source_joints, sizeof(source_joints)) == 0,
          "source descriptors remain byte-identical after live dynamic queries");
    mpLib_80057BC0(0);
    check(melee_web_collision_readiness(owner, &readiness, error, sizeof(error)) &&
              readiness.stage_joint_bindings_ready && readiness.stage_callbacks_ready &&
              !(source_collision_lines[3].flags & LINE_FLAG_ENABLED),
          "readiness revalidates the original removed state while preserving the bound JObj");
    check(melee_web_collision_line(owner, 3, &dynamic_floor, error, sizeof(error)) &&
              !(dynamic_floor.runtime_flags & LINE_FLAG_ENABLED),
          "line inspection retains the disabled original dynamic descriptor");
    check(!melee_web_collision_floor(owner, 3, 0.0F, 0.0F, &floor_result,
                                     error, sizeof(error)) &&
              error_contains("Floor query crosses a disabled source floor line"),
          "floor execution refuses an original removed dynamic joint");
    check(melee_web_collision_destroy(owner, error, sizeof(error)),
          "dynamic source collision teardown releases original storage");
    check(melee_web_collision_source_available() && mpLib_8004D164() == NULL &&
              mpGetGroundCollVtx() == NULL && mpGetGroundCollLine() == NULL &&
              mpGetGroundCollJoint() == NULL &&
              melee_web_gameplay_stats().objects == before.objects &&
              melee_web_gameplay_stats().processes == before.processes,
          "dynamic source teardown releases the original map, updater and collision storage");
    check(memcmp(source_lines_before, source_lines, sizeof(source_lines)) == 0 &&
              memcmp(source_vertices_before, source_vertices, sizeof(source_vertices)) == 0 &&
              memcmp(source_joints_before, source_joints, sizeof(source_joints)) == 0,
          "source map bytes remain unchanged after original collision teardown");
    stage_info.param = prior_param;
    stage_info.coll_data = prior_data;
    stage_info.grkind = prior_kind;
    stage_info.on_touch_line = prior_touch_line;
}

static void source_dummy_case(void)
{
    check(melee_web_collision_source_available(), "empty source collision storage is available before dummy load");
    check(!melee_web_collision_adopt_dummy(error, sizeof(error)),
          "dummy adoption rejects an uninitialized source world");

    /* Reproduce the original Results entry seam: mpLibLoad(NULL) selects the
     * authored dummy map, then the original link-6/process-4 owner is made.
     * No copied map descriptor or synthetic process is involved. */
    stage_info.grkind = Gr_Kind_Pura;
    mpLibLoad(NULL);
    mpLib_80058820();
    check(!melee_web_collision_source_available(),
          "nonempty source collision storage is not advertised as available");
    check(!melee_web_collision_adopt_dummy(error, sizeof(error)),
          "dummy adoption rejects a non-dummy stage context");
    stage_info.grkind = Gr_Kind_Unk00;
    MeleeWebCollision* owner = melee_web_collision_adopt_dummy(error, sizeof(error));
    check(owner != NULL, "dummy collision adopts original arrays and process");
    check(!melee_web_collision_adopt_dummy(error, sizeof(error)),
          "duplicate dummy adoption rejects live ownership");
    check(!melee_web_collision_create(&input, error, sizeof(error)),
          "ordinary collision construction rejects live dummy ownership");

    HSD_GObj* updater = ((HSD_GObj**) HSD_GObj_Entities)[6];
    check(updater && updater->user_data == owner && updater->proc && updater->proc->s_link == 4,
          "adopted dummy uses the original collision GObj userdata lifetime");
    HSD_GObjPLink_80390228(updater);
    check(mpLib_8004D164() == NULL && mpGetGroundCollVtx() == NULL &&
              mpGetGroundCollLine() == NULL && mpGetGroundCollJoint() == NULL &&
              melee_web_gameplay_stats().objects == 0 &&
              melee_web_gameplay_stats().processes == 0,
          "original dummy GObj destruction releases collision globals and process");
    check(melee_web_collision_destroy(owner, error, sizeof(error)),
          "stale adopted handle releases after original GObj destruction");
    check(melee_web_collision_source_available(),
          "source collision storage is available again after GObj destruction");
}

/* Exercise the production floor body and skipped-conversion consumer together.
 * A hit result alone says nothing about the last executed r5 definition. */
static int floor_callback_calls;
static int floor_callback_reject = -1;
static melee_source_bool floor_callback(Fighter_GObj* gobj, int line)
{
    (void) gobj;
    ++floor_callback_calls;
    return line != floor_callback_reject;
}

static void floor_carry_case(void* fighter, const char* label, float x,
                             float ay, float by, int skip, int callback,
                             int expected_hit, int expected_known)
{
    const MeleeWebSourceFrameId ids[] = {
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH,
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_18,
        MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY,
    };
    MeleeWebSourceFrameGuard frames[3] = {{0}};
    for (int i = 0; i < 3; ++i) melee_web_source_frame_enter(&frames[i], ids[i]);
    /* Entry must replace a previous known seed even on an all-pruned query. */
    check(melee_web_source_context_publish_seed_r5(), "seed before floor query");
    Vec3 pos, normal;
    int line = -1;
    u32 flags = 0;
    floor_callback_calls = 0;
    int hit = mpCheckFloor(x, ay, x, by, 0, &pos, &line, &flags, &normal,
                          skip, -1, -1, callback ? floor_callback : NULL, NULL);
    MeleeWebSourceRegisterWord value = melee_web_source_context_r5();
    for (int i = 2; i >= 0; --i) melee_web_source_frame_leave(&frames[i]);
    int8_t sx = 99, sy = 99;
    int accepted = melee_web_source_context_resolve_skipped(fighter, &sx, &sy);
    printf("floor carry %s: hit=%d line=%d known=%d accepted=%d callbacks=%d\n",
           label, hit, line, value.known, accepted, floor_callback_calls);
    check(hit == expected_hit && value.known == expected_known &&
              accepted == expected_known, label);
    if (expected_known) {
        check(value.kind == MELEE_WEB_SOURCE_REGISTER_STACK_LOCAL &&
                  sy == (int8_t) value.source_word,
              "executed floor-local definition reaches the consumer");
    } else {
        check(value.kind == MELEE_WEB_SOURCE_REGISTER_UNKNOWN && sx == 99 && sy == 99,
              "unsupported definition rejects without supplying stick bytes");
    }
    check(!callback || floor_callback_calls == 2, "callback order covers both lines");
}

static void source_floor_carry_cases(void)
{
    MeleeWebCollisionLine carry_lines[] = {
        {0, 1, -1, -1, -1, -1, 1, 0x104},
        {2, 3, -1, -1, -1, -1, 1, 0x205},
    };
    const MeleeWebCollisionJoint carry_joint = {
        {{0, 2}, {2, 0}, {2, 0}, {2, 0}, {0, 0}}, -10, 0, 10, 5, {0, 4}};
    MeleeWebCollisionInput carry_input = {
        vertices, 4, carry_lines, 2, &carry_joint, 1,
        {{0, 2}, {2, 0}, {2, 0}, {2, 0}, {0, 0}}, 0, Gr_Kind_Last, 1.0F};
    MeleeWebCollision* owner = melee_web_collision_create(&carry_input,error,sizeof(error));
    check(owner != NULL, "floor carry collision construction");
    void* fighter = HSD_MemAlloc(0x3000);
    MeleeWebSourceFighterAddress lease;
    check(melee_web_source_memory_fighter_acquire(fighter, 0x23ec, &lease),
          "floor carry live source Fighter lease");
    check(melee_web_source_context_begin_tick(melee_web_gameplay_generation()),
          "floor carry scheduler context");
    MeleeWebSourceFrameGuard frames[3] = {{0}};
    melee_web_source_frame_enter(&frames[0], MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH);
    melee_web_source_frame_enter(&frames[1], MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK);
    check(melee_web_source_context_enter_fighter(fighter), "floor carry Fighter binding");
    melee_web_source_frame_enter(&frames[2], MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK);
    floor_carry_case(fighter, "flat-hit", -5, 20, -10, 1, 0, 1, 1);
    floor_carry_case(fighter, "flat-miss-upward", -5, -10, 20, 1, 0, 0, 1);
    floor_carry_case(fighter, "flat-miss-horizontal", 5, 20, -10, 1, 0, 0, 1);
    floor_carry_case(fighter, "all-joints-pruned", -1000, 20, -10, -1, 0, 0, 0);
    floor_carry_case(fighter, "sloped-last-hit", 5, 20, -10, -1, 0, 1, 0);
    floor_carry_case(fighter, "flat-hit-sloped-last-miss", -5, 20, -10, -1, 0, 1, 0);
    floor_callback_reject = 1;
    floor_carry_case(fighter, "flat-hit-last-callback-rejects", -5, 20, -10, -1, 1, 1, 0);
    floor_callback_reject = -1;
    floor_carry_case(fighter, "flat-hit-last-callback-before-skip", -5, 20, -10, 1, 1, 1, 0);
    check(melee_web_collision_destroy(owner,error,sizeof(error)), "ordered floor teardown");
    MeleeWebCollisionLine first = carry_lines[0];
    carry_lines[0] = carry_lines[1]; carry_lines[1] = first;
    owner = melee_web_collision_create(&carry_input,error,sizeof(error));
    check(owner != NULL, "reverse floor iteration construction");
    floor_carry_case(fighter, "sloped-hit-later-flat-miss", 5, 20, -10, -1, 0, 1, 1);
    floor_callback_reject = 0;
    floor_carry_case(fighter, "rejected-callback-later-flat-hit", -5, 20, -10, -1, 1, 1, 1);
    MeleeWebSourceFrameGuard query[3] = {{0}};
    melee_web_source_frame_enter(&query[0], MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH);
    melee_web_source_frame_enter(&query[1], MELEE_WEB_SOURCE_FRAME_CPU_STATE_18);
    melee_web_source_frame_enter(&query[2], MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY);
    check(melee_web_source_context_publish_seed_r5(), "seed before joint query");
    check(mpJointFromLine(-1) == -1 && melee_web_source_context_r5().known,
          "joint query -1 early return preserves carry");
    check(mpJointFromLine(0) == 0 && !melee_web_source_context_r5().known,
          "executed joint lookup clobbers carry regardless of selected stage");
    for (int i = 2; i >= 0; --i) melee_web_source_frame_leave(&query[i]);
    int8_t sx, sy;
    check(!melee_web_source_context_resolve_skipped(fighter, &sx, &sy),
          "joint-array carry is rejected by the production consumer");
    for (int i = 2; i >= 0; --i) melee_web_source_frame_leave(&frames[i]);
    check(melee_web_source_context_end_tick(), "floor carry tick end");
    check(melee_web_source_memory_fighter_release(fighter), "floor carry lease release");
    HSD_Free(fighter);
    check(melee_web_collision_destroy(owner,error,sizeof(error)), "floor carry teardown");
}

static void collision_static_preflight_cases(void)
{
    uint16_t saved_v0 = lines[0].v0;
    uint16_t saved_v1 = lines[0].v1;
    lines[0].v0 = 1;
    lines[0].v1 = 0;
    check(!melee_web_collision_create(&input, error, sizeof(error)) &&
              error_contains("Static floor queries require source left-to-right"),
          "reversed static floor remains rejected");
    lines[0].v0 = saved_v0;
    lines[0].v1 = saved_v1;

    const float saved_x = vertices[1].x;
    const float saved_y = vertices[1].y;
    vertices[1].x = vertices[0].x;
    vertices[1].y = vertices[0].y + 1.0F;
    check(!melee_web_collision_create(&input, error, sizeof(error)) &&
              error_contains("Static floor queries require source left-to-right"),
          "nonzero vertical static floor remains rejected");
    vertices[1].x = saved_x;
    vertices[1].y = saved_y;

    int16_t saved_next0 = lines[2].next0;
    lines[2].next0 = 0;
    check(!melee_web_collision_create(&input, error, sizeof(error)) &&
              error_contains("Cyclic static island chains"),
          "static floor cycle remains rejected");
    lines[2].next0 = saved_next0;

    int16_t saved_prev0 = lines[3].prev0;
    lines[3].prev0 = 3;
    check(!melee_web_collision_create(&input, error, sizeof(error)) &&
              error_contains("Cyclic static island chains"),
          "static ceiling cycle remains rejected");
    lines[3].prev0 = saved_prev0;
}

int main(void)
{
    check(!melee_web_collision_create(&input, error, sizeof(error)), "uninitialized world rejects");
    check(melee_web_gameplay_startup(64 * 1024, error, sizeof(error)), "minimum valid HSD world starts");
    check(!melee_web_collision_create(&input, error, sizeof(error)), "undersized collision heap rejects before original allocation assertion");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "undersized collision attempt leaves world releasable");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "real original HSD heap startup");
    MeleeWebCollisionInput invalid = input;
    invalid.ranges[4].count = 1;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "synthetic dynamic-create refusal");
    invalid = input; invalid.stage_scale = INFINITY;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "invalid scale rejects");
    invalid = input; invalid.stage_scale = 1.0e-21F;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "SDK reciprocal-square-root arithmetic underflow rejects");
    invalid = input; invalid.stage_scale = 1.0e20F;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "SDK normal arithmetic overflow rejects");
    invalid = input; invalid.line_count = 1537;
    check(!melee_web_collision_create(&invalid, error, sizeof(error)), "source line capacity enforced before reads");
    source_dummy_case();
    source_loaded_case();
    source_dynamic_case();
    source_floor_carry_cases();
    collision_static_preflight_cases();
    lines[0].prev0 = -2;
    check(!melee_web_collision_create(&input, error, sizeof(error)), "negative adjacency rejects before source dereference");
    lines[0].prev0 = -1;
    GroundParam saved = {0}; saved.y = 7;
    stage_info.param = &saved; stage_info.grkind = Gr_Kind_Pura;
    MeleeWebCollision* owner = melee_web_collision_create(&input, error, sizeof(error));
    check(owner != NULL, "original mpLibLoad and island initialization");
    HSD_GObj* updater=((HSD_GObj**)HSD_GObj_Entities)[6];
    check(updater && updater->proc && updater->proc->s_link==4,
          "original collision updater uses link6 and priority4");
    check(melee_web_gameplay_stats().objects==1 && melee_web_gameplay_stats().processes==1,
          "one shared collision storage/update lifetime");
    check(mpLib_80458868[1].left==-10000 && mpLib_80458868[1].right==10000,
          "original updater initializes floor bounds");
    Vec3 attribute_position={-10,0,0};
    check(grDynamicAttr_801CA0F8(17,&attribute_position,0,3,1)!=NULL,
          "real timed dynamic attribute allocated");
    check(grDynamicAttr_801CA284(&attribute_position,0)==17,
          "dynamic attribute active before scheduler tick");
    check(melee_web_gameplay_step(error,sizeof(error)),"original priority4 collision process executes");
    check(mpLib_80458868[1].left==-20 && mpLib_80458868[1].right==20 &&
          mpLib_80458868[1].bottom==0 && mpLib_80458868[1].top==10,
          "original process derives floor extents from scaled source geometry");
    check(grDynamicAttr_801CA284(&attribute_position,0)==0,
          "original process expires the one-tick dynamic attribute");
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
    check(melee_web_gameplay_stats().objects==0 && melee_web_gameplay_stats().processes==0,
          "collision teardown removes update process and storage owner");
    check(melee_web_gameplay_step(error,sizeof(error)),"scheduler safely ticks after collision destruction");
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
