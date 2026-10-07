/* Networked-session input queue, checksum ring and diagnostics exports.
 * See gameplay_net_input.h. The state here never feeds source simulation
 * except the agreed PAD frames and the agreed seed. */
#include "gameplay_compat.h"
#include "gameplay_net_input.h"
#include "gameplay_net_checksum.h"
#include "gameplay_bootstrap.h"
#include "gameplay_menu_host.h"
#include "gameplay_pad_state.h"
#include "gameplay_save_profile.h"
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum LocalCaptureFailureReason {
    LOCAL_CAPTURE_FAILURE_NONE = 0,
    LOCAL_CAPTURE_FAILURE_INVALID_RAW,
    LOCAL_CAPTURE_FAILURE_CURSOR_COUNT,
    LOCAL_CAPTURE_FAILURE_POLL_SERIAL,
    LOCAL_CAPTURE_FAILURE_PAD_ERROR,
    LOCAL_CAPTURE_FAILURE_PUBLISH_REJECTED,
};

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
    uint32_t wait_start_tick;
    uint32_t wait_last_tick;
    uint32_t wait_resume_count;
    uint64_t start_barrier_callbacks;
    uint64_t backpressure_callbacks;
    int start_recorded;
    int start_required;
    int start_confirmed;
    int start_capture_failed;
    int local_capture_enabled;
    unsigned local_capture_port;
    uint32_t local_capture_input_ticks;
    uint32_t local_capture_count;
    uint32_t local_capture_last_cursor;
    uint64_t local_capture_last_poll_serial;
    uint8_t local_capture_last_bytes[MELEE_WEB_NET_PAD_BYTES];
    unsigned local_capture_failure_reason;
    uint32_t local_capture_failure_cursor;
    uint32_t local_capture_failure_count;
    uint64_t local_capture_failure_poll_serial;
    uint64_t local_capture_failure_last_poll_serial;
    unsigned local_capture_failure_port;
    int local_capture_failure_pad_error;
    uint64_t start_card_hash;
    uint64_t start_pad_history_hash;
    uint64_t start_native_context;
    MeleeWebNetChecksumRecord start;
    unsigned terminal_kind;
    uint32_t terminal_tick;
    unsigned terminal_channel;
    unsigned indexed_duplicates;
    unsigned indexed_conflicts;
    unsigned indexed_gaps;
    unsigned indexed_invalid;
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
static char status_text[16384];

static const char* local_capture_failure_name(unsigned reason)
{
    switch (reason) {
    case LOCAL_CAPTURE_FAILURE_INVALID_RAW: return "invalid_raw";
    case LOCAL_CAPTURE_FAILURE_CURSOR_COUNT: return "cursor_count";
    case LOCAL_CAPTURE_FAILURE_POLL_SERIAL: return "poll_serial";
    case LOCAL_CAPTURE_FAILURE_PAD_ERROR: return "pad_error";
    case LOCAL_CAPTURE_FAILURE_PUBLISH_REJECTED: return "publish_rejected";
    default: return "none";
    }
}

static void record_local_capture_failure(unsigned reason, uint64_t poll_serial,
                                         int pad_error)
{
    if (net.local_capture_failure_reason) return;
    net.local_capture_failure_reason = reason;
    net.local_capture_failure_cursor = net.cursor;
    net.local_capture_failure_count = net.local_capture_count;
    net.local_capture_failure_poll_serial = poll_serial;
    net.local_capture_failure_last_poll_serial = net.local_capture_last_poll_serial;
    net.local_capture_failure_port = net.local_capture_port;
    net.local_capture_failure_pad_error = pad_error;
}

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

static int net_begin(uint32_t seed, uint32_t max_frames, int require_identity,
                     char* e, size_t n)
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
    net.start_required = require_identity;
    net.start_confirmed = !require_identity;
    net.last_scene = UINT32_MAX;
    if (e && n) *e = 0;
    return 1;
}

