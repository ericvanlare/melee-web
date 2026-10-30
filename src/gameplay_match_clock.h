#ifndef MELEE_WEB_GAMEPLAY_MATCH_CLOCK_H
#define MELEE_WEB_GAMEPLAY_MATCH_CLOCK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The source clock is a singleton. MENU and MATCH are mutually exclusive
 * owners; the owner is restored, rather than inferred, at end(). */
enum {
    MELEE_WEB_SOURCE_CLOCK_MENU = 1,
    MELEE_WEB_SOURCE_CLOCK_MATCH = 2,
};

enum {
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_NONE = 0,
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_OWNER = 1,
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_CALLBACK = 2,
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_GOBJ = 3,
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_MASK_VALUE = 4,
    MELEE_WEB_SOURCE_CLOCK_END_FAILURE_CONTROLLER_MAP = 5,
};

typedef void (*MeleeWebSourceFrame)(void);

/* These functions are implemented in the MELEE_WEB_GAMEPLAY section of
 * melee/gm/gm_1A45.c. The caller owns PAD renewal and lb_80019900 cadence.
 * MATCH pre() requires lb_80019A30(0) to be ready, invokes on_frame, and
 * publishes the original gm_1A45 process mask before the caller runs audio
 * and HSD_GObj_80390CFC(). A changed HSD mask pointer is an explicit failure;
 * the source clock never overwrites another owner during rollback. */
int melee_web_source_clock_begin(int owner);
int melee_web_source_clock_pre(MeleeWebSourceFrame on_frame);
int melee_web_source_clock_post(void);
int melee_web_source_clock_present(void);
int melee_web_source_clock_request(int* request);
int melee_web_source_clock_end(void);
int melee_web_source_clock_end_failure(void);

/* A source-owned observation attached to one live match-flow lifetime.
 * The explicit CPU writer below restores simulation values only; legacy
 * flow-only restore does not invoke it. The flow identity, epoch, and world generation are
 * supplied by the existing flow owner and are checked again at admission.
 * Saved external mask pointers are borrowed identities, never dereferenced or
 * admitted as pointee state by this observation. */
typedef struct MeleeWebSourceClockSnapshot MeleeWebSourceClockSnapshot;
/* Fixed storage written by the typed CPU writer; by-value, pure and bounded. */
typedef struct MeleeWebClockCpuStorage {
    const void* record; size_t record_bytes;
    const void* process_mask; size_t process_mask_bytes;
    /* Optional v6 typed phase; NULL/0 for ordinary/v5 source. */
    const void* profile_phase; size_t profile_phase_bytes;
} MeleeWebClockCpuStorage;
MeleeWebClockCpuStorage melee_web_source_clock_cpu_storage(void);
/* Named values copied by value from the original 0x479D58 record. This is
 * deliberately not a byte view: the source owner compares these exact fields
 * and excludes struct padding. Pointer-valued fields remain identities. */
typedef struct MeleeWebClockCpuEndpoint {
    uint32_t valid;
    uint32_t unk_0, unk_4, unk_8;
    int32_t unk_C;
    uint32_t callback_x0, callback_x1, callback_x2, callback_x3;
    uintptr_t callback_x4_0, callback_x4_1;
    uintptr_t unk_10_xC;
    uint64_t unk_10_x20, unk_10_x28;
    uintptr_t unk_10_x30;
    int32_t unk_10_x34;
    uint32_t unk_10_x38_0, unk_10_x38_1;
} MeleeWebClockCpuEndpoint;
MeleeWebClockCpuEndpoint melee_web_source_clock_cpu_endpoint(void);

MeleeWebSourceClockSnapshot* melee_web_source_clock_snapshot_capture(
    const void* flow_identity, uint64_t flow_epoch, uint64_t world_generation);
int melee_web_source_clock_snapshot_admit(
    const MeleeWebSourceClockSnapshot* snapshot, const void* flow_identity,
    uint64_t flow_epoch, uint64_t world_generation);
int melee_web_source_clock_snapshot_restore(
    const MeleeWebSourceClockSnapshot*, const void*, uint64_t, uint64_t);
int melee_web_source_clock_snapshot_matches(
    const MeleeWebSourceClockSnapshot*, const void*, uint64_t, uint64_t);
