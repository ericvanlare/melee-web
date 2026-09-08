#ifndef MELEE_WEB_AUDIO_FX_H
#define MELEE_WEB_AUDIO_FX_H
#include <stddef.h>
#include <stdint.h>
typedef struct MeleeWebAudioEffects MeleeWebAudioEffects;
MeleeWebAudioEffects* melee_web_audio_fx_create(char*,size_t);
void melee_web_audio_fx_destroy(MeleeWebAudioEffects*);
/* Original AXAux callbacks and three-buffer rotation, once per160 samples. */
void melee_web_audio_fx_prepare(MeleeWebAudioEffects*);
void melee_web_audio_fx_send(MeleeWebAudioEffects*,unsigned bus,unsigned channel,unsigned sample,int32_t value);
int32_t melee_web_audio_fx_output(MeleeWebAudioEffects*,unsigned channel,unsigned sample);
#endif
