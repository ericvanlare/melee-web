#ifndef MELEE_WEB_PIPELINE_PROVENANCE_H
#define MELEE_WEB_PIPELINE_PROVENANCE_H

/* This header is intentionally C-compatible: Aurora and the game-side owner
 * exchange copied values at the source/FIFO boundary.  It contains no host
 * addresses and no descriptor bytes.  The recorder is a private opt-in build
 * feature and must never enter the public runtime. */
#if defined(MELEE_WEB_PUBLIC_RUNTIME)
#error "pipeline provenance is private and cannot be compiled into the public runtime"
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MELEE_WEB_PIPELINE_PROVENANCE_SCHEMA "melee-web-pipeline-use-v1"
#define MELEE_WEB_PIPELINE_PROVENANCE_VERSION 1u

/* These are hard upper bounds.  A private capture may request lower bounds,
 * but it can never make recorder storage or a JSON chunk unbounded. */
#define MELEE_WEB_PIPELINE_MAX_RECORDS 4096u
#define MELEE_WEB_PIPELINE_MAX_DESCRIPTORS 1024u
#define MELEE_WEB_PIPELINE_MAX_SCOPES 128u
#define MELEE_WEB_PIPELINE_MAX_EXECUTION_DEPTH 32u
#define MELEE_WEB_PIPELINE_MAX_TYPE_BYTES 1048576u
#define MELEE_WEB_PIPELINE_MAX_DESCRIPTOR_MEMO_BYTES (16u * 1024u * 1024u)
#define MELEE_WEB_PIPELINE_MAX_JSON_BYTES (4u * 1024u * 1024u)

typedef enum MeleeWebPipelineScene {
    MELEE_WEB_PIPELINE_SCENE_BOOT = 1,
    MELEE_WEB_PIPELINE_SCENE_CSS = 2,
    MELEE_WEB_PIPELINE_SCENE_SSS = 3,
    MELEE_WEB_PIPELINE_SCENE_MATCH = 4,
    MELEE_WEB_PIPELINE_SCENE_TEARDOWN = 5,
    MELEE_WEB_PIPELINE_SCENE_RETURN = 6,
    MELEE_WEB_PIPELINE_SCENE_RESULTS = 7,
    MELEE_WEB_PIPELINE_SCENE_PRIZE = 8
} MeleeWebPipelineScene;

typedef enum MeleeWebPipelinePhase {
    MELEE_WEB_PIPELINE_PHASE_PREPARATION = 1,
    MELEE_WEB_PIPELINE_PHASE_ENTRY = 2,
    MELEE_WEB_PIPELINE_PHASE_READY = 3,
    MELEE_WEB_PIPELINE_PHASE_INTERACTIVE = 4,
    MELEE_WEB_PIPELINE_PHASE_DEATH = 5,
    MELEE_WEB_PIPELINE_PHASE_RESPAWN = 6,
    MELEE_WEB_PIPELINE_PHASE_ENDING = 7,
    MELEE_WEB_PIPELINE_PHASE_TEARDOWN = 8,
    MELEE_WEB_PIPELINE_PHASE_RETURN = 9
} MeleeWebPipelinePhase;

/* A caller which cannot name an individual owner must use ROUTE_COMPOSITE.
 * UNKNOWN is reserved for an explicitly unknown owner and requires
 * owner_explicit_unknown to be nonzero. */
typedef enum MeleeWebPipelineOwnerKind {
    MELEE_WEB_PIPELINE_OWNER_UNKNOWN = 0,
    MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE = 1,
    MELEE_WEB_PIPELINE_OWNER_MENU_SCENE = 2,
    MELEE_WEB_PIPELINE_OWNER_FIGHTER = 3,
    MELEE_WEB_PIPELINE_OWNER_STAGE = 4,
    MELEE_WEB_PIPELINE_OWNER_EFFECT = 5,
    MELEE_WEB_PIPELINE_OWNER_HUD = 6,
    MELEE_WEB_PIPELINE_OWNER_ITEM = 7,
    MELEE_WEB_PIPELINE_OWNER_TEARDOWN = 8
} MeleeWebPipelineOwnerKind;

