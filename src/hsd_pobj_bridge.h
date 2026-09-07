#ifndef MELEE_WEB_HSD_POBJ_BRIDGE_H
#define MELEE_WEB_HSD_POBJ_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GX enum values are stored as integers so the importer does not depend on
 * HSD's host object layout. Attributes must be in increasing GX attribute
 * order, from matrix indices (envelopes only) through TEX7, without GX_VA_NULL. */
enum { MELEE_WEB_POBJ_MAX_ATTRIBUTES = 21,
       MELEE_WEB_POBJ_MAX_PALETTE = 10,
       MELEE_WEB_POBJ_MAX_INFLUENCES = 31,
       MELEE_WEB_SKIN_MAX_JOINTS = 256 };

typedef struct MeleeWebPObjAttribute {
    uint32_t attr;
    uint32_t attr_type;
    uint32_t comp_cnt;
    uint32_t comp_type;
    uint8_t frac;
    uint16_t stride;
    const void* data;
    uint32_t byte_size;
} MeleeWebPObjAttribute;

typedef struct MeleeWebPObjView {
    const MeleeWebPObjAttribute* attributes;
    uint32_t attribute_count;
    const void* display;
    uint32_t display_byte_size;
    uint16_t flags;
} MeleeWebPObjView;

/* Draw a rigid, unanimated PObj through original HSD descriptor/display-list
 * code. Matrix, lighting and material state must already be set by the caller.
 * Original cull flags and source-inert bit0 are supported; both cull flags skip drawing.
 * Display and indexed attribute bytes remain in GameCube big-endian format.
 *
 * The importer MUST first validate primitive packets, complete display-list
 * payloads, indices, finite geometry, and each buffer's actual allocation span.
 * This function checks the descriptor boundary, not arbitrary bytecode safety.
 * All buffers must stay alive through Aurora's frame submission; rendering is
 * single-threaded, as are the original HSD vertex descriptor caches.
 * Returns 1 for an accepted request, 0 with a diagnostic for invalid metadata.
 */
int melee_web_pobj_draw(const MeleeWebPObjView* view, char* error,
                        size_t error_size);

/* Ordinary static/animated SRT is evaluated by the transform bridge before
 * updating these owned HSD joints. UINT32_MAX denotes a root parent. */
typedef struct MeleeWebSkinJoint {
    uint32_t flags, parent;
    float world[3][4];
    float inverse_bind[3][4];
    uint8_t has_inverse_bind;
} MeleeWebSkinJoint;
typedef struct MeleeWebSkinSkeleton MeleeWebSkinSkeleton;
typedef struct MeleeWebSkinInfluence {
    uint32_t joint;
    float weight;
} MeleeWebSkinInfluence;
typedef struct MeleeWebSkinEnvelope {
    const MeleeWebSkinInfluence* influences;
    uint32_t influence_count;
} MeleeWebSkinEnvelope;
typedef struct MeleeWebPObjPalette {
    uint32_t count, normal_mask, texture_mask;
    float position[MELEE_WEB_POBJ_MAX_PALETTE][3][4];
    float normal[MELEE_WEB_POBJ_MAX_PALETTE][3][4];
    float texture[MELEE_WEB_POBJ_MAX_PALETTE][3][4];
} MeleeWebPObjPalette;

MeleeWebSkinSkeleton* melee_web_skin_create(const MeleeWebSkinJoint* joints,
    uint32_t count, char* error, size_t error_size);
int melee_web_skin_update(MeleeWebSkinSkeleton* skeleton,
    const MeleeWebSkinJoint* joints, uint32_t count, char* error, size_t error_size);
void melee_web_skin_destroy(MeleeWebSkinSkeleton* skeleton);

/* Original SetupEnvelopeModelMtx computes these matrices, captured instead of
 * uploaded during validation. Identity camera yields world positions for
 * conservative bounds. For rendering, call after material setup so original
 * HSD reflection queries determine the normal/texture matrix requirements. */
int melee_web_pobj_prepare_palette(const MeleeWebSkinSkeleton* skeleton,
    uint32_t mesh_joint, const MeleeWebSkinEnvelope* envelopes, uint32_t count,
    const float camera[3][4], uint32_t rendermode,
    MeleeWebPObjPalette* output, char* error, size_t error_size);
int melee_web_pobj_draw_palette(const MeleeWebPObjView* view,
    const MeleeWebPObjPalette* palette, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
