#ifndef MELEE_WEB_GAMEPLAY_STAGE_VISUAL_H
#define MELEE_WEB_GAMEPLAY_STAGE_VISUAL_H
#include "hsd_native_joint.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebStageVisual MeleeWebStageVisual;
/* Selected map-entry visual, not grLast OnInit or complete stage publication.
 * Uses original Ground construction, animation/material procs and grDisplay.
 * Native model and animation descriptor owners must outlive this scope.
 * Caller must publish actual Ground parameters first and separately handle
 * entry joint references, cameras, lights, fog and stage-specific callbacks.
 * Final Destination entry3 animation0 needs only joint/material descriptors;
 * its background state machine is a separate, still-required stage service. */
MeleeWebStageVisual* melee_web_stage_visual_begin(MeleeWebNativeJoint* model,
    void* joint_animation,void* material_animation,int map_id,unsigned camera_pass,
    char* error,size_t error_size);
void* melee_web_stage_visual_object(MeleeWebStageVisual*,char*,size_t);
/* End after final GPU submission, before releasing descriptor owners or world. */
int melee_web_stage_visual_end(MeleeWebStageVisual*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