typedef enum MeleeWebPipelineEventKind {
    MELEE_WEB_PIPELINE_EVENT_SCOPE_BEGIN = 1,
    MELEE_WEB_PIPELINE_EVENT_SCOPE_END = 2,
    MELEE_WEB_PIPELINE_EVENT_LAST_REF = 3,
    MELEE_WEB_PIPELINE_EVENT_READY = 4,
    MELEE_WEB_PIPELINE_EVENT_PENDING = 5,
    MELEE_WEB_PIPELINE_EVENT_CREATE = 6,
    MELEE_WEB_PIPELINE_EVENT_IMPORT = 7,
    MELEE_WEB_PIPELINE_EVENT_DRAW_CACHE_REUSE = 8,
    MELEE_WEB_PIPELINE_EVENT_MERGE = 9,
    MELEE_WEB_PIPELINE_EVENT_PACKET_USE = 10
} MeleeWebPipelineEventKind;

typedef enum MeleeWebPipelineOutcome {
    MELEE_WEB_PIPELINE_OUTCOME_SCOPE_OPEN = 1,
    MELEE_WEB_PIPELINE_OUTCOME_SCOPE_CLOSED = 2,
    MELEE_WEB_PIPELINE_OUTCOME_LAST_REF = 3,
    MELEE_WEB_PIPELINE_OUTCOME_READY = 4,
    MELEE_WEB_PIPELINE_OUTCOME_PENDING = 5,
    MELEE_WEB_PIPELINE_OUTCOME_CREATED = 6,
    MELEE_WEB_PIPELINE_OUTCOME_IMPORTED = 7,
    MELEE_WEB_PIPELINE_OUTCOME_REUSED = 8,
    MELEE_WEB_PIPELINE_OUTCOME_MERGED = 9,
    MELEE_WEB_PIPELINE_OUTCOME_USED = 10
} MeleeWebPipelineOutcome;

typedef enum MeleeWebPipelineInvalidReason {
    MELEE_WEB_PIPELINE_INVALID_NONE = 0,
    MELEE_WEB_PIPELINE_INVALID_NOT_STARTED = 1,
    MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT = 2,
    MELEE_WEB_PIPELINE_INVALID_INVALID_CONTEXT = 3,
    MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST = 4,
    MELEE_WEB_PIPELINE_INVALID_OVERFLOW = 5,
    MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW = 6,
    MELEE_WEB_PIPELINE_INVALID_STALE_CAPTURE = 7,
    MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE = 8,
    MELEE_WEB_PIPELINE_INVALID_STALE_RENDERER = 9,
    MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE = 10,
    MELEE_WEB_PIPELINE_INVALID_LIFECYCLE = 11,
    MELEE_WEB_PIPELINE_INVALID_JSON_OVERFLOW = 12,
    MELEE_WEB_PIPELINE_INVALID_OUTPUT_TOO_SMALL = 13,
    MELEE_WEB_PIPELINE_INVALID_THREAD_OVERFLOW = 14
} MeleeWebPipelineInvalidReason;

typedef struct MeleeWebPipelinePlayerContext {
    uint32_t character;
    uint32_t fighter_kind;
    uint32_t costume;
    uint32_t subcolor;
    uint32_t effect_bank;
    int32_t motion_id;
    int32_t stocks;
} MeleeWebPipelinePlayerContext;

/* All source provenance which is written to JSON is represented by these
 * numeric fields.  Owner IDs and effect banks are source table identities,
 * never host pointers. */
typedef struct MeleeWebPipelineSourceContext {
    uint32_t scene;
    uint32_t phase;
    uint64_t world_generation;
    uint64_t route_epoch;
    uint64_t source_tick;
    uint64_t coverage_case_id;
    uint8_t input_binding_sha256[32];
    uint8_t input_binding_sha256_present;
    uint8_t context_reserved[7];
    uint32_t stage;
    uint32_t ground;
    uint32_t hud_layout;
    /* Optional source render dispatch identity.  Zero means that the caller
     * has no classifier for this scope; it is never inferred from a pointer. */
    uint32_t gobj_classifier;
    uint32_t gx_link;
    uint32_t render_pass;
    uint32_t active_player_count;
    MeleeWebPipelinePlayerContext players[4];
    uint32_t owner_kind;
    uint32_t owner_id;
    uint32_t owner_effect_bank;
    uint8_t owner_explicit_unknown;
    uint8_t reserved[3];
} MeleeWebPipelineSourceContext;

