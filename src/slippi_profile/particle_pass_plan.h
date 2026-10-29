/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_PARTICLE_PASS_PLAN_H
#define SLIPPI_PROFILE_PARTICLE_PASS_PLAN_H

#include <stddef.h>
#include <sysdolphin/baselib/gobj.h>

/* The retail default is 0x3F; gobjinit.c allocates max_link + 2 entries,
 * including the next_gx sentinel. Refuse a different runtime configuration. */
enum {
    SLIPPI_PARTICLE_GX_LINK_MAX = 0x3F,
    SLIPPI_PARTICLE_GX_LINK_SLOTS = SLIPPI_PARTICLE_GX_LINK_MAX + 2,
    SLIPPI_PARTICLE_GX_MODEL = 7,
    SLIPPI_PARTICLE_GX_AUX = 8,
    SLIPPI_PARTICLE_GX_MODEL_MASK = 1u << SLIPPI_PARTICLE_GX_MODEL,
    SLIPPI_PARTICLE_GX_AUX_MASK = 1u << SLIPPI_PARTICLE_GX_AUX,
    SLIPPI_PARTICLE_TARGET_MODEL_MASK = (1u << 0) | (1u << 2),
    SLIPPI_PARTICLE_TARGET_AUX_MASK = 1u << 1,
};

typedef enum {
    SLIPPI_PARTICLE_CAMERA_MAIN_TAIL,
    SLIPPI_PARTICLE_CAMERA_BACKGROUND_FLASH,
    SLIPPI_PARTICLE_CAMERA_GROUND_ZERO,
    SLIPPI_PARTICLE_CAMERA_MAGNIFY_ZERO,
    SLIPPI_PARTICLE_CAMERA_HUD,
    SLIPPI_PARTICLE_CAMERA_NAMETAG_ZERO,
} SlippiParticleCameraKind;

typedef struct SlippiParticlePassVisit {
    SlippiParticleCameraKind camera_kind;
    const HSD_GObj* camera;
    unsigned code;
    unsigned gx_link;
    unsigned target_mask;
    const HSD_GObj* manager;
} SlippiParticlePassVisit;

/* All pointers in this view must come from a same-source-unit adapter bound to
 * the live original objects/functions. This planner owns no GObj state and
 * calls no render callback, CObj setup, GX operation, or source draw routine. */
typedef struct {
    const HSD_GObj* main_camera;
    const HSD_GObj* background_flash_camera;
    const HSD_GObj* hud_camera;
    const HSD_GObj* nametag_camera;
    const HSD_GObj* ground_camera;
    const HSD_GObj* magnify_camera;
    const void* main_cobj;
    const void* background_flash_cobj;
    const void* hud_cobj;
    const void* nametag_cobj;
    const void* ground_cobj;
    const void* magnify_cobj;
    GObj_RenderFunc main_callback;
    GObj_RenderFunc background_flash_callback;
    GObj_RenderFunc hud_callback;
    GObj_RenderFunc nametag_callback;
    GObj_RenderFunc ground_callback;
    GObj_RenderFunc magnify_callback;
    HSD_GObjEvent main_process_callback;
    GObj_RenderFunc particle_callback;
    HSD_GObjEvent model_manager_process;
    HSD_GObjEvent aux_manager_process;
    HSD_GObj* const* gx_heads;
    HSD_GObj* const* gx_tails;
    size_t gx_slot_count;
    unsigned gx_link_max;
    unsigned live_gobj_bound;
    unsigned main_x398_b3;
    unsigned hud_hidden;
    /* Adapter proves the owned CObj is on the original SCREEN render pass.
     * A null CObj or a different pass is outside this first capability. */
    int screen_render_pass;
} SlippiParticlePassSource;

/* Plans one camera only. On success, writes the immutable visits and count. On
 * refusal, visits and *visit_count remain untouched. Before runtime activation,
 * the caller must traverse the complete source camera next_gx chain, preflight
 * every per-camera plan, then execute only while that schedule remains stable.
 * This selector owns no game snapshot and cannot roll engine state back. */
int slippi_profile_particle_pass_plan(
    const HSD_GObj* camera, const SlippiParticlePassSource* source,
    SlippiParticlePassVisit* visits, size_t visit_capacity,
    size_t* visit_count, char* error, size_t error_size);

/* Source adapter variant that also returns the exact GX links selected by the
 * original camera dispatch. The output is untouched on refusal. */
int slippi_profile_particle_pass_plan_with_mask(
    const HSD_GObj* camera, const SlippiParticlePassSource* source,
    SlippiParticlePassVisit* visits, size_t visit_capacity,
    size_t* visit_count, unsigned* selected_link_mask, char* error,
    size_t error_size);

/* Shared source proof for registered GX11 JObj display callbacks. */
int slippi_profile_particle_pass_jobj_display_safe(
    const HSD_GObj* owner, char* error, size_t error_size);

#endif
