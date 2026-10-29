#ifndef MELEE_WEB_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H
#define MELEE_WEB_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H

#include "gameplay_class_piece_diagnostic.h"
#include "gameplay_lobj_writer_diagnostic.h"
#include "gameplay_tobj_writer_diagnostic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the checked GALE01r2 source-address shadow at the original VS main
 * HSD-heap replacement boundary. Bounds are derived from the pinned game-heap
 * descriptors and the source preload-3 retention transition. */
int melee_web_source_memory_begin(uint64_t world_generation, int source_heap);
int melee_web_source_memory_end(uint64_t world_generation);

/* Called only by the isolated gameplay OS allocator interposition. The source
 * heap receives the same request and validates allocation/free order. */
int melee_web_source_memory_alloc(int source_heap, void* host_payload,
                                  size_t requested_bytes);
int melee_web_source_memory_free(int source_heap, void* host_payload);
int melee_web_source_memory_healthy(void);

/* Pure observations of the active source-memory owner. These queries never
 * fail the owner, allocate a generation, or expose its allocation container. */
typedef enum MeleeWebSourceMemoryReadStatus {
    MELEE_WEB_SOURCE_MEMORY_READ_OK = 0,
    MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE = 1,
    MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY = 2,
    MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT = 3,
} MeleeWebSourceMemoryReadStatus;

typedef struct MeleeWebSourceMemoryContext {
    int source_heap_handle;
    uint64_t world_generation;
    uint64_t allocation_generation_watermark;
} MeleeWebSourceMemoryContext;

typedef struct MeleeWebSourceMemoryAllocation {
    int source_heap_handle;
    uint32_t requested_bytes;
    uint64_t world_generation;
    uint64_t allocation_generation;
    uint8_t live;
    uint8_t reserved[7];
} MeleeWebSourceMemoryAllocation;

MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(
    MeleeWebSourceMemoryContext* out);
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(
    const void* exact_host_payload, MeleeWebSourceMemoryAllocation* out);

/* Private source-memory mirror checkpoint, NOT a game/arena checkpoint.
 * Captures address-heap cells, live allocation identities, fighter leases and
 * allocation watermark by value. Host payloads and every other owner must be
 * captured/restored by their own proven contract. Calling this API alone does
 * not authorize restoring a rendered match. Caller serializes these operations
 * at a quiescent source boundary and admits all companion owners before writes.
 * A restart invalidates snapshots even if the caller reuses a world number.
 * Admission/refusal never modifies the owner. Restore stages all allocations
 * before committing; refused/failed restores preserve the complete live mirror.
 * Snapshots remain reusable until destroy; destroy accepts NULL. */
typedef struct MeleeWebSourceMemorySnapshot MeleeWebSourceMemorySnapshot;
typedef enum MeleeWebSourceMemorySnapshotStatus {
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK = 0,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_ARGUMENT = 1,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INACTIVE = 2,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_UNHEALTHY = 3,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_STALE_OWNER = 4,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_ALLOCATION_FAILED = 5,
    MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_STATE = 6,
} MeleeWebSourceMemorySnapshotStatus;
MeleeWebSourceMemorySnapshotStatus melee_web_source_memory_snapshot_capture(
    MeleeWebSourceMemorySnapshot** out);
MeleeWebSourceMemorySnapshotStatus melee_web_source_memory_snapshot_admit(
    const MeleeWebSourceMemorySnapshot* snapshot);
MeleeWebSourceMemorySnapshotStatus melee_web_source_memory_snapshot_restore(
    const MeleeWebSourceMemorySnapshot* snapshot);
void melee_web_source_memory_snapshot_destroy(MeleeWebSourceMemorySnapshot* snapshot);

/* Payload-only checkpoint for the exact live OSAllocFromHeap payloads tracked
 * by the active source-main-heap owner. This is distinct from the mirror
 * snapshot above: it copies bytes, never rewinds allocator cells or identities,
 * and restores only while the complete live topology and lifetime watermark
 * are unchanged. Callers serialize it at a quiescent source boundary and still
 * admit every other owner before any broader operation. Capture returns its
 * result by value so the caller cannot supply an output pointer that aliases a
 * live payload. */
typedef struct MeleeWebSourceMemoryPayloadSnapshot
    MeleeWebSourceMemoryPayloadSnapshot;
