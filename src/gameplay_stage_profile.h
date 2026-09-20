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
} MeleeWebStageProfile;

/* NULL means that this stage has no complete source callback profile yet. */
const MeleeWebStageProfile* melee_web_stage_profile(int stage_kind);

#ifdef __cplusplus
}
#endif

#endif
