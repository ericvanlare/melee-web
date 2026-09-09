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
} MeleeWebStageProfile;

/* NULL means that this stage has no complete source callback profile yet. */
const MeleeWebStageProfile* melee_web_stage_profile(int stage_kind);

#ifdef __cplusplus
}
#endif

#endif
