#ifndef MELEE_WEB_NATIVE_MENU_STAGE_INPUT_H
#define MELEE_WEB_NATIVE_MENU_STAGE_INPUT_H

#include <stdint.h>

#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values returned by melee_web_stage_input_drive. */
enum {
    MELEE_WEB_STAGE_INPUT_INVALID = -1,
    MELEE_WEB_STAGE_INPUT_AT_TARGET = 0,
    MELEE_WEB_STAGE_INPUT_MOVING = 1,
};

/* Read-only data supplied by an original SSS observation hook.  The hook
 * should copy the source cursor translation, selected tile and the target
 * tile's source position/half-extents into this value.  The input helper
 * never receives or changes mnStageSel's selection byte and never calls the
 * source random stage routine. */
typedef struct MeleeWebStageInputObservation {
    int selected_tile_index;
    int selected_stage_kind;
    float cursor_x;
    float cursor_y;
    int target_tile_index;
    int target_stage_kind;
    float target_x;
    float target_y;
    float target_half_width;
    float target_half_height;
} MeleeWebStageInputObservation;

/* Adapt the source read-only SSS probe to the local observation shape.  The
 * source implementation returns ids={selected tile, selected StKind,
 * target tile, target StKind} and geometry={cursor x/y, target x/y, target
 * half-width/height}. */
int melee_web_stage_input_observe(
    int stage_kind, MeleeWebStageInputObservation*);

/* Return whether the observation describes the requested source stage tile. */
int melee_web_stage_input_target_is_valid(
    const MeleeWebStageInputObservation*, int stage_kind);

/* Build one raw source PAD sample that moves port 0 toward the observed tile.
 * The caller must observe again after submitting this sample; the source
 * cursor moves in 1.5-unit steps for the emitted +/-80 stick values and is
 * clamped by mnStageSel_8025A310. */
int melee_web_stage_input_drive(
    PADStatus raw[PAD_MAX_CONTROLLERS],
    const MeleeWebStageInputObservation*, int stage_kind);

/* Build a source-neutral sample, retaining the host convention that ports 2
 * and 3 are disconnected. */
void melee_web_stage_input_neutral(PADStatus raw[PAD_MAX_CONTROLLERS]);

/* Build a one-frame button sample on port 0.  This is only a raw PAD helper;
 * the source scene remains responsible for interpreting the button. */
int melee_web_stage_input_button(PADStatus raw[PAD_MAX_CONTROLLERS],
                                 uint16_t button);

#ifdef __cplusplus
}
#endif

#endif
