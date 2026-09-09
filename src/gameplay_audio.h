#ifndef MELEE_WEB_GAMEPLAY_AUDIO_H
#define MELEE_WEB_GAMEPLAY_AUDIO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebAudioChannel {
 const int16_t* pcm;uint32_t frames;
 const int16_t* loop_pcm;uint32_t loop_frames;
 uint32_t current_nibble,end_nibble,loop_nibble;
 int16_t coefficients[16];
 uint16_t predictor_scale,loop_predictor_scale;
 int16_t history1,history2,loop_history1,loop_history2;
 uint8_t looping;
} MeleeWebAudioChannel;
typedef struct MeleeWebAudioSample {uint32_t id,rate,channels;MeleeWebAudioChannel channel[2];} MeleeWebAudioSample;
typedef struct MeleeWebAudioInput {
 const MeleeWebAudioSample* samples;uint32_t sample_count;
 const uint32_t* words;uint32_t word_count;
 const uint32_t* bank_starts;uint32_t bank_count;
 const uint32_t* program_offsets;uint32_t program_count;
 const int16_t* resample_coefficients;uint32_t resample_coefficient_count;
} MeleeWebAudioInput;
typedef struct MeleeWebAudio MeleeWebAudio;
/* Borrow decoded PCM and SEM data through end; setup publishes owned native
 * banks/programs to original synth/SEM code. This is a single stereo32kHz
 * provider; output calls advance the actual160-sample original audio clock. */
MeleeWebAudio* melee_web_audio_begin(const MeleeWebAudioInput*,char*,size_t);
int melee_web_audio_play(MeleeWebAudio*,int sound_id,uint8_t volume,uint8_t pan,int track,int channel,char*,size_t);
/* Opt-in original lbAudio STD reverb/delay; call before the first PCM block. */
int melee_web_audio_enable_effects(MeleeWebAudio*,char*,size_t);
int melee_web_audio_render(MeleeWebAudio*,float* interleaved_stereo,uint32_t frames,char*,size_t);
int melee_web_audio_active_samples(MeleeWebAudio*,uint32_t* ids,uint32_t capacity);
/* Monotonic identity for one owned provider lifetime. This lets source hosts
 * distinguish a retained audio engine from a new allocation at the same host
 * address without exposing or interpreting that address. */
uint64_t melee_web_audio_generation(MeleeWebAudio*);
int melee_web_audio_end(MeleeWebAudio*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