int melee_web_net_begin(uint32_t seed, uint32_t max_frames, char* e, size_t n)
{
    return net_begin(seed, max_frames, 0, e, n);
}

int melee_web_net_begin_lockstep(uint32_t seed, uint32_t max_frames,
                                 char* e, size_t n)
{
    return net_begin(seed, max_frames, 1, e, n);
}

EMSCRIPTEN_KEEPALIVE int melee_web_net_enable_local_input_capture(
    unsigned local_port, uint32_t input_ticks)
{
    if (!net.active || !net.start_required || net.start_confirmed ||
        net.cursor || net.local_capture_enabled || local_port > 1 ||
        !input_ticks || input_ticks > UINT32_MAX - MELEE_WEB_NET_INPUT_DELAY ||
        input_ticks + MELEE_WEB_NET_INPUT_DELAY != net.max_frames)
        return 0;
    net.local_capture_enabled = 1;
    net.local_capture_port = local_port;
    net.local_capture_input_ticks = input_ticks;
    return 1;
}

static int encode_local_pad(const PADStatus* pad,
                            uint8_t out[MELEE_WEB_NET_PAD_BYTES])
{
    if (!pad || !out || pad->err != PAD_ERR_NONE) return 0;
    out[0] = (uint8_t) (pad->button >> 8);
    out[1] = (uint8_t) pad->button;
    out[2] = (uint8_t) pad->stickX;
    out[3] = (uint8_t) pad->stickY;
    out[4] = (uint8_t) pad->substickX;
    out[5] = (uint8_t) pad->substickY;
    out[6] = pad->triggerLeft;
    out[7] = pad->triggerRight;
    out[8] = pad->analogA;
    out[9] = pad->analogB;
    out[10] = (uint8_t) pad->err;
    return 1;
}

int melee_web_net_capture_local_input(uint64_t poll_serial,
                                      const PADStatus raw[4])
{
    if (!net.active || !net.local_capture_enabled) return 1;
    if (net.terminal_kind) return 0;
    /* The first loop callback records identity before the page can confirm it.
     * Do not sample until that barrier releases source tick zero. */
    if (!net.start_confirmed) return 1;
    if (net.cursor >= net.local_capture_input_ticks) {
        if (net.local_capture_count != net.local_capture_input_ticks) {
            record_local_capture_failure(LOCAL_CAPTURE_FAILURE_CURSOR_COUNT,
                                         poll_serial, 0);
            melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                    net.cursor, net.local_capture_port);
            return 0;
        }
        return 1;
    }
    /* A network wait may revisit one cursor on later browser callbacks. Its
     * already-published contribution is immutable; never sample the new PAD. */
    if (net.local_capture_count && net.cursor == net.local_capture_last_cursor)
        return 1;
    if (!raw) {
        record_local_capture_failure(LOCAL_CAPTURE_FAILURE_INVALID_RAW,
                                     poll_serial, 0);
        melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                net.cursor, net.local_capture_port);
        return 0;
    }
    if (net.cursor != net.local_capture_count ||
        net.local_capture_count >= net.local_capture_input_ticks) {
        record_local_capture_failure(LOCAL_CAPTURE_FAILURE_CURSOR_COUNT,
                                     poll_serial, 0);
        melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                net.cursor, net.local_capture_port);
        return 0;
    }
    if (net.local_capture_count &&
        poll_serial <= net.local_capture_last_poll_serial) {
        record_local_capture_failure(LOCAL_CAPTURE_FAILURE_POLL_SERIAL,
                                     poll_serial, 0);
        melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                net.cursor, net.local_capture_port);
        return 0;
    }

    uint8_t bytes[MELEE_WEB_NET_PAD_BYTES];
    if (!encode_local_pad(&raw[net.local_capture_port], bytes)) {
        record_local_capture_failure(LOCAL_CAPTURE_FAILURE_PAD_ERROR,
                                     poll_serial, raw[net.local_capture_port].err);
        melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                net.cursor, net.local_capture_port);
        return 0;
    }
    if (!melee_web_net_publish_local_input(net.cursor, net.local_capture_port,
                                           poll_serial, bytes)) {
        record_local_capture_failure(LOCAL_CAPTURE_FAILURE_PUBLISH_REJECTED,
                                     poll_serial, 0);
        melee_web_net_terminate(MELEE_WEB_NET_TERMINAL_PROTOCOL,
                                net.cursor, net.local_capture_port);
        return 0;
    }
    memcpy(net.local_capture_last_bytes, bytes, sizeof(bytes));
    net.local_capture_last_cursor = net.cursor;
    net.local_capture_last_poll_serial = poll_serial;
    ++net.local_capture_count;
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

