// Copyright 2008 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from ResampleAudio in Dolphin AXVoice.h, revision
// a2efdf1197be8132674b90fe9cf4761df39752ed. Only scalar sample-rate conversion
// is retained; no emulator state, memory services, CPU or DSP execution engine.
#include "gameplay_audio_resample.h"
#include <string.h>
static int32_t floor_shift(int64_t value,unsigned bits){return value>=0?value>>bits:-(((-value)+((INT64_C(1)<<bits)-1))>>bits);}
int16_t melee_web_audio_resample(MeleeWebAudioResample* state,uint32_t ratio,unsigned select,const int16_t* coefficients,MeleeWebAudioReadSample read,void* context){
 if(select==2){int16_t value=read(context);memmove(state->history,state->history+1,3*sizeof(int16_t));state->history[3]=value;return value;}
 uint32_t phase=state->fraction+ratio;
 while(phase>=0x10000){memmove(state->history,state->history+1,3*sizeof(int16_t));state->history[3]=read(context);phase-=0x10000;}
 state->fraction=phase;int32_t value;
 if(select==0){const int16_t* c=coefficients+((phase>>9)<<2);int64_t sum=0;for(unsigned i=0;i<4;i++)sum+=(int64_t)state->history[i]*c[i];value=floor_shift(sum,15);}
 else if(phase){value=floor_shift((int64_t)state->history[0]*(0x10000-phase)+(int64_t)state->history[1]*phase,16);}
 else value=state->history[0];
 return value<INT16_MIN?INT16_MIN:value>INT16_MAX?INT16_MAX:value;
}
