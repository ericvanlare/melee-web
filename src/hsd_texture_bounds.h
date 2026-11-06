#ifndef MELEE_WEB_HSD_TEXTURE_BOUNDS_H
#define MELEE_WEB_HSD_TEXTURE_BOUNDS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The original TObj owns its animation binding and releases it with the
 * object. Keep table capacities outside original HSD struct/ABI layouts. */
void melee_web_texture_bounds_bind(const void* object, uint32_t images, uint32_t palettes);
void melee_web_texture_bounds_remove(const void* object);
int melee_web_texture_index_valid(const void* object, uint32_t channel, float value);
uint32_t melee_web_texture_bounds_live(void);

/* The aggregate passes its existing checked nonzero live-world identity; this
 * leaf creates no independent owner registry. Snapshot state contains only
 * exact pointer keys and bounds, never pointed-to source bytes. */
typedef struct MeleeWebTextureBoundsSnapshot MeleeWebTextureBoundsSnapshot;
typedef struct MeleeWebTextureBoundsPrepared MeleeWebTextureBoundsPrepared;
/* Capture/prepare/admit only allocate or inspect host state and never mutate the
 * live registry. The immutable snapshot must outlive its prepared plan. */
MeleeWebTextureBoundsSnapshot* melee_web_texture_bounds_capture(uint64_t world);
MeleeWebTextureBoundsPrepared* melee_web_texture_bounds_prepare(
    const MeleeWebTextureBoundsSnapshot* snapshot, uint64_t world);
int melee_web_texture_bounds_admit(MeleeWebTextureBoundsPrepared* prepared, uint64_t world);
/* Compare semantic pointer/count membership, not bucket order or node address;
 * use only at aggregate capture/armed and restored PRE admissions. */
int melee_web_texture_bounds_matches_current(
    const MeleeWebTextureBoundsSnapshot* snapshot, uint64_t world);
/* After final aggregate admission, serialized with no intervening registry
 * mutation. This swaps roots without allocation/callback/recoverable failure;
 * invariant misuse aborts before touching roots. Destroy the plan afterward. */
void melee_web_texture_bounds_commit_prevalidated(MeleeWebTextureBoundsPrepared* prepared);
/* Both destroy functions accept NULL. Destroy the plan before its snapshot. */
void melee_web_texture_bounds_prepared_destroy(MeleeWebTextureBoundsPrepared* prepared);
void melee_web_texture_bounds_snapshot_destroy(MeleeWebTextureBoundsSnapshot* snapshot);
#ifdef __cplusplus
}
#endif
#endif
