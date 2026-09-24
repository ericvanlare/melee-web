#ifndef MELEE_WEB_GAMEPLAY_SAVE_PROFILE_H
#define MELEE_WEB_GAMEPLAY_SAVE_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebSaveProfileOwner MeleeWebSaveProfileOwner;

/* This is the transient block cleared by the original gm_80172174.  It
 * contains pending trophy bits and the source trophy timestamps held outside
 * gmm_x1868. */
#define MELEE_WEB_SAVE_PROFILE_TRANSIENT_BYTES 0x4D8U

/* gmMainLib_804D3EE0 points at the first element of the original
 * gmMainLib_8045A6C0[2] backing.  gmMainLib_8015F600(2..8, 1) intentionally
 * writes seven contiguous NameTagDataBank rows from absolute offset 0x2FC8
 * in the current generated layout;
 * the generated typed gmm_x1868 view exposes only the first two rows. */
#define MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES 0x10A30U

/* Capture the already allocated original save/profile roots.  Creation does
 * not initialize, unlock, or claim anything. */
MeleeWebSaveProfileOwner* melee_web_save_profile_owner_create(char* error,
                                                               size_t error_size);

/* Snapshot the full two-object source backing, including SaveData, the
 * transient pending-prize block, and all seven authored name-bank rows.  The
 * owner deliberately has no gameplay-generation field, so it may remain
 * active while Results, Prize, and CSS use separate source worlds. */
int melee_web_save_profile_owner_activate(MeleeWebSaveProfileOwner*, char* error,
                                          size_t error_size);

/* Verify that the source aliases have not been replaced while the owner is
 * active.  This is intended for scene handoff guards. */
int melee_web_save_profile_owner_live(const MeleeWebSaveProfileOwner*, char* error,
                                      size_t error_size);

/* Apply only an explicit roster/stage mask while active.  The caller owns the
 * chosen authored setup (the current four-stage baseline is 0x07FF/0x01C0).
 * This function never modifies achievement/trophy pending, claimed, progress,
 * or timestamp fields. */
int melee_web_save_profile_owner_set_roster(MeleeWebSaveProfileOwner*,
                                            uint16_t characters,
                                            uint16_t stages, char* error,
                                            size_t error_size);

/* Apply the observer's first-CSS source context while the owner is active.
 * The two ranges are PowerPC big-endian source bytes.  This function copies
 * them into the owned source objects through the generated layouts, swapping
 * every typed scalar it installs; it never stores a guest pointer. */
int melee_web_save_profile_owner_apply_reference_context(
    MeleeWebSaveProfileOwner*, const uint8_t game_rules[0x18],
    const uint8_t save_data[0x55E8], char* error, size_t error_size);

/* Run the source's once-per-session fresh-profile initialization while the
 * complete global and Toy backing snapshots are owned. This resets source
 * profile data (never the retained scene heap), chooses the owned US language,
 * installs source GameRules, and runs gmMainLib_8015F600(1..8, 1). Original
 * DVD language discovery and audio/video platform setup belong to their own
 * initialized services. The arg1=1 form does not seed a trophy or claimed flag.
 * Apply explicitly authored roster/stage masks separately with set_roster(). */
int melee_web_save_profile_owner_initialize_default(
    MeleeWebSaveProfileOwner*, char* error, size_t error_size);

/* Restore every byte captured by activate.  Restoration is fail-closed if a
 * source alias moved, leaving the owner active for the caller to diagnose. */
int melee_web_save_profile_owner_deactivate(MeleeWebSaveProfileOwner*, char* error,
                                            size_t error_size);

int melee_web_save_profile_owner_is_active(const MeleeWebSaveProfileOwner*);

/* Destroy only after successful deactivation. */
int melee_web_save_profile_owner_destroy(MeleeWebSaveProfileOwner*, char* error,
                                         size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
