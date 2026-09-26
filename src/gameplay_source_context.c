#include "gameplay_source_context.h"
#include "source_ppc_profile_gale01r2.h"

#include <string.h>

#define FRAME_CAPACITY 12u
#define FRAME_COOKIE 0x53434652u

typedef struct SourceFrame {
    MeleeWebSourceFrameId id;
    uint32_t bytes;
} SourceFrame;

static SourceFrame frames[FRAME_CAPACITY];
static uint32_t frame_depth;
static uint32_t current_sp;
static uint64_t world_generation;
static uint64_t context_generation;
static uint8_t tick_active;
static uint8_t route_valid;
static uintptr_t fighter_host;
static uint32_t fighter_source;
static uint64_t fighter_allocation_generation;
static uint8_t fighter_valid;
static MeleeWebSourceRegisterWord r5;

static void clear_r5(void)
{
    memset(&r5, 0, sizeof(r5));
    r5.kind = MELEE_WEB_SOURCE_REGISTER_UNKNOWN;
    r5.world_generation = world_generation;
    r5.context_generation = context_generation;
    r5.fighter_allocation_generation = fighter_allocation_generation;
}

static uint32_t frame_size(MeleeWebSourceFrameId frame)
{
    switch (frame) {
    case MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH:
        return MELEE_WEB_GALE01R2_FRAME_GOBJ_DISPATCH;
    case MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK:
        return MELEE_WEB_GALE01R2_FRAME_FIGHTER_CPU_CALLBACK;
    case MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK:
        return MELEE_WEB_GALE01R2_FRAME_CPU_CALLBACK;
    case MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH:
        return MELEE_WEB_GALE01R2_FRAME_CPU_STATE_DISPATCH;
    case MELEE_WEB_SOURCE_FRAME_CPU_STATE_18:
        return MELEE_WEB_GALE01R2_FRAME_CPU_STATE_18;
    case MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY:
        return MELEE_WEB_GALE01R2_FRAME_CPU_FLOOR_QUERY;
    case MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR:
        return MELEE_WEB_GALE01R2_FRAME_MP_CHECK_FLOOR;
    }
    return 0;
}

static int expected_parent(MeleeWebSourceFrameId frame,
                           MeleeWebSourceFrameId* parent)
{
    switch (frame) {
    case MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH:
        return frame_depth == 0;
    case MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK:
        *parent = MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH;
        break;
    case MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK:
        *parent = MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK;
        break;
    case MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH:
        *parent = MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK;
        break;
    case MELEE_WEB_SOURCE_FRAME_CPU_STATE_18:
        *parent = MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH;
        break;
    case MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY:
        *parent = MELEE_WEB_SOURCE_FRAME_CPU_STATE_18;
        break;
    case MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR:
        *parent = MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY;
        break;
    }
    return frame_depth != 0 && frames[frame_depth - 1].id == *parent;
}

int melee_web_source_context_begin_tick(uint64_t generation)
{
    if (!generation || tick_active || frame_depth != 0 ||
        (world_generation && world_generation != generation)) {
        clear_r5();
        route_valid = 0;
        return 0;
    }
    if (++context_generation == 0) ++context_generation;
    world_generation = generation;
    current_sp = MELEE_WEB_GALE01R2_GM_CALLBACK_SP;
    memset(frames, 0, sizeof(frames));
    tick_active = 1;
    route_valid = 1;
    fighter_host = 0;
    fighter_source = 0;
    fighter_allocation_generation = 0;
    fighter_valid = 0;
    clear_r5();
    return 1;
}

int melee_web_source_context_end_tick(void)
{
    const int balanced = tick_active && frame_depth == 0;
    tick_active = 0;
    current_sp = 0;
    fighter_host = 0;
    fighter_source = 0;
    fighter_allocation_generation = 0;
    fighter_valid = 0;
    if (!balanced) {
        route_valid = 0;
        clear_r5();
        return 0;
    }
    return 1;
}

