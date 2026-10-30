/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_PARTICLE_PRESENTATION_H
#define SLIPPI_PROFILE_PARTICLE_PRESENTATION_H

#include <stddef.h>
#include <stdint.h>

struct HSD_GObj;
struct HSD_Particle;
struct HSD_psAppSRT;
struct SlippiParticlePassVisit;

/* Host-only proof that the particle and AppSRT source-pool incarnations were
 * observed at the checked CPU capture boundary. Counts are diagnostics, not
 * source allocator state or restore authority. */
typedef struct SlippiParticlePresentationAnchor {
    uint64_t world, serial, token;
    uint32_t particle_count, appsrt_count;
} SlippiParticlePresentationAnchor;

/* Fixed, lossless endpoint wire rows for the existing constructor receipts.
 * Local receipt/history serials remain explicit provenance; they are not
 * cross-instance particle semantics. Reserved fields must remain zero. */
#define SLIPPI_PARTICLE_CELL_ENDPOINT_SCHEMA 1u
typedef enum SlippiParticleCellRowKind {
    SLIPPI_PARTICLE_CELL_ROW_LIVE = 1,
    SLIPPI_PARTICLE_CELL_ROW_CAPTURED = 2,
} SlippiParticleCellRowKind;
typedef enum SlippiParticleCellObjectKind {
    SLIPPI_PARTICLE_CELL_OBJECT_PARTICLE = 0,
    SLIPPI_PARTICLE_CELL_OBJECT_APPSRT = 1,
} SlippiParticleCellObjectKind;
typedef struct SlippiParticleCellEndpointHeader {
    uint32_t schema, bytes, live_count, captured_count;
    uint64_t world, anchor_serial, anchor_token, history_serial;
    uint32_t reserved[4];
} SlippiParticleCellEndpointHeader;
typedef struct SlippiParticleCellEndpointRow {
    uint32_t schema, bytes, row_kind, index, object_kind, extent;
    uint64_t receipt_serial, object;
    uint64_t world, epoch, allocation, pool, descriptor;
    uint32_t source, cell_offset, stride, reserved;
} SlippiParticleCellEndpointRow;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlippiParticleDispatchResult {
    SLIPPI_PARTICLE_DISPATCH_REFUSED = -1,
    SLIPPI_PARTICLE_DISPATCH_SOURCE = 0,
    SLIPPI_PARTICLE_DISPATCH_ROUTED = 1,
} SlippiParticleDispatchResult;

#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE

/* Source ownership begins before the CPU interval and is revoked before
 * ordinary world teardown. These calls never adopt an already-live pool. */
int slippi_particle_presentation_world_begin(uint64_t world_generation);
int slippi_particle_presentation_world_end(uint64_t world_generation);
SlippiParticlePresentationAnchor slippi_particle_presentation_anchor_capture(void);
int slippi_particle_presentation_anchor_admit(
    SlippiParticlePresentationAnchor anchor);
int slippi_particle_presentation_cell_endpoint_header(
    SlippiParticlePresentationAnchor anchor,
    SlippiParticleCellEndpointHeader* out);
int slippi_particle_presentation_cell_endpoint_row(
    SlippiParticlePresentationAnchor anchor, uint32_t row_kind,
    uint32_t index, SlippiParticleCellEndpointRow* out);
void slippi_particle_presentation_anchor_disarm(
    SlippiParticlePresentationAnchor anchor);

/* Called once at the complete match-only post-source boundary. The supplied
 * visits must be the complete, ordered schedule for this source occurrence.
 * interval_token==0 is an ordinary pre-rollback source tick and is a no-op. */
int slippi_particle_presentation_prepare(
    const struct SlippiParticlePassVisit* visits, size_t visit_count,
    uint64_t world_generation, uint64_t interval_token, char* error,
    size_t error_size);

/* Root brackets the complete prepared draw and each actual source camera.
 * The callback hooks validate every manager/code/link visit in order. */
int slippi_particle_presentation_draw_begin(uint64_t world_generation,
                                             uint64_t interval_token,
                                             char* error,
                                             size_t error_size);
int slippi_particle_presentation_camera_begin(const struct HSD_GObj* camera,
                                               char* error,
                                               size_t error_size);
int slippi_particle_presentation_camera_end(const struct HSD_GObj* camera,
                                             char* error,
                                             size_t error_size);
int slippi_particle_presentation_draw_end(char* error, size_t error_size);

/* Called only after checked source restore has committed successfully. It
 * restores host-side constructor receipts to the captured pool incarnations. */
int slippi_particle_presentation_reset_after_restore(
    SlippiParticlePresentationAnchor anchor);
/* Explicitly disarm host routing before ordinary teardown, including when the
 * CPU interval failed. */
int slippi_particle_presentation_interval_end(uint64_t world_generation,
                                               uint64_t interval_token);

/* Private source-patch bridge. It is called by the real efLib render callback
 * before its original psDispParticles call. */
