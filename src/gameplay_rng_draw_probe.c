#include "gameplay_cpu_observation.h"

#include <melee/gr/grpstadium.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define RNG_DRAW_PROBE_MAX_PER_FRAME 1024
/* Each bounded v2 row is < 320 bytes even with 64-bit pointer tokens and
 * maximum decimal fields. Header/trailer < 512 bytes. Never truncate. */
#define RNG_DRAW_PROBE_LINE_CAPACITY (512 + RNG_DRAW_PROBE_MAX_PER_FRAME * 320)

typedef struct {
    uint32_t seed;
    unsigned kind;
    uintptr_t gobj, proc, callback;
    unsigned classifier, p_link, s_link;
} RngDrawProbeEntry;

static RngDrawProbeEntry rng_draw_probe_entries[RNG_DRAW_PROBE_MAX_PER_FRAME];
static size_t rng_draw_probe_count;
static int rng_draw_probe_overflow, rng_draw_probe_active;
static int rng_draw_probe_configuration_checked, rng_draw_probe_configured;
static int rng_draw_probe_owners;
static size_t rng_draw_probe_cursor = (size_t)-1;
static char rng_draw_probe_line[RNG_DRAW_PROBE_LINE_CAPACITY];
static size_t rng_draw_probe_used;
static void emit_rng_draw_probe(void);

static void put(const char* format, ...)
{
    if (rng_draw_probe_used >= sizeof(rng_draw_probe_line)) abort();
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(rng_draw_probe_line + rng_draw_probe_used,
        sizeof(rng_draw_probe_line) - rng_draw_probe_used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(rng_draw_probe_line) - rng_draw_probe_used)
        abort();
    rng_draw_probe_used += (size_t)written;
}

void melee_web_cpu_observation_set_rng_draw_cursor(size_t index)
{
    if (rng_draw_probe_active && rng_draw_probe_cursor == index) return;
    if (rng_draw_probe_active) emit_rng_draw_probe();
    rng_draw_probe_cursor = index;
    rng_draw_probe_count = 0;
    rng_draw_probe_overflow = 0;
#ifdef __EMSCRIPTEN__
    if (!rng_draw_probe_configuration_checked) {
        rng_draw_probe_configured = EM_ASM_INT({
            return Array.isArray(window.__meleeRngDrawProbeCursors) ||
                Array.isArray(window.__meleeRngDrawProbeRange);
        }, 0);
        rng_draw_probe_owners = EM_ASM_INT({
            return window.__meleeRngDrawProbeOwners === true;
        }, 0);
        rng_draw_probe_configuration_checked = 1;
    }
    if (!rng_draw_probe_configured) {
        rng_draw_probe_active = 0;
        return;
    }
    rng_draw_probe_active = EM_ASM_INT({
        const cursors = window.__meleeRngDrawProbeCursors;
        if (Array.isArray(cursors)) return cursors.includes($0);
        const range = window.__meleeRngDrawProbeRange;
        return Array.isArray(range) && range.length === 2 &&
            $0 >= range[0] && $0 <= range[1];
    }, index);
#else
    rng_draw_probe_active = 0;
#endif
}

/* The actual HSD dispatcher owns these typed globals only while on_invoke is
 * running. No list walk, dereference of user_data, or callback invocation here.
 * Their backpointer agreement is scheduled context, not a lifetime attestation.
 */
static void capture_rng_draw_owner(RngDrawProbeEntry* entry)
{
    HSD_GObj* gobj = HSD_GObj_804D781C;
    HSD_GObjProc* proc = HSD_GObj_804D7838;
    entry->gobj = entry->proc = entry->callback = 0;
    entry->classifier = entry->p_link = entry->s_link = 0;
    if (!gobj && !proc) return; /* Explicit outside_proc; never guess an owner. */
    if (!gobj || !proc || proc->gobj != gobj || !proc->on_invoke) abort();
    entry->gobj = (uintptr_t)gobj;
    entry->proc = (uintptr_t)proc;
    entry->callback = (uintptr_t)proc->on_invoke;
    entry->classifier = gobj->classifier;
    entry->p_link = gobj->p_link;
    entry->s_link = proc->s_link;
}

void melee_web_cpu_observation_rng_draw(uint32_t updated_seed, unsigned kind)
{
    if (!rng_draw_probe_active) return;
    if (kind != MELEE_WEB_RNG_DRAW_RAND && kind != MELEE_WEB_RNG_DRAW_RANDF) abort();
    if (rng_draw_probe_count == RNG_DRAW_PROBE_MAX_PER_FRAME) {
        rng_draw_probe_overflow = 1;
        return;
    }
    RngDrawProbeEntry* entry = &rng_draw_probe_entries[rng_draw_probe_count++];
    entry->seed = updated_seed;
    entry->kind = kind;
    if (rng_draw_probe_owners) capture_rng_draw_owner(entry);
}

static void emit_rng_draw_probe(void)
{
    if (!rng_draw_probe_active) return;
    rng_draw_probe_used = 0;
    put("{\"schema\":\"melee-web-rng-draw-probe\",\"version\":%u,"
        "\"source_cursor\":%zu,\"overflowed\":%s,\"draws\":[",
        rng_draw_probe_owners ? 2u : 1u, rng_draw_probe_cursor, rng_draw_probe_overflow ? "true" : "false");
    for (size_t index = 0; index < rng_draw_probe_count; ++index) {
        const RngDrawProbeEntry* entry = &rng_draw_probe_entries[index];
        put("%s{\"kind\":\"%s\",\"seed_after\":\"%08x\"",
            index ? "," : "",
            entry->kind == MELEE_WEB_RNG_DRAW_RAND ? "HSD_Rand" : "HSD_Randf",
            entry->seed);
        if (rng_draw_probe_owners)
            put(",\"owner\":{\"status\":\"%s\",\"gobj\":\"%016llx\","
                "\"proc\":\"%016llx\",\"callback\":\"%016llx\","
                "\"classifier\":%u,\"p_link\":%u,\"s_link\":%u,\"callback_symbol\":%s}",
                entry->proc ? "scheduled_proc" : "outside_proc",
                (unsigned long long)entry->gobj, (unsigned long long)entry->proc,
                (unsigned long long)entry->callback, entry->classifier,
                entry->p_link, entry->s_link,
                !entry->proc ? "null" : entry->callback == (uintptr_t)grStadium_801D1390 ?
                    "\"grStadium_801D1390\"" : "\"unknown\"");
        put("}");
    }
    put("]}");
#ifdef __EMSCRIPTEN__
    EM_ASM({
        const text = UTF8ToString($0);
        if (typeof window !== 'undefined' && window.meleeRngDrawObservation)
            window.meleeRngDrawObservation(text);
        else err('RNG_DRAW_PROBE ' + text);
    }, rng_draw_probe_line);
#else
    fprintf(stderr, "RNG_DRAW_PROBE %s\n", rng_draw_probe_line);
#endif
    rng_draw_probe_used = 0;
    rng_draw_probe_active = 0;
}

void melee_web_rng_draw_probe_scheduler_return(void)
{
    emit_rng_draw_probe();
}