typedef enum MeleeWebSourceMemoryPayloadSnapshotStatus {
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK = 0,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT = 1,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE = 2,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY = 3,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER = 4,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED = 5,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE = 6,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_ALLOCATION_FAILED = 7,
    MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OVERLAPPING_STORAGE = 8,
} MeleeWebSourceMemoryPayloadSnapshotStatus;
typedef struct MeleeWebSourceMemoryPayloadSnapshotCaptureResult {
    MeleeWebSourceMemoryPayloadSnapshotStatus status;
    MeleeWebSourceMemoryPayloadSnapshot* snapshot;
} MeleeWebSourceMemoryPayloadSnapshotCaptureResult;
MeleeWebSourceMemoryPayloadSnapshotCaptureResult
melee_web_source_memory_payload_snapshot_capture(void);
MeleeWebSourceMemoryPayloadSnapshotStatus
melee_web_source_memory_payload_snapshot_admit(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot);
MeleeWebSourceMemoryPayloadSnapshotStatus
melee_web_source_memory_payload_snapshot_restore(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot);
/* Exact live payload equality, after normal owner/topology admission. No copy
 * or pointer/mirror restoration is performed. Returns exactly 1 or 0. */
/* status: 0 equal, 1 owner/admission refusal, 2 first actual byte difference.
 * Index/offset name the exact admitted ledger row, never an inferred owner. */
typedef struct MeleeWebSourcePayloadComparison {
    uint32_t status, admission_status;
    size_t allocation_index, byte_offset;
    uintptr_t host_address;
    uint32_t source_address, requested_bytes, expected_byte, actual_byte;
} MeleeWebSourcePayloadComparison;
MeleeWebSourcePayloadComparison melee_web_source_memory_payload_snapshot_compare_live(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot);
/* Private read-only failure witness. The snapshot is an existing internal live
 * snapshot handle, as for compare_live; there is no caller-selected address,
 * row, offset or output pointer. status 0=equal, 1=admission/refusal, 2=actual
 * byte difference. Refusal/equal carry no payload/identity data. root_kind
 * is always 0=UNKNOWN; root_count/root_host/root_generation are reserved zero.
 * FighterLease is not allocation-generation-linked and is not provenance.
 * All restore and ownership admission rules are unchanged. */
typedef struct MeleeWebSourcePayloadWitness {
    uint32_t schema, bytes, status, admission_status;
    uint32_t row, offset, source_address, requested_bytes;
    uint32_t window_offset, window_count, root_kind, root_count;
    uint64_t host_address, allocation_generation, owner_epoch, world_generation;
    uint64_t topology_revision, root_host, root_generation;
    uint8_t expected[32], actual[32];
} MeleeWebSourcePayloadWitness;
MeleeWebSourcePayloadWitness melee_web_source_memory_payload_snapshot_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot);
/* Diagnostic-only actual-source pool event; not exported to JavaScript.
 * alignment_mask is original HSD_ObjAllocData.align (alignment minus one).
 * Runtime never dereferences descriptor or an untracked/interior pool pointer.
 * Inactive builds preserve generic allocator linkage (source call is guarded). */
void melee_web_source_memory_objalloc_event(const void* pool, const void* descriptor,
    uint32_t stride, uint32_t count, uint32_t alignment_mask,
    uint32_t pool_bytes, uint32_t internal_heap);
/* Fixed failure evidence. attribution 0=UNKNOWN, 1=observed pool association
 * requiring offline exact named source-symbol join, 2=REFUSED. It is never
 * ownership/type/restore admission. Original comparison remains authoritative. */
typedef struct MeleeWebSourcePayloadFailureWitness {
    uint32_t schema, bytes, original_status, first_difference_matches;
    MeleeWebSourcePayloadWitness window;
    uint32_t hook_active, attribution, reason, association_state;
    uint64_t descriptor, allocation_generation, owner_epoch, world_generation;
    uint32_t stride, count, alignment_mask, pool_bytes, cell_index, cell_offset;
} MeleeWebSourcePayloadFailureWitness;
MeleeWebSourcePayloadFailureWitness melee_web_source_memory_payload_failure_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot,
    MeleeWebSourcePayloadComparison original);
/* Private source allocation callsite, not a type/restore owner. No exported
 * caller pointer. Source literals have static lifetime; indirect calls unknown. */
void melee_web_source_memory_origin_event(const void* payload, uint32_t requested,
    const char* source_file, uint32_t source_line, uint32_t site_kind);
