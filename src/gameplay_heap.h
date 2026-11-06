#ifndef MELEE_WEB_GAMEPLAY_HEAP_H
#define MELEE_WEB_GAMEPLAY_HEAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True only when the SDK allocator has no arena metadata, selected heap or
 * wrapper owner. An initialized allocator with no selected heap is occupied. */
int melee_web_gameplay_heap_available(void);

/* Check the claimed arena identity without accessing heap descriptors. Call
 * before operating on stored SDK heap handles; another arena may reuse them. */
int melee_web_gameplay_heap_owns(const void* expected_arena);

/* Immutable singleton-root guard. Covers SDK roots (including the selected
 * heap) and wrapper roots, never HeapDesc/cell bytes inside the owned arena.
 * It refuses a retired/reinitialized wrapper owner even at a reused address.
 * Caller serializes with allocation/lifecycle operations. */
typedef struct MeleeWebGameplayHeapGuard MeleeWebGameplayHeapGuard;
MeleeWebGameplayHeapGuard* melee_web_gameplay_heap_guard_capture(
    const void* expected_arena,char*,size_t);
int melee_web_gameplay_heap_guard_admit(const MeleeWebGameplayHeapGuard*,char*,size_t);
void melee_web_gameplay_heap_guard_destroy(MeleeWebGameplayHeapGuard*);

/* Claim caller-owned memory for the isolated runtime and call original
 * OSInitAlloc with one heap slot. Returns the usable start for OSCreateHeap;
 * accepts 128 bytes through 64 MiB and does not select/create a heap or transfer
 * malloc/free ownership. On failure
 * the caller still owns its memory and no claim was established. */
void* melee_web_gameplay_heap_initialize(void* arena, size_t bytes,
                                        char* error, size_t error_size);

/* Release only the exact claimed arena, after every heap was destroyed.
 * Clears SDK metadata before the caller frees arena. Failure retains ownership
 * and the caller must retain the memory. Direct SDK reinitialization violates
 * exclusive ownership; changed arena identity is detected before release. */
int melee_web_gameplay_heap_release(void* expected_arena,
                                   char* error, size_t error_size);

/* Experimental retained SDK arena checkpoint leaf. The snapshot owns a
 * private copy of the exact current arena extent. Preparation retains all
 * restore storage and a full live-byte mutation fence. Admit after preparing
 * every companion owner. The checked commit is for standalone controls and
 * repeats the owner/byte-fence checks before its first write. Aggregate callers
 * use `commit_prevalidated` only after all owners admitted before any writes;
 * that leaf is a fixed same-base memcpy with no late check/failure/allocation.
 * The PRE snapshot must outlive its prepared plan. This leaf does not restore
 * the source-memory shadow, external borrowers, or any other owner. */