int melee_web_source_context_reset_world(uint64_t generation)
{
    if (!generation || tick_active || frame_depth != 0 ||
        (world_generation && world_generation != generation))
        return 0;
    world_generation = 0;
    current_sp = 0;
    route_valid = 0;
    fighter_host = 0;
    fighter_source = 0;
    fighter_allocation_generation = 0;
    fighter_valid = 0;
    memset(frames, 0, sizeof(frames));
    clear_r5();
    return 1;
}

void melee_web_source_frame_enter(MeleeWebSourceFrameGuard* guard,
                                  MeleeWebSourceFrameId frame)
{
    MeleeWebSourceFrameId parent = 0;
    uint32_t bytes;
    if (!guard) return;
    memset(guard, 0, sizeof(*guard));
    if (!tick_active) return;
    guard->cookie = FRAME_COOKIE;
    guard->previous_sp = current_sp;
    guard->previous_depth = frame_depth;
    guard->previous_route_valid = route_valid;
    guard->previous_fighter_host = fighter_host;
    guard->previous_fighter_source = fighter_source;
    guard->previous_fighter_allocation_generation = fighter_allocation_generation;
    guard->previous_fighter_valid = fighter_valid;
    if (!route_valid) return;
    bytes = frame_size(frame);
    if (!bytes || frame_depth >= FRAME_CAPACITY ||
        !expected_parent(frame, &parent) || current_sp < bytes) {
        route_valid = 0;
        clear_r5();
        return;
    }
    guard->entered = 1;
    current_sp -= bytes;
    frames[frame_depth++] = (SourceFrame){frame, bytes};
}

void melee_web_source_frame_leave(MeleeWebSourceFrameGuard* guard)
{
    if (!guard || guard->cookie != FRAME_COOKIE) return;
    if (!tick_active) {
        route_valid = 0;
        clear_r5();
    } else if (guard->entered && frame_depth == guard->previous_depth + 1u) {
        const int leaving_fighter = frames[frame_depth - 1].id ==
                                    MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK;
        current_sp = guard->previous_sp;
        frames[--frame_depth] = (SourceFrame){0};
        route_valid = guard->previous_route_valid;
        fighter_host = guard->previous_fighter_host;
        fighter_source = guard->previous_fighter_source;
        fighter_allocation_generation =
            guard->previous_fighter_allocation_generation;
        fighter_valid = guard->previous_fighter_valid;
        if (leaving_fighter) clear_r5();
    } else if (!guard->entered && frame_depth == guard->previous_depth) {
        current_sp = guard->previous_sp;
        route_valid = guard->previous_route_valid;
        fighter_host = guard->previous_fighter_host;
        fighter_source = guard->previous_fighter_source;
        fighter_allocation_generation =
            guard->previous_fighter_allocation_generation;
        fighter_valid = guard->previous_fighter_valid;
    } else {
        route_valid = 0;
        clear_r5();
    }
    guard->cookie = 0;
}

int melee_web_source_context_enter_fighter(void* host_fighter)
{
    MeleeWebSourceFighterAddress address = {0};
    if (!tick_active || !route_valid || !host_fighter || !frame_depth ||
        frames[frame_depth - 1].id !=
            MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK ||
        !melee_web_source_memory_fighter_read(host_fighter, &address) ||
        !address.live || address.world_generation != world_generation ||
        !address.allocation_generation || address.source_address < 0x80000000u ||
        address.source_address >= 0x81800000u || (address.source_address & 3u)) {
        clear_r5();
        return 0;
    }
    fighter_host = (uintptr_t) host_fighter;
    fighter_source = address.source_address;
    fighter_allocation_generation = address.allocation_generation;
    fighter_valid = 1;
    clear_r5();
    return 1;
}

static int fighter_owner_live(void)
{
    MeleeWebSourceFighterAddress address = {0};
    return tick_active && route_valid && fighter_valid && fighter_host &&
           fighter_allocation_generation &&
           melee_web_source_memory_fighter_read((void*) fighter_host, &address) &&
           address.live && address.world_generation == world_generation &&
           address.allocation_generation == fighter_allocation_generation &&
           address.source_address == fighter_source;
}

