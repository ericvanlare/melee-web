#ifndef MELEE_WEB_GAMEPLAY_ACTION_STORE_H
#define MELEE_WEB_GAMEPLAY_ACTION_STORE_H
#include "hsd_animation_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
struct Fighter;
struct FigaTree;
typedef struct MeleeWebNativeClip MeleeWebNativeClip;
MeleeWebNativeClip* melee_web_native_clip_create(uint32_t type, uint32_t flags, float frames,
    const uint8_t* nodes, size_t node_count, const MeleeWebAnimationTrack* tracks, size_t track_count);
void melee_web_native_clip_destroy(MeleeWebNativeClip*);
struct FigaTree* melee_web_native_clip_tree(MeleeWebNativeClip*);
/* Callback owns both active slots until unbind. A false result is fatal to the
 * original void loader; failure never masquerades as an empty source clip. */
typedef int (*MeleeWebActionSelect)(void* context, int motion, unsigned slot,
    struct FigaTree** tree, void** identity, char* error, size_t error_size);
/* A cross-fighter load uses the source store's archive row and the
 * destination store's native lease.  The Fighter arguments let the callback
 * reject a stale context after either fighter has been torn down. */
typedef int (*MeleeWebActionTransfer)(void* destination_context, void* source_context,
    struct Fighter* destination, struct Fighter* source, int motion, unsigned slot,
    struct FigaTree** tree, void** identity, char* error, size_t error_size);
/* A by-value view of one of the six existing action bindings. The generation
 * advances on bind and unbind and never wraps; exhausted rows are never reused. */
typedef struct MeleeWebActionBindingSnapshot {
    uint32_t slot;
    uint32_t reserved;
    uint64_t generation;
    struct Fighter* fighter;
    void* context;
    MeleeWebActionSelect select;
    MeleeWebActionTransfer transfer;
} MeleeWebActionBindingSnapshot;
typedef struct MeleeWebActionFighterClipState {
    struct FigaTree* trees[2];
    void* identities[2];
} MeleeWebActionFighterClipState;
int melee_web_action_bind(struct Fighter*, void* context, MeleeWebActionSelect,
    MeleeWebActionTransfer);
void melee_web_action_unbind(struct Fighter*);
uint32_t melee_web_action_binding_count(void);
int melee_web_action_binding_snapshot(uint32_t slot, MeleeWebActionBindingSnapshot*);
int melee_web_action_binding_matches(const MeleeWebActionBindingSnapshot*);
int melee_web_action_binding_for_context(void* context, MeleeWebActionBindingSnapshot*);
void melee_web_action_load(struct Fighter* destination, struct Fighter* source, int motion, unsigned slot);
void* melee_web_action_fighter_table(struct Fighter*);
int melee_web_action_fighter_clip_state(const struct Fighter*, MeleeWebActionFighterClipState*);

/* Private one-interval action-store capsule. This retains CPU object identity
 * only; it is not a Wasm export, renderer owner, or snapshot restore API. */
typedef struct MeleeWebActionInterval MeleeWebActionInterval;
typedef enum MeleeWebActionIntervalFailure {
    MELEE_WEB_ACTION_INTERVAL_OK = 0,
    MELEE_WEB_ACTION_INTERVAL_CAPTURE_REFUSED = 1,
    MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED = 2,
    MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED = 3,
    MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED = 4,
    MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH = 5,
    MELEE_WEB_ACTION_INTERVAL_REPLAY_INCOMPLETE = 6,
    MELEE_WEB_ACTION_INTERVAL_BAD_PHASE = 7,
    MELEE_WEB_ACTION_INTERVAL_DISARMED = 8
} MeleeWebActionIntervalFailure;
MeleeWebActionInterval* melee_web_action_interval_capture(void);
int melee_web_action_interval_begin_record(MeleeWebActionInterval*);
int melee_web_action_interval_seal_record(MeleeWebActionInterval*);
int melee_web_action_interval_admit(const MeleeWebActionInterval*);
/* Restores only C++ active slots/motions and decoder cache. The aggregate's
 * later source-payload owner restores Fighter x590/x598/x5A4/x5A8 fields. */