static void capture_start_identity(uint32_t scene)
{
    static const uint8_t tag[] = "MeleeWeb native start identity v1";
    static const PADStatus neutral[4];
    uint8_t pad_state[MELEE_WEB_PAD_STATE_BYTES];
    uint8_t* card = malloc(MELEE_WEB_SAVE_PROFILE_CARD_BYTES);
    char error[256] = {0};
    if (!card || !melee_web_menu_host_snapshot_card_data(
            net.host, 0, card, MELEE_WEB_SAVE_PROFILE_CARD_BYTES,
            error, sizeof(error))) {
        free(card);
        net.start_capture_failed = 1;
        net.terminal_kind = MELEE_WEB_NET_TERMINAL_START_IDENTITY;
        return;
    }
    melee_web_pad_state_capture(pad_state);
    melee_web_net_checksum_compute(UINT32_MAX, scene, neutral, net.host, &net.start);
    net.start_card_hash = melee_web_net_fnv1a64(
        0xcbf29ce484222325ull, card, MELEE_WEB_SAVE_PROFILE_CARD_BYTES);
    net.start_pad_history_hash = melee_web_net_fnv1a64(
        0xcbf29ce484222325ull, pad_state, sizeof(pad_state));
    uint64_t identity = melee_web_net_fnv1a64(
        0xcbf29ce484222325ull, tag, sizeof(tag));
    identity = melee_web_net_fnv1a64(identity, &net.start, sizeof(net.start));
    identity = melee_web_net_fnv1a64(identity, card, MELEE_WEB_SAVE_PROFILE_CARD_BYTES);
    identity = melee_web_net_fnv1a64(identity, pad_state, sizeof(pad_state));
    net.start_native_context = identity;
    net.start_recorded = 1;
    free(card);
}

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
    if (net.terminal_kind) return NULL;
    if (net.start_required) {
        /* A2's first callback is a prepared-state barrier. Its identity is
         * exchanged before tick0; the source loop cannot consume until both
         * peers explicitly confirm the same Wasm/DOL/FST/native identity. */
        if (!net.start_recorded && !net.start_capture_failed)
            capture_start_identity(scene);
        if (net.start_capture_failed) return NULL;
        if (!net.start_confirmed) {
            ++net.start_barrier_callbacks;
            return NULL;
        }
        /* The declared lockstep timeline is finite. Reaching its end is not a
         * missing remote contribution and must not grow network-wait counters. */
        if (net.cursor >= net.max_frames) return NULL;
    }
    if (net.cursor >= net.pushed) {
        ++net.wait_callbacks;
        if (!net.waiting) {
            ++net.wait_episodes;
            net.wait_start_tick = net.cursor;
        }
        net.waiting = 1;
        net.wait_last_tick = net.cursor;
        return NULL;
    }
    if (net.waiting) {
        net.waiting = 0;
        ++net.wait_resume_count;
    }
    /* Never drop a checksum: a full ring stops source time like a wait. */
    if (ring_used() >= MELEE_WEB_NET_CHECKSUM_RING) {
        ++net.backpressure_callbacks;
        return NULL;
    }
    /* Preserve A1's original checksum capture point: immediately before its
     * first available sequential frame is consumed. */
    if (!net.start_required && !net.start_recorded) {
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
    if (!net.active || net.terminal_kind || !bytes || !count ||
        count > net.max_frames - net.pushed) return 0;
    for (unsigned i = 0; i < count; ++i)
        if (!decode_frame(bytes + (size_t) i * MELEE_WEB_NET_FRAME_BYTES,
                          net.frames[net.pushed + i]))
            return 0;
    net.pushed += count;
    return 1;
}

