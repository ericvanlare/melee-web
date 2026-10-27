#ifndef MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#define MELEE_WEB_GAMEPLAY_STAGE_LAST_H
#include <stddef.h>
#include <stdint.h>
#include "gameplay_effect_banks.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_stadium_display_owner.h"
#include "gameplay_match_context.h"
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebStageLast MeleeWebStageLast;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
typedef struct MeleeWebStadiumGoAlignmentEvent {
    uint64_t generation;
    uint64_t world_ticks;
    uint32_t source_frame;
    uint32_t sequence;
    uintptr_t object_identity;
    uintptr_t proc_identity;
    int32_t callback_index;
    int32_t source_branch;
    int32_t map2_gate;
    int32_t display_mode;
    int32_t remap_branch;
    int32_t callback_identity;
    int32_t hud_enabled;
    uint32_t source_seed;
    uint32_t source_seed_valid;
} MeleeWebStadiumGoAlignmentEvent;
typedef struct MeleeWebStadiumGoAlignmentSnapshot {
    uint64_t generation;
    uint64_t armed_world_ticks;
    uint32_t armed_source_frame;
    uint32_t armed;
    uint32_t failure;
    uint32_t expected_branch;
    uint32_t next_sequence;
    MeleeWebStadiumGoAlignmentEvent stage_before;
    MeleeWebStadiumGoAlignmentEvent stage_after;
    MeleeWebStadiumGoAlignmentEvent go_after;
    MeleeWebStadiumGoAlignmentEvent hud_after;
} MeleeWebStadiumGoAlignmentSnapshot;
typedef void (*MeleeWebStadiumGoAlignmentHudCallback)(int);
#define MELEE_WEB_STADIUM_SETUP_CONSTRUCTOR_PHASE_CAPACITY 5U
typedef enum MeleeWebStadiumSetupConstructorPhase {
    MELEE_WEB_STADIUM_SETUP_PHASE_ENTRY = 1,
    MELEE_WEB_STADIUM_SETUP_PHASE_PARAMETERS = 2,
    MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL = 3,
    MELEE_WEB_STADIUM_SETUP_PHASE_UP_RETURN = 4,
    MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_CALL = 5,
    MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_RETURN = 6,
    MELEE_WEB_STADIUM_SETUP_PHASE_COMMIT = 7
} MeleeWebStadiumSetupConstructorPhase;
typedef struct MeleeWebStadiumSetupConstructorPhaseEvent {
    uint32_t ordinal;
    uint32_t phase;
    uint32_t parameter_values_valid;
    int32_t parameter_x0;
    int32_t parameter_x4;
    uint32_t scalar_value_valid;
    int32_t scalar_value;
    uint32_t source_seed_valid;
    uint32_t source_seed;
} MeleeWebStadiumSetupConstructorPhaseEvent;
typedef struct MeleeWebStadiumSetupSnapshot {
    uint32_t version;
    uint64_t world_generation;
    uint64_t world_ticks;
    uint32_t source_frame;
    int32_t map_id;
    int32_t initial_countdown;
    int32_t current_countdown;
    int32_t parameter_x0;
    int32_t parameter_x4;
    uint32_t constructor_randi_call_count;
    uint32_t constructor_random_bound_valid;
    int32_t constructor_random_bound;
    uint32_t constructor_random_result_valid;
    int32_t constructor_random_result;
    uint32_t source_seed_before;
    uint32_t source_seed_after;
    int32_t ground_source_heap_handle;
    uint32_t ground_requested_bytes;
    uint64_t ground_allocation_generation;
    int32_t object_source_heap_handle;
    uint32_t object_requested_bytes;
    uint64_t object_allocation_generation;
    uint32_t constructor_callback_count;
    uint32_t source_parameter_roots_agree;
    uint32_t constructor_phase_count;
    MeleeWebStadiumSetupConstructorPhaseEvent constructor_phases[
        MELEE_WEB_STADIUM_SETUP_CONSTRUCTOR_PHASE_CAPACITY];
} MeleeWebStadiumSetupSnapshot;
#define MELEE_WEB_STADIUM_SETUP_SNAPSHOT_VERSION 2U
/* Strict zero-tick snapshot after prepared Ready ownership, before its first
 * source callback. It does not extend GO/idle snapshots or run source work. */
int melee_web_stage_last_stadium_setup_snapshot(
    const MeleeWebStageLast*,const void*,MeleeWebStadiumSetupSnapshot*,char*,size_t);
/* Called only by the existing original map-2 OnInit callback around its
 * existing parameter read and optional randi call. */
int melee_web_stage_last_stadium_setup_constructor_begin(void*);
void melee_web_stage_last_stadium_setup_constructor_phase(
    void*,uint32_t,int32_t,int32_t,int,int32_t);
void melee_web_stage_last_stadium_setup_constructor_finish(
    void*,int32_t,int32_t,int32_t,int,int32_t,int32_t,int,uint32_t,int,uint32_t,int);
