#ifndef MELEE_WEB_GAMEPLAY_CPU_R5_CARRY_H
#define MELEE_WEB_GAMEPLAY_CPU_R5_CARRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The allocation profile generator currently emits this profile version.  A
 * caller must provide a value derived by the owned-source adapter.  This
 * low-level module validates shape and provenance tags; it cannot itself
 * distinguish a forged caller value from an owned DOL/SDA derivation. */
#define MELEE_WEB_CPU_SOURCE_PROFILE_VERSION 3u

typedef enum MeleeWebCpuWordKind {
    MELEE_WEB_CPU_WORD_UNKNOWN = 0,
    MELEE_WEB_CPU_WORD_ZERO = 1,
    MELEE_WEB_CPU_WORD_SOURCE_GLOBAL = 2,
    MELEE_WEB_CPU_WORD_SOURCE_STACK = 3,
    MELEE_WEB_CPU_WORD_SOURCE_OBJECT = 4,
    MELEE_WEB_CPU_WORD_SOURCE_FIGHTER = 5,
} MeleeWebCpuWordKind;

typedef enum MeleeWebCpuGlobalId {
    MELEE_WEB_CPU_GLOBAL_NONE = 0,
    MELEE_WEB_CPU_GLOBAL_SEED_PTR = 1,
} MeleeWebCpuGlobalId;

/* Low-level adapter input.  The source_word is the value loaded from the
 * global (the seed pointer that the original r5 carries), while
 * global_address identifies the global storage that produced it. Neither is
 * a host pointer.  This C boundary trusts the caller's independently_derived
 * attestation; only an owned DOL/SDA adapter may set that bit for production.
 * Synthetic tests may use relocated source identities, but do not establish
 * retail provenance. */
typedef struct MeleeWebCpuSourceGlobalBinding {
    uint32_t source_word;
    uint32_t global_address;
    uint32_t profile_version;
    uint8_t global_id;
    uint8_t independently_derived;
    uint8_t reserved[2];
} MeleeWebCpuSourceGlobalBinding;

/* This identity must come from the live source fighter allocation model. The
 * field is a source word, never a host pointer; the low-level API trusts the
 * caller to supply a live identity from the owned source context. */
typedef struct MeleeWebCpuSourceFighterIdentity {
    uint32_t source_word;
    uint64_t world_generation;
    uint8_t live;
    uint8_t reserved[7];
} MeleeWebCpuSourceFighterIdentity;

/* Opaque-in-practice owner capability returned by begin.  A caller must keep
 * this exact token and pass it to every transition.  World generation and
 * source fighter words alone are insufficient because a fighter address can
 * be reused within one world. */
typedef struct MeleeWebCpuR5Token {
    uint64_t world_generation;
    uint64_t carry_lifetime;
    uint32_t source_fighter_word;
    uint8_t valid;
    uint8_t reserved[7];
} MeleeWebCpuR5Token;

typedef struct MeleeWebCpuWord {
    uint32_t source_word;
    uint64_t world_generation;
    uint64_t carry_lifetime;
    uint8_t known;
    uint8_t kind;
    uint8_t source_id;
    uint8_t reserved;
} MeleeWebCpuWord;

typedef struct MeleeWebCpuR5Carry {
    uint64_t world_generation;
    uint64_t carry_lifetime;
    uint32_t source_fighter_word;
    uint8_t active;
    uint8_t reserved[7];
    MeleeWebCpuWord r5;
    MeleeWebCpuWord r30;
} MeleeWebCpuR5Carry;

/* Begin one source fighter/generation lifetime.  r30 is bound immediately;
 * it remains required even when r5 is unavailable. */
int melee_web_cpu_r5_begin(MeleeWebCpuR5Carry*,
                           const MeleeWebCpuSourceFighterIdentity*,
                           MeleeWebCpuR5Token*,
                           char*, size_t);

/* Publish the only currently supported skipped-conversion r5 definition:
 * the source seed pointer after the audited HSD_Randf call. */
int melee_web_cpu_r5_publish_seed_global(
    MeleeWebCpuR5Carry*, const MeleeWebCpuR5Token*,
    const MeleeWebCpuSourceGlobalBinding*, char*, size_t);

/* Preserve the current r5 across a source call whose DOL audit proves no
 * replacement. A missing r5 remains missing. */
int melee_web_cpu_r5_preserve(MeleeWebCpuR5Carry*, const MeleeWebCpuR5Token*,
                              char*, size_t);

/* These transitions are explicit source semantics. A known zero is not an
 * admissible substitute for the seed route; explicit neutral source branches
 * should bypass the skipped-conversion consumer. */
int melee_web_cpu_r5_set_zero(MeleeWebCpuR5Carry*, const MeleeWebCpuR5Token*,
                              char*, size_t);
int melee_web_cpu_r5_mark_unknown(MeleeWebCpuR5Carry*, const MeleeWebCpuR5Token*,
                                  char*, size_t);

/* Resolve the skipped-conversion command bytes. This succeeds only for the
 * source-global seed route and a live source-fighter r30 identity. */
int melee_web_cpu_r5_resolve_skipped(const MeleeWebCpuR5Carry*,
                                     const MeleeWebCpuR5Token*,
                                     int8_t* stick_x, int8_t* stick_y,
                                     char*, size_t);

/* Normal teardown must use the exact begin token. Discard is the explicit
 * fail-closed path for a world reset whose generation is already stale. Both
 * invalidate the current words. */
int melee_web_cpu_r5_end(MeleeWebCpuR5Carry*, const MeleeWebCpuR5Token*,
                         char*, size_t);
void melee_web_cpu_r5_discard(MeleeWebCpuR5Carry*);

/* Read-only diagnostic accessors used by focused integration tests. */
MeleeWebCpuWord melee_web_cpu_r5_word(const MeleeWebCpuR5Carry*);
MeleeWebCpuWord melee_web_cpu_r30_word(const MeleeWebCpuR5Carry*);

#ifdef __cplusplus
}
#endif

#endif