int melee_web_action_interval_restore(MeleeWebActionInterval*);
int melee_web_action_interval_finish_replay(MeleeWebActionInterval*);
/* One-shot corrected recording for an admitted resident-action interval.
 * The corrected tape and endpoint are separate from exact-replay evidence;
 * this component does not restore the Fighter source payload or accept a
 * corrected endpoint against an authoritative reference. */
int melee_web_action_interval_admit_corrected(const MeleeWebActionInterval*);
int melee_web_action_interval_restore_corrected(MeleeWebActionInterval*);
int melee_web_action_interval_seal_corrected(MeleeWebActionInterval*);
/* Private read-only endpoint transport. These are internal C APIs, never
 * direct Web exports. The trusted synchronous collector must call admit
 * before AND after a getter batch, with no source work/yield between them.
 * Transport string addresses name bytes to copy, not semantic identities.
 * All getters return POD by value; refusal has status!=0 and zero metadata. */
typedef enum MeleeWebActionEndpointStatus {
    MELEE_WEB_ACTION_ENDPOINT_OK=0, MELEE_WEB_ACTION_ENDPOINT_INVALID=1,
    MELEE_WEB_ACTION_ENDPOINT_PHASE=2, MELEE_WEB_ACTION_ENDPOINT_OWNER=3,
    MELEE_WEB_ACTION_ENDPOINT_INDEX=4, MELEE_WEB_ACTION_ENDPOINT_OVERFLOW=5
} MeleeWebActionEndpointStatus;
enum { MELEE_WEB_ACTION_ENDPOINT_PRE=1, MELEE_WEB_ACTION_ENDPOINT_END=2,
       MELEE_WEB_ACTION_ENDPOINT_REFERENCE=1, MELEE_WEB_ACTION_ENDPOINT_CORRECTED=2,
       MELEE_WEB_ACTION_ENDPOINT_CACHE_HIT=0, MELEE_WEB_ACTION_ENDPOINT_CACHE_MISS=1,
       MELEE_WEB_ACTION_ENDPOINT_ACTION_SELECTION=2 };
typedef struct MeleeWebActionEndpointHeader {
    uint32_t status,schema,view,provenance;
    uint32_t binding_count,slots_per_binding,cache_count,event_count;
} MeleeWebActionEndpointHeader;
typedef struct MeleeWebActionEndpointBinding {
    uint32_t status,index,present,reserved;
    uint64_t generation,fighter,store,select_callback,transfer_callback,table;
} MeleeWebActionEndpointBinding;
typedef struct MeleeWebActionEndpointSlot {
    uint32_t status,binding,slot,present,motion,offset,archive_bytes,symbol_bytes;
    uint64_t fighter,store,table,clip,tree,row_identity,animation,row_owner,command_owner;
    uint64_t symbol_data;
} MeleeWebActionEndpointSlot;
typedef struct MeleeWebActionEndpointCacheEntry {
    uint32_t offset,archive_bytes,native_action,symbol_bytes;
    uint64_t animation,symbol_data;
} MeleeWebActionEndpointCacheEntry;
typedef struct MeleeWebActionEndpointCache {
    uint32_t status,index,next,reserved;
    uint64_t root,owner,cache,runtime,interval_owner,hit_callback,miss_replay_callback,miss_record_callback;
    MeleeWebActionEndpointCacheEntry entries[2];
} MeleeWebActionEndpointCache;
typedef struct MeleeWebActionEndpointEvent {
    uint32_t status,index,kind,policy,cache_hit,motion,slot,result_domain;
    uint32_t offset,archive_bytes,symbol_bytes,reserved;
    uint64_t cache,runtime,row,animation,destination,source;
    uint64_t destination_generation,source_generation,source_table,row_owner,clip,command_owner;
    uint64_t symbol_data;
} MeleeWebActionEndpointEvent;
uint32_t melee_web_action_interval_endpoint_admit(const MeleeWebActionInterval*);
MeleeWebActionEndpointHeader melee_web_action_interval_endpoint_header(const MeleeWebActionInterval*);
MeleeWebActionEndpointBinding melee_web_action_interval_endpoint_binding(const MeleeWebActionInterval*,uint32_t);
MeleeWebActionEndpointSlot melee_web_action_interval_endpoint_slot(const MeleeWebActionInterval*,uint32_t,uint32_t);
MeleeWebActionEndpointCache melee_web_action_interval_endpoint_cache(const MeleeWebActionInterval*,uint32_t);
MeleeWebActionEndpointEvent melee_web_action_interval_endpoint_event(const MeleeWebActionInterval*,uint32_t);

