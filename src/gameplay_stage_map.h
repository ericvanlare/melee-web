#ifndef MELEE_WEB_GAMEPLAY_STAGE_MAP_H
#define MELEE_WEB_GAMEPLAY_STAGE_MAP_H
#include <stddef.h>
#include <stdint.h>
#include "gameplay_archive_sections.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebStageMap MeleeWebStageMap;
/* Publish an already checked, completely hydrated original UnkStageDat graph.
 * Borrows all descriptors; no raw HSD_Archive handle is manufactured. This is
 * map storage readiness only. Call original stage initialization separately
 * after its particle, collision, animation and camera services are ready. */
MeleeWebStageMap* melee_web_stage_map_publish(void* native_map_head,char*,size_t);
/* Copies the checked public catalog and owns its opaque archive handle until
 * all source stage consumers are removed. Descriptor graphs remain borrowed. */
int melee_web_stage_map_set_public(MeleeWebStageMap*,const MeleeWebArchiveSymbol*,size_t,char*,size_t);
typedef struct MeleeWebMapLightOverride {void* descriptor; int found; uint8_t flags;} MeleeWebMapLightOverride;
int melee_web_stage_map_set_overrides(MeleeWebStageMap*,const MeleeWebMapLightOverride*,size_t,char*,size_t);
int melee_web_stage_map_lookup_override(void*,int*,uint8_t*);
int melee_web_stage_map_close(MeleeWebStageMap*,char*,size_t);
/* Original grDatFiles storage adapters. NULL means no owned native context. */
void* melee_web_stage_map_archives(void);
void* melee_web_stage_map_lookup(int map_id);
#ifdef __cplusplus
}
#endif
#endif
