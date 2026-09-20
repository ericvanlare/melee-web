#ifndef MELEE_WEB_GAMEPLAY_AUDIO_RESAMPLE_H
#define MELEE_WEB_GAMEPLAY_AUDIO_RESAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The four samples are chronological: history[0] is the oldest retained
 * sample and history[3] is the newest. fraction holds only the low 16 bits
 * of the non-negative Q16.16 source phase.
 */
typedef struct MeleeWebAudioResample {
    int16_t history[4];
    uint32_t fraction;
} MeleeWebAudioResample;

typedef int16_t (*MeleeWebAudioReadSample)(void* context);

/*
 * Render one output sample.  ratio is a non-negative Q16.16 source/output
 * ratio.  select 0 is four-tap polyphase, 1 is linear, and 2 is direct.
 * Caller supplies a valid state, select <= 2, ratio <= 0x40000, and
 * fraction < 0x10000. read_sample must be callable when input is consumed.
 * For select 0, coefficients points at one signed 512-word (128 x 4) bank.
 * See docs/AUDIO_RESAMPLER_CONTRACT.md for provenance and compatibility scope.
 */
int16_t melee_web_audio_resample(MeleeWebAudioResample* state,
                                 uint32_t ratio,
                                 unsigned select,
                                 const int16_t* coefficients,
                                 MeleeWebAudioReadSample read_sample,
                                 void* context);

#ifdef __cplusplus
}
#endif

#endif
