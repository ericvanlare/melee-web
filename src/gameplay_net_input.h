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
    MELEE_WEB_NET_MAX_FRAMES = 216000,
};

#if defined(MELEE_WEB_NET_SESSION)
int melee_web_net_active(void);
/* Begin a session on a fresh prepared CSS owner (the caller checks the
 * owner). Only the agreed seed varies; the context is the canonical
 * Everything save mode with default rules, preferences and PAD history. */
int melee_web_net_begin(uint32_t seed, uint32_t max_frames, char* error,
                        size_t error_size);
/* Apply the agreed context immediately before the first CSS entry. */
int melee_web_net_apply_start_context(MeleeWebMenuHost* host, char* error,
                                      size_t error_size);
/* Next agreed frame for this source tick, or NULL for a network wait. */
const PADStatus* melee_web_net_before_step(uint32_t scene);
/* Record the checksum of the tick that consumed the frame. */
void melee_web_net_after_step(void);
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
static inline void melee_web_net_after_step(void) {}
static inline void melee_web_net_reset(void) {}
#endif

#ifdef __cplusplus
}
#endif

#endif
