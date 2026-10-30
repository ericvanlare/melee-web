#ifndef MELEE_WEB_GAMEPLAY_CPU_INTERVAL_H
#define MELEE_WEB_GAMEPLAY_CPU_INTERVAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private diagnostic interval barrier. No restore or browser export. The
 * aggregate adapter proves the completed main-thread boundary before begin.
 * Every attempted nonlocal effect is latched BEFORE its provider mutation.
 * A refusal never means the source operation succeeded; the aggregate fails.
 * Cleanup disarms first, so ordinary teardown can still be attempted. */
enum MeleeWebCpuIntervalEffect {
 MELEE_WEB_CPU_EFFECT_NONE=0,
 MELEE_WEB_CPU_EFFECT_SOURCE_FILE=1,
 MELEE_WEB_CPU_EFFECT_SSM=2,
 MELEE_WEB_CPU_EFFECT_HPS=3,
 MELEE_WEB_CPU_EFFECT_MOTOR=4,
};
#define MELEE_WEB_CPU_MOTOR_MAX_SPAN 7u
#define MELEE_WEB_CPU_MOTOR_MAX_EVENTS 28u
enum MeleeWebCpuMotorLane {
 MELEE_WEB_CPU_MOTOR_LANE_NONE=0,
 MELEE_WEB_CPU_MOTOR_LANE_BASELINE=1,
 MELEE_WEB_CPU_MOTOR_LANE_REPLAY=2,
 MELEE_WEB_CPU_MOTOR_LANE_CORRECTED=3,
};
enum MeleeWebCpuMotorSequenceState {
 MELEE_WEB_CPU_MOTOR_SEQUENCE_ABSENT=0,
 MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE=1,
 MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE=2,
};
enum MeleeWebCpuMotorCommitState {
 MELEE_WEB_CPU_MOTOR_COMMIT_PENDING=0,
 MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY=1,
 MELEE_WEB_CPU_MOTOR_COMMIT_REFUSED=2,
};
enum MeleeWebCpuMotorFailure {
 MELEE_WEB_CPU_MOTOR_FAILURE_NONE=0,
 MELEE_WEB_CPU_MOTOR_FAILURE_INVALID_OWNER=1,
 MELEE_WEB_CPU_MOTOR_FAILURE_STALE_TOKEN=2,
 MELEE_WEB_CPU_MOTOR_FAILURE_FRAME=3,
 MELEE_WEB_CPU_MOTOR_FAILURE_COMMAND=4,
 MELEE_WEB_CPU_MOTOR_FAILURE_DUPLICATE_CHANNEL=5,
 MELEE_WEB_CPU_MOTOR_FAILURE_OVERFLOW=6,
 MELEE_WEB_CPU_MOTOR_FAILURE_INCOMPLETE=7,
 MELEE_WEB_CPU_MOTOR_FAILURE_LANE=8,
 MELEE_WEB_CPU_MOTOR_FAILURE_COMMIT=9,
};
typedef struct MeleeWebCpuMotorEvent {
 uint32_t source_frame,channel,command,ordinal;
} MeleeWebCpuMotorEvent;
typedef struct MeleeWebCpuMotorSequence {
 uint32_t state,span,first_frame,frames_seen,event_count;
 uint32_t token_lo,token_hi,world_lo,world_hi,context_epoch_lo,context_epoch_hi;
 MeleeWebCpuMotorEvent events[MELEE_WEB_CPU_MOTOR_MAX_EVENTS];
} MeleeWebCpuMotorSequence;
typedef struct MeleeWebCpuMotorEvidence {
 uint32_t schema,bytes,mode,selected_lane,published,commit_state,failure,reserved;
 MeleeWebCpuMotorSequence baseline,selected;
} MeleeWebCpuMotorEvidence;
typedef struct MeleeWebCpuMotorCommitReceipt {
 uint32_t schema,bytes,status,selected_lane,published,event_count,first_frame,span;
 uint32_t token_lo,token_hi,world_lo,world_hi,context_epoch_lo,context_epoch_hi;
 uint32_t event_hash_lo,event_hash_hi;
} MeleeWebCpuMotorCommitReceipt;
/* Non-diagnostic consumers retain their original transport linkage: the
 * inactive checks fold away and require no new gameplay library dependency. */
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) || defined(MELEE_WEB_CPU_INTERVAL_IMPLEMENTATION)
uint64_t melee_web_cpu_interval_begin(void);
int melee_web_cpu_interval_admit(uint64_t token);
/* Raw armed state, including an interval whose first effect already failed. */
int melee_web_cpu_interval_active(void);
/* Identity only: a failed armed interval retains its token. Call admit before
 * source work; zero means there is no active diagnostic interval. */
