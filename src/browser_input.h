#ifndef MELEE_WEB_BROWSER_INPUT_H
#define MELEE_WEB_BROWSER_INPUT_H

#include <stdint.h>
#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebLastInput {
    uint64_t sample;
    uint32_t port;
    int valid, physical;
    PADStatus raw, clamped;
} MeleeWebLastInput;

typedef struct MeleeWebInputSnapshot {
    uint64_t samples;
    /* Physical mask uses bit 0 for port 0, through bit 3 for port 3. */
    uint32_t physical_mask;
    int ready, focused, visible, active;
    /* Legacy fields describe port 0; masks expose both explicit keyboard
     * profiles without hiding per-port physical priority. */
    int keyboard_requested, keyboard_active;
    uint32_t keyboard_requested_mask, keyboard_active_mask;
    /* Future game code consumes raw. Clamped is a separate PADClamp diagnostic
     * copy, so a later game's own clamp must not be applied twice. */
    PADStatus raw[PAD_MAX_CONTROLLERS];
    PADStatus clamped[PAD_MAX_CONTROLLERS];
    /* Diagnostic history only: retained through release/focus loss. On a sample
     * with several active ports, records the lowest port. Cleared by shutdown. */
    MeleeWebLastInput last_non_neutral;
} MeleeWebInputSnapshot;

/* Aurora owns SDL/provider startup. Call after aurora_initialize. */
int melee_web_input_startup(void);
/* Call once after aurora_update, even if aurora_begin_frame cannot draw. */
const MeleeWebInputSnapshot* melee_web_input_poll(void);
const MeleeWebInputSnapshot* melee_web_input_snapshot(void);
/* Frontend supplies canvas focus plus document/window focus and visibility. */
void melee_web_input_set_activity(int focused, int visible);
/* Keyboard fallback defaults off. A pre-startup choice is preserved; the
 * legacy setter controls port 0 and retains its existing P1 profile. */
void melee_web_input_set_keyboard(int enabled);
/* Explicit keyboard profile for ports 0/1. Returns 0 for any other port.
 * Enabling port 1 installs the P2 profile: arrows are its left stick,
 * RightShift/RightControl are A/B, End is Start, Delete is X, PageDown is Z,
 * Home/PageUp are L/R, and keypad 4/8/2/6 are its c-stick. While that
 * profile is enabled, P1's D-pad moves to Z/X/C/V to avoid arrow overlap;
 * disabling it restores the existing P1 arrow-D-pad mapping. */
int melee_web_input_set_keyboard_port(unsigned port, int enabled);
/* Small JSON diagnostic. No physical-device or gameplay validation is implied. */
const char* melee_web_input_message(void);
/* Menu buttons from physical ports 0/1 using the same mapped PAD snapshot. */
unsigned melee_web_input_menu_buttons(void);
void melee_web_input_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
