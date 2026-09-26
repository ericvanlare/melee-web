#include "gameplay_source_context.h"
#include "source_ppc_profile_gale01r2.h"

#include <stdio.h>

static unsigned char fighter_storage[0x3000] __attribute__((aligned(32)));

static int fail(const char* message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

#define ENTER(name, id, expected_sp)                                  \
    MeleeWebSourceFrameGuard name = {0};                              \
    melee_web_source_frame_enter(&name, id);                          \
    if (melee_web_source_context_current_sp() != expected_sp)          \
        return fail("DOL-derived source frame SP mismatch")

int main(void)
{
    const uint64_t world_generation = 7;
    MeleeWebSourceFighterAddress fighter = {0};
    const uintptr_t fighter_host = (uintptr_t) fighter_storage + 0x80u;

    if (!melee_web_source_memory_begin(world_generation, 19))
        return fail("GALE01r2 source address heap did not derive");
    if (!melee_web_source_memory_alloc(19, fighter_storage,
                                      sizeof(fighter_storage)))
        return fail("source address heap did not mirror a live allocation");
    if (!melee_web_source_memory_fighter_acquire((void*) fighter_host, 0x23ecu,
                                                  &fighter) ||
        !fighter.live || !fighter.source_address ||
        fighter.world_generation != world_generation ||
        !fighter.allocation_generation)
        return fail("live Fighter did not receive a source allocation lease");

    if (!melee_web_source_context_begin_tick(world_generation))
        return fail("source stack context did not begin");
    if (melee_web_source_context_current_sp() !=
        MELEE_WEB_GALE01R2_GM_CALLBACK_SP)
        return fail("source manager callback root differs from DOL profile");

    ENTER(gobj, MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH, 0x804EEAC8u);
    ENTER(fighter_frame, MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK,
          0x804EEAB0u);
    if (!melee_web_source_context_enter_fighter((void*) fighter_host))
        return fail("active Fighter callback did not bind its live source lease");
    ENTER(cpu, MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK, 0x804EEA90u);
    ENTER(dispatch, MELEE_WEB_SOURCE_FRAME_CPU_STATE_DISPATCH, 0x804EE9C8u);
    ENTER(state18, MELEE_WEB_SOURCE_FRAME_CPU_STATE_18, 0x804EE980u);
    ENTER(floor, MELEE_WEB_SOURCE_FRAME_CPU_FLOOR_QUERY, 0x804EE8F0u);

    if (!melee_web_source_context_publish_seed_r5())
        return fail("exact seed-global call route was not accepted");
    MeleeWebSourceRegisterWord r5 = melee_web_source_context_r5();
    int8_t stick_x = 0, stick_y = 0;
    if (!r5.known || r5.kind != MELEE_WEB_SOURCE_REGISTER_SEED_GLOBAL ||
        r5.source_word != MELEE_WEB_GALE01R2_SEED_WORD ||
        r5.fighter_allocation_generation != fighter.allocation_generation ||
        !melee_web_source_context_resolve_skipped((void*) fighter_host,
                                                  &stick_x, &stick_y) ||
        stick_x != (int8_t) fighter.source_address || stick_y != (int8_t) 0x90)
        return fail("seed-global carry did not resolve in original command/local order");

    ENTER(mp, MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR, 0x804EE800u);
    if (!melee_web_source_context_publish_floor_r5())
        return fail("audited floor call path did not publish its source local");
    r5 = melee_web_source_context_r5();
    if (!r5.known || r5.kind != MELEE_WEB_SOURCE_REGISTER_STACK_LOCAL ||
        r5.source_word != 0x804EE844u || (int8_t) r5.source_word != 68 ||
        r5.world_generation != world_generation ||
        r5.context_generation != melee_web_source_context_generation() ||
        r5.fighter_allocation_generation != fighter.allocation_generation)
        return fail("floor r5 differs from the DOL-derived source stack local");

    melee_web_source_frame_leave(&mp);
    if (!melee_web_source_context_resolve_skipped((void*) fighter_host,
                                                  &stick_x, &stick_y) ||
        stick_x != (int8_t) fighter.source_address || stick_y != 68)
        return fail("stack-local carry did not survive its source callee return");
    melee_web_source_frame_leave(&floor);
    melee_web_source_frame_leave(&state18);
    melee_web_source_frame_leave(&dispatch);
    melee_web_source_frame_leave(&cpu);
    melee_web_source_frame_leave(&fighter_frame);
    melee_web_source_frame_leave(&gobj);
    if (melee_web_source_context_current_sp() !=
        MELEE_WEB_GALE01R2_GM_CALLBACK_SP)
        return fail("source stack did not restore after nested returns");
    if (!melee_web_source_context_end_tick())
        return fail("balanced source stack context did not end");

    if (!melee_web_source_context_begin_tick(world_generation))
        return fail("source stack context did not begin for route rejection");
    ENTER(gobj_again, MELEE_WEB_SOURCE_FRAME_GOBJ_DISPATCH, 0x804EEAC8u);
    ENTER(fighter_again, MELEE_WEB_SOURCE_FRAME_FIGHTER_CPU_CALLBACK,
          0x804EEAB0u);
    if (!melee_web_source_context_enter_fighter((void*) fighter_host))
        return fail("reused live Fighter was not rebound in a later callback");
    ENTER(cpu_again, MELEE_WEB_SOURCE_FRAME_CPU_CALLBACK, 0x804EEA90u);
    MeleeWebSourceFrameGuard unrelated_mp = {0};
    melee_web_source_frame_enter(&unrelated_mp,
                                 MELEE_WEB_SOURCE_FRAME_MP_CHECK_FLOOR);
    if (melee_web_source_context_publish_floor_r5())
        return fail("unrelated mpCheckFloor caller was accepted as the CPU route");
    melee_web_source_frame_leave(&unrelated_mp);
    melee_web_source_frame_leave(&cpu_again);
    melee_web_source_frame_leave(&fighter_again);
    melee_web_source_frame_leave(&gobj_again);
    if (!melee_web_source_context_end_tick())
        return fail("source stack did not recover after route rejection");

    if (!melee_web_source_memory_fighter_release((void*) fighter_host))
        return fail("Fighter lease did not retire at source unload");
    MeleeWebSourceFighterAddress stale = {0};
    if (melee_web_source_memory_fighter_read((void*) fighter_host, &stale))
        return fail("retired Fighter remained a live source owner");
    if (!melee_web_source_memory_fighter_acquire((void*) fighter_host, 0x23ecu,
                                                  &stale) ||
        stale.allocation_generation == fighter.allocation_generation)
        return fail("reused Fighter address did not advance its allocation generation");
    if (!melee_web_source_memory_fighter_release((void*) fighter_host))
        return fail("reused Fighter lease did not retire");
    if (!melee_web_source_memory_end(world_generation))
        return fail("healthy source address heap did not end");
    if (!melee_web_source_context_reset_world(world_generation))
        return fail("source context did not release its world owner");

    puts("gameplay source context trace: passed");
    return 0;
}
