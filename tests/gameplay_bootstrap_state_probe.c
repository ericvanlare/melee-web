/* Opt-in Wasm fixture wrapper for the typed bootstrap-state producer. */
#include "gameplay_bootstrap.h"
#include "gameplay_source_memory_runtime.h"
#include <emscripten.h>
#include <stdint.h>
#include <string.h>

#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_PROBE_SHA256
#error "pin the exact probe source bytes in the target"
#endif
#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_C_SHA256
#error "pin the exact gameplay_bootstrap.c source bytes in the target"
#endif
#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_H_SHA256
#error "pin the exact gameplay_bootstrap.h source bytes in the target"
#endif

EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_gameplay_bootstrap_state_abi_size(void)
{
    return (uint32_t)sizeof(MeleeWebGameplayBootstrapState);
}

EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_gameplay_bootstrap_state_abi_version(void)
{
    return MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_ABI_VERSION;
}

EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_gameplay_bootstrap_state_schema(void)
{
    return MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SCHEMA;
}

EMSCRIPTEN_KEEPALIVE const char*
melee_web_gameplay_bootstrap_state_probe_identity(void)
{
    return MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_PROBE_SHA256;
}

EMSCRIPTEN_KEEPALIVE const char*
melee_web_gameplay_bootstrap_state_source_c_identity(void)
{
    return MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_C_SHA256;
}

EMSCRIPTEN_KEEPALIVE const char*
melee_web_gameplay_bootstrap_state_source_h_identity(void)
{
    return MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_H_SHA256;
}

EMSCRIPTEN_KEEPALIVE int
melee_web_gameplay_bootstrap_state_capture(MeleeWebGameplayBootstrapState* out,
                                           uint32_t out_size)
{
    if (!out || out_size != sizeof(*out)) return 0;
    MeleeWebGameplayBootstrapState state;
    if (!melee_web_gameplay_bootstrap_state(&state, sizeof(state))) return 0;
    if (!state.tables_live || state.stepping || state.shutting_down ||
        state.startup_in_progress) return 0;
    if (!melee_web_source_memory_healthy()) return 0;
    if (melee_web_gameplay_world_exists() != (state.arena_identity != 0)) return 0;
    if (melee_web_gameplay_session_active() != (state.session_identity != 0)) return 0;
    const MeleeWebGameplayAllocation allocation = melee_web_gameplay_allocation();
    const uint32_t expected_identity = state.arena_identity ?
        state.arena_identity : state.session_identity;
    const uint64_t expected_bytes = state.arena_identity ?
        state.arena_bytes : state.session_bytes;
    if (allocation.identity != expected_identity ||
        allocation.generation != state.allocation_generation ||
        allocation.bytes != expected_bytes) return 0;
    if (state.tables_live) {
        const MeleeWebGameplayStats stats = melee_web_gameplay_stats();
        if (stats.generation != state.generation || stats.ticks != state.ticks) return 0;
    }
    memcpy(out, &state, sizeof(state));
    return 1;
}
