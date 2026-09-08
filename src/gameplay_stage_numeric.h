#ifndef MELEE_WEB_GAMEPLAY_STAGE_NUMERIC_H
#define MELEE_WEB_GAMEPLAY_STAGE_NUMERIC_H
#include "native_dat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebStageMarkers MeleeWebStageMarkers;
typedef struct MeleeWebStageNumeric MeleeWebStageNumeric;
/* Strict Final Destination marker-only tree. The reader arena must outlive the
 * context and its original HSD objects. No rendering/stage on_init is invoked. */
MeleeWebStageMarkers* melee_web_stage_markers_decode(const MeleeWebNativeDat*,uint32_t map_head);
/* Borrowed original HSD_Joint descriptor for a complete native map owner. */
void* melee_web_stage_markers_descriptor(MeleeWebStageMarkers*);
/* Requires the decoded GroundParam already published. Retains a full StageInfo
 * snapshot: close before removing any pre-existing stage resource owners. */
MeleeWebStageNumeric* melee_web_stage_numeric_begin(MeleeWebStageMarkers*,char*,size_t);
int melee_web_stage_numeric_bounds(MeleeWebStageNumeric*,float camera[4],float blast[4],float offset[2],char*,size_t);
/* Reads one of the four original player spawn markers while this numeric
 * stage scope owns the published Ground marker table. */
int melee_web_stage_numeric_spawn(MeleeWebStageNumeric*,uint32_t slot,float position[3],char*,size_t);
int melee_web_stage_numeric_end(MeleeWebStageNumeric*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
