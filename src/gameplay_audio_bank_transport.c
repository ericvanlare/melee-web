#include "gameplay_audio_bank_transport.h"
#include "gameplay_platform.h"
#include "gameplay_audio_stream.h"
#include <melee/lb/lbaudio_ax.h>
#include <dolphin/ax.h>
#include <sysdolphin/baselib/synth.h>
#include <sysdolphin/baselib/axdriver.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BANK_BASE 0x10000000u
#define REQUEST_BASE 0x20000000u
#define MAX_REQUESTS 64u
extern int melee_web_audio_is_active(MeleeWebAudio*);
extern AXVPB* __AXGetStackHead(u32);
extern int melee_web_audio_lb_scope_begin(void);
extern int melee_web_audio_lb_scope_end(void);
extern void melee_web_audio_source_finish(MeleeWebAudio*);
extern void melee_web_audio_source_banks_begin(u32,u32);
extern void melee_web_audio_source_banks_end(void);
extern uint32_t melee_web_audio_source_sem_size(MeleeWebAudio*);
extern void melee_web_audio_source_sem_read(MeleeWebAudio*,void*,uint32_t);
typedef struct Transfer {
 struct Transfer* next;int id,file,type,priority,cancelled;
 uintptr_t source,dest;size_t size;HSD_DevComCallback callback;void* args;
} Transfer;
typedef struct Mapping {int file;uint32_t base,size;} Mapping;
static struct {
 MeleeWebAudio* audio;MeleeWebAudioResidency* registry;char sem_path[256];
 unsigned char* auxiliary;uint32_t size,next_id,pending;Transfer* requests;
 Mapping mappings[MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS];unsigned mapping_count;
 int pumping,configured;
} state;
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void check(int condition,const char* why){if(!condition)melee_web_platform_unavailable(why);}
static uint32_t be32(const unsigned char* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t be16(const unsigned char* p){return (uint16_t)p[0]<<8|p[1];}
static MeleeWebAudioResidencyInfo info(int file){
 MeleeWebAudioResidencyInfo result;
 check(melee_web_audio_residency_info(state.registry,file,&result,NULL,0),"SSM file is not registered");return result;
}
int melee_web_audio_bank_transport_active(void){return state.audio!=NULL;}
int melee_web_audio_bank_transport_busy(void){return state.pending||state.pumping;}
int melee_web_audio_bank_transport_begin(MeleeWebAudio* audio,MeleeWebAudioResidency* registry,const char* sem_path,char* e,size_t n){
 if(state.audio||!melee_web_audio_is_active(audio)||melee_web_audio_stream_owned(audio)||!registry||!sem_path||!*sem_path||strlen(sem_path)>=sizeof(state.sem_path))return fail(e,n,"Invalid or already active source audio bank transport");
 MeleeWebAudioResidencyInfo reserved;
 if(melee_web_audio_residency_info(registry,1,&reserved,NULL,0))return fail(e,n,"SSM entry identity conflicts with the reserved HPS file");
 for(unsigned i=1;i<32;i++)if(__AXGetStackHead(i))return fail(e,n,"Stop existing audio voices before source bank startup");
 if(!melee_web_audio_lb_scope_begin())return fail(e,n,"Original lbAudio state is already owned");
 state.audio=audio;state.registry=registry;strcpy(state.sem_path,sem_path);state.next_id=REQUEST_BASE;
 if(e&&n)*e=0;return 1;
}
void melee_web_audio_bank_transport_configure(uint32_t bytes){
 check(state.audio&&!state.configured&&bytes&&bytes<=32u*1024u*1024u,"Invalid original audio bank allocation");
 state.auxiliary=aligned_alloc(32,(bytes+31u)&~31u);check(state.auxiliary!=NULL,"Cannot allocate source audio auxiliary storage");
 memset(state.auxiliary,0,bytes);state.size=bytes;
 melee_web_audio_source_banks_begin(BANK_BASE,bytes);state.configured=1;
}
int melee_web_audio_bank_transport_path(const char* path){
 int entry=-1;MeleeWebAudioResidencyInfo found;if(state.registry)melee_web_audio_residency_find_path(state.registry,path,&entry,&found,NULL,0);return entry;
}
int melee_web_audio_bank_transport_file(int file){MeleeWebAudioResidencyInfo found;return state.registry&&melee_web_audio_residency_info(state.registry,file,&found,NULL,0);}
int melee_web_audio_bank_transport_request(int file,uintptr_t source,uintptr_t dest,size_t size,int type,int priority,HSD_DevComCallback callback,void* args){
 const MeleeWebAudioResidencyInfo f=type==0x1B?(MeleeWebAudioResidencyInfo){0}:info(file);
 check(state.configured&&state.pending<MAX_REQUESTS&&priority>=0&&priority<=2&&!(source%32)&&!(size%32)&&size,"Invalid source SSM transfer");
 const uint32_t payload=(f.header_bytes+47u)&~31u;
 if(type==0x1B){
  check(file==0&&source>=BANK_BASE&&source-BANK_BASE<=state.size&&size<=state.size-(source-BANK_BASE)&&dest>=BANK_BASE&&dest-BANK_BASE<=state.size&&size<=state.size-(dest-BANK_BASE),"SSM relocation exceeds owned auxiliary bank");
 }else if(type==0x21){
  check(dest&&((source==0&&size==32)||(source==32&&size==((f.header_bytes+15u)&~31u))),"SSM descriptor transfer disagrees with validated source header");
 }else if(type==0x23){
  check(source==payload&&size==f.payload_bytes&&dest>=BANK_BASE&&dest-BANK_BASE<=state.size&&size<=state.size-(dest-BANK_BASE),"SSM payload transfer exceeds owned auxiliary bank");
 }else check(0,"Unsupported SSM transfer type");
 Transfer* r=calloc(1,sizeof(*r));check(r!=NULL&&state.next_id<=0x3ffffff8u,"SSM transfer allocation or identity exhausted");
 r->id=state.next_id+priority;state.next_id+=4;r->file=file;r->source=source;r->dest=dest;r->size=size;r->type=type;r->priority=priority;r->callback=callback;r->args=args;
 Transfer** tail=&state.requests;while(*tail)tail=&(*tail)->next;*tail=r;state.pending++;return r->id;
}
int melee_web_audio_bank_transport_cancel(int id,u32 flags,HSD_DevComCallback callback,void* args){
 if(id<(int)REQUEST_BASE||id>=0x40000000)return 0;
 check(state.audio&&!(flags&~3u),"Invalid source SSM cancellation");
 for(Transfer* r=state.requests;r;r=r->next)if(r->id==id){if(flags&1)r->callback=callback;if(flags&2)r->args=args;r->cancelled=1;break;}
 return 1;
}
static void header_read(const Transfer* r,const MeleeWebAudioResidencyInfo* f){
 const size_t size=(f->header_bytes+47u)&~(size_t)31;
 unsigned char* native=malloc(size);check(native!=NULL,"SSM header conversion allocation failed");
 check(melee_web_audio_residency_read(state.registry,r->file,0,native,size,NULL,0),"SSM header read failed");
 for(unsigned i=0;i<4;i++){uint32_t word=be32(native+i*4);memcpy(native+i*4,&word,4);}
 size_t at=16;
 for(uint32_t i=0;i<f->sample_count;i++){
  uint32_t channels=be32(native+at),rate=be32(native+at+4);
  memcpy(native+at,&channels,4);memcpy(native+at+4,&rate,4);at+=8;
  for(uint32_t k=0;k<channels*32;k++){uint16_t half=be16(native+at+k*2);memcpy(native+at+k*2,&half,2);}at+=channels*64;
 }
 check(at==f->header_bytes+16u&&r->source<=size&&r->size<=size-r->source,"SSM typed header extent mismatch");
 memcpy((void*)r->dest,native+r->source,r->size);free(native);
}
void melee_web_audio_bank_transport_forget(int file,uint32_t base){
 for(unsigned i=0;i<state.mapping_count;)if(state.mappings[i].file==file&&state.mappings[i].base==base){memmove(state.mappings+i,state.mappings+i+1,(--state.mapping_count-i)*sizeof(Mapping));}else i++;
}
void melee_web_audio_bank_transport_pump(void){
 if(!state.audio)return;check(!state.pumping,"Reentrant source SSM transfer pump");state.pumping=1;
 unsigned budget=MAX_REQUESTS;
 while(state.requests&&budget--){
  Transfer** chosen=&state.requests;for(Transfer** p=&state.requests;*p;p=&(*p)->next)if((*p)->priority<(*chosen)->priority)chosen=p;
  Transfer* r=*chosen;*chosen=r->next;state.pending--;
  const MeleeWebAudioResidencyInfo f=r->type==0x1B?(MeleeWebAudioResidencyInfo){0}:info(r->file);
  if(!r->cancelled){
   if(r->type==0x1B){
    unsigned found=0;
    for(unsigned i=0;i<state.mapping_count;i++)if(state.mappings[i].base==r->source&&state.mappings[i].size==r->size){state.mappings[i].base=r->dest;found++;}
    check(found==1,"SSM relocation does not identify one live source group");
    memmove(state.auxiliary+(r->dest-BANK_BASE),state.auxiliary+(r->source-BANK_BASE),r->size);
   }else if(r->type==0x21)header_read(r,&f);
   else{
    check(melee_web_audio_residency_read(state.registry,r->file,r->source,state.auxiliary+(r->dest-BANK_BASE),r->size,NULL,0),"SSM payload read failed");
    melee_web_audio_bank_transport_forget(r->file,r->dest);
    check(state.mapping_count<MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS,"Too many source SSM address mappings");
    state.mappings[state.mapping_count++]=(Mapping){r->file,r->dest,r->size};
   }
  }
  if(r->callback)r->callback(r->id,(int)(uintptr_t)r->args,NULL,r->cancelled);
  free(r);
 }
 state.pumping=0; /* Remaining transfers resume on the next original wait/pump. */
}
int melee_web_audio_bank_transport_resolve(MeleeWebAudio* audio,uint32_t address,uint32_t end,uint32_t* sample,uint32_t* channel,uint32_t* base){
 if(audio!=state.audio)return 0;
 for(unsigned i=0;i<state.mapping_count;i++){
  Mapping* m=&state.mappings[i];if(address<m->base*2u||address>=m->base*2u+m->size*2u)continue;
  MeleeWebAudioResidencyInfo f=info(m->file);size_t at=16;
  for(uint32_t j=0;j<f.sample_count;j++){
   unsigned char record[8];check(melee_web_audio_residency_read(state.registry,m->file,at,record,8,NULL,0),"SSM descriptor lookup failed");uint32_t channels=be32(record);at+=8;
   for(uint32_t k=0;k<channels;k++,at+=64){
    unsigned char d[16];check(melee_web_audio_residency_read(state.registry,m->file,at,d,16,NULL,0),"SSM channel lookup failed");
    uint32_t last=be32(d+8),first=be32(d+12);
    if(address>=m->base*2u+first&&address<=m->base*2u+last&&end==m->base*2u+last){*sample=f.base_sample_id+j;*channel=k;*base=m->base*2u;return 1;}
   }
  }
 }
 return 0;
}
uint32_t melee_web_audio_bank_sem_size(const char* path){check(state.audio&&path&&!strcmp(path,state.sem_path),"SEM source path is not the validated program file");return melee_web_audio_source_sem_size(state.audio);}
void melee_web_audio_bank_sem_read(const char* path,void* dest,uint32_t size){check(size==melee_web_audio_bank_sem_size(path),"SEM source byte count mismatch");melee_web_audio_source_sem_read(state.audio,dest,size);}
int melee_web_audio_bank_transport_end(char* e,size_t n){
 if(!state.audio||state.pumping||state.requests||melee_web_audio_stream_owned(state.audio))return fail(e,n,"Source SSM transport is absent, busy or still owns a nested HPS scope");
 if(state.configured)lbAudioAx_80027DBC();
 melee_web_audio_bank_transport_pump();
 HSD_AudioSFXKeyOffAll();
 AXDriver_8038DCFC();
 if(state.configured)melee_web_audio_source_banks_end();
 check(melee_web_audio_lb_scope_end(),"Original lbAudio state ownership mismatch");
 melee_web_audio_source_finish(state.audio);
 free(state.auxiliary);memset(&state,0,sizeof(state));if(e&&n)*e=0;return 1;
}
