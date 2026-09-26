#include "gameplay_stage_numeric.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "numeric readiness line %d: %s (%s)\n", __LINE__, #c, error); abort(); } } while (0)

/* Exercise the source-stage boundary without a renderer or a complete match.
 * A readiness check must not evaluate lazy source matrices before their first
 * original consumer. Descriptor and consumed-position checks remain separate. */
void melee_web_test_stage_numeric_readiness(MeleeWebStageMarkers* markers)
{
    char error[256] = {0};
    MeleeWebStageNumeric* context = melee_web_stage_numeric_begin_source_stage_kind(
        markers, St_Kind_Last, error, sizeof(error));
    CHECK(context);
    CHECK(!melee_web_stage_numeric_source_stage_ready(context, error, sizeof(error)));
    HSD_Joint descriptor = {0};
    descriptor.scale = (Vec3){1, 1, 1};
    descriptor.position = (Vec3){7, 11, 13};
    HSD_Joint parent = {0};
    parent.scale = (Vec3){1, 1, 1};
    parent.child = &descriptor;
    HSD_JObj* tree = HSD_JObjLoadJoint(&parent);
    CHECK(tree && tree->child);
    HSD_JObj* root = tree->child;
    CHECK(root && !root->scl);
    for (unsigned i = 0; i < 4; ++i) Ground_801C2D0C(i, root);
    for (unsigned i = 148; i <= 152; ++i) Ground_801C2D0C(i, root);
    stage_info.cam_info.cam_bounds = (StageBlastZone){-10, 10, 10, -10};
    stage_info.blast_zone = (StageBlastZone){-20, 20, 20, -20};
    CHECK(melee_web_stage_numeric_source_stage_ready(context, error, sizeof(error)));
    CHECK(!root->scl);
    CHECK(!melee_web_stage_numeric_source_stage_ready(context, error, sizeof(error)));
    float position[3];
    CHECK(melee_web_stage_numeric_spawn(context, 0, position, error, sizeof(error)));
    CHECK(root->scl && position[0] == 7 && position[1] == 11 && position[2] == 13);
    HSD_JObjSetTranslateX(root, NAN);
    CHECK(!melee_web_stage_numeric_spawn(context, 0, position, error, sizeof(error)));
    CHECK(melee_web_stage_numeric_end(context, error, sizeof(error)));
    HSD_JObjRemoveAll(tree);
}