EMSCRIPTEN_KEEPALIVE int melee_web_net_push_indexed(uint32_t first_tick,
                                                    const uint8_t* bytes,
                                                    unsigned count)
{
    PADStatus parsed[4];
    if (!net.active || net.terminal_kind || !bytes || !count ||
        first_tick > net.max_frames ||
        count > net.max_frames - first_tick) {
        ++net.indexed_invalid;
        return 0;
    }
    if (first_tick > net.pushed) {
        ++net.indexed_gaps;
        net.terminal_kind = MELEE_WEB_NET_TERMINAL_PROTOCOL;
        net.terminal_tick = net.pushed;
        return 0;
    }
    /* Decode and validate the entire envelope before changing the native
     * contiguous frontier. Only byte-identical game values may be retried. */
    for (unsigned i = 0; i < count; ++i) {
        if (!decode_frame(bytes + (size_t) i * MELEE_WEB_NET_FRAME_BYTES, parsed)) {
            ++net.indexed_invalid;
            net.terminal_kind = MELEE_WEB_NET_TERMINAL_PROTOCOL;
            net.terminal_tick = first_tick + i;
            return 0;
        }
        uint32_t tick = first_tick + i;
        if (tick < net.pushed &&
            memcmp(parsed, net.frames[tick], sizeof(parsed)) != 0) {
            ++net.indexed_conflicts;
            net.terminal_kind = MELEE_WEB_NET_TERMINAL_PROTOCOL;
            net.terminal_tick = tick;
            return 0;
        }
    }
    uint32_t overlap = net.pushed - first_tick;
    if (overlap > count) overlap = count;
    if (overlap) net.indexed_duplicates += overlap;
    for (unsigned i = overlap; i < count; ++i) {
        if (!decode_frame(bytes + (size_t) i * MELEE_WEB_NET_FRAME_BYTES, parsed))
            abort(); /* The full packet was decoded in the validation pass. */
        memcpy(net.frames[net.pushed], parsed, sizeof(parsed));
        ++net.pushed;
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE int melee_web_net_confirm_start(void)
{
    if (!net.active || !net.start_recorded || net.start_capture_failed ||
        net.terminal_kind) return 0;
    net.start_confirmed = 1;
    return 1;
}

EMSCRIPTEN_KEEPALIVE void melee_web_net_terminate(unsigned kind,
                                                  uint32_t tick,
                                                  unsigned channel)
{
    if (!net.active || net.terminal_kind) return;
    if (kind < MELEE_WEB_NET_TERMINAL_DESYNC ||
        kind > MELEE_WEB_NET_TERMINAL_START_IDENTITY)
        kind = MELEE_WEB_NET_TERMINAL_PROTOCOL;
    net.terminal_kind = kind;
    net.terminal_tick = tick;
    net.terminal_channel = channel;
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
    char local_capture_failure[320];
    if (!net.local_capture_failure_reason) {
        snprintf(local_capture_failure, sizeof(local_capture_failure), "null");
    } else {
        char pad_error[24];
        if (net.local_capture_failure_reason == LOCAL_CAPTURE_FAILURE_PAD_ERROR)
            snprintf(pad_error, sizeof(pad_error), "%d", net.local_capture_failure_pad_error);
        else
            snprintf(pad_error, sizeof(pad_error), "null");
        snprintf(local_capture_failure, sizeof(local_capture_failure),
            "{\"reason\":\"%s\",\"cursor\":%u,\"count\":%u,"
            "\"poll_serial\":\"%llu\",\"last_poll_serial\":\"%llu\","
            "\"port\":%u,\"pad_error\":%s}",
            local_capture_failure_name(net.local_capture_failure_reason),
            net.local_capture_failure_cursor, net.local_capture_failure_count,
            (unsigned long long) net.local_capture_failure_poll_serial,
            (unsigned long long) net.local_capture_failure_last_poll_serial,
            net.local_capture_failure_port, pad_error);
    }
    const char* blocker = net.terminal_kind ? "terminal" :
        net.start_capture_failed ? "start_identity_error" :
        (!net.start_recorded || !net.start_confirmed) ? "start_identity" :
        (net.start_required && net.cursor >= net.max_frames) ? "complete" :
        (net.cursor >= net.pushed) ? (net.start_required ? "network_wait" : "remote_input") :
        (ring_used() >= MELEE_WEB_NET_CHECKSUM_RING) ? "checksum_backpressure" : "none";
    int written = snprintf(status_text, sizeof(status_text),
        "{\"active\":%d,\"context_applied\":%d,\"seed\":%u,\"max_frames\":%u,"
        "\"pushed\":%u,\"cursor\":%u,\"blocker\":\"%s\","
        "\"start_barrier_callbacks\":%llu,\"wait_callbacks\":%llu,\"wait_episodes\":%u,"
        "\"wait_start_tick\":%u,\"wait_last_tick\":%u,\"wait_resume_count\":%u,"
        "\"backpressure_callbacks\":%llu,\"ring_pending\":%u,\"arena_fill\":%d,"
        "\"indexed\":{\"duplicates\":%u,\"conflicts\":%u,\"gaps\":%u,\"invalid\":%u},"
        "\"terminal\":{\"kind\":%u,\"tick\":%u,\"channel\":%u},"
        "\"local_capture_failure\":%s,"
        "\"network_wait\":{\"active\":%d,\"callbacks\":%llu,\"episodes\":%u,"
        "\"start_tick\":%u,\"last_tick\":%u,\"resume_count\":%u},"
        "\"start\":{\"required\":%d,\"recorded\":%d,\"scene\":%u,\"seed\":%u,\"frame\":%u,"
        "\"confirmed\":%d,\"capture_failed\":%d,\"card\":\"%016llx\","
        "\"pad_history\":\"%016llx\",\"native_context\":\"%016llx\","
        "\"total\":\"%016llx\",\"pad\":\"%016llx\",\"scene_state\":\"%016llx\","
        "\"object_state\":\"%016llx\",\"objects\":%u,\"flags\":%u},"
        "\"arena_overflow\":%u,\"arena\":[",
        net.active, net.context_applied, net.seed, net.max_frames, net.pushed,
        net.cursor, blocker, (unsigned long long) net.start_barrier_callbacks,
        (unsigned long long) net.wait_callbacks, net.wait_episodes,
        net.wait_start_tick, net.wait_last_tick, net.wait_resume_count,
        (unsigned long long) net.backpressure_callbacks, ring_used(), net.arena_fill,
        net.indexed_duplicates, net.indexed_conflicts, net.indexed_gaps,
        net.indexed_invalid, net.terminal_kind, net.terminal_tick, net.terminal_channel,
        local_capture_failure,
        net.start_required && net.waiting,
        (unsigned long long) net.wait_callbacks, net.wait_episodes,
        net.wait_start_tick, net.wait_last_tick, net.wait_resume_count,
        net.start_required, net.start_recorded, net.start.scene, net.start.seed, net.start.frame,
        net.start_confirmed, net.start_capture_failed,
        (unsigned long long) net.start_card_hash,
        (unsigned long long) net.start_pad_history_hash,
        (unsigned long long) net.start_native_context,
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
