#include "gameplay_audio.h"
#include "gameplay_audio_resample.h"
#include "gameplay_audio_itd.h"
#if defined(MELEE_WEB_AUDIO_STREAM)
#include "gameplay_audio_stream.h"
#endif
#if defined(MELEE_WEB_AUDIO_FX)
#include "gameplay_audio_fx.h"
#endif
#include <dolphin/ax.h>
#include <sysdolphin/baselib/axdriver.h>
#include <sysdolphin/baselib/synth.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern u32 melee_web_audio_aux_enabled(void);
extern void __AXAllocInit(void);
extern void __AXAllocQuit(void);
extern void __AXVPBInit(void);
extern AXVPB* __AXGetStackHead(u32);
extern void melee_web_audio_synth_begin(void**);
extern void melee_web_audio_synth_end(void);
extern void melee_web_audio_driver_begin(int,u32*,int,u32**);
extern void melee_web_audio_driver_end(void);
typedef struct VoiceParameters {AXPBADDR addr;AXPBADPCM adpcm;AXPBADPCMLOOP loop;u16 pad;} VoiceParameters;
typedef struct SampleEntry {struct SampleEntry* next;int id,channels,rate;VoiceParameters voice[2];} SampleEntry;
_Static_assert(sizeof(VoiceParameters)==64&&offsetof(SampleEntry,voice)==16,"Original synth SSM entry ABI");
_Static_assert(sizeof(AXVPB)==0x1f8&&sizeof(AXPB)==0xc0,"Original SDK voice/control ABI");
typedef struct Binding {const MeleeWebAudioChannel* channel;uint32_t id,base;} Binding;
typedef struct Playback {const Binding* binding;Binding stream_binding;size_t position;MeleeWebAudioResample resample;int loop,playing;int16_t history[64];unsigned history_at;} Playback;
struct MeleeWebAudio {const MeleeWebAudioInput* input;SampleEntry* entries;Binding* bindings;uint32_t binding_count;u32** programs;u32* words;u32* starts;Playback playback[64];unsigned block_pos;
#if defined(MELEE_WEB_AUDIO_FX)
MeleeWebAudioEffects* effects;
#endif
};
static MeleeWebAudio* active;
static uint32_t command_budget;
void melee_web_audio_program_check(const u32* command){
 if(!active)return;
 uintptr_t at=(uintptr_t)command,begin=(uintptr_t)active->words,end=begin+active->input->word_count*4;
 if(at<begin||at>=end||(at-begin)%4||!command_budget){fprintf(stderr,"Native SEM command pointer or callback work budget invalid\n");abort();}
 --command_budget;
}
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int live(MeleeWebAudio* a){return a&&active==a;}
int melee_web_audio_is_active(MeleeWebAudio* a){return live(a);}
static uint32_t addr(u16 hi,u16 lo){return (uint32_t)hi<<16|lo;}
static void split(uint32_t v,u16* hi,u16* lo){*hi=v>>16;*lo=v;}
static void free_owner(MeleeWebAudio* a){if(a){free(a->entries);free(a->bindings);free(a->programs);free(a->words);free(a->starts);free(a);}}
MeleeWebAudio* melee_web_audio_begin(const MeleeWebAudioInput* in,char* e,size_t n){
 if(active||!in||!in->resample_coefficients||in->resample_coefficient_count!=2048||!in->samples||!in->sample_count||in->sample_count>4096||!in->words||!in->word_count||in->word_count>1048576||!in->bank_starts||!in->bank_count||in->bank_count>4096||!in->program_offsets||!in->program_count||in->program_count>65536){fail(e,n,"Invalid or already-owned audio inputs");return NULL;}
 for(unsigned priority=1;priority<32;priority++)if(__AXGetStackHead(priority)){fail(e,n,"Original AX voices are already owned");return NULL;}
 for(uint32_t i=0;i<in->bank_count;i++)if(in->bank_starts[i]>=in->program_count||(i&&in->bank_starts[i]<in->bank_starts[i-1])){fail(e,n,"Invalid SEM bank starts");return NULL;}
 for(uint32_t i=0;i<in->program_count;i++)if(in->program_offsets[i]%4||in->program_offsets[i]/4>=in->word_count){fail(e,n,"Invalid SEM program pointer");return NULL;}
 uint32_t binding_count=0;
 for(uint32_t i=0;i<in->sample_count;i++){
  const MeleeWebAudioSample* s=&in->samples[i];if(s->id>65535||s->channels<1||s->channels>2||!s->rate||s->rate>192000){fail(e,n,"Invalid synth sample metadata");return NULL;}
  for(uint32_t j=0;j<i;j++)if(in->samples[j].id==s->id){fail(e,n,"Duplicate synth sample ID");return NULL;}
  for(uint32_t j=0;j<s->channels;j++){const MeleeWebAudioChannel* c=&s->channel[j];if(!c->pcm||!c->frames||c->current_nibble%16<2||c->end_nibble%16<2||c->current_nibble>c->end_nibble||c->end_nibble>0xfffffff||c->looping>1||(c->looping&&(!c->loop_pcm||!c->loop_frames||c->loop_nibble%16<2||c->loop_nibble<c->current_nibble||c->loop_nibble>c->end_nibble))){fail(e,n,"Invalid PCM binding");return NULL;}}
  for(uint32_t j=0;j<s->channels;j++){
   const MeleeWebAudioChannel* c=&s->channel[j];uint32_t end=c->end_nibble/16*14+c->end_nibble%16-2,start=c->current_nibble/16*14+c->current_nibble%16-2;
   if(c->frames!=end-start+1||(c->looping&&c->loop_frames!=end-(c->loop_nibble/16*14+c->loop_nibble%16-2)+1)){fail(e,n,"PCM frame extent disagrees with original nibble addresses");return NULL;}
  }
  binding_count+=s->channels;
 }
 MeleeWebAudio* a=calloc(1,sizeof(*a));if(!a){fail(e,n,"Audio scope allocation failed");return NULL;}
 a->entries=calloc(in->sample_count,sizeof(*a->entries));a->bindings=calloc(binding_count,sizeof(*a->bindings));a->programs=calloc(in->program_count,sizeof(*a->programs));a->words=malloc(in->word_count*4);a->starts=malloc(in->bank_count*4);
 if(!a->entries||!a->bindings||!a->programs||!a->words||!a->starts){free_owner(a);fail(e,n,"Audio descriptor allocation failed");return NULL;}
 a->input=in;a->binding_count=binding_count;memcpy(a->words,in->words,in->word_count*4);memcpy(a->starts,in->bank_starts,in->bank_count*4);
 for(uint32_t i=0;i<in->program_count;i++)a->programs[i]=a->words+in->program_offsets[i]/4;
 void* buckets[32]={0};uint32_t bound=0;uint64_t base=0x10000;
 for(uint32_t i=0;i<in->sample_count;i++){
  const MeleeWebAudioSample* s=&in->samples[i];SampleEntry* entry=&a->entries[i];entry->id=s->id;entry->channels=s->channels;entry->rate=s->rate;entry->next=buckets[s->id&31];buckets[s->id&31]=entry;
  for(uint32_t j=0;j<s->channels;j++){
   const MeleeWebAudioChannel* c=&s->channel[j];if(base+c->end_nibble>=0x80000000u){free_owner(a);fail(e,n,"Audio address-key budget exceeded");return NULL;}
   VoiceParameters* v=&entry->voice[j];v->addr.loopFlag=c->looping;split(base+c->current_nibble,&v->addr.currentAddressHi,&v->addr.currentAddressLo);split(base+c->end_nibble,&v->addr.endAddressHi,&v->addr.endAddressLo);split(base+c->loop_nibble,&v->addr.loopAddressHi,&v->addr.loopAddressLo);
   memcpy(v->adpcm.a,c->coefficients,32);v->adpcm.pred_scale=c->predictor_scale;v->adpcm.yn1=c->history1;v->adpcm.yn2=c->history2;v->loop.loop_pred_scale=c->loop_predictor_scale;v->loop.loop_yn1=c->loop_history1;v->loop.loop_yn2=c->loop_history2;
   a->bindings[bound++]=(Binding){c,s->id,(uint32_t)base};base=(base+c->end_nibble+32)&~(uint64_t)15;
  }
 }
 __AXAllocInit();__AXVPBInit();melee_web_audio_synth_begin(buckets);melee_web_audio_driver_begin(in->bank_count,a->starts,in->program_count,a->programs);active=a;if(e&&n)*e=0;return a;
}
int melee_web_audio_play(MeleeWebAudio* a,int id,uint8_t volume,uint8_t pan,int track,int channel,char* e,size_t n){
 if(!live(a)||id<0){fail(e,n,"Audio scope or sound ID is invalid");return -1;}
 int result=AXDriver_8038CFF4(id,volume,pan,track,channel);if(result<0)fail(e,n,"Original SEM driver rejected sound request");else if(e&&n)*e=0;return result;
}
int melee_web_audio_enable_effects(MeleeWebAudio* a,char* e,size_t n){
 if(!live(a)||a->block_pos)return fail(e,n,"Effects require an active audio scope at a block boundary");
#if defined(MELEE_WEB_AUDIO_FX)
 if(a->effects)return 1;
 if(melee_web_audio_aux_enabled())return fail(e,n,"Original auxiliary effects are already owned");
 a->effects=melee_web_audio_fx_create(e,n);return a->effects!=NULL;
#else
 return fail(e,n,"Original AXFX provider was not built");
#endif
}
static int16_t mix_sample(int16_t sample,int volume){int64_t value=(int64_t)sample*volume;value=value>=0?value/32768:-((-value+32767)/32768);return value<-32768?-32768:value>32767?32767:value;}
static const Binding* binding(MeleeWebAudio* a,uint32_t at,uint32_t end){for(uint32_t i=0;i<a->binding_count;i++){const Binding* b=&a->bindings[i];if(at>=b->base+b->channel->current_nibble&&at<=b->base+b->channel->end_nibble&&end==b->base+b->channel->end_nibble)return b;}return NULL;}
static size_t index_from_nibble(uint32_t at){return (size_t)(at/16)*14+at%16-2;}
static uint32_t nibble_from_index(size_t index){return (uint32_t)(index/14*16+index%14+2);}
static const Binding* find_binding(MeleeWebAudio* a,Playback* p,uint32_t at,uint32_t end){
 const Binding* found=binding(a,at,end);if(found)return found;
#if defined(MELEE_WEB_AUDIO_STREAM)
 const MeleeWebAudioChannel* c;uint32_t base;
 if(melee_web_audio_stream_resolve(a,at,&c,&base)){p->stream_binding=(Binding){c,UINT32_MAX,base};return &p->stream_binding;}
#endif
 return NULL;
}
typedef struct SampleRead {Playback* playback;AXPB* pb;MeleeWebAudio* audio;int failed;} SampleRead;
static int16_t read_pcm(void* context){
 SampleRead* read=context;if(read->failed)return 0;Playback* p=read->playback;const MeleeWebAudioChannel* c=p->binding->channel;
 uint32_t count=p->loop?c->loop_frames:c->frames;
 if(p->position>=count){if(read->pb->addr.loopFlag){
#if defined(MELEE_WEB_AUDIO_STREAM)
 if(p->binding->id==UINT32_MAX){
  uint32_t at=addr(read->pb->addr.loopAddressHi,read->pb->addr.loopAddressLo);
  p->binding=find_binding(read->audio,p,at,0);if(!p->binding||p->binding->id!=UINT32_MAX){read->failed=1;return 0;}
  c=p->binding->channel;p->position=index_from_nibble(at-p->binding->base)-index_from_nibble(c->current_nibble);p->loop=0;
 }else
#endif
 {p->position=0;p->loop=1;}
 }else{read->pb->state=0;return 0;}}
 return (p->loop?c->loop_pcm:c->pcm)[p->position++];
}
int melee_web_audio_render(MeleeWebAudio* a,float* output,uint32_t frames,char* e,size_t n){
 if(!live(a)||!output||frames>32000)return fail(e,n,"Invalid audio output request");
 memset(output,0,frames*2*sizeof(float));
 for(uint32_t frame=0;frame<frames;frame++){
  if(a->block_pos==0){
#if defined(MELEE_WEB_AUDIO_STREAM)
   if(!melee_web_audio_stream_pump_for(a,e,n))return 0;
#endif
#if defined(MELEE_WEB_AUDIO_FX)
   if(a->effects)melee_web_audio_fx_prepare(a->effects);
#endif
   command_budget=65536;HSD_SynthCallback();
   if(melee_web_audio_aux_enabled()
#if defined(MELEE_WEB_AUDIO_FX)
      &&!a->effects
#endif
   )return fail(e,n,"Native audio provider has no configured auxiliary effect renderer");
  }
  int32_t mixed[2]={0};
  for(unsigned priority=1;priority<32;priority++)for(AXVPB* voice=__AXGetStackHead(priority);voice;voice=voice->next){
   Playback* p=&a->playback[voice->index];AXPB* pb=&voice->pb;
   if(!pb->state){p->playing=0;continue;}
   uint32_t at=addr(pb->addr.currentAddressHi,pb->addr.currentAddressLo),end=addr(pb->addr.endAddressHi,pb->addr.endAddressLo);
   if(!p->playing||(voice->sync&(AX_SYNC_FLAG_COPYADDR|AX_SYNC_FLAG_COPYCURADDR))){
    p->binding=find_binding(a,p,at,end);if(!p->binding)return fail(e,n,"Original AX voice addresses are not bound to decoded SSM/HPS PCM");
    if(at%16<2)return fail(e,n,"Original AX current address points to DSP header");
    p->position=index_from_nibble(at-p->binding->base)-index_from_nibble(p->binding->channel->current_nibble);p->loop=0;p->playing=1;p->resample.fraction=pb->src.currentAddressFrac;memcpy(p->resample.history,pb->src.last_samples,sizeof(p->resample.history));p->history_at=0;memset(p->history,0,sizeof(p->history));
   }
   const MeleeWebAudioChannel* c=p->binding->channel;
   if(pb->addr.format||pb->mix.vS)return fail(e,n,"Native audio provider does not yet implement non-ADPCM or auxiliary mixing");
   if(pb->itd.flag&&(pb->itd.shiftL>31||pb->itd.shiftR>31||pb->itd.targetShiftL>31||pb->itd.targetShiftR>31))return fail(e,n,"Original AX interaural delay exceeds32-sample history");
   if(voice->sync&AX_SYNC_FLAG_COPYITD)memset(p->history,0,sizeof(p->history));
   if(p->binding->id!=UINT32_MAX&&pb->addr.loopFlag&&(!c->looping||addr(pb->addr.loopAddressHi,pb->addr.loopAddressLo)!=p->binding->base+c->loop_nibble))return fail(e,n,"Original AX loop differs from decoded SSM loop");
   if(pb->srcSelect>2||pb->coefSelect>2)return fail(e,n,"Unknown original AX resampler selector");
   uint32_t ratio=addr(pb->src.ratioHi,pb->src.ratioLo);if(!ratio||ratio>0x40000)return fail(e,n,"Original AX sample ratio is unsupported");
   SampleRead read={p,pb,a,0};int16_t value=melee_web_audio_resample(&p->resample,ratio,pb->srcSelect,a->input->resample_coefficients+pb->coefSelect*512,read_pcm,&read);
   if(read.failed)return fail(e,n,"Original HPS loop points to an unloaded auxiliary slot");
   c=p->binding->channel;
   value=mix_sample(value,(int16_t)pb->ve.currentVolume);p->history[p->history_at]=value;
   unsigned left=pb->itd.flag?melee_web_audio_itd_delay(pb->itd.shiftL):0,right=pb->itd.flag?melee_web_audio_itd_delay(pb->itd.shiftR):0;
   int16_t l=p->history[(p->history_at+64-left)%64],r=p->history[(p->history_at+64-right)%64];
   mixed[0]+=mix_sample(l,pb->mix.vL);mixed[1]+=mix_sample(r,pb->mix.vR);p->history_at=(p->history_at+1)%64;
#if defined(MELEE_WEB_AUDIO_FX)
   if(a->effects){
    if(pb->mixerCtrl&1){melee_web_audio_fx_send(a->effects,0,0,a->block_pos,mix_sample(l,pb->mix.vAuxAL));melee_web_audio_fx_send(a->effects,0,1,a->block_pos,mix_sample(r,pb->mix.vAuxAR));}
    if(pb->mixerCtrl&2){melee_web_audio_fx_send(a->effects,1,0,a->block_pos,mix_sample(l,pb->mix.vAuxBL));melee_web_audio_fx_send(a->effects,1,1,a->block_pos,mix_sample(r,pb->mix.vAuxBR));}
   }
#endif
   if(pb->mixerCtrl&8){pb->mix.vL+=pb->mix.vDeltaL;pb->mix.vR+=pb->mix.vDeltaR;if(pb->mixerCtrl&1){pb->mix.vAuxAL+=pb->mix.vDeltaAuxAL;pb->mix.vAuxAR+=pb->mix.vDeltaAuxAR;}if(pb->mixerCtrl&2){pb->mix.vAuxBL+=pb->mix.vDeltaAuxBL;pb->mix.vAuxBR+=pb->mix.vDeltaAuxBR;}}

   int volume=(int)pb->ve.currentVolume+pb->ve.currentDelta;pb->ve.currentVolume=(u16)volume;
   uint32_t count=p->loop?c->loop_frames:c->frames;uint32_t start=p->loop?c->loop_nibble:c->current_nibble;size_t next=(size_t)p->position;if(next>=count)next=count-1;
   uint32_t address=p->binding->base+nibble_from_index(index_from_nibble(start)+next);split(address,&pb->addr.currentAddressHi,&pb->addr.currentAddressLo);pb->src.currentAddressFrac=p->resample.fraction;memcpy(pb->src.last_samples,p->resample.history,sizeof(p->resample.history));voice->sync=0;
   if(pb->itd.flag&&(a->block_pos+1)%32==0){pb->itd.shiftL=melee_web_audio_itd_step(pb->itd.shiftL,pb->itd.targetShiftL);pb->itd.shiftR=melee_web_audio_itd_step(pb->itd.shiftR,pb->itd.targetShiftR);}
  }
  for(unsigned side=0;side<2;side++){
#if defined(MELEE_WEB_AUDIO_FX)
   if(a->effects)mixed[side]+=melee_web_audio_fx_output(a->effects,side,a->block_pos);
#endif
   int32_t value=mixed[side];output[frame*2+side]=(value<-32768?-32768:value>32767?32767:value)/32768.0f;
  }
  a->block_pos=(a->block_pos+1)%160;
 }
 if(e&&n)*e=0;return 1;
}
int melee_web_audio_active_samples(MeleeWebAudio* a,uint32_t* ids,uint32_t capacity){
 if(!live(a)||!ids)return -1;uint32_t count=0;for(unsigned i=0;i<64;i++)if(a->playback[i].playing){if(count>=capacity)return -1;ids[count++]=a->playback[i].binding->id;}return count;
}
int melee_web_audio_end(MeleeWebAudio* a,char* e,size_t n){
 if(!live(a))return fail(e,n,"Audio scope is not active");
#if defined(MELEE_WEB_AUDIO_STREAM)
 if(melee_web_audio_stream_owned(a))return fail(e,n,"Release owned HPS stream before enclosing audio scope");
#endif
 HSD_AudioSFXKeyOffAll();
 for(unsigned priority=1;priority<32;priority++)while(__AXGetStackHead(priority))AXFreeVoice(__AXGetStackHead(priority));

#if defined(MELEE_WEB_AUDIO_FX)
 melee_web_audio_fx_destroy(a->effects);
#endif
 melee_web_audio_driver_end();melee_web_audio_synth_end();__AXAllocQuit();active=NULL;free_owner(a);if(e&&n)*e=0;return 1;
}