typedef enum MeleeWebGameplayHeapHistoryStatus {
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK = 0,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT = 1,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INACTIVE = 2,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER = 3,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALLOCATION_FAILED = 4,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE = 5,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE = 6,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_NOT_ADMITTED = 7,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_ALREADY_COMMITTED = 8,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_VISITOR_STOPPED = 9,
} MeleeWebGameplayHeapHistoryStatus;
typedef struct MeleeWebGameplayHeapHistorySnapshot MeleeWebGameplayHeapHistorySnapshot;
typedef struct MeleeWebGameplayHeapHistoryPrepared MeleeWebGameplayHeapHistoryPrepared;
typedef struct MeleeWebGameplayHeapHistoryCaptureResult {
    MeleeWebGameplayHeapHistoryStatus status;
    MeleeWebGameplayHeapHistorySnapshot* snapshot;
} MeleeWebGameplayHeapHistoryCaptureResult;
typedef struct MeleeWebGameplayHeapHistoryPrepareResult {
    MeleeWebGameplayHeapHistoryStatus status;
    MeleeWebGameplayHeapHistoryPrepared* prepared;
} MeleeWebGameplayHeapHistoryPrepareResult;
typedef struct MeleeWebGameplayHeapHistoryObservation {
    MeleeWebGameplayHeapHistoryStatus status;
    uint32_t schema;
    uint32_t bytes;
    uint32_t exact_equal;
    int32_t selected_heap;
    uint64_t owner_epoch;
    uint64_t arena_base;
    uint64_t arena_bytes;
    uint64_t compared_bytes;
    uint64_t first_difference;
} MeleeWebGameplayHeapHistoryObservation;
typedef enum MeleeWebGameplayHeapHistoryPlanStage {
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_PLAN_PREPARED = 1,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_PLAN_ADMITTED = 2,
    MELEE_WEB_GAMEPLAY_HEAP_HISTORY_PLAN_COMMITTED = 3,
} MeleeWebGameplayHeapHistoryPlanStage;
MeleeWebGameplayHeapHistoryCaptureResult melee_web_gameplay_heap_history_capture(void);
MeleeWebGameplayHeapHistoryPrepareResult melee_web_gameplay_heap_history_prepare(
    const MeleeWebGameplayHeapHistorySnapshot* snapshot);
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_admit(
    MeleeWebGameplayHeapHistoryPrepared* prepared);
/* No-allocation selected-current proof; owner and live allocator graph only,
 * without requiring the current arena to equal the frozen PRE image. */
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_current_admit(
    const MeleeWebGameplayHeapHistorySnapshot* snapshot);
/* Typed companion proof for a source-memory class-piece sidecar restore.
 * Snapshot check requires exact live bytes and the current owned SDK heap.
 * Plan-stage check binds a prepared/admitted/committed plan to that exact PRE
 * snapshot and current owner; it does not repeat full arena validation. */
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_snapshot_match_current(
    const MeleeWebGameplayHeapHistorySnapshot* snapshot);
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_plan_stage(
    const MeleeWebGameplayHeapHistoryPrepared* prepared,
    const MeleeWebGameplayHeapHistorySnapshot* snapshot,
    MeleeWebGameplayHeapHistoryPlanStage required_stage);
/* Reports exact bytes compared from live storage against PRE; first_difference
 * is UINT64_MAX on equality. This never re-emits saved bytes as live evidence. */
MeleeWebGameplayHeapHistoryObservation melee_web_gameplay_heap_history_observe(
    const MeleeWebGameplayHeapHistorySnapshot* snapshot);
/* Enumerates the validated live original-SDK allocated chain without storing a
 * second row registry. payload_capacity is the original cell referent extent;
 * cross-check exact requested bytes/generation through the source-shadow owner.
 * The visitor is read-only and serialized; returning zero stops enumeration. */
typedef int (*MeleeWebGameplayHeapHistoryAllocationVisitor)(
    void* user, uint64_t ordinal, const void* payload, uint32_t payload_capacity);
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_visit_allocations(
    const MeleeWebGameplayHeapHistorySnapshot* snapshot,
    MeleeWebGameplayHeapHistoryAllocationVisitor visitor, void* user,
    uint64_t* out_count);
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_commit(
    MeleeWebGameplayHeapHistoryPrepared* prepared);
void melee_web_gameplay_heap_history_commit_prevalidated(
    MeleeWebGameplayHeapHistoryPrepared* prepared);
void melee_web_gameplay_heap_history_snapshot_destroy(
    MeleeWebGameplayHeapHistorySnapshot* snapshot);
void melee_web_gameplay_heap_history_prepared_destroy(
    MeleeWebGameplayHeapHistoryPrepared* prepared);

/* Recreate the original OS heap over the exact owned arena after its previous
 * heap handle has been destroyed. This resets the SDK free list and descriptor
 * state in place, retaining the backing bytes just as a retail scene reset
 * does. The caller must keep the arena owner and must not have a selected heap.
 * The resulting handle is returned through out_heap. */
int melee_web_gameplay_heap_recreate(void* expected_arena, int* out_heap,
                                    char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
