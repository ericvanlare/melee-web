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
#define STREAM_MAX_FILES 16u
extern void melee_web_audio_stream_source_bind(u32,u32,void**);
extern int melee_web_audio_stream_source_loading(void);
extern void melee_web_audio_stream_source_stop(void);
extern void melee_web_audio_stream_lb_begin(void);
extern void melee_web_audio_stream_lb_end(void);
extern int melee_web_audio_is_active(MeleeWebAudio*);
typedef struct Request {
 struct Request* next;int id,type,priority,cancelled;uintptr_t args,dest,src;size_t size;
 HSD_DevComCallback callback;const MeleeWebAudioStreamBlock* block;unsigned slot,file;
} Request;
typedef struct StreamFile {
 uint64_t handle;
 const MeleeWebAudioStreamInput* input;
} StreamFile;
struct MeleeWebAudioStream {
 MeleeWebAudio* audio;const MeleeWebAudioStreamInput* input;uint32_t input_count;MeleeWebIo* io;
 StreamFile files[STREAM_MAX_FILES];uint64_t aux,relay;unsigned selected_file;void* headers[3];const MeleeWebAudioStreamBlock* slots[3];unsigned slot_file[3];
 MeleeWebAudioChannel slot_channels[3][2];int16_t* slot_pcm[3][2];
 Request* requests;uint32_t next_id,completed,revisited;uint8_t* visited;int pumping;
};
static MeleeWebAudioStream* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static void stop(const char* why){fprintf(stderr,"Original HPS transport: %s\n",why);abort();}
static void split(u32 v,u16* hi,u16* lo){*hi=v>>16;*lo=v;}
static const StreamFile* file_at(unsigned file){
 if(!file||file>active->input_count)return NULL;return &active->files[file-1];
}
static const StreamFile* file_for_path(const char* path){
 if(!path)return NULL;
 for(uint32_t i=0;i<active->input_count;i++)if(!strcmp(path,active->files[i].input->path))return &active->files[i];
 return NULL;
}
static const MeleeWebAudioStreamBlock* block_at(const StreamFile* file,uintptr_t offset){
 for(uint32_t i=0;i<file->input->count;i++)if(file->input->blocks[i].offset==offset)return &file->input->blocks[i];return NULL;
}
static void header_native(void* out,const StreamFile* file){
 struct Channel {AXPBADDR addr;AXPBADPCM adpcm;};
 struct Header {uint8_t magic[8];u32 rate,channels;struct Channel channel[2];} h={0};
 _Static_assert(sizeof(h)==128&&sizeof(struct Channel)==56,"Original HPS header ABI");
 memcpy(h.magic,out,8);h.rate=file->input->rate;h.channels=file->input->channels;
 for(unsigned i=0;i<h.channels;i++){
  const MeleeWebAudioChannel* c=&file->input->header[i];struct Channel* d=&h.channel[i];
  d->addr.loopFlag=c->looping;split(c->current_nibble,&d->addr.currentAddressHi,&d->addr.currentAddressLo);split(c->loop_nibble,&d->addr.loopAddressHi,&d->addr.loopAddressLo);split(c->end_nibble,&d->addr.endAddressHi,&d->addr.endAddressLo);
  memcpy(d->adpcm.a,c->coefficients,32);d->adpcm.pred_scale=c->predictor_scale;d->adpcm.yn1=c->history1;d->adpcm.yn2=c->history2;
 }
 memcpy(out,&h,sizeof(h));
}
static void block_native(void* out,const MeleeWebAudioStreamBlock* b,const StreamFile* file){
 struct History {AXPBADPCMLOOP dsp;u16 padding;};
 struct Header {u32 size,end,next;struct History history[2];u32 padding;} h={0};
 _Static_assert(sizeof(h)==32,"Original HPS block header ABI");
 h.size=b->size;h.end=b->end;h.next=b->next;
 for(unsigned i=0;i<file->input->channels;i++){h.history[i].dsp.loop_pred_scale=b->channel[i].predictor_scale;h.history[i].dsp.loop_yn1=b->channel[i].history1;h.history[i].dsp.loop_yn2=b->channel[i].history2;}
 memcpy(out,&h,sizeof(h));
}
static size_t sample_index(uint32_t address){return (size_t)(address/16)*14+address%16-2;}
static int decode_slot(unsigned slot,const MeleeWebAudioStreamBlock* block,const StreamFile* file,char* e,size_t n){
 for(unsigned side=0;side<file->input->channels;side++){
  const MeleeWebAudioChannel* source=&block->channel[side];
  const size_t frames=sample_index(source->end_nibble)-sample_index(source->current_nibble)+1;
  int16_t* pcm=malloc(frames*sizeof(*pcm));
  if(!pcm)return fail(e,n,"HPS slot PCM allocation failed");
  uint16_t predictor_scale=source->predictor_scale;
  int16_t history1=source->history1,history2=source->history2;
  size_t output=0;
  for(uint32_t at=source->current_nibble;at<=source->end_nibble;){
   if(at%16==0){predictor_scale=block->payload[side][at/2];if(predictor_scale&0x80){free(pcm);return fail(e,n,"Invalid HPS DSP frame predictor");}at+=2;}
   if(at>source->end_nibble)break;
   const unsigned raw=(at&1)?block->payload[side][at/2]&15:block->payload[side][at/2]>>4;
   const int nibble=raw<8?(int)raw:(int)raw-16;
   int64_t rounded=(int64_t)source->coefficients[(predictor_scale>>4)*2]*history1+
                   (int64_t)source->coefficients[(predictor_scale>>4)*2+1]*history2+
                   (int64_t)nibble*((int64_t)1<<(predictor_scale&15))*2048;
   rounded=rounded*32+0x8000;
   if(rounded<INT32_MIN)rounded=INT32_MIN;else if(rounded>INT32_MAX)rounded=INT32_MAX;
   const int16_t value=(int16_t)(rounded>=0?rounded/65536:-((-rounded+65535)/65536));
   pcm[output++]=value;history2=history1;history1=value;++at;
  }
  if(output!=frames){free(pcm);return fail(e,n,"HPS slot PCM extent mismatch");}
  free(active->slot_pcm[slot][side]);active->slot_pcm[slot][side]=pcm;
  active->slot_channels[slot][side]=*source;
  active->slot_channels[slot][side].pcm=pcm;
  active->slot_channels[slot][side].frames=frames;
 }
 return 1;
}
MeleeWebAudioStream* melee_web_audio_stream_begin_registry(MeleeWebAudio* audio,const MeleeWebAudioStreamInput* inputs,uint32_t input_count,char* e,size_t n){
 if(active||!melee_web_audio_is_active(audio)||!inputs||!input_count||input_count>STREAM_MAX_FILES){fail(e,n,"Invalid or already-owned HPS scope");return NULL;}
 size_t total_blocks=0;
 for(uint32_t i=0;i<input_count;i++){
  const MeleeWebAudioStreamInput* in=&inputs[i];
  if(!in->path||!in->bytes||in->size<128||!in->blocks||!in->count||in->count>16384||in->channels<1||in->channels>2||!in->rate||in->rate>192000){fail(e,n,"Invalid HPS registry input");return NULL;}
  for(uint32_t j=0;j<i;j++)if(!strcmp(inputs[j].path,in->path)){fail(e,n,"Duplicate HPS registry path");return NULL;}
  if(total_blocks>SIZE_MAX-in->count){fail(e,n,"HPS block tracking size overflow");return NULL;}total_blocks+=in->count;
 }
 MeleeWebAudioStream* s=calloc(1,sizeof(*s));if(!s){fail(e,n,"HPS allocation failed");return NULL;}
 s->audio=audio;s->input=inputs;s->input_count=input_count;s->visited=calloc(total_blocks,1);if(!s->visited){free(s);fail(e,n,"HPS block tracking allocation failed");return NULL;}s->io=melee_web_io_create(e,n);
 if(!s->io){free(s->visited);free(s);return NULL;}
 for(uint32_t i=0;i<input_count;i++){
  s->files[i].input=&inputs[i];
  if(!melee_web_io_add_file(s->io,inputs[i].path,inputs[i].bytes,inputs[i].size,&s->files[i].handle,e,n)){
   melee_web_io_destroy(s->io,NULL,0);free(s->visited);free(s);return NULL;
  }
 }
 if(!melee_web_io_add_buffer(s->io,MELEE_WEB_IO_AUXILIARY,0x30000,&s->aux,e,n)||!melee_web_io_add_buffer(s->io,MELEE_WEB_IO_MEMORY,128,&s->relay,e,n)){
  melee_web_io_destroy(s->io,NULL,0);free(s->visited);free(s);return NULL;
 }
 s->selected_file=input_count==1?1:0;active=s;melee_web_audio_stream_lb_begin();melee_web_audio_stream_source_bind(STREAM_BASE,0,s->headers);if(e&&n)*e=0;return s;
}
MeleeWebAudioStream* melee_web_audio_stream_begin(MeleeWebAudio* audio,const MeleeWebAudioStreamInput* input,char* e,size_t n){
 return melee_web_audio_stream_begin_registry(audio,input,1,e,n);
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
 const StreamFile* file=file_for_path(path);if(file){active->selected_file=(unsigned)(file-active->files)+1;return (s32)active->selected_file;}
 fprintf(stderr,"Original HPS path is not owned: %s\n",path?path:"(null)");
 return -1;
}
int HSD_DevComRequest(int file,uintptr_t src,uintptr_t dest,size_t size,int type,int priority,HSD_DevComCallback callback,void* args){
 if(melee_web_audio_bank_transport_file(file)||(type==0x1B&&melee_web_audio_bank_transport_active()))return melee_web_audio_bank_transport_request(file,src,dest,size,type,priority,callback,args);
 const StreamFile* selected=active?file_at((unsigned)file):NULL;
 if(!active||!selected||priority<0||priority>2||src%32||size%32||!size||!callback){
  fprintf(stderr,"DevCom file=%d src=%lu dest=%lu size=%lu type=%d priority=%d callback=%p stream=%p bank=%d\n",
      file,(unsigned long)src,(unsigned long)dest,(unsigned long)size,type,priority,(void*)callback,(void*)active,
      melee_web_audio_bank_transport_active());
  stop("unsupported file/request alignment, priority, or callback");
 }
 Request* r=calloc(1,sizeof(*r));if(!r)stop("request allocation failed");
 r->src=src;r->dest=dest;r->size=size;r->type=type;r->priority=priority;r->callback=callback;r->args=(uintptr_t)args;r->file=(unsigned)file;
 if(type==0x22){if(src||dest||size!=128)stop("unsupported relay request");}
 else if(type==0x21){
  r->block=block_at(selected,src);if(!r->block||size!=32)stop("header request is not a validated HPS block");
  for(r->slot=0;r->slot<3&&dest!=(uintptr_t)active->headers[r->slot];r->slot++){ }if(r->slot==3)stop("header destination is not source-owned");
 }else if(type==0x23){
  r->block=src>=32?block_at(selected,src-32):NULL;if(!r->block||size!=r->block->size||dest<STREAM_BASE||(dest-STREAM_BASE)%65536||(dest-STREAM_BASE)/65536>=3)stop("payload request is not a validated HPS slot transfer");
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
  Request* r=*chosen;*chosen=r->next;const StreamFile* file=file_at(r->file);if(!file)stop("HPS request lost its registered file");void* relay=NULL;size_t length=0;
  if(!r->cancelled){
   MeleeWebIoEndpoint from={MELEE_WEB_IO_FILE,file->handle,r->src},to={r->type==0x23?MELEE_WEB_IO_AUXILIARY:MELEE_WEB_IO_MEMORY,r->type==0x23?active->aux:active->relay,r->type==0x23?r->slot*65536:0};uint64_t request;uint32_t completed;
   if(!melee_web_io_submit(active->io,from,to,r->size,NULL,NULL,&request,e,n)||!melee_web_io_pump(active->io,1,&completed,e,n)||completed!=1)stop("owned byte transfer failed");
   if(r->type==0x22){if(!melee_web_io_buffer(active->io,active->relay,&relay,&length,e,n))stop("relay missing");header_native(relay,file);}
   if(r->type==0x21)block_native((void*)r->dest,r->block,file);
   if(r->type==0x23){if(!decode_slot(r->slot,r->block,file,e,n)){free(r);active->pumping=0;return 0;}active->slots[r->slot]=r->block;active->slot_file[r->slot]=r->file;active->completed++;size_t index=0;for(unsigned i=0;i<r->file-1;i++)index+=active->files[i].input->count;index+=(size_t)(r->block-file->input->blocks);if(active->visited[index])active->revisited++;active->visited[index]=1;}
  }
  if(r->callback)r->callback(r->id,(int)r->args,relay,r->cancelled);free(r);
 }
 active->pumping=0;if(active->requests)return fail(e,n,"HPS transport callback budget exceeded");if(e&&n)*e=0;return 1;
}
int melee_web_audio_stream_resolve(MeleeWebAudio* audio,uint32_t address,const MeleeWebAudioChannel** channel,uint32_t* base){
 if(!active||active->audio!=audio||address<STREAM_BASE*2u)return 0;
 uint32_t relative=address-STREAM_BASE*2u,slot=relative/0x20000;
 if(slot>=3||!active->slots[slot])return 0;
 const MeleeWebAudioStreamBlock* b=active->slots[slot];const StreamFile* file=file_at(active->slot_file[slot]);if(!file)return 0;
 for(unsigned i=0;i<file->input->channels;i++){
  uint32_t start=STREAM_BASE*2u+slot*0x20000+i*(b->size*2/file->input->channels);
  if(address>=start+2&&address<=start+b->end){*channel=&active->slot_channels[slot][i];*base=start;return 1;}
 }
 return 0;
}
int melee_web_audio_stream_progress(MeleeWebAudio* a,uint32_t* completed,uint32_t* revisited){if(!active||active->audio!=a||!completed||!revisited)return 0;*completed=active->completed;*revisited=active->revisited;return 1;}
const char* melee_web_audio_stream_path(MeleeWebAudio* a){if(!active||active->audio!=a||!active->selected_file)return NULL;return active->files[active->selected_file-1].input->path;}
int melee_web_audio_stream_owned(MeleeWebAudio* a){return active&&active->audio==a;}
int melee_web_audio_stream_end(MeleeWebAudioStream* s,char* e,size_t n){
 if(!s||active!=s||s->pumping)return fail(e,n,"HPS scope is not active or is pumping");
 if(!melee_web_audio_stream_pump_for(s->audio,e,n))return 0;
 if(melee_web_audio_is_active(s->audio)){
  if(melee_web_audio_stream_source_loading())return fail(e,n,"Original HPS source remains loading");
  melee_web_audio_stream_source_stop();
 }
 if(!melee_web_io_destroy(s->io,e,n))return 0;melee_web_audio_stream_lb_end();active=NULL;for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++)free(s->slot_pcm[slot][side]);free(s->visited);free(s);return 1;
}