/* A source token is deliberately a copied POD.  The context remains usable by
 * a worker after source_scope_end; capture_generation is checked when the
 * worker pushes it. */
typedef struct MeleeWebPipelineSourceToken {
    MeleeWebPipelineSourceContext context;
    uint64_t capture_generation;
    uint64_t device_generation;
    uint64_t renderer_generation;
    uint64_t frame_id;
    uint64_t packet_id;
    uint64_t scope_id;
    uint64_t token_nonce;
    uint8_t closed;
    uint8_t valid;
    uint8_t reserved[6];
} MeleeWebPipelineSourceToken;

/* This is the copied token carried by a deferred FIFO/frame/packet. */
typedef struct MeleeWebPipelineExecutionToken {
    MeleeWebPipelineSourceContext context;
    uint64_t capture_generation;
    uint64_t device_generation;
    uint64_t renderer_generation;
    uint64_t frame_id;
    uint64_t packet_id;
    uint64_t scope_id;
    uint64_t token_nonce;
    uint64_t execution_id;
    uint8_t valid;
    uint8_t reserved[7];
} MeleeWebPipelineExecutionToken;

typedef struct MeleeWebPipelineRecorderConfig {
    uint32_t max_records;
    uint32_t max_descriptors;
    uint32_t max_scopes;
    uint32_t max_json_bytes;
    /* The explicit boot context is copied at create time and is also the
     * allowed context for background import before a source scope exists. */
    MeleeWebPipelineSourceContext boot_context;
} MeleeWebPipelineRecorderConfig;

typedef struct MeleeWebPipelineStatus {
    uint8_t capture_active;
    uint8_t valid;
    uint8_t final;
    uint8_t reserved;
    uint32_t invalid_reason;
    uint64_t capture_generation;
    /* Current generation while active, otherwise the last completed
     * generation retained for capture validation. */
    uint64_t renderer_generation;
    uint64_t device_generation;
    uint64_t next_sequence;
    uint64_t errors;
    uint64_t dropped;
    uint64_t total_records;
    uint64_t drained_records;
    uint32_t pending_records;
    uint32_t descriptor_count;
    uint32_t open_scopes;
    uint32_t active_execution_depth;
    uint64_t frame_id;
    uint64_t packet_id;
} MeleeWebPipelineStatus;

typedef struct MeleeWebPipelineRecorder MeleeWebPipelineRecorder;

/* Return 1 on success and 0 on a rejected operation.  On rejection, error is
 * populated when provided and the capture becomes sticky-invalid. */
MeleeWebPipelineRecorder* melee_web_pipeline_recorder_create(
    const MeleeWebPipelineRecorderConfig* config, char* error, size_t error_size);
int melee_web_pipeline_recorder_destroy(MeleeWebPipelineRecorder* recorder,
    char* error, size_t error_size);

/* Capture must begin before Aurora/device initialization.  begin copies the
 * supplied BOOT/PREPARATION context and invalidates tokens from prior runs. */
int melee_web_pipeline_capture_begin(MeleeWebPipelineRecorder* recorder,
    const MeleeWebPipelineSourceContext* boot_context, char* error, size_t error_size);
int melee_web_pipeline_capture_end(MeleeWebPipelineRecorder* recorder,
    char* error, size_t error_size);

/* Device and renderer generations are explicit and monotonic within a
 * capture.  A token/event carrying an old generation is rejected. */
int melee_web_pipeline_device_begin(MeleeWebPipelineRecorder* recorder,
    uint64_t generation, char* error, size_t error_size);
int melee_web_pipeline_device_end(MeleeWebPipelineRecorder* recorder,
    uint64_t generation, char* error, size_t error_size);
