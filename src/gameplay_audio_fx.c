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
struct MeleeWebAudioEffects{void* reverb_heap;void* delay_heap;int32_t* input[2];int32_t* output[2];};
MeleeWebAudioEffects* melee_web_audio_fx_create(char* error,size_t size){
 MeleeWebAudioEffects* effects=calloc(1,sizeof(*effects));if(effects){effects->reverb_heap=aligned_alloc(32,53*1024);effects->delay_heap=aligned_alloc(32,71*1024);}
 if(!effects||!effects->reverb_heap||!effects->delay_heap){if(effects){free(effects->reverb_heap);free(effects->delay_heap);free(effects);}if(error&&size)snprintf(error,size,"Original AXFX heaps could not be allocated");return NULL;}
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
