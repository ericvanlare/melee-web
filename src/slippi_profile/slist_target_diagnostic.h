/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_SLIST_TARGET_DIAGNOSTIC_H
#define SLIPPI_SLIST_TARGET_DIAGNOSTIC_H

#include <stdint.h>

#include "gameplay_class_piece_diagnostic.h"

#ifdef __cplusplus
extern "C" {
#endif

enum MeleeWebSListTraceOperation {
    MELEE_WEB_SLIST_TRACE_ALLOC_ZERO = 1,
    MELEE_WEB_SLIST_TRACE_ALLOC_APPEND_DATA = 2,
    MELEE_WEB_SLIST_TRACE_ALLOC_PREPEND_DATA = 3,
    MELEE_WEB_SLIST_TRACE_APPEND_LIST = 4,
    MELEE_WEB_SLIST_TRACE_PREPEND_LIST = 5,
    MELEE_WEB_SLIST_TRACE_REMOVE = 6,
    MELEE_WEB_SLIST_TRACE_OBJALLOC = 7,
    MELEE_WEB_SLIST_TRACE_OBJFREE = 8,
};

enum MeleeWebSListTraceBoundary {
    MELEE_WEB_SLIST_TRACE_CAPTURE = 1,
    MELEE_WEB_SLIST_TRACE_BASELINE_SEAL = 2,
    MELEE_WEB_SLIST_TRACE_DRAW = 3,
    MELEE_WEB_SLIST_TRACE_REPLAY_STEP = 4,
    MELEE_WEB_SLIST_TRACE_REPLAY_FINAL = 5,
};

typedef struct MeleeWebSListTraceTicket {
    uint32_t valid, operation, operand_mask, command;
    uint32_t scope, frame, sample, used_before, free_before;
    uintptr_t descriptor, node0, node1, input;
    uint32_t before[2];
} MeleeWebSListTraceTicket;

struct MeleeWebSourceMemoryPayloadSnapshot;
/* Temporary exact-target SList trace. It reuses the class-piece trace's
 * captured payload row and final comparison join; it is diagnostic only. */
int melee_web_source_memory_slist_target_trace_arm(
    const struct MeleeWebSourceMemoryPayloadSnapshot*, MeleeWebClassPieceAnchor,
    uint32_t locator_source, uint32_t locator_extent, uint32_t target_offset);
void melee_web_source_memory_slist_trace_context(
    uint32_t command, uint32_t scope, uint32_t frame, uint32_t sample);
void melee_web_source_memory_slist_trace_before(
    uint32_t operation, const void* descriptor, const void* node0,
    const void* node1, const void* input, MeleeWebSListTraceTicket* ticket);
void melee_web_source_memory_slist_trace_after(
    const MeleeWebSListTraceTicket* ticket, uint32_t used, uint32_t free);
void melee_web_source_memory_slist_trace_allocator_before(
    uint32_t operation, const void* descriptor, const void* node,
    const void* freehead, uint32_t used, uint32_t free,
    MeleeWebSListTraceTicket* ticket);
void melee_web_source_memory_slist_trace_allocator_after(
    const MeleeWebSListTraceTicket* ticket, const void* descriptor,
    const void* node, const void* freehead, uint32_t used, uint32_t free);
void melee_web_source_memory_slist_trace_membership(
    uint32_t boundary, const void* current_lights, const void* descriptor,
    const void* freehead, uint32_t used, uint32_t free);
int melee_web_source_memory_slist_trace_active(void);
void melee_web_source_memory_slist_trace_emit(void);
void melee_web_source_memory_slist_trace_finalize(void);
void melee_web_source_memory_slist_trace_early_close(void);
void melee_web_lobj_slist_trace_sample(uint32_t boundary);


#ifdef __cplusplus
}
#endif

#endif
