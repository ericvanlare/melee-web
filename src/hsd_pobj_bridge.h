#ifndef MELEE_WEB_HSD_POBJ_BRIDGE_H
#define MELEE_WEB_HSD_POBJ_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GX enum values are stored as integers so the importer does not depend on
 * HSD's host object layout. Attributes must be in increasing GX attribute
 * order, from position through TEX7, with no GX_VA_NULL entry. */
enum { MELEE_WEB_POBJ_MAX_ATTRIBUTES = 12 };

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
 * Only the original cull flags are supported; both flags together skip drawing.
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

#ifdef __cplusplus
}
#endif

#endif
