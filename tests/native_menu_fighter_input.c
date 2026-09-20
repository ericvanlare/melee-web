#include "gameplay_compat.h"
#include "native_menu_fighter_input.h"
#include <melee/ft/forward.h>

#include <math.h>

extern int melee_web_css_observe(int character_kind, int ids[4],
                                 float geometry[8]);

static int finite_float(float value)
{
    return isfinite(value) != 0;
}

int melee_web_fighter_input_observe(
    int character_kind, MeleeWebFighterInputObservation* observation)
{
    int ids[4];
    float geometry[8];
    if (!observation ||
        !melee_web_css_observe(character_kind, ids, geometry))
        return 0;
    observation->cursor_port = ids[0];
    observation->held_door = ids[1];
    observation->selected_character_kind = ids[2];
    observation->cursor_x = geometry[0];
    observation->cursor_y = geometry[1];
    observation->model_x = geometry[2];
    observation->model_y = geometry[3];
    observation->target_left = geometry[4];
    observation->target_right = geometry[5];
    observation->target_top = geometry[6];
    observation->target_bottom = geometry[7];
    return melee_web_fighter_input_observe_valid(observation,
                                                 character_kind);
}

void melee_web_fighter_input_neutral(PADStatus raw[PAD_MAX_CONTROLLERS])
{
    if (!raw) return;
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        raw[port] = (PADStatus){0};
        raw[port].err = PAD_ERR_NONE;
    }
    raw[2].err = PAD_ERR_NO_CONTROLLER;
    raw[3].err = PAD_ERR_NO_CONTROLLER;
}

static int model_inside_target(
    const MeleeWebFighterInputObservation* observation)
{
    return observation->model_x > observation->target_left &&
           observation->model_x < observation->target_right &&
           observation->model_y < observation->target_top &&
           observation->model_y > observation->target_bottom;
}

int melee_web_fighter_input_observe_valid(
    const MeleeWebFighterInputObservation* observation, int character_kind)
{
    if (!observation || observation->cursor_port < 0 ||
        observation->cursor_port >= PAD_MAX_CONTROLLERS ||
        observation->held_door < -1 || observation->held_door >= 4 ||
        character_kind < 0 || character_kind >= CKIND_PLAYABLE_COUNT ||
        !finite_float(observation->cursor_x) ||
        !finite_float(observation->cursor_y) ||
        !finite_float(observation->model_x) ||
        !finite_float(observation->model_y) ||
        !finite_float(observation->target_left) ||
        !finite_float(observation->target_right) ||
        !finite_float(observation->target_top) ||
        !finite_float(observation->target_bottom) ||
        observation->target_left >= observation->target_right ||
        observation->target_bottom >= observation->target_top)
        return 0;
    return 1;
}

static void steer_axis(float current, float target, float tolerance, s8* axis)
{
    if (current < target - tolerance)
        *axis = 80;
    else if (current > target + tolerance)
        *axis = -80;
}

int melee_web_fighter_input_drive(
    PADStatus raw[PAD_MAX_CONTROLLERS],
    const MeleeWebFighterInputObservation* observation, int character_kind)
{
    const float source_stick_step = 1.24f;
    const float center_tolerance = source_stick_step * 0.5f;
    float target_x;
    float target_y;
    float dx;
    float dy;
    if (!raw || !melee_web_fighter_input_observe_valid(observation,
                                                       character_kind))
        return MELEE_WEB_FIGHTER_INPUT_INVALID;

    melee_web_fighter_input_neutral(raw);
    if (observation->held_door < 0) {
        if (observation->selected_character_kind == character_kind &&
            model_inside_target(observation))
            return MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED;

        /* Source mnCharSel_CursorThink uses
         *   dx = 3.8 + cursor.x - model.x8
         *   dy = -2.6 + cursor.y - model.xC
         * and attaches when dx*dx + dy*dy < 9. */
        target_x = observation->model_x - 3.8f;
        target_y = observation->model_y + 2.6f;
        dx = target_x - observation->cursor_x;
        dy = target_y - observation->cursor_y;
        if ((dx * dx + dy * dy) < 9.0f)
            return MELEE_WEB_FIGHTER_INPUT_PICKUP_READY;
    } else {
        if (model_inside_target(observation))
            return MELEE_WEB_FIGHTER_INPUT_TARGET_READY;

        /* After attachment the source cursor is snapped to model.x8 - 2.7,
         * model.xC + 2.0.  Drive to the matching point at the target icon's
         * center; the source model's normal smoothing then settles inside
         * the strict icon bounds. */
        target_x = (observation->target_left + observation->target_right) *
                   0.5f - 2.7f;
        target_y = (observation->target_top + observation->target_bottom) *
                   0.5f + 2.0f;
        dx = target_x - observation->cursor_x;
        dy = target_y - observation->cursor_y;
    }

    /* Use one axis at a time.  This preserves the source's 1.24-unit CSS
     * step and avoids a diagonal sample overshooting a narrow icon. */
    steer_axis(observation->cursor_x, target_x, center_tolerance,
               &raw[observation->cursor_port].stickX);
    steer_axis(observation->cursor_y, target_y, center_tolerance,
               &raw[observation->cursor_port].stickY);
    (void) dx;
    (void) dy;
    return MELEE_WEB_FIGHTER_INPUT_MOVING_TO_PICKUP;
}

int melee_web_fighter_input_button(PADStatus raw[PAD_MAX_CONTROLLERS],
                                   uint16_t button)
{
    if (!raw) return 0;
    melee_web_fighter_input_neutral(raw);
    raw[0].button = button;
    return 1;
}
