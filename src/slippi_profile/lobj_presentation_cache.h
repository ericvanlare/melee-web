/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_LOBJ_PRESENTATION_CACHE_H
#define SLIPPI_LOBJ_PRESENTATION_CACHE_H
#include <stdint.h>
typedef struct SlippiLObjAnchor {
    uint64_t world, serial, token;
    uint32_t count, reserved;
} SlippiLObjAnchor;
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
#ifndef __cplusplus
#include <sysdolphin/baselib/lobj.h>
typedef struct SlippiLObjCache {
    GXColor hw_color;
    float shininess;
    Vec3 lvec;
    GXLightID id;
    GXLightObj lightobj;
    GXLightID spec_id;
    GXLightObj spec_lightobj;
    uint16_t dirty;
} SlippiLObjCache;
#else
typedef struct SlippiLObjCache SlippiLObjCache;
extern "C" {
#endif
/* Only actual source initializer/constructor/lifetime seams call these. No
 * exports, pointer adoption or cache restore interface. */
int slippi_lobj_world_begin(uint64_t world);
void slippi_lobj_world_end(uint64_t world);
void slippi_lobj_descriptor_attest(void* info);
void slippi_lobj_constructed(void* object, void* info);
void slippi_lobj_retiring(void* object);
void slippi_lobj_rebound(void* object, void* previous, void* next);
void slippi_lobj_amnesia(void* info);
void slippi_lobj_require_descriptor(void* info);
SlippiLObjCache* slippi_lobj_cache_require(void* object);
void slippi_lobj_dirty_setter(uint32_t flags);
SlippiLObjAnchor slippi_lobj_anchor_capture(void);
int slippi_lobj_anchor_admit(SlippiLObjAnchor anchor);
void slippi_lobj_anchor_disarm(SlippiLObjAnchor anchor);
uint32_t slippi_lobj_failure(void);
#ifdef __cplusplus
}
#endif
#else
static inline int slippi_lobj_world_begin(uint64_t world){(void)world;return 1;}
static inline void slippi_lobj_world_end(uint64_t world){(void)world;}
static inline SlippiLObjAnchor slippi_lobj_anchor_capture(void){SlippiLObjAnchor a={0,0,0,0,0};return a;}
static inline int slippi_lobj_anchor_admit(SlippiLObjAnchor a){(void)a;return 1;}
static inline void slippi_lobj_anchor_disarm(SlippiLObjAnchor a){(void)a;}
static inline uint32_t slippi_lobj_failure(void){return 0;}
#endif
#endif
