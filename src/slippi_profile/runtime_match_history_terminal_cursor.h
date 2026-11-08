/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_CURSOR_H
#define MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_CURSOR_H

#include <stddef.h>
#include <stdint.h>

/* A private traversal witness for the existing arena history owner. The
 * bootstrap tick and original match frame are deliberately separate: an
 * authored VS ending can complete its scheduler traversal without advancing
 * the original match-frame counter. This record grants no source-write or
 * history-continuation authority. */
typedef struct MeleeWebRuntimeMatchHistoryTerminalOwner {
    const void *capsule_identity;
    const void *checkpoint_identity;
    const void *storage_identity;
    const void *context_identity;
    const void *flow_identity;
    const void *audio_identity;
    const void *ticks_identity;
    uint64_t world_generation;
    uint64_t context_epoch;
    uint32_t ticks_bytes;
} MeleeWebRuntimeMatchHistoryTerminalOwner;

typedef struct MeleeWebRuntimeMatchHistoryTerminalCursor {
    MeleeWebRuntimeMatchHistoryTerminalOwner owner;
    uint64_t pre_completed_tick;
    uint64_t completed_tick;
    uint32_t pre_input_ordinal;
    uint32_t pre_source_frame;
    uint32_t input_ordinal;
    uint32_t source_frame;
    uint32_t ending;
    uint32_t complete;
    uint32_t bound;
    uint32_t first_terminal_valid;
    uint64_t first_terminal_completed_tick;
    uint32_t first_terminal_input_ordinal;
    uint32_t first_terminal_source_frame_before;
    uint32_t first_terminal_source_frame_after;
    uint32_t first_terminal_ending_before;
    uint32_t first_terminal_ending_after;
    uint32_t first_terminal_complete_before;
    uint32_t first_terminal_complete_after;
    uint32_t selected_terminal_valid;
    uint64_t selected_terminal_completed_tick;
    uint32_t selected_terminal_input_ordinal;
    uint32_t selected_terminal_source_frame_before;
    uint32_t selected_terminal_source_frame_after;
    uint32_t selected_terminal_ending_before;
    uint32_t selected_terminal_ending_after;
    uint32_t selected_terminal_complete_before;
    uint32_t selected_terminal_complete_after;
} MeleeWebRuntimeMatchHistoryTerminalCursor;

enum { MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_MAX_SPAN = 7 };

static inline int melee_web_runtime_match_history_terminal_owner_equal(
    const MeleeWebRuntimeMatchHistoryTerminalOwner *a,
    const MeleeWebRuntimeMatchHistoryTerminalOwner *b)
{
    return a && b && a->capsule_identity && a->checkpoint_identity &&
        a->storage_identity && a->context_identity && a->flow_identity &&
        a->audio_identity && a->ticks_identity && a->world_generation &&
        a->context_epoch && a->ticks_bytes == sizeof(uint64_t) &&
        a->capsule_identity == b->capsule_identity &&
        a->checkpoint_identity == b->checkpoint_identity &&
        a->storage_identity == b->storage_identity &&
        a->context_identity == b->context_identity &&
        a->flow_identity == b->flow_identity &&
        a->audio_identity == b->audio_identity &&
        a->ticks_identity == b->ticks_identity &&
        a->world_generation == b->world_generation &&
        a->context_epoch == b->context_epoch &&
        a->ticks_bytes == b->ticks_bytes;
}

static inline int melee_web_runtime_match_history_terminal_bind(
    MeleeWebRuntimeMatchHistoryTerminalCursor *cursor,
    const MeleeWebRuntimeMatchHistoryTerminalOwner *owner,
    uint64_t completed_tick, uint32_t input_ordinal,
    uint32_t source_frame)
{
    if (!cursor || !owner || !owner->capsule_identity ||
        !owner->checkpoint_identity || !owner->storage_identity ||
        !owner->context_identity || !owner->flow_identity ||
        !owner->audio_identity || !owner->ticks_identity ||
        !owner->world_generation || !owner->context_epoch ||
        owner->ticks_bytes != sizeof(uint64_t) ||
        input_ordinal != source_frame || source_frame == UINT32_MAX)
        return 0;

#ifdef __cplusplus
    *cursor = {};
#else
    *cursor = (MeleeWebRuntimeMatchHistoryTerminalCursor){0};
#endif
    cursor->owner = *owner;
    cursor->pre_completed_tick = completed_tick;
    cursor->completed_tick = completed_tick;
    cursor->pre_input_ordinal = input_ordinal;
    cursor->pre_source_frame = source_frame;
    cursor->input_ordinal = input_ordinal;
    cursor->source_frame = source_frame;
    cursor->bound = 1;
    return 1;
}

/* Preflight before any source mutation. A matching owner alone is insufficient:
 * the measured bootstrap tick and live source frame must be the exact cursor
 * position retained from the last completed traversal (or restored PRE). */
static inline int melee_web_runtime_match_history_terminal_before(
    const MeleeWebRuntimeMatchHistoryTerminalCursor *cursor,
    const MeleeWebRuntimeMatchHistoryTerminalOwner *owner,
    uint64_t completed_tick, uint32_t source_frame,
    uint32_t ending, uint32_t complete)
{
    if (!cursor || !cursor->bound ||
        !melee_web_runtime_match_history_terminal_owner_equal(&cursor->owner, owner) ||
        cursor->ending || cursor->complete || ending > 1u || complete > 1u ||
        complete > ending || ending != cursor->ending || complete != cursor->complete ||
        completed_tick != cursor->completed_tick ||
        completed_tick < cursor->pre_completed_tick ||
        source_frame != cursor->source_frame)
        return 0;

    const uint64_t delta = completed_tick - cursor->pre_completed_tick;
    if (delta >= MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_MAX_SPAN ||
        delta > UINT32_MAX - cursor->pre_input_ordinal)
        return 0;
    const uint32_t ordinal = cursor->pre_input_ordinal + (uint32_t)delta;
    return cursor->input_ordinal == ordinal && source_frame == ordinal;
}

