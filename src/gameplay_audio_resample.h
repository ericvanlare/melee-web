/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef MELEE_WEB_AUDIO_RESAMPLE_H
#define MELEE_WEB_AUDIO_RESAMPLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebAudioResample {int16_t history[4];uint32_t fraction;} MeleeWebAudioResample;
typedef int16_t (*MeleeWebAudioReadSample)(void*);
/* src_select is the original AXPB selector: 0 four-tap,1 linear,2 nearest.
 * The four-tap coefficient pointer addresses one512-entry DSP coefficient bank.
 * Caller validates select<=2, ratio<=0x40000 and coefficient availability. */
int16_t melee_web_audio_resample(MeleeWebAudioResample*,uint32_t ratio,unsigned src_select,const int16_t* coefficients,MeleeWebAudioReadSample,void*);
#ifdef __cplusplus
}
#endif
#endif
