/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_FIGHTER_PROJECTION_H
#define SLIPPI_FIGHTER_PROJECTION_H
#include <stddef.h>
#include <stdint.h>
struct HSD_GObj;
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#ifdef __cplusplus
extern "C" {
#endif
int slippi_fighter_projection_world_begin(uint64_t);
int slippi_fighter_projection_world_end(uint64_t);
int slippi_fighter_projection_created(void*, const void*, size_t);
int slippi_fighter_projection_shadow_retiring(void*, void*);
int slippi_fighter_projection_retiring(void*);
int slippi_fighter_projection_require(void*);
int slippi_fighter_projection_draw_begin(void);
int slippi_fighter_projection_draw_end(void);
int slippi_fighter_projection_private_arm(uint64_t world, uint32_t source_frame);
int slippi_fighter_projection_source_prepare(uint64_t world, uint64_t token,
    struct HSD_GObj* main_camera, struct HSD_GObj* hud_camera,
    struct HSD_GObj* magnifier_camera, unsigned hud_hidden);
int slippi_fighter_projection_magnifier_private_draw(void);
int slippi_fighter_projection_private_armed(void);
/* These hooks return -1 outside an armed draw, 1 when the operation was
 * projected/authorized, and 0 after a sticky refusal. */
int slippi_fighter_projection_shadow_members(void*, void*, int);
int slippi_fighter_projection_shadow_active(void*, int);
int slippi_fighter_projection_shadow_texture_operation(void*, void*, int);
int slippi_fighter_projection_mobj_preflight(void*, void*);
int slippi_fighter_projection_mobj_compile(void);
int slippi_fighter_projection_mobj_toon_link(void*, void*);
const char* slippi_fighter_projection_failure_reason(void);
#ifdef __cplusplus
}
#endif
#else
static inline int slippi_fighter_projection_world_begin(uint64_t w){(void)w;return 1;}
static inline int slippi_fighter_projection_world_end(uint64_t w){(void)w;return 1;}
static inline int slippi_fighter_projection_shadow_retiring(void* lb,void* s){(void)lb;(void)s;return 1;}
static inline int slippi_fighter_projection_draw_begin(void){return 1;}
static inline int slippi_fighter_projection_draw_end(void){return 1;}
static inline int slippi_fighter_projection_private_arm(uint64_t w,uint32_t f)
{(void)w;(void)f;return 1;}
static inline int slippi_fighter_projection_source_prepare(uint64_t w,uint64_t t,
 struct HSD_GObj* m,struct HSD_GObj* h,struct HSD_GObj* z,unsigned x)
{(void)w;(void)t;(void)m;(void)h;(void)z;(void)x;return 1;}
static inline int slippi_fighter_projection_magnifier_private_draw(void){return 0;}
static inline int slippi_fighter_projection_private_armed(void){return 0;}
static inline const char* slippi_fighter_projection_failure_reason(void){return "profile projection detail unavailable";}
#endif
#endif