static int seed_route_active(void)
{
    static const MeleeWebSourceFrameId expected[] = {
        MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH,
        MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK,
        MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK,
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH,
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_18,
        MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY,
    };
    if (!tick_active || !route_valid || frame_depth !=
            sizeof(expected) / sizeof(expected[0]) || !fighter_owner_live())
        return 0;
    for (uint32_t i = 0; i < frame_depth; ++i)
        if (frames[i].id != expected[i]) return 0;
    return 1;
}

static int floor_route_active(void)
{
    static const MeleeWebSourceFrameId expected[] = {
        MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH,
        MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK,
        MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK,
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH,
        MELEE_WEB_SOURCE_FRAME_CPU_STATE_18,
        MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY,
        MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR,
    };
    if (!tick_active || !route_valid || !fighter_owner_live() ||
        frame_depth != sizeof(expected) / sizeof(expected[0]))
        return 0;
    for (uint32_t i = 0; i < frame_depth; ++i)
        if (frames[i].id != expected[i]) return 0;
    return 1;
}

int melee_web_source_context_publish_floor_r5(void)
{
    if (!floor_route_active() ||
        current_sp > UINT32_MAX - MELEE_WEB_GALE01R2_FLOOR_LOCAL_Y0) {
        melee_web_source_context_invalidate_r5();
        return 0;
    }
    r5.source_word = current_sp + MELEE_WEB_GALE01R2_FLOOR_LOCAL_Y0;
    r5.kind = MELEE_WEB_SOURCE_REGISTER_STACK_LOCAL;
    r5.known = 1;
    r5.world_generation = world_generation;
    r5.context_generation = context_generation;
    r5.fighter_allocation_generation = fighter_allocation_generation;
    return 1;
}

int melee_web_source_context_publish_seed_r5(void)
{
    if (!seed_route_active() || !world_generation) {
        melee_web_source_context_invalidate_r5();
        return 0;
    }
    r5.source_word = MELEE_WEB_GALE01R2_SEED_WORD;
    r5.kind = MELEE_WEB_SOURCE_REGISTER_SEED_GLOBAL;
    r5.known = 1;
    r5.world_generation = world_generation;
    r5.context_generation = context_generation;
    r5.fighter_allocation_generation = fighter_allocation_generation;
    return 1;
}

int melee_web_source_context_resolve_skipped(void* host_fighter,
                                              int8_t* stick_x,
                                              int8_t* stick_y)
{
    if (!stick_x || !stick_y || !fighter_owner_live() ||
        !host_fighter || (uintptr_t) host_fighter != fighter_host ||
        r5.known == 0 || r5.world_generation != world_generation ||
        r5.context_generation != context_generation ||
        r5.fighter_allocation_generation != fighter_allocation_generation ||
        (r5.kind != MELEE_WEB_SOURCE_REGISTER_SEED_GLOBAL &&
         r5.kind != MELEE_WEB_SOURCE_REGISTER_STACK_LOCAL) ||
        frame_depth < 3 ||
        frames[0].id != MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH ||
        frames[1].id != MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK ||
        frames[2].id != MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK) {
        melee_web_source_context_invalidate_r5();
        return 0;
    }
    /* The recovered consumer's commands intentionally reverse local names:
     * SetLstickX reads stick_y (r5), SetLstickY reads stick_x (r30). */
    *stick_x = (int8_t) fighter_source;
    *stick_y = (int8_t) r5.source_word;
    return 1;
}

void melee_web_source_context_invalidate_r5(void)
{
    clear_r5();
}

MeleeWebSourceRegisterWord melee_web_source_context_r5(void)
{
    return r5;
}

uint32_t melee_web_source_context_current_sp(void)
{
    return tick_active && route_valid ? current_sp : 0;
}

uint64_t melee_web_source_context_generation(void)
{
    return tick_active ? context_generation : 0;
}
