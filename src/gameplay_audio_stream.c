#include "gameplay_audio_bank_transport.h"
#include "gameplay_audio_stream.h"
#include "gameplay_io.h"
#include <dolphin/ax.h>
#include <dolphin/dvd.h>
#include <sysdolphin/baselib/devcom.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Address keys reserve the upper half of the native audio namespace. They
 * identify owned auxiliary slots, never host pointers or emulated CPU memory. */
#define STREAM_BASE 0x40000000u
extern void melee_web_audio_stream_source_bind(u32,u32,void**);
extern int melee_web_audio_stream_source_loading(void);
extern void melee_web_audio_stream_source_stop(void);
extern void melee_web_audio_stream_lb_begin(void);
extern void melee_web_audio_stream_lb_end(void);
extern int melee_web_audio_is_active(MeleeWebAudio*);
typedef struct Request {
 struct Request* next;int id,type,priority,cancelled;uintptr_t args,dest,src;size_t size;
 HSD_DevComCallback callback;const MeleeWebAudioStreamBlock* block;unsigned slot;
} Request;
struct MeleeWebAudioStream {
 MeleeWebAudio* audio;const MeleeWebAudioStreamInput* input;MeleeWebIo* io;
 uint64_t file,aux,relay;void* headers[3];const MeleeWebAudioStreamBlock* slots[3];
 Request* requests;uint32_t next_id,completed,revisited;uint8_t* visited;int pumping;
};
static MeleeWebAudioStream* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static void stop(const char* why){fprintf(stderr,"Original HPS transport: %s\n",why);abort();}
static void split(u32 v,u16* hi,u16* lo){*hi=v>>16;*lo=v;}
static const MeleeWebAudioStreamBlock* block_at(uintptr_t offset){
 for(uint32_t i=0;i<active->input->count;i++)if(active->input->blocks[i].offset==offset)return &active->input->blocks[i];return NULL;
}
static void header_native(void* out){
 struct Channel {AXPBADDR addr;AXPBADPCM adpcm;};
 struct Header {uint8_t magic[8];u32 rate,channels;struct Channel channel[2];} h={0};
 _Static_assert(sizeof(h)==128&&sizeof(struct Channel)==56,"Original HPS header ABI");
 memcpy(h.magic,out,8);h.rate=active->input->rate;h.channels=active->input->channels;
 for(unsigned i=0;i<h.channels;i++){
  const MeleeWebAudioChannel* c=&active->input->header[i];struct Channel* d=&h.channel[i];
  d->addr.loopFlag=c->looping;split(c->current_nibble,&d->addr.currentAddressHi,&d->addr.currentAddressLo);split(c->loop_nibble,&d->addr.loopAddressHi,&d->addr.loopAddressLo);split(c->end_nibble,&d->addr.endAddressHi,&d->addr.endAddressLo);
  memcpy(d->adpcm.a,c->coefficients,32);d->adpcm.pred_scale=c->predictor_scale;d->adpcm.yn1=c->history1;d->adpcm.yn2=c->history2;
 }
 memcpy(out,&h,sizeof(h));
}
static void block_native(void* out,const MeleeWebAudioStreamBlock* b){
 struct History {AXPBADPCMLOOP dsp;u16 padding;};
 struct Header {u32 size,end,next;struct History history[2];u32 padding;} h={0};
 _Static_assert(sizeof(h)==32,"Original HPS block header ABI");
 h.size=b->size;h.end=b->end;h.next=b->next;
 for(unsigned i=0;i<active->input->channels;i++){h.history[i].dsp.loop_pred_scale=b->channel[i].predictor_scale;h.history[i].dsp.loop_yn1=b->channel[i].history1;h.history[i].dsp.loop_yn2=b->channel[i].history2;}
 memcpy(out,&h,sizeof(h));
}
MeleeWebAudioStream* melee_web_audio_stream_begin(MeleeWebAudio* audio,const MeleeWebAudioStreamInput* in,char* e,size_t n){
 if(active||!melee_web_audio_is_active(audio)||!in||!in->path||!in->bytes||in->size<128||!in->blocks||!in->count||in->count>16384||in->channels<1||in->channels>2||!in->rate||in->rate>192000){fail(e,n,"Invalid or already-owned HPS scope");return NULL;}
 MeleeWebAudioStream* s=calloc(1,sizeof(*s));if(!s){fail(e,n,"HPS allocation failed");return NULL;}
 s->audio=audio;s->input=in;s->visited=calloc(in->count,1);if(!s->visited){free(s);fail(e,n,"HPS block tracking allocation failed");return NULL;}s->io=melee_web_io_create(e,n);
 if(!s->io||!melee_web_io_add_file(s->io,in->path,in->bytes,in->size,&s->file,e,n)||!melee_web_io_add_buffer(s->io,MELEE_WEB_IO_AUXILIARY,0x30000,&s->aux,e,n)||!melee_web_io_add_buffer(s->io,MELEE_WEB_IO_MEMORY,128,&s->relay,e,n)){if(s->io)melee_web_io_destroy(s->io,NULL,0);free(s->visited);free(s);return NULL;}
 active=s;melee_web_audio_stream_lb_begin();melee_web_audio_stream_source_bind(STREAM_BASE,0,s->headers);if(e&&n)*e=0;return s;
}
/* Only the owned local audio registry implements disc requests here. Its
 * pending queue determines busy/end; no hardware-cover state is fabricated. */
