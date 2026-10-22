#ifndef MELEE_WEB_AUDIO_FX_H
#define MELEE_WEB_AUDIO_FX_H
#include <stddef.h>
#include <stdint.h>
typedef struct MeleeWebAudioEffects MeleeWebAudioEffects;
typedef struct MeleeWebAudioEffectsSnapshot MeleeWebAudioEffectsSnapshot;
MeleeWebAudioEffects* melee_web_audio_fx_create(char*,size_t);
void melee_web_audio_fx_destroy(MeleeWebAudioEffects*);
/* Original AXAux callbacks and three-buffer rotation, once per160 samples. */
void melee_web_audio_fx_prepare(MeleeWebAudioEffects*);
void melee_web_audio_fx_send(MeleeWebAudioEffects*,unsigned bus,unsigned channel,unsigned sample,int32_t value);
int32_t melee_web_audio_fx_output(MeleeWebAudioEffects*,unsigned channel,unsigned sample);
/* Owned effect heaps only. Original AX/AXFX globals and auxiliary ring storage
 * are separate source-state ranges; the caller must restore those together. */
MeleeWebAudioEffectsSnapshot* melee_web_audio_fx_snapshot_capture(MeleeWebAudioEffects*);
int melee_web_audio_fx_snapshot_admit(MeleeWebAudioEffects*,const MeleeWebAudioEffectsSnapshot*);
int melee_web_audio_fx_snapshot_restore(MeleeWebAudioEffects*,const MeleeWebAudioEffectsSnapshot*);
void melee_web_audio_fx_snapshot_destroy(MeleeWebAudioEffectsSnapshot*);
#endif
