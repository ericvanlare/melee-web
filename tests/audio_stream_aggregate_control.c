/* Actual audio/FX aggregate with a synthetic stream leaf. This verifies the
 * wrapper's admission, ordering and lifecycle, not HPS decoding or playback. */
#define MELEE_WEB_AUDIO_STREAM 1
#define main mixer_owner_control_main
#include "audio_checkpoint_owner_control.c"
#undef main

struct MeleeWebAudioStreamSnapshot { unsigned value; uint64_t token; };
static unsigned stream_present,stream_prepared,stream_value,stream_refuse;
static unsigned stream_restores,stream_captures,stream_prepares;
static MeleeWebAudioStreamSnapshot* bound_stream;
static unsigned stream_owner;

int melee_web_audio_stream_owned(MeleeWebAudio* audio){(void)audio;return stream_present;}
int melee_web_audio_stream_prepare_snapshot(MeleeWebAudio* audio,char* error,size_t size){
 (void)audio;(void)error;(void)size;++stream_prepares;
 if(stream_refuse)return 0;stream_prepared=1;return 1;
}
int melee_web_audio_stream_owner_info(MeleeWebAudio* audio,MeleeWebAudioStreamOwnerInfo* out){
 *out=(MeleeWebAudioStreamOwnerInfo){.present=stream_present,.prepared=stream_prepared,
  .audio_identity=audio,.audio_generation=audio->generation,
  .stream_identity=&stream_owner,.stream_generation=31};return 1;
}
int melee_web_audio_stream_snapshot_capture(MeleeWebAudio* audio,MeleeWebAudioStreamSnapshot** out,char* error,size_t size){
 (void)audio;(void)error;(void)size;*out=NULL;++stream_captures;
 if(!stream_present)return 1;
 if(!stream_prepared||stream_refuse||bound_stream)return 0;
 *out=malloc(sizeof(**out));if(!*out)return 0;
 **out=(MeleeWebAudioStreamSnapshot){.value=stream_value};return 1;
}
int melee_web_audio_stream_snapshot_admit(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* error,size_t size){
 (void)audio;(void)error;(void)size;
 return stream_present&&stream_prepared&&!stream_refuse&&snapshot&&
  snapshot->token==token&&(!token||bound_stream==snapshot);
}
int melee_web_audio_stream_snapshot_bind_interval(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* error,size_t size){
 if(!token||bound_stream||!melee_web_audio_stream_snapshot_admit(audio,snapshot,0,error,size))return 0;
 bound_stream=(MeleeWebAudioStreamSnapshot*)snapshot;bound_stream->token=token;return 1;
}
int melee_web_audio_stream_snapshot_restore(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* error,size_t size){
 if(!melee_web_audio_stream_snapshot_admit(audio,snapshot,token,error,size))return 0;
 ++stream_restores;stream_value=snapshot->value;return 1;
}
int melee_web_audio_stream_snapshot_pending_admit(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token){
 (void)audio;return stream_refuse&&snapshot&&bound_stream==snapshot&&snapshot->token==token;
}
int melee_web_audio_stream_snapshot_end_interval(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token){
 (void)audio;if(!token||bound_stream!=snapshot||snapshot->token!=token)return 0;
 bound_stream->token=0;bound_stream=NULL;return 1;
}
int melee_web_audio_stream_snapshot_destroy(MeleeWebAudioStreamSnapshot* snapshot,char* error,size_t size){
 (void)error;(void)size;if(snapshot&&bound_stream==snapshot)return 0;free(snapshot);return 1;
}
MeleeWebAudioStreamRangeVisitResult melee_web_audio_stream_visit_owned_ranges(
 MeleeWebAudio* audio,uint64_t generation,size_t capacity,MeleeWebAudioStreamOwnedRangeVisitor visitor,void* context,size_t* required){
 (void)audio;(void)generation;*required=1;
 if(!stream_present||!stream_prepared||stream_refuse)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 if(capacity<1)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INSUFFICIENT_CAPACITY;
 const MeleeWebAudioStreamOwnedRange row={MELEE_WEB_AUDIO_STREAM_RANGE_SELECTED_FILE,0,
  &stream_value,sizeof(stream_value),&stream_owner,31};
 return visitor(&row,context)?MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE:MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_STOPPED;
}

