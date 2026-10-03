#include "gameplay_cpu_observation.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define RNG_DRAW_PROBE_MAX_PER_FRAME 1024
#define RNG_DRAW_PROBE_LINE_CAPACITY 65536

typedef struct {
    uint32_t seed;
    unsigned kind;
} RngDrawProbeEntry;

static RngDrawProbeEntry rng_draw_probe_entries[RNG_DRAW_PROBE_MAX_PER_FRAME];
static size_t rng_draw_probe_count;
static int rng_draw_probe_overflow, rng_draw_probe_active;
static int rng_draw_probe_configuration_checked, rng_draw_probe_configured;
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
}

static void emit_rng_draw_probe(void)
{
    if (!rng_draw_probe_active) return;
    rng_draw_probe_used = 0;
    put("{\"schema\":\"melee-web-rng-draw-probe\",\"version\":1,"
        "\"source_cursor\":%zu,\"overflowed\":%s,\"draws\":[",
        rng_draw_probe_cursor, rng_draw_probe_overflow ? "true" : "false");
    for (size_t index = 0; index < rng_draw_probe_count; ++index) {
        const RngDrawProbeEntry* entry = &rng_draw_probe_entries[index];
        put("%s{\"kind\":\"%s\",\"seed_after\":\"%08x\"}",
            index ? "," : "",
            entry->kind == MELEE_WEB_RNG_DRAW_RAND ? "HSD_Rand" : "HSD_Randf",
            entry->seed);
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
