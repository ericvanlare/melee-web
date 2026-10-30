/* Compile the actual owner implementations in wasm32. The input data here is
 * synthetic: this checks checkpoint bytes/admission and owner-range provenance,
 * not original DSP behavior. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int allocation_guard;
static size_t allocation_attempts;
static void* audio_test_malloc(size_t);
static void* audio_test_calloc(size_t,size_t);
static void* audio_test_realloc(void*,size_t);
static void* audio_test_aligned_alloc(size_t,size_t);
#define malloc audio_test_malloc
#define calloc audio_test_calloc
#define realloc audio_test_realloc
#define aligned_alloc audio_test_aligned_alloc
#define MELEE_WEB_AUDIO_FX 1
#include "../src/gameplay_audio.c"
#include "../src/gameplay_audio_fx.c"
#undef malloc
#undef calloc
#undef realloc
#undef aligned_alloc

static void* audio_test_malloc(size_t size){
 if(allocation_guard){allocation_attempts++;return NULL;}
 return malloc(size);
}
static void* audio_test_calloc(size_t count,size_t size){
 if(allocation_guard){allocation_attempts++;return NULL;}
 return calloc(count,size);
}
static void* audio_test_realloc(void* old,size_t size){
 if(allocation_guard){allocation_attempts++;return NULL;}
 return realloc(old,size);
}
static void* audio_test_aligned_alloc(size_t alignment,size_t size){
 if(allocation_guard){allocation_attempts++;return NULL;}
 return aligned_alloc(alignment,size);
}

static MeleeWebAudioRangeVisitResult guarded_visit(MeleeWebAudio* audio,
 uint64_t generation,size_t capacity,MeleeWebAudioOwnedRangeVisitor visitor,
 void* context){
 size_t before=allocation_attempts;allocation_guard=1;
 MeleeWebAudioRangeVisitResult result=melee_web_audio_visit_owned_ranges(
  audio,generation,capacity,visitor,context);
 allocation_guard=0;assert(allocation_attempts==before);return result;
}

typedef struct RangeCollector {
 MeleeWebAudioOwnedRange rows[MELEE_WEB_AUDIO_OWNED_RANGE_MAX];
 size_t count;
 size_t fail_at;
} RangeCollector;

static int collect_range(const MeleeWebAudioOwnedRange* row,void* context){
 RangeCollector* collector=context;
 assert(collector&&collector->count<sizeof(collector->rows)/sizeof(collector->rows[0]));
 collector->rows[collector->count++]=*row;
 return !collector->fail_at||collector->count<collector->fail_at;
}

static void expect_range(const MeleeWebAudioOwnedRange* row,
 MeleeWebAudioOwnedRangeKind kind,const void* address,size_t extent,
 const void* owner,uint64_t generation){
 assert(row->kind==kind);assert(row->address==address);assert(row->byte_extent==extent);
 assert(row->owner_identity==owner);assert(row->generation==generation);
}

static void assert_audio_unchanged(const MeleeWebAudio* audio,
 const MeleeWebAudioInput* input,SampleEntry* entries,Binding* bindings,
 u32** programs,u32* owned_words,u32* starts,u32* source_sem,
 MeleeWebAudioEffects* effects,uint64_t next_audio,uint64_t next_fx,
 uint32_t budget,u32 sound_mode){
 assert(audio->input==input&&audio->entries==entries&&audio->bindings==bindings);
 assert(audio->binding_count==1&&audio->programs==programs&&audio->words==owned_words);
 assert(audio->starts==starts&&audio->source_sem==source_sem&&audio->generation==17);
 assert(audio->block_pos==57&&audio->playback[0].binding==bindings);
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 assert(audio->playback[0].resample.fraction==123);
 assert(audio->playback[0].history[63]==-1234);
#else
 assert(audio->playback[0].timing_fraction==123);
#endif
 assert(audio->ai_stream_volume[1]==53);
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 assert(audio->effects==effects&&effects->generation==13);
 assert(effects->reverb_heap&&effects->delay_heap);
#else
 (void)effects;
#endif
 assert(next_generation==next_audio&&next_effect_generation==next_fx);
 assert(command_budget==budget&&source_sound_mode==sound_mode);
}

int main(void){
 MeleeWebAudioSample samples[1]={0};int16_t borrowed_pcm[32]={0};
 samples[0].channel[0].pcm=borrowed_pcm;samples[0].channel[0].loop_pcm=borrowed_pcm;
 uint32_t borrowed_words[2]={11,12},owned_words[2]={11,12};
 uint32_t source_sem[2]={91,92},starts[1]={0},offsets[1]={0};
 int16_t coefficients[2048]={0};
 MeleeWebAudioInput input={samples,1,borrowed_words,2,starts,1,offsets,1,coefficients,2048};
 SampleEntry entries[1]={0};Binding bindings[1]={0};u32* programs[1]={owned_words};
 int32_t aux_input[2][480]={{0}},aux_output[2][480]={{0}};
 MeleeWebAudioEffects effects={0};effects.generation=13;
 effects.reverb_heap=calloc(1,REVERB_HEAP_BYTES);
 effects.delay_heap=calloc(1,DELAY_HEAP_BYTES);
 assert(effects.reverb_heap&&effects.delay_heap);
 effects.input[0]=aux_input[0];effects.input[1]=aux_input[1];
 effects.output[0]=aux_output[0];effects.output[1]=aux_output[1];
 MeleeWebAudio audio={0};
 audio.input=&input;audio.entries=entries;audio.bindings=bindings;audio.binding_count=1;
 audio.programs=programs;audio.words=owned_words;audio.starts=starts;audio.generation=17;
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.effects=&effects;
#endif
 audio.source_sem=source_sem;audio.playback[0].binding=bindings;
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.playback[0].resample.fraction=123;audio.playback[0].history[63]=-1234;
#else
 audio.playback[0].timing_fraction=123;
#endif
 audio.block_pos=57;audio.ai_stream_volume[1]=53;
 active=&audio;command_budget=349;source_sound_mode=OS_SOUND_MODE_STEREO;
 next_generation=23;next_effect_generation=31;

 /* The live inventory reports exact allocation extents and distinct audio/FX
  * owner identities. It allocates nothing and leaves the source counters alone. */
 const uint64_t audio_counter=next_generation,fx_counter=next_effect_generation;
 const uint32_t budget=command_budget,sound_mode=source_sound_mode;
 RangeCollector ranges={0};
 MeleeWebAudioRangeVisitResult result=guarded_visit(
  &audio,melee_web_audio_generation(&audio),MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&ranges);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE&&ranges.count==9);
 assert(allocation_attempts==0);
 expect_range(&ranges.rows[0],MELEE_WEB_AUDIO_RANGE_AUDIO_OWNER,&audio,sizeof(audio),&audio,17);
 expect_range(&ranges.rows[1],MELEE_WEB_AUDIO_RANGE_AUDIO_ENTRIES,entries,sizeof(entries),&audio,17);
 expect_range(&ranges.rows[2],MELEE_WEB_AUDIO_RANGE_AUDIO_BINDINGS,bindings,sizeof(bindings),&audio,17);
 expect_range(&ranges.rows[3],MELEE_WEB_AUDIO_RANGE_AUDIO_PROGRAMS,programs,sizeof(programs),&audio,17);
 expect_range(&ranges.rows[4],MELEE_WEB_AUDIO_RANGE_AUDIO_WORDS,owned_words,sizeof(owned_words),&audio,17);
 expect_range(&ranges.rows[5],MELEE_WEB_AUDIO_RANGE_AUDIO_STARTS,starts,sizeof(starts),&audio,17);
 expect_range(&ranges.rows[6],MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER,&effects,sizeof(effects),&effects,13);
 expect_range(&ranges.rows[7],MELEE_WEB_AUDIO_RANGE_EFFECTS_REVERB_HEAP,effects.reverb_heap,REVERB_HEAP_BYTES,&effects,13);
 expect_range(&ranges.rows[8],MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP,effects.delay_heap,DELAY_HEAP_BYTES,&effects,13);
 for(size_t i=0;i<ranges.count;i++){
  assert(ranges.rows[i].address!=samples&&ranges.rows[i].address!=borrowed_pcm);
  assert(ranges.rows[i].address!=borrowed_words);
  assert(ranges.rows[i].address!=source_sem);
  assert(ranges.rows[i].address!=aux_input[0]&&ranges.rows[i].address!=aux_input[1]);
  assert(ranges.rows[i].address!=aux_output[0]&&ranges.rows[i].address!=aux_output[1]);
 }
 assert_audio_unchanged(&audio,&input,entries,bindings,programs,owned_words,starts,
                        source_sem,&effects,audio_counter,fx_counter,budget,sound_mode);
 assert(effects.generation==13&&effects.input[0]==aux_input[0]&&
        effects.input[1]==aux_input[1]&&effects.output[0]==aux_output[0]&&
        effects.output[1]==aux_output[1]);

 /* Capacity and callback failures never report a complete inventory. Capacity
  * is rejected before any callback; visitor refusal reports a failed prefix. */
 RangeCollector rejected={0};
 result=guarded_visit(&audio,17,ranges.count-1,collect_range,&rejected);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_INSUFFICIENT_CAPACITY&&rejected.count==0);
 result=guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,NULL,NULL);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_ARGUMENT);
 rejected=(RangeCollector){.fail_at=3};
 result=guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&rejected);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_STOPPED&&rejected.count==3);
 assert_audio_unchanged(&audio,&input,entries,bindings,programs,owned_words,starts,
                        source_sem,&effects,audio_counter,fx_counter,budget,sound_mode);

 /* A compiled FX provider with no enabled owner reports only the six audio
  * ranges. Public-audio-disabled builds take the same explicit inventory. */
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.effects=NULL;
#endif
 RangeCollector no_fx={0};
 result=guarded_visit(&audio,17,6,collect_range,&no_fx);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE&&no_fx.count==6);
 for(size_t i=0;i<no_fx.count;i++)
  assert(no_fx.rows[i].kind<MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER);
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.effects=&effects;
#endif

 /* Wrong/inactive handles and a stale generation at the same live address
  * fail before visiting. This models reuse; it does not allocate a replacement. */
 MeleeWebAudio wrong={0};RangeCollector refused={0};
 result=guarded_visit(&wrong,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&refused);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER&&refused.count==0);
 audio.generation=18;
 result=guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&refused);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER&&refused.count==0);
 audio.generation=17;active=NULL;
 result=guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&refused);
 assert(result==MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER&&refused.count==0);
 active=&audio;

 MeleeWebAudioSnapshot* snapshot=melee_web_audio_snapshot_capture(&audio);
 assert(snapshot&&melee_web_audio_snapshot_admit(&audio,snapshot));
 owned_words[1]=99;entries[0].id=73;bindings[0].base=500;programs[0]=owned_words+1;starts[0]=44;
 audio.block_pos=7;
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.playback[0].resample.fraction=8;audio.playback[0].history[63]=9;
#else
 audio.playback[0].timing_fraction=8;
#endif
 audio.ai_stream_volume[1]=0;
 command_budget=8;source_sound_mode=OS_SOUND_MODE_MONO;
 ((unsigned char*)effects.reverb_heap)[REVERB_HEAP_BYTES-1]=71;
 ((unsigned char*)effects.delay_heap)[DELAY_HEAP_BYTES-1]=72;
 effects.input[0]=(int32_t*)source_sem;
 assert(melee_web_audio_snapshot_restore(&audio,snapshot));
 assert(owned_words[1]==12&&entries[0].id==0&&bindings[0].base==0&&programs[0]==owned_words&&starts[0]==0);
 assert(audio.block_pos==57&&audio.playback[0].binding==bindings);
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 assert(audio.playback[0].resample.fraction==123&&audio.playback[0].history[63]==-1234);
#else
 assert(audio.playback[0].timing_fraction==123);
#endif
 assert(audio.ai_stream_volume[1]==53);
 assert(command_budget==349&&source_sound_mode==OS_SOUND_MODE_STEREO);
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 assert(!((unsigned char*)effects.reverb_heap)[REVERB_HEAP_BYTES-1]);
 assert(!((unsigned char*)effects.delay_heap)[DELAY_HEAP_BYTES-1]);
 assert(effects.input[0]==aux_input[0]&&effects.output[0]==aux_output[0]);
#endif

 /* Refuse every changed ownership boundary before any buffer is modified. */
 owned_words[1]=79;
 ++audio.generation;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--audio.generation;
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 ++effects.generation;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--effects.generation;
#endif
 ++input.word_count;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--input.word_count;
 ++audio.binding_count;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));--audio.binding_count;
 audio.source_sem=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.source_sem=source_sem;
 audio.source_closed=1;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.source_closed=0;
#if defined(MELEE_WEB_AUDIO_FX) && !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 audio.effects=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));audio.effects=&effects;
#endif
 active=NULL;assert(!melee_web_audio_snapshot_restore(&audio,snapshot));active=&audio;
 assert(owned_words[1]==79);
 assert(melee_web_audio_snapshot_restore(&audio,snapshot)&&owned_words[1]==12);
 melee_web_audio_snapshot_destroy(snapshot);
 active=NULL;free(effects.reverb_heap);free(effects.delay_heap);
 puts("Actual wasm32 audio owner checkpoint controls passed");return 0;
}
