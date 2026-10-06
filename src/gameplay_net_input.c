/* Networked-session input queue, checksum ring and diagnostics exports.
 * See gameplay_net_input.h. The state here never feeds source simulation
 * except the agreed PAD frames and the agreed seed. */
#include "gameplay_compat.h"
#include "gameplay_net_input.h"
#include "gameplay_net_checksum.h"
#include "gameplay_bootstrap.h"
#include "gameplay_menu_host.h"
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct NetSession {
    int active;
    int context_applied;
    uint32_t seed;
    uint32_t max_frames;
    uint32_t pushed;
    uint32_t cursor;
    PADStatus (*frames)[4];
    MeleeWebMenuHost* host;
    /* Pending step: set by before_step, consumed by after_step. */
    int step_pending;
    uint32_t step_scene;
    uint32_t last_scene;
    /* Network waits are their own events, never timing pauses. */
    uint64_t wait_callbacks;
    uint32_t wait_episodes;
    int waiting;
    uint64_t backpressure_callbacks;
    int start_recorded;
    MeleeWebNetChecksumRecord start;
    MeleeWebNetChecksumRecord ring[MELEE_WEB_NET_CHECKSUM_RING];
    uint32_t ring_read, ring_write;
    MeleeWebNetArenaRecord arena[MELEE_WEB_NET_ARENA_RECORDS];
    uint32_t arena_count;
    uint32_t arena_overflow;
    /* Diagnostic initialization requested before import, then consumed once
     * by net_session_begin at the backing arena allocation boundary. */
    int arena_fill;
    int arena_fill_pending;
} NetSession;

static NetSession net = {.arena_fill = -1};
static char status_text[8192];

static int fail(char* e, size_t n, const char* message)
{
    if (e && n) snprintf(e, n, "%s", message);
    return 0;
}

int melee_web_net_active(void) { return net.active; }

void melee_web_net_reset(void)
{
    const int fill = net.arena_fill;
    const int fill_pending = net.arena_fill_pending;
    free(net.frames);
    memset(&net, 0, sizeof(net));
    net.arena_fill = fill;
    net.arena_fill_pending = fill_pending;
}

int melee_web_net_session_begin(size_t bytes, char* error, size_t error_size)
{
    if (net.arena_fill_pending) {
        if (!melee_web_gameplay_session_begin_with_pattern(bytes, net.arena_fill,
                                                           error, error_size))
            return 0;
        net.arena_fill_pending = 0;
        return 1;
    }
    if (!melee_web_gameplay_session_begin(bytes, error, error_size)) return 0;
    net.arena_fill = -1;
    return 1;
}

int melee_web_net_begin(uint32_t seed, uint32_t max_frames, char* e, size_t n)
{
    if (net.active) return fail(e, n, "A networked session is already active");
    if (net.arena_fill_pending)
        return fail(e, n, "Diagnostic arena pattern missed the session allocation boundary");
    if (!max_frames || max_frames > MELEE_WEB_NET_MAX_FRAMES)
        return fail(e, n, "Networked session frame bound is outside 1-216000");
    melee_web_net_reset();
    net.frames = calloc(max_frames, sizeof(*net.frames));
    if (!net.frames) return fail(e, n, "Networked input queue allocation failed");
    net.active = 1;
    net.seed = seed;
    net.max_frames = max_frames;
    net.last_scene = UINT32_MAX;
    if (e && n) *e = 0;
    return 1;
}

int melee_web_net_apply_start_context(MeleeWebMenuHost* host, char* e, size_t n)
{
    if (!net.active || net.context_applied || !host)
        return fail(e, n, "Networked start context requires one fresh active session");
    if (!melee_web_menu_host_apply_net_context(host, net.seed, e, n)) return 0;
    net.host = host;
    net.context_applied = 1;
    return 1;
}

static uint32_t ring_used(void) { return net.ring_write - net.ring_read; }

static void record_arena(uint32_t scene)
{
    MeleeWebNetArenaRecord row = {net.cursor, scene, 0, 0, 0};
    if (!melee_web_net_arena_hash(&row.base, &row.bytes, &row.hash)) return;
    if (net.arena_count < MELEE_WEB_NET_ARENA_RECORDS)
        net.arena[net.arena_count++] = row;
    else
        ++net.arena_overflow;
}

const PADStatus* melee_web_net_before_step(uint32_t scene)
{
    if (!net.active || !net.context_applied || net.step_pending) abort();
    if (net.cursor >= net.pushed) {
        ++net.wait_callbacks;
        if (!net.waiting) ++net.wait_episodes;
        net.waiting = 1;
        return NULL;
    }
    /* Never drop a checksum: a full ring stops source time like a wait. */
    if (ring_used() >= MELEE_WEB_NET_CHECKSUM_RING) {
        ++net.backpressure_callbacks;
        return NULL;
    }
    net.waiting = 0;
    if (!net.start_recorded) {
        static const PADStatus neutral[4];
        melee_web_net_checksum_compute(UINT32_MAX, scene, neutral, net.host, &net.start);
        net.start_recorded = 1;
    }
    if (scene != net.last_scene) {
        record_arena(scene);
        net.last_scene = scene;
    }
    net.step_pending = 1;
    net.step_scene = scene;
    return net.frames[net.cursor];
}

