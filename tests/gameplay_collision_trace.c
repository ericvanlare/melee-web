#include "gameplay_collision.h"
#include "gameplay_bootstrap.h"
#include "gameplay_source_context.h"
#include <sysdolphin/baselib/memory.h>
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/mp/mplib.h>
#include <melee/gr/grdynamicattr.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
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
    mpLibLoad(&source_map);
    mpLib_80058820();
    MeleeWebCollisionInput invalid=input; invalid.stage_scale=INFINITY;
    check(!melee_web_collision_adopt_loaded(&invalid,error,sizeof(error)),
          "invalid input rejects after original source loading");
    check(!melee_web_collision_retire_unadopted(&input,error,sizeof(error)),
          "rollback rejects a different source map identity");
    check(melee_web_collision_retire_unadopted(&source_map,error,sizeof(error)),
          "failed source adoption rolls back original storage and updater");
    check(melee_web_collision_source_available()&&
              melee_web_gameplay_stats().objects==before.objects&&
              melee_web_gameplay_stats().processes==before.processes,
          "rollback leaves collision ready for another construction");
    check(melee_web_collision_retire_unadopted(&source_map,error,sizeof(error)),
          "already retired source collision rollback is idempotent");
    stage_info.param = prior_param;
    stage_info.coll_data = prior_data;
    stage_info.grkind = prior_kind;
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
    source_dummy_case();
    source_loaded_case();
    source_floor_carry_cases();
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
