/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_CAMERA_SOURCE_PREPARE_H
#define SLIPPI_PROFILE_CAMERA_SOURCE_PREPARE_H
#include <stddef.h>
#include <stdint.h>
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
#include <dolphin/mtx.h>
#include <melee/cm/types.h>
#include <sysdolphin/baselib/cobj.h>
#endif
struct HSD_GObj;
#ifdef __cplusplus
extern "C" {
#endif
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
int slippi_camera_source_prepare(uint64_t world, uint64_t token,
                                 struct HSD_GObj** main_camera,
                                 char* error, size_t error_size);
int slippi_camera_source_project_subject(HSD_CObj* camera, CmSubject* subject,
    S32Vec2* screen, int* wrote, int* culled, int* draw);
#else
static inline int slippi_camera_source_prepare(uint64_t w, uint64_t t,
 struct HSD_GObj** camera, char* e, size_t n)
{ (void)w; (void)t; (void)camera; (void)e; (void)n; return 1; }
#endif
#ifdef __cplusplus
}
#endif
#endif