int melee_web_source_clock_snapshot_bootstrap_mask_admit(
    const MeleeWebSourceClockSnapshot*);
/* CPU-only closure for the two exact source-supported saved-mask owners.
 * Caller supplies current flow identity/epoch/world, never captured stand-ins. */
int melee_web_source_clock_snapshot_cpu_mask_admit(
    const MeleeWebSourceClockSnapshot*, const void*, uint64_t, uint64_t);
void melee_web_source_clock_snapshot_destroy(
    MeleeWebSourceClockSnapshot* snapshot);

/* Compatibility entry points used by the current native menu host. They
 * retain the existing MENU ordering: scene OnFrame is called by the menu
 * session, then the scheduler runs, then menu_clock_tick updates counters. */
/* MENU constructor counter continuity only. Non-counter begin/reset semantics
 * stay unchanged. Capture requires idle MENU ownership. Begin acquires a fresh
 * ordinary MENU lease, then installs actual captured counters; no source
 * pre/post/present is permitted until finish resets exactly these three once.
 * Request observation and checked end/rollback remain available while pending.
 * The host owns provenance, one-use handoff and failure revocation separately. */
typedef struct MeleeWebMenuClockCounters {
    uint32_t counter_0;
    uint32_t counter_4;
    uint32_t counter_8;
} MeleeWebMenuClockCounters;
int melee_web_menu_clock_capture_counters(MeleeWebMenuClockCounters* out);
int melee_web_menu_clock_begin_with_counters(
    const MeleeWebMenuClockCounters* counters);
int melee_web_menu_clock_finish_constructor(void);

int melee_web_menu_clock_begin(void);
int melee_web_menu_clock_request(int* request);
int melee_web_menu_clock_tick(void);
int melee_web_menu_clock_present(void);
int melee_web_menu_clock_end(void);

/* The browser owns the source cadence record for a nominal 60 Hz match tick.
 * begin() snapshots the complete lb_804329F0 record and rejects a live source
 * alarm; tick() calls the original lb_80019900 after Master PAD renewal and
 * before source pre(); end() restores the snapshot. These APIs do not emulate
 * VI retrace or GameCube PAD sampling hardware. */
int melee_web_source_cadence_begin(void);
int melee_web_source_cadence_tick(void);
int melee_web_source_cadence_end(void);
/* Opaque typed values and lifetime guards for the original lb_0195 cadence.
 * The capsule owns these snapshots; no caller-provided destination aliases. */
typedef struct MeleeWebSourceCadenceSnapshot MeleeWebSourceCadenceSnapshot;
typedef struct MeleeWebCadenceCpuStorage {
    const void* record; size_t record_bytes;
} MeleeWebCadenceCpuStorage;
MeleeWebCadenceCpuStorage melee_web_source_cadence_cpu_storage(void);
/* Named values used by the original cadence comparator. Only alarm is a raw
 * byte range because that comparator alone uses memcmp for its state. */
typedef struct MeleeWebCadenceCpuEndpoint {
    uint32_t valid;
    int64_t x0_0_x0, x0_0_x8;
    int32_t x0_0_x10;
    int64_t x0_1_x0, x0_1_x8;
    int32_t x0_1_x10;
    uint32_t x4;
    uint64_t x38;
    int64_t x40;
    int32_t x48;
    const void* alarm;
    size_t alarm_bytes;
} MeleeWebCadenceCpuEndpoint;
MeleeWebCadenceCpuEndpoint melee_web_source_cadence_cpu_endpoint(void);

MeleeWebSourceCadenceSnapshot* melee_web_source_cadence_snapshot_capture(
    const void*, uint64_t, uint64_t);
int melee_web_source_cadence_snapshot_admit(
    const MeleeWebSourceCadenceSnapshot*, const void*, uint64_t, uint64_t);
int melee_web_source_cadence_snapshot_restore(
    const MeleeWebSourceCadenceSnapshot*, const void*, uint64_t, uint64_t);
int melee_web_source_cadence_snapshot_matches(
    const MeleeWebSourceCadenceSnapshot*, const void*, uint64_t, uint64_t);
void melee_web_source_cadence_snapshot_destroy(MeleeWebSourceCadenceSnapshot*);


#ifdef __cplusplus
}
#endif

#endif
