#ifndef MELEE_WEB_GAMEPLAY_SOURCE_PRELOAD_H
#define MELEE_WEB_GAMEPLAY_SOURCE_PRELOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MeleeWebSourcePreloadStatus {
    MELEE_WEB_SOURCE_PRELOAD_INVALID = -1,
    MELEE_WEB_SOURCE_PRELOAD_ABSENT = 0,
    MELEE_WEB_SOURCE_PRELOAD_PRESENT = 1,
} MeleeWebSourcePreloadStatus;

/* Read the actual lbdvd.c static cache with its authored bound and lookup
 * predicate. The scalar predicate is exposed so synthetic controls can test
 * the source rule without writing or manufacturing a cache entry. */
int melee_web_source_preload_predicate(int state, int load_score,
                                       int entry_num, int requested_entry);
MeleeWebSourcePreloadStatus melee_web_source_preload_observe(
    int requested_entry);

/* Layout facts measured from grpstadium.c's private ImageDescWrapper in that
 * actual translation unit. The flag container fields are declaration-derived
 * candidates, not observations of the compiler's bitfield access width. This
 * never calls the constructor or reads an instance of the wrapper. */
typedef struct MeleeWebStadiumBufferLayout {
    uint32_t pointer_bytes;
    uint32_t image_desc_bytes;
    uint32_t wrapper_bytes;
    uint32_t desc_offset;
    uint32_t flag_container_candidate_offset;
    uint32_t flag_container_candidate_bytes;
    uint32_t x1a_offset;
    uint32_t x1c_offset;
    uint32_t constructor_allocation_bytes;
    uint32_t layout_prefix_bound_bytes;
} MeleeWebStadiumBufferLayout;

int melee_web_stadium_buffer_layout_read(MeleeWebStadiumBufferLayout* out);

#ifdef __cplusplus
}
#endif
#endif