/* Observe one already completed source traversal. The caller supplies fresh
 * owner identities and reads the exact typed bootstrap counter immediately
 * before and after the synchronous traversal. */
static inline int melee_web_runtime_match_history_terminal_observe(
    MeleeWebRuntimeMatchHistoryTerminalCursor *cursor,
    const MeleeWebRuntimeMatchHistoryTerminalOwner *owner,
    uint64_t tick_before, uint64_t tick_after,
    uint32_t source_frame_before, uint32_t source_frame_after,
    uint32_t ending_before, uint32_t ending_after,
    uint32_t complete_before, uint32_t complete_after)
{
    if (!melee_web_runtime_match_history_terminal_before(
            cursor, owner, tick_before, source_frame_before,
            ending_before, complete_before) ||
        ending_after > 1u || complete_before > 1u || complete_after > 1u ||
        complete_after > ending_after || tick_before == UINT64_MAX ||
        tick_after != tick_before + UINT64_C(1) ||
        tick_before < cursor->pre_completed_tick ||
        source_frame_before != cursor->source_frame)
        return 0;

    const uint64_t before_delta = tick_before - cursor->pre_completed_tick;
    const uint64_t after_delta = tick_after - cursor->pre_completed_tick;
    if (before_delta > MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_MAX_SPAN ||
        after_delta > MELEE_WEB_RUNTIME_MATCH_HISTORY_TERMINAL_MAX_SPAN ||
        before_delta > UINT32_MAX - cursor->pre_input_ordinal ||
        after_delta > UINT32_MAX - cursor->pre_input_ordinal)
        return 0;

    const uint32_t expected_before = cursor->pre_input_ordinal + (uint32_t)before_delta;
    const uint32_t expected_after = cursor->pre_input_ordinal + (uint32_t)after_delta;
    if (cursor->input_ordinal != expected_before ||
        source_frame_before != expected_before)
        return 0;

    if (ending_after || complete_after) {
        /* The first original VS ending transition is a completed scheduler
         * traversal, but its authored frame counter stays at the pre-tick. */
        if (ending_before || !ending_after ||
            source_frame_after != source_frame_before)
            return 0;
        cursor->selected_terminal_valid = 1;
        cursor->selected_terminal_completed_tick = tick_after;
        cursor->selected_terminal_input_ordinal = expected_after;
        cursor->selected_terminal_source_frame_before = source_frame_before;
        cursor->selected_terminal_source_frame_after = source_frame_after;
        cursor->selected_terminal_ending_before = ending_before;
        cursor->selected_terminal_ending_after = ending_after;
        cursor->selected_terminal_complete_before = complete_before;
        cursor->selected_terminal_complete_after = complete_after;
        if (!cursor->first_terminal_valid) {
            cursor->first_terminal_valid = 1;
            cursor->first_terminal_completed_tick = tick_after;
            cursor->first_terminal_input_ordinal = expected_after;
            cursor->first_terminal_source_frame_before = source_frame_before;
            cursor->first_terminal_source_frame_after = source_frame_after;
            cursor->first_terminal_ending_before = ending_before;
            cursor->first_terminal_ending_after = ending_after;
            cursor->first_terminal_complete_before = complete_before;
            cursor->first_terminal_complete_after = complete_after;
        }
    } else if (source_frame_after != expected_after) {
        return 0;
    }

    cursor->completed_tick = tick_after;
    cursor->input_ordinal = expected_after;
    cursor->source_frame = source_frame_after;
    cursor->ending = ending_after;
    cursor->complete = complete_after;
    return 1;
}

/* The existing checkpoint restore is the only rewind authority. This helper
 * merely verifies that it returned the exact captured PRE and resets the
 * active cursor; first-ending evidence remains available for disposition. */
static inline int melee_web_runtime_match_history_terminal_rewind_pre(
    MeleeWebRuntimeMatchHistoryTerminalCursor *cursor,
    const MeleeWebRuntimeMatchHistoryTerminalOwner *owner,
    uint64_t completed_tick, uint32_t source_frame,
    uint32_t ending, uint32_t complete)
{
    if (!cursor || !cursor->bound ||
        !melee_web_runtime_match_history_terminal_owner_equal(&cursor->owner, owner) ||
        completed_tick != cursor->pre_completed_tick ||
        source_frame != cursor->pre_source_frame ||
        ending != 0u || complete != 0u)
        return 0;
    cursor->completed_tick = completed_tick;
    cursor->input_ordinal = cursor->pre_input_ordinal;
    cursor->source_frame = source_frame;
    cursor->ending = 0;
    cursor->complete = 0;
    cursor->selected_terminal_valid = 0;
    cursor->selected_terminal_completed_tick = 0;
    cursor->selected_terminal_input_ordinal = 0;
    cursor->selected_terminal_source_frame_before = 0;
    cursor->selected_terminal_source_frame_after = 0;
    cursor->selected_terminal_ending_before = 0;
    cursor->selected_terminal_ending_after = 0;
    cursor->selected_terminal_complete_before = 0;
    cursor->selected_terminal_complete_after = 0;
    return 1;
}

#endif
