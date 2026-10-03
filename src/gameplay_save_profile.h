#ifndef MELEE_WEB_GAMEPLAY_SAVE_PROFILE_H
#define MELEE_WEB_GAMEPLAY_SAVE_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebSaveProfileOwner MeleeWebSaveProfileOwner;

/* Preferences the browser menu host temporarily overrides at scene entry.
 * Keep these source-typed rather than exposing opaque SaveData patch offsets. */
typedef struct MeleeWebSaveProfilePreferences {
    uint8_t item_frequency;
    uint64_t item_mask;
    uint8_t rumble_enabled[4];
    uint8_t deflicker;
    uint8_t saved_language;
} MeleeWebSaveProfilePreferences;

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

/* Exact non-transient chunks in lbcardgame.c's GameData card manifest:
 * SaveData[0..0x178f] followed by seven authored NameTagDataBank rows. */
#define MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES 0x1790U
#define MELEE_WEB_SAVE_PROFILE_NAME_BANK_BYTES 0x1F2CU
#define MELEE_WEB_SAVE_PROFILE_CARD_BANK_COUNT 7U
#define MELEE_WEB_SAVE_PROFILE_CARD_BYTES \
    (MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES + \
     MELEE_WEB_SAVE_PROFILE_CARD_BANK_COUNT * \
         MELEE_WEB_SAVE_PROFILE_NAME_BANK_BYTES)

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

/* Copy/apply the complete original card-manifest data without pointers or
 * transient runtime fields. Bytes use the GameCube card's big-endian scalar
 * representation and are safe to keep outside the Wasm heap. */
int melee_web_save_profile_owner_snapshot_card_data(
    const MeleeWebSaveProfileOwner*, uint8_t* output, size_t output_size,
    char* error, size_t error_size);
int melee_web_save_profile_owner_capture_preferences(
    const MeleeWebSaveProfileOwner*, MeleeWebSaveProfilePreferences*,
    char* error, size_t error_size);
int melee_web_save_profile_owner_snapshot_card_data_with_preferences(
    const MeleeWebSaveProfileOwner*,
    const MeleeWebSaveProfilePreferences*, uint8_t* output,
    size_t output_size, char* error, size_t error_size);
int melee_web_save_profile_owner_apply_card_data(
    MeleeWebSaveProfileOwner*, const uint8_t* input, size_t input_size,
    char* error, size_t error_size);

/* Run the source's once-per-session fresh-profile initialization while the
 * complete global and Toy backing snapshots are owned. This resets source
 * profile data (never the retained scene heap), chooses the owned US language,
 * installs source GameRules, and runs gmMainLib_8015F600(1..8, 1). Original
 * DVD language discovery and audio/video platform setup belong to their own
 * initialized services. The arg1=1 form does not seed a trophy or claimed flag.
 * Apply explicitly authored roster/stage masks separately with set_roster(). */
int melee_web_save_profile_owner_initialize_default(
    MeleeWebSaveProfileOwner*, char* error, size_t error_size);

/* Build the original source-owned completed/unlocked profile from a fresh
 * original default. The inventory is constrained to source-authored unlock
 * tables, trophy IDs, event-clear bits, eligible challenge-completion IDs,
 * and 1P mode-clear IDs. Personal records and unknown/padding bytes remain at
 * source fresh defaults. */
int melee_web_save_profile_owner_initialize_everything(
    MeleeWebSaveProfileOwner*, char* error, size_t error_size);

/* Restore the complete original fresh source and Toy state captured after
 * initialize_default(), including fields outside the GCI card manifest. */
int melee_web_save_profile_owner_restore_default(
    MeleeWebSaveProfileOwner*, char* error, size_t error_size);

/* Build the bounded default used by the original CSS-first menu route. The
 * unlockable-character mask, unlock notifications and reward ledger are
 * initialized by their original routines; the supplied stage mask remains a
 * browser capability boundary. */
int melee_web_save_profile_owner_initialize_menu_roster(
    MeleeWebSaveProfileOwner*, uint16_t stages, char* error,
    size_t error_size);

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
