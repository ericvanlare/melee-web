#ifndef MELEE_WEB_NATIVE_MENU_FIGHTER_INPUT_H
#define MELEE_WEB_NATIVE_MENU_FIGHTER_INPUT_H

#include <stdint.h>

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values returned by melee_web_fighter_input_drive and the fixed recipe. */
enum {
    MELEE_WEB_FIGHTER_INPUT_INVALID = -1,
    MELEE_WEB_FIGHTER_INPUT_MOVING_TO_PICKUP = 0,
    MELEE_WEB_FIGHTER_INPUT_PICKUP_READY = 1,
    MELEE_WEB_FIGHTER_INPUT_TARGET_READY = 2,
    MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED = 3,
};

/* One observation supplied by the read-only CSS source hook.  The
 * original cursor/model positions are in the same coordinate space as the
 * source icon bounds.  held_door is -1 until an original A-button pickup
 * attaches the model and sets cursor->x5. */
typedef struct MeleeWebFighterInputObservation {
    int cursor_port;
    int held_door;
    int selected_character_kind;
    float cursor_x;
    float cursor_y;
    float model_x;
    float model_y;
    float target_left;
    float target_right;
    float target_top;
    float target_bottom;
} MeleeWebFighterInputObservation;

/* Read and validate P1's live original CSS geometry for the requested
 * character. The source hook does not alter cursor, selection, or RNG state. */
int melee_web_fighter_input_observe(
    int character_kind, MeleeWebFighterInputObservation*);

/* Validate an observation for the requested character kind.  This function
 * only checks caller-supplied data and performs no source reads or writes. */
int melee_web_fighter_input_observe_valid(
    const MeleeWebFighterInputObservation*, int character_kind);

/* Build one raw source PAD sample.  Observe again after each sample.  Before
 * pickup this drives to the source's A-button attachment point
 * (model.x8 - 3.8, model.xC + 2.6), whose radius is sqrt(9).  While held it
 * drives the cursor to the source model offset (target center - 2.7,
 * target center + 2.0), then returns TARGET_READY once the model lies inside
 * the strict source icon bounds. The caller presses A at PICKUP_READY and
 * TARGET_READY. No selection byte, model state, or RNG is changed by this
 * helper. */
int melee_web_fighter_input_drive(
    PADStatus raw[PAD_MAX_CONTROLLERS],
    const MeleeWebFighterInputObservation*, int character_kind);

/* Build a source-neutral sample, retaining the host convention that ports 2
 * and 3 are disconnected. */
void melee_web_fighter_input_neutral(PADStatus raw[PAD_MAX_CONTROLLERS]);

/* Build a one-frame button sample on port 0. */
int melee_web_fighter_input_button(PADStatus raw[PAD_MAX_CONTROLLERS],
                                   uint16_t button);

#ifdef __cplusplus
}
#endif

#endif