typedef struct MeleeWebSourceAllocationOriginWitness {
    uint32_t schema, bytes, status, reason, first_difference_matches, hook_active, row, offset;
    uint64_t host, generation, epoch, world;
    uint32_t source, requested, site_kind, line, path_bytes, reserved;
    char path[232];
} MeleeWebSourceAllocationOriginWitness;
MeleeWebSourceAllocationOriginWitness melee_web_source_memory_allocation_origin_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot, MeleeWebSourcePayloadComparison original);
int melee_web_source_memory_payload_snapshot_matches_live(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot);
void melee_web_source_memory_payload_snapshot_destroy(
    MeleeWebSourceMemoryPayloadSnapshot* snapshot);

/* Fighter_Create / ftDemo_CreateFighter and Fighter_Unload bind the allocation to its
 * live host owner. Reuse advances allocation_generation even if an HSD free
 * chain returns the same object address. */
typedef struct MeleeWebSourceFighterAddress {
    uint32_t source_address;
    uint64_t world_generation;
    uint64_t allocation_generation;
    uint8_t live;
    uint8_t reserved[7];
} MeleeWebSourceFighterAddress;

int melee_web_source_memory_fighter_acquire(void* host_fighter,
                                            size_t fighter_bytes,
                                            MeleeWebSourceFighterAddress* out);
int melee_web_source_memory_fighter_read(void* host_fighter,
                                         MeleeWebSourceFighterAddress* out);
int melee_web_source_memory_fighter_release(void* host_fighter);

/* Private presentation provenance: actual create receipt + current main-heap
 * pool association, not FighterLease alone. No memory restoration authority. */
typedef struct MeleeWebFighterProjectionIdentity {
    uint64_t world, epoch, allocation, fighter;
    uintptr_t pool;
    uint32_t source, cell_offset;
} MeleeWebFighterProjectionIdentity;
int melee_web_source_memory_fighter_projection_identity(
    void*, const void* descriptor, size_t extent,
    MeleeWebFighterProjectionIdentity*);
int melee_web_source_memory_fighter_projection_matches(
    void*, const void* descriptor, size_t extent,
    const MeleeWebFighterProjectionIdentity*);

/* Read-only private diagnostic, paired with the original strict comparison. */
MeleeWebClassPieceCaptureWitness melee_web_source_memory_class_piece_capture_witness(
 const MeleeWebSourceMemoryPayloadSnapshot* pre,const MeleeWebSourceMemoryPayloadSnapshot* post,
 MeleeWebSourcePayloadComparison,MeleeWebClassPieceAnchor);
MeleeWebClassPieceWitness melee_web_source_memory_class_piece_witness(
 const MeleeWebSourceMemoryPayloadSnapshot*,MeleeWebSourcePayloadComparison,MeleeWebClassPieceAnchor);

/* Bounded read-only trace of existing class-piece hooks around one exact
 * payload-difference locator. This is diagnostic only: it never affects the
 * allocator, PieceOwner ledger, source bytes, or CPU comparison. */
enum MeleeWebClassPieceSlabTraceScope {
    MELEE_WEB_CLASS_PIECE_TRACE_COMMAND = 0,
    MELEE_WEB_CLASS_PIECE_TRACE_STEP = 1,
    MELEE_WEB_CLASS_PIECE_TRACE_DRAW = 2
};
int melee_web_source_memory_class_piece_slab_trace_arm(
    const MeleeWebSourceMemoryPayloadSnapshot*, MeleeWebClassPieceAnchor,
    uint32_t locator_source, uint32_t locator_extent, uint32_t target_offset);
void melee_web_source_memory_class_piece_slab_trace_context(
    uint32_t command, uint32_t scope, uint32_t frame, uint32_t sample);
void melee_web_source_memory_class_piece_slab_trace_finalize(
    const MeleeWebSourceMemoryPayloadSnapshot* pre,
    const MeleeWebSourceMemoryPayloadSnapshot* post,
    MeleeWebSourcePayloadComparison original);
void melee_web_source_memory_class_piece_slab_trace_early_close(void);

/* Temporary exact-target diagnostic ticket; no source pointer authority. */
typedef struct MeleeWebPieceJObjTicket {
    uint32_t valid, creator, allocation_event, leaf, begin_sequence, flags_before, before[12];
} MeleeWebPieceJObjTicket;
int melee_web_piece_jobj_begin(void* object, uint32_t leaf,
                              MeleeWebPieceJObjTicket* ticket);
void melee_web_piece_jobj_end(void* object, const MeleeWebPieceJObjTicket* ticket);

#ifdef __cplusplus
}
#endif
#endif
