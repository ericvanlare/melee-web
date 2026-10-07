#ifndef MELEE_WEB_GAMEPLAY_NET_INPUT_H
#define MELEE_WEB_GAMEPLAY_NET_INPUT_H

/* Opt-in networked input seam for development builds (architecture decision
 * 016, Track A1). JavaScript buffers agreed per-tick PAD frames; the native
 * step loop pulls exactly one frame per source tick at the same point where
 * whole-session replay injects recorded frames. When the next frame has not
 * arrived the step loop stops (a network wait); it never skips, inserts or
 * bursts a source tick. The public and audio-preview players do not compile
 * this file. */

#include <stddef.h>
#include <stdint.h>
#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebMenuHost MeleeWebMenuHost;

enum {
    MELEE_WEB_NET_FRAME_BYTES = 44,
    MELEE_WEB_NET_INPUT_DELAY = 2,
    MELEE_WEB_NET_PAD_BYTES = 11,
    MELEE_WEB_NET_MAX_FRAMES = 216000,
    MELEE_WEB_NET_TERMINAL_DESYNC = 1,
    MELEE_WEB_NET_TERMINAL_DISCONNECT = 2,
    MELEE_WEB_NET_TERMINAL_PROTOCOL = 3,
    MELEE_WEB_NET_TERMINAL_START_IDENTITY = 4,
};

#if defined(MELEE_WEB_NET_SESSION)
int melee_web_net_active(void);
/* Begin a session on a fresh prepared CSS owner (the caller checks the
 * owner). Only the agreed seed varies; the context is the canonical
 * Everything save mode with default rules, preferences and PAD history. */
int melee_web_net_begin(uint32_t seed, uint32_t max_frames, char* error,
                        size_t error_size);
/* A2 lockstep starts at a mandatory pre-tick identity handshake; the A1
 * sequential replay adapter keeps its established non-handshaked entry. */
int melee_web_net_begin_lockstep(uint32_t seed, uint32_t max_frames,
                                 char* error, size_t error_size);
/* Optional A3 page-owned sampling mode. Configure only before the identity
 * barrier is confirmed; capture observes the PADRead snapshot already polled
 * by the browser runtime and never polls hardware itself. */
int melee_web_net_enable_local_input_capture(unsigned local_port,
                                             uint32_t input_ticks);
/* Read-only scheduler hint: after start confirmation, a new native PAD sample
 * is still required for the current source cursor. It never polls, publishes
 * or advances the session; a cursor with already-published bytes is immutable. */
int melee_web_net_local_capture_pending(void);
int melee_web_net_capture_local_input(uint64_t poll_serial,
                                      const PADStatus raw[4]);
/* Internal synchronous bridge into the page callback. */
int melee_web_net_publish_local_input(uint32_t source_tick,
                                      unsigned local_port,
                                      uint64_t poll_serial,
                                      const uint8_t bytes[MELEE_WEB_NET_PAD_BYTES]);
/* Apply the agreed context immediately before the first CSS entry. */
int melee_web_net_apply_start_context(MeleeWebMenuHost* host, char* error,
                                      size_t error_size);
/* Next agreed frame for this source tick, or NULL for a network wait. */
const PADStatus* melee_web_net_before_step(uint32_t scene);
/* Record the checksum of the tick that consumed the frame. */
void melee_web_net_after_step(void);
/* Append a contiguous indexed packet at the native queue's current boundary.
 * An identical retransmission is idempotent; conflicting overlap and gaps
 * are refused. Sequential A1 pushes remain supported by the existing export. */
int melee_web_net_push_indexed(uint32_t first_tick, const uint8_t* bytes,
                               unsigned count);
/* Confirm a peer's identity only after the transport has compared the full
 * prepared start record. Until then the native source loop stays before tick0. */
int melee_web_net_confirm_start(void);
/* Stop an active session on an explicit protocol terminal. `kind` is a small
 * stable enum used only for diagnostics; tick/channel identify desynces. */
void melee_web_net_terminate(unsigned kind, uint32_t tick, unsigned channel);
void melee_web_net_reset(void);
/* Begin the reserved arena. An optional diagnostic fill is applied only while
 * that allocation is created, and the request is consumed once. */
int melee_web_net_session_begin(size_t heap_bytes, char* error, size_t error_size);
#else
/* Player builds compile the hooks away; no networked state exists. */
static inline int melee_web_net_active(void) { return 0; }
static inline int melee_web_net_apply_start_context(MeleeWebMenuHost* host,
                                                    char* error, size_t error_size)
{
    (void) host; (void) error; (void) error_size;
    return 0;
}
static inline const PADStatus* melee_web_net_before_step(uint32_t scene)
{
    (void) scene;
    return NULL;
}
static inline int melee_web_net_enable_local_input_capture(unsigned port,
                                                            uint32_t ticks)
{
    (void) port; (void) ticks;
    return 0;
}
static inline int melee_web_net_local_capture_pending(void) { return 0; }
static inline int melee_web_net_capture_local_input(uint64_t serial,
                                                     const PADStatus raw[4])
{
    (void) serial; (void) raw;
    return 1;
}
static inline void melee_web_net_after_step(void) {}
static inline int melee_web_net_push_indexed(uint32_t first_tick,
                                             const uint8_t* bytes,
                                             unsigned count)
{
    (void) first_tick; (void) bytes; (void) count;
    return 0;
}
static inline int melee_web_net_confirm_start(void) { return 0; }
static inline void melee_web_net_terminate(unsigned kind, uint32_t tick,
                                           unsigned channel)
{
    (void) kind; (void) tick; (void) channel;
}
static inline void melee_web_net_reset(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif
