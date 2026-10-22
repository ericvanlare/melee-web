/* Compile the actual owner implementations in wasm32. The input data here is
 * synthetic: this checks checkpoint bytes/admission, not original DSP behavior. */
#define MELEE_WEB_AUDIO_FX 1
#include "../src/gameplay_audio.c"
#include "../src/gameplay_audio_fx.c"
#include <assert.h>

int main(void){
 MeleeWebAudioSample samples[1]={0};
 uint32_t words[2]={11,12},starts[1]={0},offsets[1]={0};
 int16_t coefficients[2048]={0};
 MeleeWebAudioInput input={samples,1,words,2,starts,1,offsets,1,coefficients,2048};
 SampleEntry entries[1]={0};Binding bindings[1]={0};u32* programs[1]={words};
 MeleeWebAudioEffects effects={0};
 effects.generation=13;
 effects.reverb_heap=calloc(1,REVERB_HEAP_BYTES);
 effects.delay_heap=calloc(1,DELAY_HEAP_BYTES);
 assert(effects.reverb_heap&&effects.delay_heap);
 MeleeWebAudio audio={0};
 audio.input=&input;audio.entries=entries;audio.bindings=bindings;audio.binding_count=1;
 audio.programs=programs;audio.words=words;audio.starts=starts;audio.generation=17;
 audio.effects=&effects;audio.source_sem=words;audio.playback[0].binding=bindings;
 audio.playback[0].resample.fraction=123;audio.block_pos=57;
 audio.playback[0].history[63]=-1234;audio.ai_stream_volume[1]=53;
 active=&audio;command_budget=349;source_sound_mode=OS_SOUND_MODE_STEREO;
 MeleeWebAudioSnapshot* snapshot=melee_web_audio_snapshot_capture(&audio);
 assert(snapshot&&melee_web_audio_snapshot_admit(&audio,snapshot));
 words[1]=99;entries[0].id=73;bindings[0].base=500;programs[0]=words+1;starts[0]=44;
 audio.block_pos=7;audio.playback[0].resample.fraction=8;
 audio.playback[0].history[63]=9;audio.ai_stream_volume[1]=0;
 command_budget=8;source_sound_mode=OS_SOUND_MODE_MONO;
 ((unsigned char*)effects.reverb_heap)[REVERB_HEAP_BYTES-1]=71;
 ((unsigned char*)effects.delay_heap)[DELAY_HEAP_BYTES-1]=72;
 effects.input[0]=(int32_t*)words;
 assert(melee_web_audio_snapshot_restore(&audio,snapshot));
 assert(words[1]==12&&entries[0].id==0&&bindings[0].base==0&&programs[0]==words&&starts[0]==0);
 assert(audio.block_pos==57&&audio.playback[0].resample.fraction==123&&audio.playback[0].history[63]==-1234);
 assert(audio.playback[0].binding==bindings&&audio.ai_stream_volume[1]==53);
 assert(command_budget==349&&source_sound_mode==OS_SOUND_MODE_STEREO);
 assert(!((unsigned char*)effects.reverb_heap)[REVERB_HEAP_BYTES-1]);
 assert(!((unsigned char*)effects.delay_heap)[DELAY_HEAP_BYTES-1]&&!effects.input[0]);

 // Refuse every changed ownership boundary before any buffer is modified.
 words[1]=79;
 ++audio.generation;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--audio.generation;
 ++effects.generation;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--effects.generation;
 ++input.word_count;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--input.word_count;
 ++audio.binding_count;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--audio.binding_count;
 audio.source_sem=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.source_sem=words;
 audio.source_closed=1;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.source_closed=0;
 audio.effects=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.effects=&effects;
 active=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));active=&audio;
 assert(words[1]==79);
 assert(melee_web_audio_snapshot_restore(&audio,snapshot)&&words[1]==12);
 melee_web_audio_snapshot_destroy(snapshot);
 active=NULL;free(effects.reverb_heap);free(effects.delay_heap);
 puts("Actual wasm32 audio owner checkpoint controls passed");return 0;
}
