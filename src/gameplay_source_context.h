#ifndef MELEE_WEB_GAMEPLAY_SOURCE_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_SOURCE_CONTEXT_H

#include <stdint.h>
#include "gameplay_source_memory_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MeleeWebSourceFrameId {
    MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH = 1,
    MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK,
    MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK,
    MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH,
    MELEE_WEB_SOURCE_FRAME_CPU_STATE_18,
    MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY,
    MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR,
} MeleeWebSourceFrameId;

typedef enum MeleeWebSourceRegisterKind {
    MELEE_WEB_SOURCE_REGISTER_UNKNOWN = 0,
    MELEE_WEB_SOURCE_REGISTER_SEED_GLOBAL = 1,
    MELEE_WEB_SOURCE_REGISTER_STACK_LOCAL = 2,
} MeleeWebSourceRegisterKind;

typedef struct MeleeWebSourceFrameGuard {
    uint32_t previous_sp;
    uint32_t previous_depth;
    uint32_t cookie;
    uintptr_t previous_fighter_host;
    uint32_t previous_fighter_source;
    uint64_t previous_fighter_allocation_generation;
    uint8_t previous_route_valid;
    uint8_t previous_fighter_valid;
    uint8_t entered;
    uint8_t reserved[5];
} MeleeWebSourceFrameGuard;

typedef struct MeleeWebSourceRegisterWord {
    uint32_t source_word;
    uint64_t world_generation;
    uint64_t context_generation;
    uint64_t fighter_allocation_generation;
    uint8_t kind;
    uint8_t known;
    uint8_t reserved[6];
} MeleeWebSourceRegisterWord;

/* The browser runs one original GObj scheduler tick per consumed source PAD
 * sample. This establishes the pinned GALE01r2 source callback boundary and
 * invalidates residue from the prior scheduler tick. */
int melee_web_source_context_begin_tick(uint64_t world_generation);
int melee_web_source_context_end_tick(void);
int melee_web_source_context_reset_world(uint64_t world_generation);

void melee_web_source_frame_enter(MeleeWebSourceFrameGuard* guard,
                                  MeleeWebSourceFrameId frame);
void melee_web_source_frame_leave(MeleeWebSourceFrameGuard* guard);

/* Bind the active Fighter CPU callback to its live source-address allocation.
 * The owner is the host pointer; its source address and allocation generation
 * come from the original allocator's shadow, never from pointer arithmetic. */
int melee_web_source_context_enter_fighter(void* host_fighter);

/* Called by the original mpCheckFloor source body after its floor scan. The
 * provider accepts only the DOL-audited CPU route and derives the local from
 * the active logical PPC r1, never from the Wasm stack or a captured value. */
int melee_web_source_context_publish_floor_r5(void);

/* HSD_Randf's DOL implementation loads seed_ptr into r5. The address comes
 * from the pinned GALE01r2 DOL profile, independently of the host pointer used
 * by the browser's isolated RNG owner. */
int melee_web_source_context_publish_seed_r5(void);
/* Resolve only the PPC skipped-conversion route. ftCo_800AC5A0 emits its
 * locals in reversed argument order: stick_y is command 80/r5 and stick_x is
 * command 81/r30. This API returns those source bytes in local-variable order. */
int melee_web_source_context_resolve_skipped(void* host_fighter,
                                             int8_t* stick_x,
                                             int8_t* stick_y);
void melee_web_source_context_invalidate_r5(void);
MeleeWebSourceRegisterWord melee_web_source_context_r5(void);
uint32_t melee_web_source_context_current_sp(void);
uint64_t melee_web_source_context_generation(void);

#if defined(__GNUC__) || defined(__clang__)
#define MELEE_WEB_SOURCE_FRAME(frame_id)                                  \
    MeleeWebSourceFrameGuard melee_web_source_guard                      \
        __attribute__((cleanup(melee_web_source_frame_leave))) = {0};     \
    melee_web_source_frame_enter(&melee_web_source_guard, (frame_id))
#else
#error "The source stack guard requires compiler cleanup support"
#endif

#ifdef __cplusplus
}
#endif
#endif