/* Read-only phase-matched postpresent proof; no status/lifetime mutation. */
int melee_web_action_interval_matches_finished(const MeleeWebActionInterval*);
uint32_t melee_web_action_interval_failure_code(const MeleeWebActionInterval*);
/* Read-only active journal guard; excludes final slot/cache and cursor checks. */
uint32_t melee_web_action_interval_active_failure(const MeleeWebActionInterval*);
/* Stops interval callbacks without releasing retained Clip/animation leases.
 * Retire source owners after disarm; destroy the capsule only afterward. */
void melee_web_action_interval_disarm(MeleeWebActionInterval*);
void melee_web_action_interval_destroy(MeleeWebActionInterval*);

#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
uint64_t melee_web_action_interval_test_clip_create_count(void);
uint64_t melee_web_action_interval_test_clip_destroy_count(void);
void melee_web_action_interval_test_fail_next_event_push(void);
void melee_web_action_interval_test_fail_next_seal_capture(void);
void melee_web_action_interval_test_set_fighter_table(struct Fighter*, void*);
void melee_web_action_interval_test_set_fighter_clip_state(struct Fighter*,
    const MeleeWebActionFighterClipState*);
#endif
/* Canonical instruction words are decoded into original native named fields.
 * target is the native word index of a checked branch; UINT32_MAX otherwise. */
typedef struct MeleeWebCommandWord { uint32_t word, target; } MeleeWebCommandWord;
void* melee_web_commands_create(const MeleeWebCommandWord*, size_t count);
uint32_t melee_web_command_original_word(const void* command);
int melee_web_command_original_word_checked(const void* command, uint32_t* word);
void melee_web_commands_destroy(void*);
void* melee_web_commands_at(void*, size_t index);
void* melee_web_commands_unsupported(void);
void melee_web_command_require_supported(uint32_t opcode);
typedef struct MeleeWebActionRow {
    const char* symbol;
    uint32_t offset, size, flags;
    void* commands;
    uint8_t blend[2];
} MeleeWebActionRow;
typedef struct MeleeWebWaitChoice { uint32_t motion, weight; } MeleeWebWaitChoice;
typedef struct MeleeWebNativeActionRows MeleeWebNativeActionRows;
MeleeWebNativeActionRows* melee_web_action_rows_create(const MeleeWebActionRow*, size_t count,
    const MeleeWebWaitChoice*, size_t wait_count);
void melee_web_action_rows_destroy(MeleeWebNativeActionRows*);
void* melee_web_action_rows(MeleeWebNativeActionRows*);
size_t melee_web_action_row_count(const MeleeWebNativeActionRows*);
void* melee_web_action_identity(MeleeWebNativeActionRows*, size_t motion);
void* melee_web_action_blends(MeleeWebNativeActionRows*);
void* melee_web_action_waits(MeleeWebNativeActionRows*);
#ifdef __cplusplus
}
#endif
#endif
