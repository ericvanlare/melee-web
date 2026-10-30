/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_CAMERA_SOURCE_PREPARE_H
#define SLIPPI_PROFILE_CAMERA_SOURCE_PREPARE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
int slippi_camera_source_prepare(uint64_t world, uint64_t token,
                                 char* error, size_t error_size);
#else
static inline int slippi_camera_source_prepare(uint64_t w, uint64_t t, char* e, size_t n)
{ (void)w; (void)t; (void)e; (void)n; return 1; }
#endif
#ifdef __cplusplus
}
#endif
#endif
