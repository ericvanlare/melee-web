#include "gameplay_stage_numeric.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "numeric readiness line %d: %s (%s)\n", __LINE__, #c, error); abort(); } } while (0)

static int same_zone(const StageBlastZone* left,const StageBlastZone* right)
{
    return memcmp(left,right,sizeof(*left))==0;
}

static void expect_ready_failure(MeleeWebStageNumeric* owner,HSD_JObj* root,
                                 char* error,const char* expected)
{
    HSD_JObj* prior_markers[261];
    memcpy(prior_markers,stage_info.x280,sizeof(prior_markers));
    const StageBlastZone prior_camera=stage_info.cam_info.cam_bounds;
    const StageBlastZone prior_blast=stage_info.blast_zone;
    const MeleeWebGameplayStats prior_stats=melee_web_gameplay_stats();
    memset(error,0,256);
    CHECK(!melee_web_stage_numeric_source_stage_ready(owner,error,256));
    CHECK(strstr(error,expected)!=NULL);
    CHECK(memcmp(prior_markers,stage_info.x280,sizeof(prior_markers))==0);
    CHECK(same_zone(&prior_camera,&stage_info.cam_info.cam_bounds));
    CHECK(same_zone(&prior_blast,&stage_info.blast_zone));
    const MeleeWebGameplayStats after_stats=melee_web_gameplay_stats();
    CHECK(after_stats.ticks==prior_stats.ticks&&
          after_stats.objects==prior_stats.objects&&
          after_stats.processes==prior_stats.processes&&
          after_stats.generation==prior_stats.generation);
    CHECK(!root||!root->scl);
}

/* Exercise the source-stage boundary without a renderer or a complete match.
 * A readiness check must not evaluate lazy source matrices before their first
 * original consumer. Descriptor and consumed-position checks remain separate. */
void melee_web_test_stage_numeric_readiness(MeleeWebStageMarkers* markers)
{
    char error[256] = {0};
    MeleeWebStageNumeric* context = melee_web_stage_numeric_begin_source_stage_kind(
        markers, St_Kind_Last, error, sizeof(error));
    CHECK(context);
    expect_ready_failure(context,NULL,error,"player spawn marker");
    expect_ready_failure(NULL,NULL,error,"pending source-stage numeric context");
    /* A live marker owner is not a numeric context. The active-owner guard
     * must reject it before reading the opaque pointer as a context object. */
    expect_ready_failure((MeleeWebStageNumeric*)(void*)markers,NULL,error,
                         "pending source-stage numeric context");
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
    for (unsigned i = 0; i < 4; ++i) if (i != 2) Ground_801C2D0C(i, root);
    for (unsigned i = 149; i <= 152; ++i) Ground_801C2D0C(i, root);
    stage_info.cam_info.cam_bounds = (StageBlastZone){-10, 10, 10, -10};
    stage_info.blast_zone = (StageBlastZone){-20, 20, 20, -20};
    expect_ready_failure(context,root,error,"player spawn marker");
    Ground_801C2D0C(2,root);
    expect_ready_failure(context,root,error,"camera/blast marker");
    Ground_801C2D0C(148,root);
    Ground_801C2D0C(152,NULL);
    expect_ready_failure(context,root,error,"camera/blast marker");
    Ground_801C2D0C(152,root);

    stage_info.cam_info.cam_bounds.left=NAN;
    expect_ready_failure(context,root,error,"nonfinite");
    stage_info.cam_info.cam_bounds=(StageBlastZone){10,-10,10,-10};
    expect_ready_failure(context,root,error,"inverted");
    stage_info.cam_info.cam_bounds=(StageBlastZone){-10,10,10,-10};
    stage_info.blast_zone.top=NAN;
    expect_ready_failure(context,root,error,"nonfinite");
    stage_info.blast_zone=(StageBlastZone){-20,20,-40,-30};
    expect_ready_failure(context,root,error,"inverted");
    stage_info.blast_zone=(StageBlastZone){-20,20,20,-20};

    CHECK(melee_web_stage_numeric_source_stage_ready(context, error, sizeof(error)));
    CHECK(!root->scl);
    expect_ready_failure(context,root,error,"pending source-stage numeric context");
    float position[3];
    CHECK(melee_web_stage_numeric_spawn(context, 0, position, error, sizeof(error)));
    CHECK(root->scl && position[0] == 7 && position[1] == 11 && position[2] == 13);
    HSD_JObjSetTranslateX(root, NAN);
    CHECK(!melee_web_stage_numeric_spawn(context, 0, position, error, sizeof(error)));
    CHECK(melee_web_stage_numeric_end(context, error, sizeof(error)));
    HSD_JObjRemoveAll(tree);
}