int main(void){
 /* The old mixer-only controls must work unchanged with the stream compiled. */
 assert(mixer_owner_control_main()==0);
 MeleeWebAudioSample samples[1]={0};u32 words[1]={11},starts[1]={0},offsets[1]={0};
 int16_t coefficients[2048]={0};SampleEntry entries[1]={0};Binding bindings[1]={0};u32* programs[1]={words};
 MeleeWebAudioInput input={samples,1,words,1,starts,1,offsets,1,coefficients,2048};
 MeleeWebAudioEffects effects={0};effects.generation=13;
 effects.reverb_heap=calloc(1,REVERB_HEAP_BYTES);effects.delay_heap=calloc(1,DELAY_HEAP_BYTES);
 assert(effects.reverb_heap&&effects.delay_heap);
 MeleeWebAudio audio={0};audio.input=&input;audio.entries=entries;audio.bindings=bindings;
 audio.binding_count=1;audio.programs=programs;audio.words=words;audio.starts=starts;audio.generation=17;
 audio.effects=&effects;active=&audio;

 /* An explicitly absent PRE cannot silently admit a newly created stream. */
 MeleeWebAudioSnapshot* absent=melee_web_audio_snapshot_capture_with_stream(&audio);assert(absent);
 stream_present=1;assert(!melee_web_audio_snapshot_admit(&audio,absent));
 assert(!melee_web_audio_snapshot_interval_begin(&audio,absent,1));
 melee_web_audio_snapshot_destroy(absent);
 /* Legacy factory must not prepare or capture a present stream. */
 unsigned captures=stream_captures,prepares=stream_prepares;
 MeleeWebAudioSnapshot* legacy=melee_web_audio_snapshot_capture(&audio);assert(legacy);
 assert(captures==stream_captures&&prepares==stream_prepares&&!stream_prepared);
 melee_web_audio_snapshot_destroy(legacy);

 stream_value=23;MeleeWebAudioSnapshot* snapshot=melee_web_audio_snapshot_capture_with_stream(&audio);
 assert(snapshot&&stream_prepared&&stream_prepares==prepares+1);
 assert(melee_web_audio_snapshot_interval_begin(&audio,snapshot,42));
 assert(!melee_web_audio_snapshot_interval_begin(&audio,snapshot,43));
 assert(!melee_web_audio_snapshot_interval_end(&audio,snapshot,41));
 assert(bound_stream&&bound_stream->token==42);
 words[0]=71;stream_value=99;((unsigned char*)effects.delay_heap)[0]=72;
 stream_refuse=1;
 assert(melee_web_audio_snapshot_interval_pending_admit(&audio,snapshot,42));
 assert(!melee_web_audio_snapshot_interval_pending_admit(&audio,snapshot,41));
 assert(!melee_web_audio_snapshot_restore(&audio,snapshot));
 assert(words[0]==71&&stream_value==99&&((unsigned char*)effects.delay_heap)[0]==72&&!stream_restores);
 stream_refuse=0;++effects.generation;
 assert(!melee_web_audio_snapshot_restore(&audio,snapshot));
 assert(stream_value==99&&!stream_restores);--effects.generation;
 size_t attempts=allocation_attempts;allocation_guard=1;
 assert(melee_web_audio_snapshot_restore(&audio,snapshot));allocation_guard=0;
 assert(attempts==allocation_attempts&&words[0]==11&&stream_value==23&&!((unsigned char*)effects.delay_heap)[0]);

 RangeCollector rows={0};assert(guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&rows)==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE);
 assert(rows.count==10);expect_range(&rows.rows[9],MELEE_WEB_AUDIO_RANGE_STREAM_BASE+8,
  &stream_value,sizeof(stream_value),&stream_owner,31);
 RangeCollector refused={0};assert(guarded_visit(&audio,17,9,collect_range,&refused)==MELEE_WEB_AUDIO_RANGE_VISIT_INSUFFICIENT_CAPACITY);
 assert(!refused.count);stream_refuse=1;
 assert(guarded_visit(&audio,17,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,collect_range,&refused)==MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER);
 assert(!refused.count);
 /* Failed intervals still disarm. Retiring the stream afterwards does not
  * prevent destruction of its receipt-owned bytes. */
 assert(melee_web_audio_snapshot_interval_end(&audio,snapshot,42)&&!bound_stream);
 stream_present=stream_prepared=0;
 melee_web_audio_snapshot_destroy(snapshot);
 active=NULL;free(effects.reverb_heap);free(effects.delay_heap);
 puts("Actual audio aggregate / synthetic stream lifecycle controls passed");return 0;
}