uint64_t melee_web_cpu_interval_token(void);
int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect);
unsigned melee_web_cpu_interval_failure(void);
int melee_web_cpu_interval_active_failure(void);
int melee_web_cpu_interval_end(uint64_t token);
/* Reusable capture_span-only, explicitly non-publishing HSD rumble journal. */
int melee_web_cpu_interval_motor_capture_begin(uint64_t token,uint64_t world,
    const void* context,uint64_t context_epoch,uint32_t first_frame,uint32_t span);
int melee_web_cpu_interval_motor_frame_begin(uint64_t token,const void* context,
    uint64_t world,uint64_t context_epoch,uint64_t source_frame);
int melee_web_cpu_interval_motor_frame_end(uint64_t token,const void* context,
    uint64_t world,uint64_t context_epoch,uint64_t source_frame);
int melee_web_cpu_interval_motor_baseline_seal(uint64_t token,uint32_t post_frame);
int melee_web_cpu_interval_motor_restore_admit(uint64_t token,uint32_t lane);
void melee_web_cpu_interval_motor_restore_commit(uint64_t token,uint32_t lane);
int melee_web_cpu_interval_motor_selected_finish(uint64_t token,uint32_t post_frame);
int melee_web_cpu_interval_motor_commit_admit(uint64_t token,uint64_t world,
    const void* context,uint64_t context_epoch,uint32_t first_frame,uint32_t span);
void melee_web_cpu_interval_motor_capture_commit(uint64_t token);
/* Additive private retained-history journal. Legacy capture/restore above keeps
 * its fixed span and single restore. History records one bounded source prefix,
 * resets selected output only after the aggregate PRE commit succeeds, then
 * permits a later forward incarnation to append to that selected prefix. */
int melee_web_cpu_interval_motor_history_capture_begin(uint64_t token,uint64_t world,
    const void* context,uint64_t context_epoch,uint32_t pre_frame);
int melee_web_cpu_interval_motor_history_begin_incarnation(uint64_t token,
    uint64_t incarnation,uint32_t source_frame);
int melee_web_cpu_interval_motor_history_seal(uint64_t token,uint64_t incarnation,
    uint32_t actual_end);
int melee_web_cpu_interval_motor_history_restore_admit(uint64_t token,
    uint64_t incarnation,uint32_t actual_end);
/* Unchecked post-aggregate bookkeeping. Caller must have just passed the pure
 * history_restore_admit check and committed every source owner successfully. */
void melee_web_cpu_interval_motor_history_restore_commit(uint64_t token,
    uint64_t incarnation,uint32_t actual_end);
int melee_web_cpu_interval_motor_history_replay_finish(uint64_t token,
    uint64_t incarnation,uint32_t actual_end);
int melee_web_cpu_interval_motor_history_retire_admit(uint64_t token,
    uint64_t incarnation,uint32_t actual_end);
void melee_web_cpu_interval_motor_history_retire_commit(uint64_t token,
    uint64_t incarnation,uint32_t actual_end);
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_history_evidence_for(
    uint64_t token,uint64_t world,const void* context,uint64_t context_epoch,
    uint32_t pre_frame,uint64_t incarnation,uint32_t actual_end);
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence(void);
/* The current capture endpoint is readable only through the exact active
 * capsule identity. The adapter must not expose process-global evidence as
 * if it belonged to an arbitrary endpoint. */
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence_for(
    uint64_t token,uint64_t world,const void* context,uint64_t context_epoch,
    uint32_t first_frame,uint32_t span);
