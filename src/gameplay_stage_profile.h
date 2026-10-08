#ifndef MELEE_WEB_GAMEPLAY_STAGE_PROFILE_H
#define MELEE_WEB_GAMEPLAY_STAGE_PROFILE_H

#include <stddef.h>
#include <stdint.h>
#include "native_dat.h"
#include "gameplay_compat.h"
#include <melee/gr/forward.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*MeleeWebStageYakumonoExchange)(void* value);
typedef void* (*MeleeWebStageYakumonoDecode)(const MeleeWebNativeDat*, uint32_t root);
typedef enum MeleeWebStagePublicKind {
    MELEE_WEB_STAGE_PUBLIC_JOINT,
    MELEE_WEB_STAGE_PUBLIC_IMAGE,
} MeleeWebStagePublicKind;
typedef struct MeleeWebStagePublic {
    const char* name;
    MeleeWebStagePublicKind kind;
} MeleeWebStagePublic;

/* A complete-stage profile must choose how it owns the map graph. Zero is
 * deliberately invalid so an omitted initializer cannot silently inherit
 * archive-derived defaults. CURRENT_ALL_RESIDENT names the behavior used by
 * the profiles that predate authored map contracts. AUTHORED requires a
 * complete, non-NULL declaration and never falls back to derived data. */
typedef enum MeleeWebStageMapOwnershipPolicy {
    MELEE_WEB_STAGE_MAP_OWNERSHIP_UNSPECIFIED = 0,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT = 1,
    MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED = 2,
} MeleeWebStageMapOwnershipPolicy;

typedef enum MeleeWebStageMapFlagKind {
    MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL = 0,
    MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL = 1,
    MELEE_WEB_STAGE_MAP_FLAG_NULL = 2,
} MeleeWebStageMapFlagKind;

typedef struct MeleeWebStageMapExternalReference {
    uint32_t entry_index;
    uint32_t field_offset;
    const char* symbol;
} MeleeWebStageMapExternalReference;

typedef struct MeleeWebStageMapFlagExpectation {
    uint32_t index;
    MeleeWebStageMapFlagKind kind;
    uint32_t target_offset;
    const char* symbol;
} MeleeWebStageMapFlagExpectation;

typedef struct MeleeWebStageMapOwnership {
    const uint32_t* resident_entry_ids;
    size_t resident_entry_count;
    const MeleeWebStageMapExternalReference* external_references;
    size_t external_reference_count;
    const uint32_t* animation_flag_entry_ids;
    size_t animation_flag_entry_count;
    const MeleeWebStageMapFlagExpectation* flagged_objects;
    size_t flagged_object_count;
} MeleeWebStageMapOwnership;

/* Source callback and object-layout details live here. Content names and
 * archive filenames remain in gameplay_content.h; this profile only describes
 * the source runtime boundary that consumes those assets. */
typedef struct MeleeWebStageProfile {
    int stage_kind;
    GrKind ground_kind;
    const StageData* source;
    const uint8_t* required_map_ids;
    size_t required_map_count;
    MeleeWebStageYakumonoExchange exchange_yakumono;
    /* Scalar stage parameters use a stage-specific checked decoder. A null
     * callback selects the material-command pointer-table decoder below. */
    MeleeWebStageYakumonoDecode decode_yakumono;
    /* The source type is stage-specific.  Keep this count explicit: the
     * Battlefield source owns two overlay-program words while Final
     * Destination owns four.  DAT allocation boundaries cannot be used to
     * infer this ABI. */
    size_t yakumono_program_count;
    /* The map entry table is a complete descriptor table, while source
     * OnInit consumes only selected animation slots from each entry. */
    size_t entry_count;
    const uint8_t* animation_counts;
    size_t animation_count_count;
    /* Source grDatFiles initializes particle bank64 and Ground's bank30 only
     * when both map_ptcl and map_texg are present. Keep this capability
     * explicit so a valid source-null stage does not receive a fake bank while
     * a required stage still fails at its boundary. */
    int allow_absent_particle_bank;
    /* Some source stages publish a present four-byte opaque yakumono_param
     * root whose contents are zero and whose callbacks never dereference it.
     * Preserve that source pointer's presence with an arena-owned copy. */
    int opaque_yakumono;
    /* Additional original HSD archive queries outside map_head. Images must
     * resolve to the very same descriptor used by the map texture graph. */
    const MeleeWebStagePublic* public_symbols;
    size_t public_symbol_count;
    MeleeWebStageMapOwnershipPolicy map_ownership_policy;
    const MeleeWebStageMapOwnership* map_ownership;
} MeleeWebStageProfile;

/* NULL means that this stage has no complete source callback profile yet. */
const MeleeWebStageProfile* melee_web_stage_profile(int stage_kind);

#ifdef __cplusplus
}
#endif

#endif