void melee_web_net_after_step(void)
{
    if (!net.active || !net.step_pending) abort();
    MeleeWebNetChecksumRecord* row =
        &net.ring[net.ring_write % MELEE_WEB_NET_CHECKSUM_RING];
    melee_web_net_checksum_compute(net.cursor, net.step_scene, net.frames[net.cursor],
                                   net.host, row);
    ++net.ring_write;
    ++net.cursor;
    net.step_pending = 0;
}

static int decode_frame(const uint8_t* p, PADStatus out[4])
{
    for (unsigned port = 0; port < 4; ++port, p += 11) {
        PADStatus* pad = &out[port];
        memset(pad, 0, sizeof(*pad));
        pad->button = (u16) (p[0] << 8 | p[1]);
        pad->stickX = (s8) p[2];
        pad->stickY = (s8) p[3];
        pad->substickX = (s8) p[4];
        pad->substickY = (s8) p[5];
        pad->triggerLeft = p[6];
        pad->triggerRight = p[7];
        pad->analogA = p[8];
        pad->analogB = p[9];
        pad->err = (s8) p[10];
        if (pad->button & ~0x1f7fu) return 0;
        if (pad->err != PAD_ERR_NONE && pad->err != PAD_ERR_NO_CONTROLLER) return 0;
        if (pad->err != PAD_ERR_NONE) {
            for (unsigned i = 0; i < 10; ++i)
                if (p[i]) return 0;
        }
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE int melee_web_net_push(const uint8_t* bytes, unsigned count)
{
    if (!net.active || !bytes || !count || count > net.max_frames - net.pushed) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (!decode_frame(bytes + (size_t) i * MELEE_WEB_NET_FRAME_BYTES,
                          net.frames[net.pushed + i]))
            return 0;
    net.pushed += count;
    return 1;
}

EMSCRIPTEN_KEEPALIVE unsigned melee_web_net_cursor(void) { return net.cursor; }
EMSCRIPTEN_KEEPALIVE unsigned melee_web_net_pushed(void) { return net.pushed; }

/* Copy up to max_records 64-byte records, oldest first. */
EMSCRIPTEN_KEEPALIVE unsigned melee_web_net_checksum_drain(uint8_t* out, unsigned max_records)
{
    unsigned copied = 0;
    if (!out) return 0;
    while (copied < max_records && ring_used()) {
        memcpy(out + (size_t) copied * sizeof(MeleeWebNetChecksumRecord),
               &net.ring[net.ring_read % MELEE_WEB_NET_CHECKSUM_RING],
               sizeof(MeleeWebNetChecksumRecord));
        ++net.ring_read;
        ++copied;
    }
    return copied;
}

/* Diagnostic arena pattern for channel experiments; before disc import. */
EMSCRIPTEN_KEEPALIVE int melee_web_net_arena_fill(int pattern)
{
    if (melee_web_gameplay_session_active() || pattern < -1 || pattern > 255) return 0;
    net.arena_fill = pattern;
    net.arena_fill_pending = pattern >= 0;
    return 1;
}

EMSCRIPTEN_KEEPALIVE const char* melee_web_net_status(void)
{
    size_t used = 0;
    int written = snprintf(status_text, sizeof(status_text),
        "{\"active\":%d,\"context_applied\":%d,\"seed\":%u,\"max_frames\":%u,"
        "\"pushed\":%u,\"cursor\":%u,\"wait_callbacks\":%llu,\"wait_episodes\":%u,"
        "\"backpressure_callbacks\":%llu,\"ring_pending\":%u,\"arena_fill\":%d,"
        "\"start\":{\"recorded\":%d,\"scene\":%u,\"seed\":%u,\"frame\":%u,"
        "\"total\":\"%016llx\",\"pad\":\"%016llx\",\"scene_state\":\"%016llx\","
        "\"object_state\":\"%016llx\",\"objects\":%u,\"flags\":%u},"
        "\"arena_overflow\":%u,\"arena\":[",
        net.active, net.context_applied, net.seed, net.max_frames, net.pushed,
        net.cursor, (unsigned long long) net.wait_callbacks, net.wait_episodes,
        (unsigned long long) net.backpressure_callbacks, ring_used(), net.arena_fill,
        net.start_recorded, net.start.scene, net.start.seed, net.start.frame,
        (unsigned long long) net.start.total, (unsigned long long) net.start.pad,
        (unsigned long long) net.start.scene_state,
        (unsigned long long) net.start.object_state, net.start.objects,
        net.start.flags, net.arena_overflow);
    if (written < 0 || (size_t) written >= sizeof(status_text)) return "{\"error\":\"status\"}";
    used = (size_t) written;
    for (uint32_t i = 0; i < net.arena_count; ++i) {
        const MeleeWebNetArenaRecord* r = &net.arena[i];
        written = snprintf(status_text + used, sizeof(status_text) - used,
            "%s{\"tick\":%u,\"scene\":%u,\"base\":%u,\"bytes\":%u,\"hash\":\"%016llx\"}",
            i ? "," : "", r->tick, r->scene, r->base, r->bytes,
            (unsigned long long) r->hash);
        if (written < 0 || (size_t) written >= sizeof(status_text) - used)
            return "{\"error\":\"status\"}";
        used += (size_t) written;
    }
    written = snprintf(status_text + used, sizeof(status_text) - used, "]}");
    if (written < 0 || (size_t) written >= sizeof(status_text) - used)
        return "{\"error\":\"status\"}";
    return status_text;
}