SlippiParticleDispatchResult slippi_particle_presentation_dispatch_begin(
    const struct HSD_GObj* manager, unsigned code, unsigned gx_link,
    unsigned target_mask, char* error, size_t error_size);
int slippi_particle_presentation_dispatch_end(char* error,
                                               size_t error_size);

/* Supplies the prepared source key and sort heads to the actual psdisp leaf.
 * A non-routed result is never silently treated as a private-profile hit. */
SlippiParticleDispatchResult slippi_particle_presentation_render_data(
    unsigned target_link, unsigned code, uint8_t* key,
    struct HSD_Particle** sorted, struct HSD_Particle** nonedge);
/* Source psdisp bridge asks for a stack-local render projection. It may return
 * the canonical object only for an authenticated original cache hit. */
struct HSD_psAppSRT* slippi_particle_presentation_app_srt_view(
    struct HSD_psAppSRT* canonical, struct HSD_psAppSRT* scratch);

#else

static inline int slippi_particle_presentation_world_begin(uint64_t world)
{ (void) world; return 1; }
static inline int slippi_particle_presentation_world_end(uint64_t world)
{ (void) world; return 1; }
static inline SlippiParticlePresentationAnchor
slippi_particle_presentation_anchor_capture(void)
{ SlippiParticlePresentationAnchor a = {0, 0, 0, 0, 0}; return a; }
static inline int slippi_particle_presentation_anchor_admit(
    SlippiParticlePresentationAnchor anchor)
{ (void) anchor; return 1; }
static inline int slippi_particle_presentation_cell_endpoint_header(
    SlippiParticlePresentationAnchor anchor,
    SlippiParticleCellEndpointHeader* out)
{ (void) anchor; if (out) *out = (SlippiParticleCellEndpointHeader){0}; return 0; }
static inline int slippi_particle_presentation_cell_endpoint_row(
    SlippiParticlePresentationAnchor anchor, uint32_t row_kind,
    uint32_t index, SlippiParticleCellEndpointRow* out)
{ (void) anchor; (void) row_kind; (void) index; if (out) *out = (SlippiParticleCellEndpointRow){0}; return 0; }
static inline void slippi_particle_presentation_anchor_disarm(
    SlippiParticlePresentationAnchor anchor)
{ (void) anchor; }
static inline int slippi_particle_presentation_prepare(
    const struct SlippiParticlePassVisit* visits, size_t count, uint64_t world,
    uint64_t interval, char* error, size_t error_size)
{
    (void) visits; (void) count; (void) world; (void) interval;
    (void) error; (void) error_size; return 1;
}
static inline int slippi_particle_presentation_draw_begin(
    uint64_t world, uint64_t interval, char* error, size_t error_size)
{
    (void) world; (void) interval; (void) error; (void) error_size; return 1;
}
static inline int slippi_particle_presentation_camera_begin(
    const struct HSD_GObj* camera, char* error, size_t error_size)
{
    (void) camera; (void) error; (void) error_size; return 1;
}
static inline int slippi_particle_presentation_camera_end(
    const struct HSD_GObj* camera, char* error, size_t error_size)
{
    (void) camera; (void) error; (void) error_size; return 1;
}
static inline int slippi_particle_presentation_draw_end(char* error,
                                                         size_t error_size)
{
    (void) error; (void) error_size; return 1;
}
static inline int slippi_particle_presentation_reset_after_restore(
    SlippiParticlePresentationAnchor anchor)
{ (void) anchor; return 1; }
static inline int slippi_particle_presentation_interval_end(uint64_t world,
                                                            uint64_t interval)
{ (void) world; (void) interval; return 1; }
static inline SlippiParticleDispatchResult
slippi_particle_presentation_dispatch_begin(const struct HSD_GObj* manager,
                                           unsigned code,
                                           unsigned gx_link,
                                           unsigned target_mask,
                                           char* error,
                                           size_t error_size)
{
    (void) manager; (void) code; (void) gx_link; (void) target_mask;
    (void) error; (void) error_size; return SLIPPI_PARTICLE_DISPATCH_SOURCE;
}
static inline int slippi_particle_presentation_dispatch_end(char* error,
                                                             size_t error_size)
{
    (void) error; (void) error_size; return 1;
}
static inline SlippiParticleDispatchResult
slippi_particle_presentation_render_data(unsigned target, unsigned code,
                                        uint8_t* key,
                                        struct HSD_Particle** sorted,
                                        struct HSD_Particle** nonedge)
{
    (void) target; (void) code; (void) key; (void) sorted; (void) nonedge;
    return SLIPPI_PARTICLE_DISPATCH_SOURCE;
}
static inline struct HSD_psAppSRT*
slippi_particle_presentation_app_srt_view(struct HSD_psAppSRT* canonical,
                                          struct HSD_psAppSRT* scratch)
{ (void) scratch; return canonical; }

#endif

#ifdef __cplusplus
}
#endif

#endif
