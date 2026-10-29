/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_TOBJ_PRESENTATION_CACHE_H
#define SLIPPI_TOBJ_PRESENTATION_CACHE_H
#include <stdint.h>

/* Keep layout stable for the C++ checkpoint; serial/count/reserved are zeroed
 * compatibility fields, not TObj inventory or lifetime evidence. */
typedef struct SlippiTObjAnchor {
    uint64_t world, serial, token;
    uint32_t count, reserved;
} SlippiTObjAnchor;

#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#ifndef __cplusplus
#include <sysdolphin/baselib/tobj.h>
#else
extern "C" {
#endif
int slippi_tobj_world_begin(uint64_t world);
void slippi_tobj_world_end(uint64_t world);
void slippi_tobj_descriptor_attest(void* info);
void slippi_tobj_selected(void* object, void* info);
void slippi_tobj_loaded(void* object);
void slippi_tobj_rebound(void* object, void* previous, void* next);
void slippi_tobj_amnesia(void* info);
void slippi_tobj_unsupported_write(void* object);
/* Internal gate used only at original TObjSetupMtx. Returns 1 for canonical
 * UV/Reflection scratch, 0 for original Shadow/Toon, and fails closed for
 * custom/unknown roles. It does not establish arbitrary-object Load lineage. */
int slippi_tobj_managed_require(void* object);
SlippiTObjAnchor slippi_tobj_anchor_capture(void);
int slippi_tobj_anchor_admit(SlippiTObjAnchor anchor);
void slippi_tobj_anchor_disarm(SlippiTObjAnchor anchor);
uint32_t slippi_tobj_failure(void);
#ifdef __cplusplus
}
#endif
#else
static inline int slippi_tobj_world_begin(uint64_t w) {
    (void) w;
    return 1;
}
static inline void slippi_tobj_world_end(uint64_t w) { (void) w; }
static inline SlippiTObjAnchor slippi_tobj_anchor_capture(void) {
    SlippiTObjAnchor a = {0, 0, 0, 0, 0};
    return a;
}
static inline int slippi_tobj_anchor_admit(SlippiTObjAnchor a) {
    (void) a;
    return 1;
}
static inline void slippi_tobj_anchor_disarm(SlippiTObjAnchor a) { (void) a; }
static inline uint32_t slippi_tobj_failure(void) { return 0; }
#endif
#endif
