#ifndef MELEE_WEB_AUDIO_STREAM_H
#define MELEE_WEB_AUDIO_STREAM_H
#include "gameplay_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebAudioStreamBlock {
 uint32_t offset,size,end,next;
 const uint8_t* payload[2];uint32_t channel_bytes;
 MeleeWebAudioChannel channel[2];
} MeleeWebAudioStreamBlock;
typedef struct MeleeWebAudioStreamInput {
 const char* path;const uint8_t* bytes;size_t size;
 uint32_t rate,channels;MeleeWebAudioChannel header[2];
 const MeleeWebAudioStreamBlock* blocks;uint32_t count;
} MeleeWebAudioStreamInput;
typedef struct MeleeWebAudioStream MeleeWebAudioStream;
/* Borrows validated native HPS descriptors; owns byte transport and three slots.
 * Start is performed by the original lbAudio/AXDriver file-path call. */
MeleeWebAudioStream* melee_web_audio_stream_begin(MeleeWebAudio*,const MeleeWebAudioStreamInput*,char*,size_t);
int melee_web_audio_stream_end(MeleeWebAudioStream*,char*,size_t);
int melee_web_audio_stream_pump_for(MeleeWebAudio*,char*,size_t);
int melee_web_audio_stream_resolve(MeleeWebAudio*,uint32_t address,const MeleeWebAudioChannel**,uint32_t* base);
int melee_web_audio_stream_owned(MeleeWebAudio*);
int melee_web_audio_stream_progress(MeleeWebAudio*,uint32_t* completed_payloads,uint32_t* revisited_blocks);
#ifdef __cplusplus
}
#endif
#endif
