/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_UCF_DBOOC_FIX_H
#define MELEE_WEB_SLIPPI_UCF_DBOOC_FIX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal scalar trace used by the isolated UCF diagnostic reducer. */
typedef struct SlippiUcfDboocTrace {
    uint32_t x_fmsubs_bits;
    uint32_t y_fmsubs_bits;
    uint32_t x_conversion_word;
    uint32_t y_conversion_word;
    uint32_t x_q_word;
    uint32_t y_q_word;
    uint32_t x_square_low32;
    uint32_t y_square_low32;
    uint32_t radius_sum_low32;
    uint32_t selected_threshold_bits;
    uint8_t timer;
    uint8_t override_selected;
} SlippiUcfDboocTrace;

/*
 * Apply the total binary32 scalar rule to raw IEEE-754 words.  Callers must
 * provide the source's unsigned timer byte and incoming threshold word.  The
 * trace is diagnostic state for focused tests, not a gameplay observation API.
 * Arithmetic requires RN-even and masked floating exceptions.  FPSCR flags,
 * traps, other rounding modes and NaN payload identity are outside this scalar
 * result contract.  No live profile activation or runtime wiring is implied.
 */
uint32_t slippi_profile_ucf_dbooc_threshold_bits(
    uint32_t stick_x_bits, uint32_t stick_y_bits, uint8_t timer,
    uint32_t common_threshold_bits, SlippiUcfDboocTrace *trace);

/* Gameplay-facing source helper. This is an ordinary internal C symbol, not
 * an Emscripten runtime export or a diagnostic trace API. */
float slippi_profile_ucf_dbooc_squat_reversal_threshold(
    float stick_x, float stick_y, uint8_t timer, float common_threshold);

/* Signed cmpwi-equivalent test used by the threshold helper. */
int slippi_profile_ucf_dbooc_radius_exceeds_threshold(uint32_t radius_word);

#ifdef __cplusplus
}
#endif

#endif
