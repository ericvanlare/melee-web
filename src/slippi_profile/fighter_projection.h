/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_FIGHTER_PROJECTION_H
#define SLIPPI_FIGHTER_PROJECTION_H
#include <stddef.h>
#include <stdint.h>
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#ifdef __cplusplus
extern "C" {
#endif
int slippi_fighter_projection_world_begin(uint64_t);
int slippi_fighter_projection_world_end(uint64_t);
int slippi_fighter_projection_created(void*, const void*, size_t);
int slippi_fighter_projection_retiring(void*);
int slippi_fighter_projection_require(void*);
int slippi_fighter_projection_draw_begin(void);
int slippi_fighter_projection_draw_end(void);
#ifdef __cplusplus
}
#endif
#else
static inline int slippi_fighter_projection_world_begin(uint64_t w){(void)w;return 1;}
static inline int slippi_fighter_projection_world_end(uint64_t w){(void)w;return 1;}
static inline int slippi_fighter_projection_draw_begin(void){return 1;}
static inline int slippi_fighter_projection_draw_end(void){return 1;}
#endif
#endif