uint32_t melee_web_cpu_interval_motor_evidence_bytes(void);
const MeleeWebCpuMotorCommitReceipt* melee_web_cpu_interval_motor_commit_receipt(void);
uint32_t melee_web_cpu_interval_motor_commit_receipt_bytes(void);
#else
static inline uint64_t melee_web_cpu_interval_begin(void){return 0;}
static inline int melee_web_cpu_interval_admit(uint64_t token){(void)token;return 0;}
static inline int melee_web_cpu_interval_active(void){return 0;}
static inline uint64_t melee_web_cpu_interval_token(void){return 0;}
static inline int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect effect){(void)effect;return 1;}
static inline unsigned melee_web_cpu_interval_failure(void){return 0;}
static inline int melee_web_cpu_interval_active_failure(void){return 0;}
static inline int melee_web_cpu_interval_end(uint64_t token){(void)token;return 0;}
static inline int melee_web_cpu_interval_motor_capture_begin(uint64_t t,uint64_t w,const void* c,uint64_t e,uint32_t f,uint32_t s){(void)t;(void)w;(void)c;(void)e;(void)f;(void)s;return 0;}
static inline int melee_web_cpu_interval_motor_frame_begin(uint64_t t,const void* c,uint64_t w,uint64_t e,uint64_t f){(void)t;(void)c;(void)w;(void)e;(void)f;return 1;}
static inline int melee_web_cpu_interval_motor_frame_end(uint64_t t,const void* c,uint64_t w,uint64_t e,uint64_t f){(void)t;(void)c;(void)w;(void)e;(void)f;return 1;}
static inline int melee_web_cpu_interval_motor_baseline_seal(uint64_t t,uint32_t f){(void)t;(void)f;return 1;}
static inline int melee_web_cpu_interval_motor_restore_admit(uint64_t t,uint32_t l){(void)t;(void)l;return 1;}
static inline void melee_web_cpu_interval_motor_restore_commit(uint64_t t,uint32_t l){(void)t;(void)l;}
static inline int melee_web_cpu_interval_motor_selected_finish(uint64_t t,uint32_t f){(void)t;(void)f;return 1;}
static inline int melee_web_cpu_interval_motor_commit_admit(uint64_t t,uint64_t w,const void* c,uint64_t e,uint32_t f,uint32_t s){(void)t;(void)w;(void)c;(void)e;(void)f;(void)s;return 1;}
static inline void melee_web_cpu_interval_motor_capture_commit(uint64_t t){(void)t;}
static inline int melee_web_cpu_interval_motor_history_capture_begin(uint64_t t,uint64_t w,const void* c,uint64_t e,uint32_t f){(void)t;(void)w;(void)c;(void)e;(void)f;return 0;}
static inline int melee_web_cpu_interval_motor_history_begin_incarnation(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;return 0;}
static inline int melee_web_cpu_interval_motor_history_seal(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;return 0;}
static inline int melee_web_cpu_interval_motor_history_restore_admit(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;return 0;}
static inline void melee_web_cpu_interval_motor_history_restore_commit(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;}
static inline int melee_web_cpu_interval_motor_history_replay_finish(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;return 0;}
static inline int melee_web_cpu_interval_motor_history_retire_admit(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;return 0;}
static inline void melee_web_cpu_interval_motor_history_retire_commit(uint64_t t,uint64_t i,uint32_t f){(void)t;(void)i;(void)f;}
static inline const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_history_evidence_for(uint64_t t,uint64_t w,const void* c,uint64_t e,uint32_t f,uint64_t i,uint32_t end){(void)t;(void)w;(void)c;(void)e;(void)f;(void)i;(void)end;return (const MeleeWebCpuMotorEvidence*)0;}
static inline const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence(void){return (const MeleeWebCpuMotorEvidence*)0;}
static inline const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence_for(uint64_t t,uint64_t w,const void* c,uint64_t e,uint32_t f,uint32_t s){(void)t;(void)w;(void)c;(void)e;(void)f;(void)s;return (const MeleeWebCpuMotorEvidence*)0;}
static inline uint32_t melee_web_cpu_interval_motor_evidence_bytes(void){return 0;}
static inline const MeleeWebCpuMotorCommitReceipt* melee_web_cpu_interval_motor_commit_receipt(void){return (const MeleeWebCpuMotorCommitReceipt*)0;}
static inline uint32_t melee_web_cpu_interval_motor_commit_receipt_bytes(void){return 0;}
#endif
void melee_web_cpu_interval_pad_motor(uint32_t channel,uint32_t command);
#ifdef __cplusplus
}
#endif
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) || defined(MELEE_WEB_CPU_INTERVAL_IMPLEMENTATION)
#if defined(__cplusplus)
static_assert(sizeof(MeleeWebCpuMotorEvent)==16,"motor event ABI");
static_assert(sizeof(MeleeWebCpuMotorSequence)==492,"motor sequence ABI");
static_assert(sizeof(MeleeWebCpuMotorEvidence)==1016,"motor evidence ABI");
static_assert(sizeof(MeleeWebCpuMotorCommitReceipt)==64,"motor receipt ABI");
#else
_Static_assert(sizeof(MeleeWebCpuMotorEvent)==16,"motor event ABI");
_Static_assert(sizeof(MeleeWebCpuMotorSequence)==492,"motor sequence ABI");
_Static_assert(sizeof(MeleeWebCpuMotorEvidence)==1016,"motor evidence ABI");
_Static_assert(sizeof(MeleeWebCpuMotorCommitReceipt)==64,"motor receipt ABI");
#endif
#endif
#endif
