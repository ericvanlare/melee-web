#include "gameplay_audio_fx.h"
#include <dolphin/axfx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void melee_web_audio_effects_setup(void*,void*);
extern void melee_web_audio_effects_release(void);
extern void melee_web_audio_aux_begin(void);
extern void melee_web_audio_aux_end(void);
extern void __AXProcessAux(void);
extern void __AXGetAuxAInput(u32*);
extern void __AXGetAuxBInput(u32*);
extern void __AXGetAuxAOutput(u32*);
extern void __AXGetAuxBOutput(u32*);
enum { REVERB_HEAP_BYTES=53*1024, DELAY_HEAP_BYTES=71*1024 };
struct MeleeWebAudioEffects{void* reverb_heap;void* delay_heap;int32_t* input[2];int32_t* output[2];uint64_t generation;};
static uint64_t next_effect_generation;
MeleeWebAudioEffects* melee_web_audio_fx_create(char* error,size_t size){
 if(next_effect_generation==UINT64_MAX){if(error&&size)snprintf(error,size,"Audio effect lifetime identity exhausted");return NULL;}
 MeleeWebAudioEffects* effects=calloc(1,sizeof(*effects));if(effects){effects->reverb_heap=aligned_alloc(32,REVERB_HEAP_BYTES);effects->delay_heap=aligned_alloc(32,DELAY_HEAP_BYTES);}
 if(!effects||!effects->reverb_heap||!effects->delay_heap){if(effects){free(effects->reverb_heap);free(effects->delay_heap);free(effects);}if(error&&size)snprintf(error,size,"Original AXFX heaps could not be allocated");return NULL;}
 effects->generation=++next_effect_generation;
 melee_web_audio_aux_begin();melee_web_audio_effects_setup(effects->reverb_heap,effects->delay_heap);return effects;
}
void melee_web_audio_fx_destroy(MeleeWebAudioEffects* effects){if(effects){melee_web_audio_effects_release();melee_web_audio_aux_end();free(effects->reverb_heap);free(effects->delay_heap);free(effects);}}
void melee_web_audio_fx_prepare(MeleeWebAudioEffects* effects){
 u32 address;__AXProcessAux();__AXGetAuxAInput(&address);effects->input[0]=(int32_t*)(uintptr_t)address;__AXGetAuxBInput(&address);effects->input[1]=(int32_t*)(uintptr_t)address;__AXGetAuxAOutput(&address);effects->output[0]=(int32_t*)(uintptr_t)address;__AXGetAuxBOutput(&address);effects->output[1]=(int32_t*)(uintptr_t)address;
 for(unsigned bus=0;bus<2;bus++)if(effects->input[bus])memset(effects->input[bus],0,480*sizeof(int32_t));
}
void melee_web_audio_fx_send(MeleeWebAudioEffects* effects,unsigned bus,unsigned channel,unsigned sample,int32_t value){if(effects->input[bus])effects->input[bus][channel*160+sample]+=value;}
int32_t melee_web_audio_fx_output(MeleeWebAudioEffects* effects,unsigned channel,unsigned sample){return effects->output[0][channel*160+sample]+effects->output[1][channel*160+sample];}
/* Original setup's other effect cases remain linkable but explicitly unsupported.
 * This provider implements the STD reverb + delay selected by lbAudioAx init. */
static _Noreturn void unsupported(const char* name){fprintf(stderr,"Native audio effect unavailable: %s\n",name);abort();}
int AXFXReverbHiInit(struct AXFX_REVERBHI* p){(void)p;unsupported(__func__);}
int AXFXReverbHiShutdown(struct AXFX_REVERBHI* p){(void)p;unsupported(__func__);}
void AXFXReverbHiCallback(struct AXFX_BUFFERUPDATE* p,struct AXFX_REVERBHI* r){(void)p;(void)r;unsupported(__func__);}
int AXFXChorusInit(struct AXFX_CHORUS* p){(void)p;unsupported(__func__);}
int AXFXChorusShutdown(struct AXFX_CHORUS* p){(void)p;unsupported(__func__);}
void AXFXChorusCallback(struct AXFX_BUFFERUPDATE* p,struct AXFX_CHORUS* r){(void)p;(void)r;unsupported(__func__);}

struct MeleeWebAudioEffectsSnapshot {
 const MeleeWebAudioEffects* owner;
 MeleeWebAudioEffects state;
 unsigned char reverb[REVERB_HEAP_BYTES],delay[DELAY_HEAP_BYTES];
};
MeleeWebAudioEffectsSnapshot* melee_web_audio_fx_snapshot_capture(MeleeWebAudioEffects* effects){
 if(!effects||!effects->generation||!effects->reverb_heap||!effects->delay_heap)return NULL;
 MeleeWebAudioEffectsSnapshot* snapshot=malloc(sizeof(*snapshot));
 if(!snapshot)return NULL;
 snapshot->owner=effects;snapshot->state=*effects;
 memcpy(snapshot->reverb,effects->reverb_heap,REVERB_HEAP_BYTES);
 memcpy(snapshot->delay,effects->delay_heap,DELAY_HEAP_BYTES);
 return snapshot;
}
int melee_web_audio_fx_snapshot_admit(MeleeWebAudioEffects* effects,const MeleeWebAudioEffectsSnapshot* snapshot){
 return effects&&snapshot&&snapshot->owner==effects&&
  effects->generation==snapshot->state.generation&&effects->generation&&
  effects->reverb_heap==snapshot->state.reverb_heap&&effects->delay_heap==snapshot->state.delay_heap;
}
int melee_web_audio_fx_snapshot_restore(MeleeWebAudioEffects* effects,const MeleeWebAudioEffectsSnapshot* snapshot){
 if(!melee_web_audio_fx_snapshot_admit(effects,snapshot))return 0;
 memcpy(effects->reverb_heap,snapshot->reverb,REVERB_HEAP_BYTES);
 memcpy(effects->delay_heap,snapshot->delay,DELAY_HEAP_BYTES);
 *effects=snapshot->state;return 1;
}
void melee_web_audio_fx_snapshot_destroy(MeleeWebAudioEffectsSnapshot* snapshot){free(snapshot);}


int melee_web_audio_fx_fill_owned_ranges(const MeleeWebAudioEffects* effects,
 MeleeWebAudioOwnedRange* ranges,size_t capacity,size_t* written){
 if(!effects||!ranges||!written||capacity<3||!effects->generation||
    !effects->reverb_heap||!effects->delay_heap)return 0;
 MeleeWebAudioOwnedRange rows[3]={
  {MELEE_WEB_AUDIO_RANGE_EFFECTS_OWNER,effects,sizeof(*effects),effects,effects->generation},
  {MELEE_WEB_AUDIO_RANGE_EFFECTS_REVERB_HEAP,effects->reverb_heap,REVERB_HEAP_BYTES,effects,effects->generation},
  {MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP,effects->delay_heap,DELAY_HEAP_BYTES,effects,effects->generation}
 };
 memcpy(ranges,rows,sizeof(rows));*written=3;return 1;
}
