/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_PARTICLE_SOURCE_SCHEDULE_H
#define SLIPPI_PROFILE_PARTICLE_SOURCE_SCHEDULE_H

#include <stddef.h>
#include <stdint.h>
#include "particle_presentation.h"

struct HSD_GObj;

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE

/* Runtime wrappers for the source-owned match schedule. The source adapter
 * must build its complete camera/callback signature before prepare performs
 * any canonical source preparation. These calls own no game snapshot. */
/* Exact IfCoGet/DevText constructors and actual GObj destruction observations. No
 * capture-time adoption; the single receipt ends before the source owner frees. */
int slippi_particle_source_ifcoget_constructing(void);
int slippi_particle_source_devtext_constructing(int classifier, int p_link,
                                                int priority, int gx_link,
                                                unsigned camera_priority);
int slippi_particle_source_devtext_created(struct HSD_GObj* camera);
int slippi_particle_source_ifcoget_created(struct HSD_GObj* camera);
int slippi_particle_source_gobj_retiring(const struct HSD_GObj* object);
int slippi_particle_source_world_begin(uint64_t world_generation);
int slippi_particle_source_world_end(uint64_t world_generation);
int slippi_particle_source_prepare(uint64_t world_generation,
                                   uint64_t interval_token, char* error,
                                   size_t error_size);
int slippi_particle_source_draw_begin(uint64_t world_generation,
                                      uint64_t interval_token, char* error,
                                      size_t error_size);
int slippi_particle_source_camera_begin(const struct HSD_GObj* camera,
                                        char* error, size_t error_size);
int slippi_particle_source_camera_end(const struct HSD_GObj* camera,
                                      char* error, size_t error_size);
int slippi_particle_source_draw_end(char* error, size_t error_size);

/* Caller must have admitted the captured anchor and completed the checked
 * CPU commit before this function. It discards the old source schedule and
 * restores only host presentation receipts; it cannot restore engine state. */
int slippi_particle_source_reset_after_restore(
    SlippiParticlePresentationAnchor anchor);
int slippi_particle_source_interval_end(uint64_t world_generation,
                                       uint64_t interval_token);

#else

static inline int slippi_particle_source_ifcoget_constructing(void) { return 1; }
static inline int slippi_particle_source_devtext_constructing(
    int classifier, int p_link, int priority, int gx_link, unsigned camera_priority)
{
    (void) classifier; (void) p_link; (void) priority; (void) gx_link;
    (void) camera_priority; return 1;
}
static inline int slippi_particle_source_devtext_created(struct HSD_GObj* camera)
{ (void) camera; return 1; }
static inline int slippi_particle_source_ifcoget_created(struct HSD_GObj* camera)
{ (void) camera; return 1; }
static inline int slippi_particle_source_gobj_retiring(const struct HSD_GObj* object)
{ (void) object; return 1; }
static inline int slippi_particle_source_world_begin(uint64_t world)
{ (void) world; return 1; }
static inline int slippi_particle_source_world_end(uint64_t world)
{ (void) world; return 1; }
static inline int slippi_particle_source_prepare(uint64_t world,
                                                uint64_t interval,
                                                char* error, size_t size)
{
    (void) world; (void) interval; (void) error; (void) size; return 1;
}
static inline int slippi_particle_source_draw_begin(uint64_t world,
                                                    uint64_t interval,
                                                    char* error, size_t size)
{
    (void) world; (void) interval; (void) error; (void) size; return 1;
}
static inline int slippi_particle_source_camera_begin(
    const struct HSD_GObj* camera, char* error, size_t size)
{
    (void) camera; (void) error; (void) size; return 1;
}
static inline int slippi_particle_source_camera_end(
    const struct HSD_GObj* camera, char* error, size_t size)
{
    (void) camera; (void) error; (void) size; return 1;
}
static inline int slippi_particle_source_draw_end(char* error, size_t size)
{ (void) error; (void) size; return 1; }
static inline int slippi_particle_source_reset_after_restore(
    SlippiParticlePresentationAnchor anchor)
{ (void) anchor; return 1; }
static inline int slippi_particle_source_interval_end(uint64_t world,
                                                      uint64_t interval)
{ (void) world; (void) interval; return 1; }

#endif

#ifdef __cplusplus
}
#endif

#endif