s32 DVDGetDriveStatus(void){
 if(!active&&!melee_web_audio_bank_transport_active())stop("Drive status requested without owned disc files");
 return (active&&(active->requests||active->pumping))||melee_web_audio_bank_transport_busy()?DVD_STATE_BUSY:DVD_STATE_END;
}
s32 DVDConvertPathToEntrynum(const char* path){
 int entry=melee_web_audio_bank_transport_path(path);if(entry>=0)return entry;
 if(!active)stop("DVD path requested without an owned file registry");
 if(!melee_web_audio_stream_pump_for(active->audio,NULL,0))stop("pending HPS transfer failed before file selection");
 uint64_t file;size_t length;
 return path&&melee_web_io_find_file(active->io,path,&file,&length,NULL,0)?1:-1;
}
int HSD_DevComRequest(int file,uintptr_t src,uintptr_t dest,size_t size,int type,int priority,HSD_DevComCallback callback,void* args){
 if(melee_web_audio_bank_transport_file(file)||(type==0x1B&&melee_web_audio_bank_transport_active()))return melee_web_audio_bank_transport_request(file,src,dest,size,type,priority,callback,args);
 if(!active||file!=1||priority<0||priority>2||src%32||size%32||!size||!callback)stop("unsupported file/request alignment, priority, or callback");
 Request* r=calloc(1,sizeof(*r));if(!r)stop("request allocation failed");
 r->src=src;r->dest=dest;r->size=size;r->type=type;r->priority=priority;r->callback=callback;r->args=(uintptr_t)args;
 if(type==0x22){if(src||dest||size!=128)stop("unsupported relay request");}
 else if(type==0x21){
  r->block=block_at(src);if(!r->block||size!=32)stop("header request is not a validated HPS block");
  for(r->slot=0;r->slot<3&&dest!=(uintptr_t)active->headers[r->slot];r->slot++){ }if(r->slot==3)stop("header destination is not source-owned");
 }else if(type==0x23){
  r->block=src>=32?block_at(src-32):NULL;if(!r->block||size!=r->block->size||dest<STREAM_BASE||(dest-STREAM_BASE)%65536||(dest-STREAM_BASE)/65536>=3)stop("payload request is not a validated HPS slot transfer");
  r->slot=(dest-STREAM_BASE)/65536;
 }else stop("unsupported DevCom request type");
 if(active->next_id>0x1ffffff8)stop("request ID exhausted");r->id=active->next_id+priority;active->next_id+=4;
 Request** tail=&active->requests;while(*tail)tail=&(*tail)->next;*tail=r;return r->id;
}
int HSD_DevComCancelEx(int id,u32 flags,HSD_DevComCallback callback,void* args){
 if(melee_web_audio_bank_transport_cancel(id,flags,callback,args))return 0;
 if(!active||flags&~3u)stop("unsupported DevCom cancellation");
 for(Request* r=active->requests;r;r=r->next)if(r->id==id){if(flags&1)r->callback=callback;if(flags&2)r->args=(uintptr_t)args;r->cancelled=1;break;}return 0;
}
int melee_web_audio_stream_pump_for(MeleeWebAudio* audio,char* e,size_t n){
 if(!active||active->audio!=audio)return 1;if(active->pumping)return fail(e,n,"Reentrant HPS transport pump");active->pumping=1;
 unsigned budget=64;
 while(active->requests&&budget--){
  Request** chosen=&active->requests;for(Request** at=&active->requests;*at;at=&(*at)->next)if((*at)->priority<(*chosen)->priority)chosen=at;
  Request* r=*chosen;*chosen=r->next;void* relay=NULL;size_t length=0;
  if(!r->cancelled){
   MeleeWebIoEndpoint from={MELEE_WEB_IO_FILE,active->file,r->src},to={r->type==0x23?MELEE_WEB_IO_AUXILIARY:MELEE_WEB_IO_MEMORY,r->type==0x23?active->aux:active->relay,r->type==0x23?r->slot*65536:0};uint64_t request;uint32_t completed;
   if(!melee_web_io_submit(active->io,from,to,r->size,NULL,NULL,&request,e,n)||!melee_web_io_pump(active->io,1,&completed,e,n)||completed!=1)stop("owned byte transfer failed");
   if(r->type==0x22){if(!melee_web_io_buffer(active->io,active->relay,&relay,&length,e,n))stop("relay missing");header_native(relay);}
   if(r->type==0x21)block_native((void*)r->dest,r->block);
   if(r->type==0x23){active->slots[r->slot]=r->block;active->completed++;size_t index=r->block-active->input->blocks;if(active->visited[index])active->revisited++;active->visited[index]=1;}
  }
  if(r->callback)r->callback(r->id,(int)r->args,relay,r->cancelled);free(r);
 }
 active->pumping=0;if(active->requests)return fail(e,n,"HPS transport callback budget exceeded");if(e&&n)*e=0;return 1;
}
int melee_web_audio_stream_resolve(MeleeWebAudio* audio,uint32_t address,const MeleeWebAudioChannel** channel,uint32_t* base){
 if(!active||active->audio!=audio||address<STREAM_BASE*2u)return 0;
 uint32_t relative=address-STREAM_BASE*2u,slot=relative/0x20000;
 if(slot>=3||!active->slots[slot])return 0;
 const MeleeWebAudioStreamBlock* b=active->slots[slot];
 for(unsigned i=0;i<active->input->channels;i++){
  uint32_t start=STREAM_BASE*2u+slot*0x20000+i*(b->size*2/active->input->channels);
  if(address>=start+2&&address<=start+b->end){*channel=&b->channel[i];*base=start;return 1;}
 }
 return 0;
}
int melee_web_audio_stream_progress(MeleeWebAudio* a,uint32_t* completed,uint32_t* revisited){if(!active||active->audio!=a||!completed||!revisited)return 0;*completed=active->completed;*revisited=active->revisited;return 1;}
int melee_web_audio_stream_owned(MeleeWebAudio* a){return active&&active->audio==a;}
int melee_web_audio_stream_end(MeleeWebAudioStream* s,char* e,size_t n){
 if(!s||active!=s||s->pumping)return fail(e,n,"HPS scope is not active or is pumping");
 if(!melee_web_audio_stream_pump_for(s->audio,e,n))return 0;
 if(melee_web_audio_is_active(s->audio)){
  if(melee_web_audio_stream_source_loading())return fail(e,n,"Original HPS source remains loading");
  melee_web_audio_stream_source_stop();
 }
 if(!melee_web_io_destroy(s->io,e,n))return 0;melee_web_audio_stream_lb_end();active=NULL;free(s->visited);free(s);return 1;
}
