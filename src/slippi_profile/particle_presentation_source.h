/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_PARTICLE_PRESENTATION_SOURCE_H
#define SLIPPI_PROFILE_PARTICLE_PRESENTATION_SOURCE_H

#include <slippi_profile/particle_presentation.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/psstructs.h>

/* This header is for the patched C source units only. The C++ checkpoint
 * includes particle_presentation.h, which keeps baselib's C ABI out of it. */
typedef struct SlippiParticlePreparedDispatch {
    uint8_t key;
    HSD_Particle* sorted[16];
    HSD_Particle* nonedge[16];
} SlippiParticlePreparedDispatch;

typedef enum SlippiParticlePresentationCacheSlot {
    SLIPPI_PARTICLE_CACHE_PREV_POINT_SIZE,
    SLIPPI_PARTICLE_CACHE_PREV_LINE_WIDTH,
    SLIPPI_PARTICLE_CACHE_BLEND_MODE,
    SLIPPI_PARTICLE_CACHE_CHAN_CTRL,
    SLIPPI_PARTICLE_CACHE_HSD_804D7914,
    SLIPPI_PARTICLE_CACHE_HSD_804D7918,
    SLIPPI_PARTICLE_CACHE_HSD_804D791C,
    SLIPPI_PARTICLE_CACHE_HSD_804D7920,
    SLIPPI_PARTICLE_CACHE_HSD_804D7924,
    SLIPPI_PARTICLE_CACHE_HSD_804D7928,
    SLIPPI_PARTICLE_CACHE_PREV_CHAN_MAT,
    SLIPPI_PARTICLE_CACHE_PREV_CHAN_AMB,
    SLIPPI_PARTICLE_CACHE_PREV_COLOR_PRIM,
    SLIPPI_PARTICLE_CACHE_PREV_COLOR_ENV,
    SLIPPI_PARTICLE_CACHE_PREV_COLOR_MAT,
    SLIPPI_PARTICLE_CACHE_PNMTX_SLOT0,
    SLIPPI_PARTICLE_CACHE_PNMTX_SLOT1,
    SLIPPI_PARTICLE_CACHE_VMTX,
    SLIPPI_PARTICLE_CACHE_RVMTX,
    SLIPPI_PARTICLE_CACHE_PVMTX,
    SLIPPI_PARTICLE_CACHE_PRJ,
    SLIPPI_PARTICLE_CACHE_COUNT,
} SlippiParticlePresentationCacheSlot;

/* Same-unit bridges: no source draw, camera setup or GPU call. */
uint8_t slippi_particle_source_frame_key(void);
int slippi_particle_source_preflight_visit(const struct SlippiParticlePassVisit*, uint8_t key);
int slippi_particle_source_prepare_visit(const struct SlippiParticlePassVisit*);
int slippi_particle_presentation_particle_require(HSD_Particle*);
int slippi_particle_presentation_app_future(HSD_psAppSRT*, unsigned key,
                                          const struct HSD_GObj* camera);
u32 (*slippi_particle_presentation_tev_slots(u32 (*)[2]))[2];
struct HSD_Fog;
struct HSD_Fog** slippi_particle_presentation_fog_slot(struct HSD_Fog**);
size_t slippi_particle_presentation_particle_bound(void);

int slippi_particle_presentation_pool_reinitialization_allowed(void);
int slippi_particle_presentation_pool_initialized(HSD_ObjAllocData* pool,
                                                   size_t extent,
                                                   unsigned align);
int slippi_particle_presentation_particle_created(HSD_Particle* particle,
                                                   HSD_ObjAllocData* pool);
int slippi_particle_presentation_particle_retiring(HSD_Particle* particle,
                                                    HSD_ObjAllocData* pool);
int slippi_particle_presentation_appsrt_pool_initialized(HSD_ObjAllocData* pool,
                                                          size_t extent,
                                                          unsigned align);
int slippi_particle_presentation_appsrt_created(HSD_psAppSRT* appsrt,
                                                 HSD_ObjAllocData* pool);
int slippi_particle_presentation_appsrt_retiring(HSD_psAppSRT* appsrt,
                                                  HSD_ObjAllocData* pool);
int slippi_particle_presentation_appsrt_attached(void* owner,
                                                  HSD_psAppSRT* appsrt,
                                                  unsigned kind);
int slippi_particle_presentation_appsrt_detaching(void* owner,
                                                  HSD_psAppSRT* appsrt,
                                                  unsigned kind);
int slippi_particle_presentation_source_mutation_allowed(void);
int slippi_particle_presentation_app_require(HSD_psAppSRT* appsrt);
void slippi_particle_presentation_app_prepared(HSD_psAppSRT* appsrt,
                                                unsigned key,
                                                int refreshed);
int slippi_particle_presentation_record_prepared(
    const struct SlippiParticlePassVisit* visit,
    const SlippiParticlePreparedDispatch* prepared);
int slippi_particle_presentation_preparing(void);
int slippi_particle_presentation_dispatch_active(void);
uint8_t* slippi_particle_presentation_frame_slot(uint8_t* fallback);
s32* slippi_particle_presentation_s32_slot(
    SlippiParticlePresentationCacheSlot slot, s32* fallback);
f32* slippi_particle_presentation_f32_slot(
    SlippiParticlePresentationCacheSlot slot, f32* fallback);
GXColor* slippi_particle_presentation_color_slot(
    SlippiParticlePresentationCacheSlot slot, GXColor* fallback);
Mtx* slippi_particle_presentation_mtx_slot(
    SlippiParticlePresentationCacheSlot slot, Mtx* fallback);
f32* slippi_particle_presentation_projection_slot(f32* fallback);
s32 (*slippi_particle_presentation_slot_array2(s32 (*fallback)[2]))[2];
uint32_t slippi_particle_presentation_failure(void);

#endif