int melee_web_pipeline_renderer_begin(MeleeWebPipelineRecorder* recorder,
    uint64_t generation, char* error, size_t error_size);
int melee_web_pipeline_renderer_end(MeleeWebPipelineRecorder* recorder,
    uint64_t generation, char* error, size_t error_size);
int melee_web_pipeline_frame_begin(MeleeWebPipelineRecorder* recorder,
    uint64_t frame_id, char* error, size_t error_size);
int melee_web_pipeline_frame_end(MeleeWebPipelineRecorder* recorder,
    uint64_t frame_id, char* error, size_t error_size);
int melee_web_pipeline_packet_begin(MeleeWebPipelineRecorder* recorder,
    uint64_t packet_id, char* error, size_t error_size);
int melee_web_pipeline_packet_end(MeleeWebPipelineRecorder* recorder,
    uint64_t packet_id, char* error, size_t error_size);

/* Scope begin/end emits numeric-context records.  Ending a scope does not
 * invalidate a copied source token; it only closes the source-side pair. */
int melee_web_pipeline_source_scope_begin(MeleeWebPipelineRecorder* recorder,
    const MeleeWebPipelineSourceContext* context,
    MeleeWebPipelineSourceToken* token, char* error, size_t error_size);
int melee_web_pipeline_source_scope_end(MeleeWebPipelineRecorder* recorder,
    MeleeWebPipelineSourceToken* token, char* error, size_t error_size);
int melee_web_pipeline_execution_push(MeleeWebPipelineRecorder* recorder,
    const MeleeWebPipelineSourceToken* token,
    MeleeWebPipelineExecutionToken* execution, char* error, size_t error_size);
int melee_web_pipeline_execution_pop(MeleeWebPipelineRecorder* recorder,
    const MeleeWebPipelineExecutionToken* execution,
    char* error, size_t error_size);

/* Record a lookup or use edge.  The blob is hashed immediately and never
 * retained or exported.  A NULL blob is valid only with blob_length == 0 and
 * produces the SHA-256 digest of the empty string. */
int melee_web_pipeline_observe(MeleeWebPipelineRecorder* recorder,
    uint32_t event_kind, uint32_t type, uint64_t pipeline_ref,
    uint32_t config_version, const void* blob, size_t blob_length,
    uint32_t outcome, char* error, size_t error_size);
/* Worker/draw hooks use the copied frame/packet IDs carried with their token.
 * The IDs are metadata only and do not alter source simulation. */
int melee_web_pipeline_observe_at(MeleeWebPipelineRecorder* recorder,
    uint32_t event_kind, uint32_t type, uint64_t pipeline_ref,
    uint32_t config_version, const void* blob, size_t blob_length,
    uint32_t outcome, uint64_t frame_id, uint64_t packet_id,
    char* error, size_t error_size);

/* Integrators use this for a bounded FIFO/token failure that has no pipeline
 * row to observe.  It only latches the capture invalid and increments errors;
 * it has no effect on gameplay or Aurora state. */
int melee_web_pipeline_invalidate(MeleeWebPipelineRecorder* recorder,
    uint32_t reason, const char* message, char* error, size_t error_size);

/* Drain removes a bounded prefix of pending records.  Sequence numbers in a
 * chunk are contiguous.  final_snapshot latches an unpaired-scope error when
 * source/execution pairs remain open, but still exports retained evidence and
 * leaves the capture final so the invalid result can be inspected.  The output
 * is private JSON using MELEE_WEB_PIPELINE_PROVENANCE_SCHEMA. */
int melee_web_pipeline_json_drain(MeleeWebPipelineRecorder* recorder,
    char* output, size_t output_size, size_t* written, int final_snapshot,
    char* error, size_t error_size);
int melee_web_pipeline_json_status(const MeleeWebPipelineRecorder* recorder,
    char* output, size_t output_size, size_t* written,
    char* error, size_t error_size);
int melee_web_pipeline_status(const MeleeWebPipelineRecorder* recorder,
    MeleeWebPipelineStatus* status, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
