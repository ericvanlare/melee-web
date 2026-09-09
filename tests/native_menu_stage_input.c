#include "native_menu_stage_input.h"

#include <math.h>

/* Implemented in the source menu patch.  It only reads the live SSS cursor,
 * source tile table and p_link5 target objects; it does not write selection
 * state or consume the source RNG. */
extern int melee_web_sss_observe(int stage_kind, int ids[4], float geometry[6]);

static int finite_float(float value)
{
    return isfinite(value) != 0;
}

int melee_web_stage_input_observe(
    int stage_kind, MeleeWebStageInputObservation* observation)
{
    int ids[4];
    float geometry[6];
    if (!observation || !melee_web_sss_observe(stage_kind, ids, geometry))
        return 0;
    observation->selected_tile_index = ids[0];
    observation->selected_stage_kind = ids[1];
    observation->cursor_x = geometry[0];
    observation->cursor_y = geometry[1];
    observation->target_tile_index = ids[2];
    observation->target_stage_kind = ids[3];
    observation->target_x = geometry[2];
    observation->target_y = geometry[3];
    observation->target_half_width = geometry[4];
    observation->target_half_height = geometry[5];
    return melee_web_stage_input_target_is_valid(observation, stage_kind);
}

int melee_web_stage_input_target_is_valid(
    const MeleeWebStageInputObservation* observation, int stage_kind)
{
    if (!observation || observation->target_stage_kind != stage_kind ||
        observation->target_tile_index < 0 || observation->target_tile_index >= 30 ||
        !finite_float(observation->cursor_x) ||
        !finite_float(observation->cursor_y) ||
        !finite_float(observation->target_x) ||
        !finite_float(observation->target_y) ||
        !finite_float(observation->target_half_width) ||
        !finite_float(observation->target_half_height) ||
        observation->target_half_width <= 0.0f ||
        observation->target_half_height <= 0.0f)
        return 0;
    return 1;
}

void melee_web_stage_input_neutral(PADStatus raw[PAD_MAX_CONTROLLERS])
{
    if (!raw) return;
    for (unsigned port = 0; port < PAD_MAX_CONTROLLERS; ++port) {
        raw[port] = (PADStatus){0};
        raw[port].err = PAD_ERR_NONE;
    }
    raw[2].err = PAD_ERR_NO_CONTROLLER;
    raw[3].err = PAD_ERR_NO_CONTROLLER;
}

int melee_web_stage_input_drive(
    PADStatus raw[PAD_MAX_CONTROLLERS],
    const MeleeWebStageInputObservation* observation, int stage_kind)
{
    const float source_stick_step = 1.5f;
    const float center_tolerance = 0.75f * source_stick_step;
    const float dx = observation ? observation->target_x - observation->cursor_x : 0.0f;
    const float dy = observation ? observation->target_y - observation->cursor_y : 0.0f;
    const int selected_target = observation &&
                                observation->selected_stage_kind == stage_kind;
    if (!raw || !melee_web_stage_input_target_is_valid(observation, stage_kind))
        return MELEE_WEB_STAGE_INPUT_INVALID;

    melee_web_stage_input_neutral(raw);
    /* The source chooses a tile using strict interior bounds, while the
     * cursor itself moves by 0.03 * (80 - 30) = 1.5 units per sample.  A
     * whole tile is therefore too broad a stopping condition: at its edge
     * the source may still report the previous tile.  Aim for the center and
     * stop only after the source has reported the requested stage. */
    const int x_close = fabsf(dx) <= center_tolerance;
    const int y_close = fabsf(dy) <= center_tolerance;
    if (selected_target && x_close && y_close)
        return MELEE_WEB_STAGE_INPUT_AT_TARGET;

    if (!x_close)
        raw[0].stickX = dx > 0.0f ? 80 : -80;
    if (!y_close)
        raw[0].stickY = dy > 0.0f ? 80 : -80;
    /* If the source's selected tile is stale while the cursor is already
     * within the center tolerance, keep feeding a deterministic nudge.  It
     * is preferable to one extra source update to returning neutral forever;
     * the next observation either reports the requested tile or supplies a
     * new center direction. */
    if (selected_target == 0 && x_close && y_close)
        raw[0].stickX = dx >= 0.0f ? 80 : -80;

    return MELEE_WEB_STAGE_INPUT_MOVING;
}

int melee_web_stage_input_button(PADStatus raw[PAD_MAX_CONTROLLERS],
                                 uint16_t button)
{
    if (!raw) return 0;
    melee_web_stage_input_neutral(raw);
    raw[0].button = button;
    return 1;
}