#endif
/* Begin a source-owned stage callback scope for one supported StKind. The
 * caller must have published that stage's GroundParam, native map archive,
 * lights and effect bank first. source_ordered selects the original
 * Stage_802251E8/Stage_8022524C setup and adopts its collision owner. Native
 * descriptors, material programs and particle bank must outlive this scope. */
MeleeWebStageLast* melee_web_stage_begin_kind(int stage_kind, void* yakumono,
    MeleeWebEffectBank*, int defer_start, int source_ordered, char*, size_t);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* Diagnostic-only PStadium boundary: run original source-ordered OnInit,
 * return its live owner scope before camera/OnStart, and leave inspection and
 * teardown to the caller through melee_web_stage_last_end. owner_out is
 * required and receives a live scope when post-E8 ownership must be retained
 * after a failed initialization check. */
MeleeWebStageLast* melee_web_stage_begin_kind_on_init_diagnostic(
    int stage_kind, void* yakumono, MeleeWebEffectBank*,
    MeleeWebStageLast** owner_out, char*, size_t);
/* Separate continuation of the retained diagnostic OnInit owner. Original
 * OnLoad/OnStart run once while maps and the borrowed match camera pool live.
 * On refusal the caller must retain both owners; no cancellation is applied. */
int melee_web_stage_last_stadium_start(MeleeWebStageLast*,MeleeWebMatchContext*,char*,size_t);
/* Source-session OnLoad now, then exactly one original HUD-triggered OnStart.
 * Pre-Ready close cancels only the fully checked untouched prepared lease. */
int melee_web_stage_last_stadium_on_load(MeleeWebStageLast*,char*,size_t);
int melee_web_stage_last_stadium_prepare_ready(MeleeWebStageLast*,MeleeWebMatchContext*,char*,size_t);
int melee_web_stage_last_stadium_cancel_prepared_ready(MeleeWebStageLast*,char*,size_t);
int melee_web_stage_last_stadium_ready_before(int stage_kind,char*,size_t);
int melee_web_stage_last_stadium_ready_after(int stage_kind,char*,size_t);
int melee_web_stage_last_stadium_go_alignment_arm(
    MeleeWebStageLast*,int expected_branch,char*,size_t);
int melee_web_stage_last_stadium_go_alignment_snapshot(
    const MeleeWebStageLast*,MeleeWebStadiumGoAlignmentSnapshot*,char*,size_t);
/* Separate live scalar snapshot for a started diagnostic Stadium idle owner.
 * It does not read or modify the frozen GO event history above. */
int melee_web_stage_last_stadium_idle_snapshot(
    const MeleeWebStageLast*,MeleeWebStadiumIdleSnapshot*,char*,size_t);
void melee_web_stage_last_stadium_go_alignment_note_go(int source_branch);
void melee_web_stage_last_stadium_go_alignment_note_hud(
    int callback_index,MeleeWebStadiumGoAlignmentHudCallback,int hud_enabled);
/* Asset-free reducer for the diagnostic bind-refusal reporting path. It
 * injects the bind failure and E8 journal event, then exercises the real
 * display-owner cancellation guard. The partial owner remains live until the
 * standalone control process exits; no archive or source Stage routine runs. */
int melee_web_stage_last_on_init_bind_refusal_controls(void);
/* Read the already-captured private Stadium map2 journal while its StageLast
 * owner is still active. This copies the existing record; it does not inspect
 * or extend the private Ground layout. Call only before stage_last_end. */
int melee_web_stage_last_stadium_map2_buffer_snapshot(
    const MeleeWebStageLast*, MeleeWebStadiumMap2BufferOwner* out);
/* Snapshot the actual guarded source taps while their StageLast owner is live. */
int melee_web_stage_last_stadium_source_journal_snapshot(
    const MeleeWebStageLast*, MeleeWebStadiumSourceJournal* out);
#endif
/* A successful map-2 DMA unregister begins irreversible StageLast retirement.
 * World pump/tick/draw must refuse source work while a failed close retains
 * this owner. NULL means no StageLast has yet been constructed. */
int melee_web_stage_last_source_work_allowed(const MeleeWebStageLast*);

/* Requires full native map/overrides, stage particle bank64, original effect
 * runtime, numeric stage/collision and original camera contexts already live.
 * These compatibility wrappers select Final Destination. */
MeleeWebStageLast* melee_web_stage_last_begin(void* yakumono,MeleeWebEffectBank* map_bank,char*,size_t);
/* Create stage objects now; original Ready completion starts its manager. */
MeleeWebStageLast* melee_web_stage_last_begin_intro(void*,MeleeWebEffectBank*,char*,size_t);
/* End after source callbacks/GX complete, before effect runtime or map owners. */
int melee_web_stage_last_end(MeleeWebStageLast*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
