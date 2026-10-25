/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_TOBJ_PRESENTATION_CACHE_H
#define SLIPPI_TOBJ_PRESENTATION_CACHE_H
#include <stdint.h>
typedef struct SlippiTObjAnchor { uint64_t world, serial, token; uint32_t count, reserved; } SlippiTObjAnchor;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#ifndef __cplusplus
#include <sysdolphin/baselib/tobj.h>
#else
extern "C" {
#endif
int slippi_tobj_world_begin(uint64_t world);
void slippi_tobj_world_end(uint64_t world);
void slippi_tobj_descriptor_attest(void* info);
void slippi_tobj_constructed(void* object, void* info);
void slippi_tobj_selected(void* object, void* info);
void slippi_tobj_loaded(void* object);
void slippi_tobj_shadow(void* object);
void slippi_tobj_retiring(void* object);
void slippi_tobj_rebound(void* object, void* previous, void* next);
void slippi_tobj_amnesia(void* info);
void slippi_tobj_unsupported_write(void* object);
/* Returns 1 for admitted managed UV/TOON, 0 ONLY for named original Shadow.
 * Unknown/pending/custom/poisoned is an explicit panic, never fallback. */
int slippi_tobj_managed_require(void* object);
#ifndef __cplusplus
MtxPtr slippi_tobj_matrix_require(void* object);
#endif
uint32_t slippi_tobj_effective_flags(void* object);
void slippi_tobj_authored_dirty(void* object);
void slippi_tobj_consumed(void* object);
SlippiTObjAnchor slippi_tobj_anchor_capture(void);
int slippi_tobj_anchor_admit(SlippiTObjAnchor anchor);
void slippi_tobj_anchor_disarm(SlippiTObjAnchor anchor);
uint32_t slippi_tobj_failure(void);
#ifdef __cplusplus
}
#endif
#else
static inline int slippi_tobj_world_begin(uint64_t w){(void)w;return 1;}
static inline void slippi_tobj_world_end(uint64_t w){(void)w;}
static inline SlippiTObjAnchor slippi_tobj_anchor_capture(void){SlippiTObjAnchor a={0,0,0,0,0};return a;}
static inline int slippi_tobj_anchor_admit(SlippiTObjAnchor a){(void)a;return 1;}
static inline void slippi_tobj_anchor_disarm(SlippiTObjAnchor a){(void)a;}
static inline uint32_t slippi_tobj_failure(void){return 0;}
#endif
#endif
