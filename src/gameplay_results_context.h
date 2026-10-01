#ifndef MELEE_WEB_GAMEPLAY_RESULTS_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_RESULTS_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#include <dolphin/pad.h>
#include "gameplay_compat.h"

struct ResultsMatchInfo;

#include "gameplay_audio.h"
#include "gameplay_pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebResultsContext MeleeWebResultsContext;

#define MELEE_WEB_RESULTS_CAMERA_POOL_EVENT_CAPACITY 8
#define MELEE_WEB_RESULTS_CAMERA_SUBJECT_EVENT_CAPACITY 128

typedef struct MeleeWebResultsCameraPoolEvent {
    char phase[80];
    uint32_t source_tick;
    int32_t scene_entered;
    int32_t subject_count;
    const void* source_free;
    const void* source_pool;
    const void* source_active;
    const void* source_tail;
    const void* owner_pool;
    const void* context_pool;
    const void* expected_pool;
    uint32_t allocation_generation;
    uint32_t generation_before_onenter;
    uint32_t expected_generation;
} MeleeWebResultsCameraPoolEvent;

typedef struct MeleeWebResultsCameraSubjectEvent {
    char phase[80];
    uint32_t source_tick;
    int32_t scene_entered;
    const void* subject;
    const void* subject_prev;
    const void* subject_next;
    const void* source_free;
    const void* source_pool;
    const void* source_active;
    const void* source_tail;
    const void* owner_pool;
    const void* context_pool;
    uint32_t allocation_generation;
    int32_t subject_in_pool;
    int32_t subject_prev_in_pool;
    int32_t subject_next_in_pool;
    int32_t free_in_pool;
    int32_t active_in_pool;
    int32_t tail_in_pool;
} MeleeWebResultsCameraSubjectEvent;

/* Read-only pointer snapshots around source Results OnEnter and native
 * collision adoption. These do not establish ownership or relax its guards. */
typedef struct MeleeWebResultsCameraEntrySnapshot {
    const void* saved_pool_at_context_begin;
    const void* source_pool_before_onenter;
    const void* owner_pool_before_onenter;
    const void* source_pool_after_onenter;
    const void* source_pool_after_collision_adoption;
    const void* context_pool_after_adoption;
    const void* owner_pool_after_adoption;
    uint32_t source_camera_allocation_generation_before_onenter;
    uint32_t source_camera_allocation_generation_after_onenter;
    int32_t source_camera_allocation_subject_count_after_onenter;
    uint32_t source_camera_allocation_generation_after_collision_adoption;
    int32_t source_camera_allocation_subject_count_after_collision_adoption;
    uint32_t source_camera_pool_event_count;
    int32_t source_camera_pool_event_overflow;
    MeleeWebResultsCameraPoolEvent source_camera_pool_events[
        MELEE_WEB_RESULTS_CAMERA_POOL_EVENT_CAPACITY];
    uint32_t source_camera_subject_event_count;
    int32_t source_camera_subject_event_overflow;
    MeleeWebResultsCameraSubjectEvent source_camera_subject_events[
        MELEE_WEB_RESULTS_CAMERA_SUBJECT_EVENT_CAPACITY];
} MeleeWebResultsCameraEntrySnapshot;

/* Owns one original Results scene inside an already prepared Results world.
 * The caller retains the prepared C++ world/assets/audio owners through end(). */
MeleeWebResultsContext* melee_web_results_context_begin(
    const struct ResultsMatchInfo* match, uint32_t seed,
    const MeleeWebPadState* input, MeleeWebAudio* audio,
    char* error, size_t error_size);
int melee_web_results_context_tick(MeleeWebResultsContext*, const PADStatus raw[4],
    char* error, size_t error_size);
int melee_web_results_context_draw(MeleeWebResultsContext*, char* error, size_t error_size);
int melee_web_results_context_requested(const MeleeWebResultsContext*);
uint32_t melee_web_results_context_random_seed(const MeleeWebResultsContext*);
uint32_t melee_web_results_context_ticks(const MeleeWebResultsContext*);
int melee_web_results_context_camera_entry_snapshot(
    const MeleeWebResultsContext*, MeleeWebResultsCameraEntrySnapshot*);
/* Camera source list transitions are observed before and after the authored
 * operation. Invalid/out-of-pool pointers are recorded without dereferencing
 * them, preserving the operation itself for the bounded reproducer. */
void melee_web_results_camera_subject_list_trace(const char*, const void*);
/* Run scene OnExit before the enclosing mode OnExit. Keep scene assets/heap
 * resident until end(), matching the original scene-manager ordering. */
int melee_web_results_context_exit(MeleeWebResultsContext*, char*, size_t);
/* Read-only enclosing-mode handoff check after scene OnExit and before end().
 * Reuses the exact camera triple and reports the caller's boundary phase. */
int melee_web_results_context_check_handoff(const char* phase, char*, size_t);
int melee_web_results_context_end(MeleeWebResultsContext*,
    MeleeWebResultsCameraEntrySnapshot*, char* error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
