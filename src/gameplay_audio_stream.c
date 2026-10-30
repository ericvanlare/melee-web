#include "gameplay_audio_bank_transport.h"
#include "gameplay_cpu_interval.h"
#include "gameplay_audio_stream.h"
#include "gameplay_io.h"
#include "gameplay_source_files.h"
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
 MeleeWebAudioChannel slot_channels[3][2];int16_t* slot_pcm[3][2];size_t pcm_capacity[3][2];int16_t* decode_scratch[3][2];uint64_t generation;int prepared;
 const struct MeleeWebAudioStreamSnapshot* interval_snapshot;uint64_t interval_token;
 Request* requests;uint32_t next_id,completed,revisited;uint8_t* visited;int pumping;
};
typedef struct StreamChannelState {
 const int16_t* pcm;uint32_t frames;const int16_t* loop_pcm;uint32_t loop_frames;
 uint32_t current_nibble,end_nibble,loop_nibble;int16_t coefficients[16];
 uint16_t predictor_scale,loop_predictor_scale;int16_t history1,history2,loop_history1,loop_history2;uint8_t looping;
} StreamChannelState;
struct MeleeWebAudioStreamSnapshot {
 MeleeWebAudio* audio;uint64_t audio_generation,stream_generation;
 const MeleeWebAudioStream* stream_identity;const MeleeWebAudioStreamInput* registry;uint32_t input_count;
 MeleeWebIo* io_identity;uint64_t file_handles[STREAM_MAX_FILES];const MeleeWebAudioStreamInput* file_inputs[STREAM_MAX_FILES];
 uint64_t aux_handle,relay_handle;void* aux_identity;void* relay_identity;
 unsigned selected_file;void* headers[3];const MeleeWebAudioStreamBlock* slots[3];unsigned slot_file[3];
 StreamChannelState channels[3][2];int16_t* pcm_identity[3][2];size_t pcm_capacity[3][2];int16_t* pcm_bytes[3][2];
 uint32_t next_id,completed,revisited;uint8_t* visited_identity;size_t visited_count;uint8_t* visited_bytes;
 uint8_t* aux_bytes;size_t aux_size;uint8_t* relay_bytes;size_t relay_size;
};
static MeleeWebAudioStream* active;
static uint64_t next_stream_generation;
static int snapshot_matches_live(MeleeWebAudio*,const MeleeWebAudioStreamSnapshot*,MeleeWebAudioStream*);
typedef struct StreamProbeState {
 MeleeWebAudio* owner;int armed,overflow;uint32_t count;
 MeleeWebAudioStreamProbeEvent events[MELEE_WEB_AUDIO_STREAM_PROBE_EVENT_CAP];
} StreamProbeState;
static StreamProbeState stream_probe;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int hps_interval_guard(const char* site){
 int first=melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_NONE;
 if(melee_web_cpu_interval_attempt(MELEE_WEB_CPU_EFFECT_HPS))return 1;
 if(first)fprintf(stderr,"HPS_INTERVAL_FIRST_REFUSAL site=%s\n",site);
 return 0;
}
static void stop(const char* why){fprintf(stderr,"Original HPS transport: %s\n",why);abort();}
static void split(u32 v,u16* hi,u16* lo){*hi=v>>16;*lo=v;}
static const StreamFile* file_at(unsigned file){
 if(!file||file>active->input_count)return NULL;return &active->files[file-1];
}
static void probe_record(unsigned kind,const Request* r,uint32_t previous_file,uint32_t previous_block,uint32_t current_file,uint32_t current_block){
 if(!stream_probe.armed||!active||stream_probe.owner!=active->audio)return;
 if(stream_probe.count>=MELEE_WEB_AUDIO_STREAM_PROBE_EVENT_CAP){stream_probe.overflow=1;return;}
 MeleeWebAudioStreamProbeEvent* e=&stream_probe.events[stream_probe.count];memset(e,0,sizeof(*e));
 e->sequence=++stream_probe.count;e->kind=kind;e->previous_file=previous_file;e->previous_block_offset=previous_block;e->current_file=current_file;e->current_block_offset=current_block;
 if(r){e->id=(uint32_t)r->id;e->type=(uint32_t)r->type;e->priority=(uint32_t)r->priority;e->file=r->file;e->slot=(r->type==0x21||r->type==0x23)?r->slot:UINT32_MAX;e->block_offset=r->block?r->block->offset:UINT32_MAX;
  e->size=(uint64_t)r->size;e->source=r->src;e->destination=r->dest;e->callback=(uintptr_t)r->callback;e->args=r->args;}
 else {e->slot=UINT32_MAX;e->block_offset=UINT32_MAX;}
}
static const StreamFile* file_for_path(const char* path){
 if(!path)return NULL;
 for(uint32_t i=0;i<active->input_count;i++)if(!strcmp(path,active->files[i].input->path))return &active->files[i];
 return NULL;
}
static const MeleeWebAudioStreamBlock* block_at(const StreamFile* file,uintptr_t offset){
 for(uint32_t i=0;i<file->input->count;i++)if(file->input->blocks[i].offset==offset)return &file->input->blocks[i];return NULL;
}
static int block_pointer_in_file(const StreamFile* file,const MeleeWebAudioStreamBlock* block){
 if(!file||!block)return 0;
 for(uint32_t i=0;i<file->input->count;i++)if(&file->input->blocks[i]==block)return 1;
 return 0;
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
static int channel_frame_count(const MeleeWebAudioChannel* source,size_t* out){
 if(!source||!out||source->current_nibble>source->end_nibble||
    source->current_nibble%16<2||source->end_nibble%16<2)return 0;
 size_t first=sample_index(source->current_nibble),last=sample_index(source->end_nibble);
 if(last<first||last-first==SIZE_MAX)return 0;*out=last-first+1;
 return *out<=SIZE_MAX/sizeof(int16_t);
}
static int decode_pcm(const MeleeWebAudioStreamBlock* block,unsigned side,
                      const MeleeWebAudioChannel* source,int16_t* pcm,
                      size_t capacity,size_t* out_frames,char* e,size_t n){
 size_t frames;if(!channel_frame_count(source,&frames)||frames>capacity||
    !block->payload[side])return fail(e,n,"Invalid HPS DSP frame extent");
 uint16_t predictor_scale=source->predictor_scale;
 int16_t history1=source->history1,history2=source->history2;
 size_t output=0;
 for(uint32_t at=source->current_nibble;at<=source->end_nibble;){
  if(at%16==0){predictor_scale=block->payload[side][at/2];if(predictor_scale&0x80)return fail(e,n,"Invalid HPS DSP frame predictor");at+=2;}
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
 if(output!=frames)return fail(e,n,"HPS slot PCM extent mismatch");*out_frames=frames;return 1;
}
static void channel_state_assign(MeleeWebAudioChannel* to,const StreamChannelState* from){
 to->pcm=from->pcm;to->frames=from->frames;to->loop_pcm=from->loop_pcm;to->loop_frames=from->loop_frames;
 to->current_nibble=from->current_nibble;to->end_nibble=from->end_nibble;to->loop_nibble=from->loop_nibble;
 memcpy(to->coefficients,from->coefficients,sizeof(to->coefficients));
 to->predictor_scale=from->predictor_scale;to->loop_predictor_scale=from->loop_predictor_scale;
 to->history1=from->history1;to->history2=from->history2;to->loop_history1=from->loop_history1;to->loop_history2=from->loop_history2;to->looping=from->looping;
}
static void channel_state_capture(StreamChannelState* to,const MeleeWebAudioChannel* from){
 to->pcm=from->pcm;to->frames=from->frames;to->loop_pcm=from->loop_pcm;to->loop_frames=from->loop_frames;
 to->current_nibble=from->current_nibble;to->end_nibble=from->end_nibble;to->loop_nibble=from->loop_nibble;
 memcpy(to->coefficients,from->coefficients,sizeof(to->coefficients));
 to->predictor_scale=from->predictor_scale;to->loop_predictor_scale=from->loop_predictor_scale;
 to->history1=from->history1;to->history2=from->history2;to->loop_history1=from->loop_history1;to->loop_history2=from->loop_history2;to->looping=from->looping;
}
static int decode_slot(unsigned slot,const MeleeWebAudioStreamBlock* block,const StreamFile* file,char* e,size_t n){
 size_t frames[2]={0,0};
 for(unsigned side=0;side<file->input->channels;side++){
  const MeleeWebAudioChannel* source=&block->channel[side];
  if(active->prepared){
   if(!decode_pcm(block,side,source,active->decode_scratch[slot][side],active->pcm_capacity[slot][side],&frames[side],e,n))return 0;
  }else{
   if(!channel_frame_count(source,&frames[side]))return fail(e,n,"Invalid HPS DSP frame extent");
   int16_t* pcm=malloc(frames[side]*sizeof(*pcm));if(!pcm)return fail(e,n,"HPS slot PCM allocation failed");
   if(!decode_pcm(block,side,source,pcm,frames[side],&frames[side],e,n)){free(pcm);return 0;}
   free(active->slot_pcm[slot][side]);active->slot_pcm[slot][side]=pcm;
   active->slot_channels[slot][side]=*source;active->slot_channels[slot][side].pcm=pcm;active->slot_channels[slot][side].frames=(uint32_t)frames[side];
  }
 }
 if(active->prepared){
  for(unsigned side=0;side<file->input->channels;side++){
   const size_t capacity=active->pcm_capacity[slot][side];
   memset(active->slot_pcm[slot][side],0,capacity*sizeof(int16_t));
   memcpy(active->slot_pcm[slot][side],active->decode_scratch[slot][side],frames[side]*sizeof(int16_t));
   active->slot_channels[slot][side]=block->channel[side];
   active->slot_channels[slot][side].pcm=active->slot_pcm[slot][side];
   active->slot_channels[slot][side].frames=(uint32_t)frames[side];
  }
 }
 return 1;
}
static uint64_t probe_pcm_hash(const int16_t* pcm,size_t frames){
 const uint8_t* bytes=(const uint8_t*)pcm;uint64_t hash=14695981039346656037ULL;
 for(size_t i=0;i<frames*sizeof(*pcm);i++){hash^=bytes[i];hash*=1099511628211ULL;}return hash;
}
static int probe_slot_snapshot(MeleeWebAudioStream* s,unsigned slot,MeleeWebAudioStreamProbeSlot* out){
 if(!s||!out||slot>=3)return 0;memset(out,0,sizeof(*out));out->slot=slot;
 if(!s->slots[slot])return 1;
 const StreamFile* file=file_at(s->slot_file[slot]);if(!file)return 0;
 out->valid=1;out->file=s->slot_file[slot];out->block_offset=s->slots[slot]->offset;
 for(unsigned side=0;side<file->input->channels;side++){
  const MeleeWebAudioChannel* channel=&s->slot_channels[slot][side];
  if(!s->slot_pcm[slot][side]||channel->pcm!=s->slot_pcm[slot][side]||channel->frames>SIZE_MAX/sizeof(int16_t))return 0;
  out->pcm[side]=(uintptr_t)s->slot_pcm[slot][side];out->frames[side]=(uint64_t)channel->frames;
  out->pcm_fnv1a[side]=probe_pcm_hash(s->slot_pcm[slot][side],channel->frames);
 }
 return 1;
}
int melee_web_audio_stream_probe_begin(MeleeWebAudio* audio){
 if(!active||active->audio!=audio||stream_probe.armed)return 0;
 memset(&stream_probe,0,sizeof(stream_probe));stream_probe.owner=audio;stream_probe.armed=1;return 1;
}
int melee_web_audio_stream_probe_end(MeleeWebAudio* audio){
 if(!active||active->audio!=audio||stream_probe.owner!=audio||!stream_probe.armed)return 0;
 stream_probe.armed=0;return 1;
}
int melee_web_audio_stream_probe_boundary(MeleeWebAudio* audio,MeleeWebAudioStreamProbeBoundary* out){
 if(!active||active->audio!=audio||stream_probe.owner!=audio||!out)return 0;
 memset(out,0,sizeof(*out));for(Request* r=active->requests;r;r=r->next)out->pending_requests++;
 out->pumping=(uint32_t)(active->pumping!=0);out->source_loading=(uint32_t)(melee_web_audio_stream_source_loading()!=0);return 1;
}
int melee_web_audio_stream_probe_next_refill(MeleeWebAudio* audio,MeleeWebAudioStreamProbeRequest* out,MeleeWebAudioStreamProbeSlot* before){
 if(!active||active->audio!=audio||stream_probe.owner!=audio||!out||!before)return -1;
 Request* chosen=active->requests;if(!chosen)return 0;
 for(Request* r=chosen->next;r;r=r->next)if(r->priority<chosen->priority)chosen=r;
 if((chosen->type!=0x21&&chosen->type!=0x23)||chosen->slot>=3||!chosen->block||!active->slots[chosen->slot])return 0;
 if(active->slot_file[chosen->slot]==chosen->file&&active->slots[chosen->slot]->offset==chosen->block->offset)return 0;
 memset(out,0,sizeof(*out));out->id=(uint32_t)chosen->id;out->type=(uint32_t)chosen->type;out->priority=(uint32_t)chosen->priority;out->file=chosen->file;out->slot=chosen->slot;
 out->block_offset=chosen->block->offset;out->size=(uint64_t)chosen->size;out->source=chosen->src;out->destination=chosen->dest;out->callback=(uintptr_t)chosen->callback;out->args=chosen->args;
 return probe_slot_snapshot(active,chosen->slot,before)?1:-1;
}
int melee_web_audio_stream_probe_slot(MeleeWebAudio* audio,uint32_t slot,MeleeWebAudioStreamProbeSlot* out){
 if(!active||active->audio!=audio||stream_probe.owner!=audio)return 0;return probe_slot_snapshot(active,slot,out);
}
int melee_web_audio_stream_probe_event_count(MeleeWebAudio* audio,uint32_t* count,uint32_t* overflow){
 if(!active||active->audio!=audio||stream_probe.owner!=audio||!count||!overflow)return 0;
 *count=stream_probe.count;*overflow=(uint32_t)(stream_probe.overflow!=0);return 1;
}
int melee_web_audio_stream_probe_event(MeleeWebAudio* audio,uint32_t index,MeleeWebAudioStreamProbeEvent* out){
 if(!active||active->audio!=audio||stream_probe.owner!=audio||!out||index>=stream_probe.count)return 0;
 *out=stream_probe.events[index];return 1;
}
MeleeWebAudioStream* melee_web_audio_stream_begin_registry(MeleeWebAudio* audio,const MeleeWebAudioStreamInput* inputs,uint32_t input_count,char* e,size_t n){
 if(melee_web_cpu_interval_active()){
  (void)hps_interval_guard("stream-begin");fail(e,n,"HPS scope creation is refused during a CPU interval");return NULL;
 }
 if(active||!melee_web_audio_is_active(audio)||!inputs||!input_count||input_count>STREAM_MAX_FILES){fail(e,n,"Invalid or already-owned HPS scope");return NULL;}
 if(next_stream_generation==UINT64_MAX){fail(e,n,"HPS stream lifetime identity exhausted");return NULL;}
 size_t total_blocks=0;
 for(uint32_t i=0;i<input_count;i++){
  const MeleeWebAudioStreamInput* in=&inputs[i];
  if(!in->path||!in->bytes||in->size<128||!in->blocks||!in->count||in->count>16384||in->channels<1||in->channels>2||!in->rate||in->rate>192000){fail(e,n,"Invalid HPS registry input");return NULL;}
  for(uint32_t j=0;j<i;j++)if(!strcmp(inputs[j].path,in->path)){fail(e,n,"Duplicate HPS registry path");return NULL;}
  if(total_blocks>SIZE_MAX-in->count){fail(e,n,"HPS block tracking size overflow");return NULL;}total_blocks+=in->count;
 }
 MeleeWebAudioStream* s=calloc(1,sizeof(*s));if(!s){fail(e,n,"HPS allocation failed");return NULL;}
 s->audio=audio;s->input=inputs;s->input_count=input_count;s->generation=++next_stream_generation;s->visited=calloc(total_blocks,1);if(!s->visited){free(s);fail(e,n,"HPS block tracking allocation failed");return NULL;}s->io=melee_web_io_create(e,n);
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
 if(!active&&!melee_web_audio_bank_transport_active()&&!melee_web_source_files_active())stop("Drive status requested without owned disc files");
 return (active&&(active->requests||active->pumping))||melee_web_audio_bank_transport_busy()||melee_web_source_file_drive_busy()?DVD_STATE_BUSY:DVD_STATE_END;
}
s32 DVDConvertPathToEntrynum(const char* path){
 int entry=melee_web_audio_bank_transport_path(path);if(entry>=0)return entry;
 entry=melee_web_source_file_entry(path);if(entry>=0)return entry;
 if(!active)stop("DVD path requested without an owned file registry");
 if(!hps_interval_guard("dvd-path"))return -1;
 if(!melee_web_audio_stream_pump_for(active->audio,NULL,0))stop("pending HPS transfer failed before file selection");
 const StreamFile* file=file_for_path(path);if(file){active->selected_file=(unsigned)(file-active->files)+1;return (s32)active->selected_file;}
 fprintf(stderr,"Original HPS path is not owned: %s\n",path?path:"(null)");
 return -1;
}
/* Validation only: no source calls, allocation, queue or slot mutation. */
static const char* validate_stream_request(int file,uintptr_t src,uintptr_t dest,
 size_t size,int type,int priority,HSD_DevComCallback callback,void* args,Request* out){
 const StreamFile* selected=active?file_at((unsigned)file):NULL;
 if(!active||!selected||priority<0||priority>2||src%32||size%32||!size||!callback)
  return "unsupported file/request alignment, priority, or callback";
 memset(out,0,sizeof(*out));
 out->src=src;out->dest=dest;out->size=size;out->type=type;out->priority=priority;
 out->callback=callback;out->args=(uintptr_t)args;out->file=(unsigned)file;
 if(type==0x22){if(src||dest||size!=128)return "unsupported relay request";}
 else if(type==0x21){
  out->block=block_at(selected,src);
  if(!out->block||size!=32)return "header request is not a validated HPS block";
  for(out->slot=0;out->slot<3&&dest!=(uintptr_t)active->headers[out->slot];out->slot++){}
  if(out->slot==3)return "header destination is not source-owned";
 }else if(type==0x23){
  out->block=src>=32?block_at(selected,src-32):NULL;
  if(!out->block||size!=out->block->size||dest<STREAM_BASE||
     (dest-STREAM_BASE)%65536||(dest-STREAM_BASE)/65536>=3)
   return "payload request is not a validated HPS slot transfer";
  out->slot=(dest-STREAM_BASE)/65536;
 }else return "unsupported DevCom request type";
 return NULL;
}
static int stream_interval_bound(void){
 if(!active||!active->interval_snapshot||!active->interval_token||
    !melee_web_cpu_interval_active()||
    active->interval_token!=melee_web_cpu_interval_token()||
    !melee_web_cpu_interval_admit(active->interval_token))return 0;
 /* Pump/callback work is intentionally not quiescent. Its ownership must
  * match the quiescent PRE receipt; values may advance through real refills. */
 return snapshot_matches_live(active->audio,active->interval_snapshot,active);
}
int HSD_DevComRequest(int file,uintptr_t src,uintptr_t dest,size_t size,int type,int priority,HSD_DevComCallback callback,void* args){

 if(melee_web_audio_bank_transport_file(file)||(type==0x1B&&melee_web_audio_bank_transport_active()))return melee_web_audio_bank_transport_request(file,src,dest,size,type,priority,callback,args);
 if(melee_web_source_file_entry_owned(file)){
  int request;
  if(!melee_web_source_file_request(file,src,dest,size,type,priority,callback,args,&request,NULL,0)){
   if(melee_web_cpu_interval_active_failure())return -1;
   stop("unsupported or unowned source DVD/DevCom transfer");
  }
  return request;
 }
 Request validated;
 const char* invalid=validate_stream_request(file,src,dest,size,type,priority,callback,args,&validated);
 if(melee_web_cpu_interval_active()){
  if(invalid||(type!=0x21&&type!=0x23)||!stream_interval_bound()){
   (void)hps_interval_guard("devcom-request");return -1;
  }
 }else{
  if(!hps_interval_guard("devcom-request"))return -1;
  if(invalid)stop(invalid);
 }
 Request* r=calloc(1,sizeof(*r));if(!r)stop("request allocation failed");
 *r=validated;
 if(active->next_id>0x1ffffff8)stop("request ID exhausted");r->id=active->next_id+priority;active->next_id+=4;
 Request** tail=&active->requests;while(*tail)tail=&(*tail)->next;*tail=r;probe_record(MELEE_WEB_AUDIO_STREAM_PROBE_QUEUED,r,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX);return r->id;
}
int HSD_DevComCancelEx(int id,u32 flags,HSD_DevComCallback callback,void* args){
    if(!hps_interval_guard("devcom-cancel"))return -1;

 if(melee_web_audio_bank_transport_cancel(id,flags,callback,args))return 0;
 if(melee_web_source_file_cancel(id,flags,callback,args))return 0;
 if(!active||flags&~3u)stop("unsupported DevCom cancellation");
 for(Request* r=active->requests;r;r=r->next)if(r->id==id){if(flags&1)r->callback=callback;if(flags&2)r->args=(uintptr_t)args;r->cancelled=1;break;}return 0;
}
int melee_web_audio_stream_pump_for(MeleeWebAudio* audio,char* e,size_t n){
 if(!active||active->audio!=audio)return 1;
 if(active->pumping)return fail(e,n,"Reentrant HPS transport pump");
 if(active->requests&&melee_web_cpu_interval_active()&&!stream_interval_bound()){
  (void)hps_interval_guard("queued-pump");
  return fail(e,n,"CPU interval forbids unbound queued HPS work");
 }
 if(active->requests&&!melee_web_cpu_interval_active()&&!hps_interval_guard("queued-pump"))
  return fail(e,n,"CPU interval forbids queued HPS work");
 active->pumping=1;
 unsigned budget=64;
 while(active->requests&&budget--){
 Request** chosen=&active->requests;for(Request** at=&active->requests;*at;at=&(*at)->next)if((*at)->priority<(*chosen)->priority)chosen=at;
 Request* r=*chosen;
 if(melee_web_cpu_interval_active()){
  Request validated;
  const char* invalid=validate_stream_request((int)r->file,r->src,r->dest,r->size,
    r->type,r->priority,r->callback,(void*)r->args,&validated);
  if(invalid||(r->type!=0x21&&r->type!=0x23)||r->cancelled||
     !stream_interval_bound()||validated.block!=r->block||validated.slot!=r->slot){
   (void)hps_interval_guard("queued-pump");active->pumping=0;
   return fail(e,n,"CPU interval refuses a changed HPS refill request");
  }
 }
 *chosen=r->next;const StreamFile* file=file_at(r->file);if(!file)stop("HPS request lost its registered file");void* relay=NULL;size_t length=0;
  probe_record(MELEE_WEB_AUDIO_STREAM_PROBE_SELECTED,r,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX);
  if(!r->cancelled){
   MeleeWebIoEndpoint from={MELEE_WEB_IO_FILE,file->handle,r->src},to={r->type==0x23?MELEE_WEB_IO_AUXILIARY:MELEE_WEB_IO_MEMORY,r->type==0x23?active->aux:active->relay,r->type==0x23?r->slot*65536:0};uint64_t request;uint32_t completed;
   if(!melee_web_io_submit(active->io,from,to,r->size,NULL,NULL,&request,e,n)||!melee_web_io_pump(active->io,1,&completed,e,n)||completed!=1)stop("owned byte transfer failed");
   if(r->type==0x22){if(!melee_web_io_buffer(active->io,active->relay,&relay,&length,e,n))stop("relay missing");header_native(relay,file);}
   if(r->type==0x21)block_native((void*)r->dest,r->block,file);
   if(r->type==0x23){MeleeWebAudioStreamProbeSlot before;int observe=stream_probe.armed&&stream_probe.owner==active->audio;int have_before=observe&&probe_slot_snapshot(active,r->slot,&before);if(!decode_slot(r->slot,r->block,file,e,n)){free(r);active->pumping=0;return 0;}uint32_t old_file=have_before&&before.valid?before.file:UINT32_MAX,old_block=have_before&&before.valid?before.block_offset:UINT32_MAX;active->slots[r->slot]=r->block;active->slot_file[r->slot]=r->file;active->completed++;size_t index=0;for(unsigned i=0;i<r->file-1;i++)index+=active->files[i].input->count;index+=(size_t)(r->block-file->input->blocks);if(active->visited[index])active->revisited++;active->visited[index]=1;if(observe)probe_record(MELEE_WEB_AUDIO_STREAM_PROBE_SLOT_REPLACED,r,old_file,old_block,r->file,r->block->offset);}
  }
  if(r->callback){r->callback(r->id,(int)r->args,relay,r->cancelled);probe_record(MELEE_WEB_AUDIO_STREAM_PROBE_CALLBACK,r,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX);}free(r);
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
static size_t stream_block_count(const MeleeWebAudioStream* s){
 size_t count=0;
 for(uint32_t i=0;i<s->input_count;i++){
  if(count>SIZE_MAX-s->files[i].input->count)return 0;
  count+=s->files[i].input->count;
 }
 return count;
}
static int stream_quiescent(const MeleeWebAudioStream* s){
 return s&&!s->requests&&!s->pumping&&s->io&&melee_web_io_pending(s->io)==0&&
        !melee_web_audio_stream_source_loading();
}
static int stream_buffer_pair(MeleeWebAudioStream* s,void** aux,size_t* aux_size,
                              void** relay,size_t* relay_size,char* e,size_t n){
 return melee_web_io_buffer(s->io,s->aux,aux,aux_size,e,n)&&
        melee_web_io_buffer(s->io,s->relay,relay,relay_size,e,n)&&
        *aux_size==0x30000&&*relay_size==128;
}
static void snapshot_free(MeleeWebAudioStreamSnapshot* snapshot){
 if(!snapshot)return;
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++)free(snapshot->pcm_bytes[slot][side]);
 free(snapshot->visited_bytes);free(snapshot->aux_bytes);free(snapshot->relay_bytes);free(snapshot);
}
int melee_web_audio_stream_prepare_snapshot(MeleeWebAudio* audio,char* e,size_t n){
 if(melee_web_cpu_interval_active()){
  (void)hps_interval_guard("stream-prepare");return fail(e,n,"HPS PCM preparation is refused during a CPU interval");
 }
 if(!active||active->audio!=audio||!melee_web_audio_is_active(audio))return fail(e,n,"No matching live HPS stream to prepare");
 MeleeWebAudioStream* s=active;
 if(s->prepared){if(e&&n)*e=0;return 1;}
 if(!stream_quiescent(s))return fail(e,n,"HPS PCM preparation requires a naturally quiescent stream");
 size_t capacity[2]={1,1};
 for(uint32_t fi=0;fi<s->input_count;fi++){
  const MeleeWebAudioStreamInput* in=s->files[fi].input;
  if(!in||!in->blocks||!in->count||in->channels<1||in->channels>2)return fail(e,n,"HPS input registry changed before PCM preparation");
  for(uint32_t bi=0;bi<in->count;bi++)for(unsigned side=0;side<in->channels;side++){
   const MeleeWebAudioStreamBlock* b=&in->blocks[bi];const MeleeWebAudioChannel* c=&b->channel[side];size_t frames;
   if(!b->payload[side]||!channel_frame_count(c,&frames)||c->end_nibble/2>=b->channel_bytes)
    return fail(e,n,"Invalid HPS block extent during PCM preparation");
   if(frames>capacity[side])capacity[side]=frames;
  }
 }
 int16_t* storage[3][2]={{0}};int16_t* scratch[3][2]={{0}};
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  if(capacity[side]>SIZE_MAX/sizeof(int16_t))goto allocation_failed;
  storage[slot][side]=malloc(capacity[side]*sizeof(int16_t));
  scratch[slot][side]=malloc(capacity[side]*sizeof(int16_t));
  if(!storage[slot][side]||!scratch[slot][side])goto allocation_failed;
  memset(storage[slot][side],0,capacity[side]*sizeof(int16_t));
  memset(scratch[slot][side],0,capacity[side]*sizeof(int16_t));
  if(s->slot_channels[slot][side].pcm!=s->slot_pcm[slot][side]||
     (s->slot_channels[slot][side].frames&&
      (!s->slot_pcm[slot][side]||s->slot_channels[slot][side].frames>capacity[side])))
   goto invalid_existing_pcm;
  if(s->slot_channels[slot][side].frames){
   memcpy(storage[slot][side],s->slot_pcm[slot][side],
          (size_t)s->slot_channels[slot][side].frames*sizeof(int16_t));
  }
 }
 /* All allocations and current-slot checks passed. Publish stable buffers in
  * one bounded commit, preserving the canonical channel values. */
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  int16_t* old=s->slot_pcm[slot][side];
  int points_to_old=old&&s->slot_channels[slot][side].pcm==old;
  free(old);s->slot_pcm[slot][side]=storage[slot][side];storage[slot][side]=NULL;
  s->decode_scratch[slot][side]=scratch[slot][side];scratch[slot][side]=NULL;
  s->pcm_capacity[slot][side]=capacity[side];
  if(points_to_old)s->slot_channels[slot][side].pcm=s->slot_pcm[slot][side];
 }
 s->prepared=1;if(e&&n)*e=0;return 1;
invalid_existing_pcm:
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){free(storage[slot][side]);free(scratch[slot][side]);}
 return fail(e,n,"Existing HPS PCM is not a canonical slot allocation");
allocation_failed:
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){free(storage[slot][side]);free(scratch[slot][side]);}
 return fail(e,n,"Stable HPS PCM preparation allocation failed");
}
static int snapshot_matches_live(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,
                                 MeleeWebAudioStream* s){
 if(!snapshot||!s||active!=s||snapshot->audio!=audio||s->audio!=audio||
    snapshot->stream_identity!=s||snapshot->audio_generation!=melee_web_audio_generation(audio)||
    snapshot->stream_generation!=s->generation||snapshot->registry!=s->input||
    snapshot->input_count!=s->input_count||snapshot->input_count>STREAM_MAX_FILES||
    snapshot->selected_file>s->input_count||snapshot->io_identity!=s->io||!s->prepared||!s->visited||
    snapshot->visited_identity!=s->visited||snapshot->visited_count!=stream_block_count(s)||
    !snapshot->visited_bytes||!snapshot->aux_bytes||!snapshot->relay_bytes||
    snapshot->aux_handle!=s->aux||snapshot->relay_handle!=s->relay)return 0;
 for(uint32_t i=0;i<s->input_count;i++)if(snapshot->file_handles[i]!=s->files[i].handle||
    snapshot->file_inputs[i]!=s->files[i].input)return 0;
 for(unsigned slot=0;slot<3;slot++){
  if(snapshot->headers[slot]!=s->headers[slot])return 0;
  if(snapshot->slots[slot]){
   const StreamFile* file=file_at(snapshot->slot_file[slot]);
   if(!file||!block_pointer_in_file(file,snapshot->slots[slot]))return 0;
  }else if(snapshot->slot_file[slot]>s->input_count)return 0;
  for(unsigned side=0;side<2;side++){
   const StreamChannelState* channel=&snapshot->channels[slot][side];
   const MeleeWebAudioChannel* live_channel=&s->slot_channels[slot][side];
   if(snapshot->pcm_identity[slot][side]!=s->slot_pcm[slot][side]||
      snapshot->pcm_capacity[slot][side]!=s->pcm_capacity[slot][side]||
      !snapshot->pcm_bytes[slot][side]||!s->slot_pcm[slot][side]||!s->decode_scratch[slot][side]||
      channel->frames>s->pcm_capacity[slot][side]||
      (channel->frames&&channel->pcm!=snapshot->pcm_identity[slot][side])||
      live_channel->frames>s->pcm_capacity[slot][side]||
      (live_channel->pcm&&live_channel->pcm!=s->slot_pcm[slot][side])||
      (live_channel->frames&&live_channel->pcm!=s->slot_pcm[slot][side]))return 0;
  }
 }
 void *aux=NULL,*relay=NULL;size_t aux_size=0,relay_size=0;
 return stream_buffer_pair(s,&aux,&aux_size,&relay,&relay_size,NULL,0)&&
        snapshot->aux_identity==aux&&snapshot->relay_identity==relay&&
        snapshot->aux_size==aux_size&&snapshot->relay_size==relay_size;
}
static int snapshot_quiescent(const MeleeWebAudioStream* s){
 return stream_quiescent(s)&&!melee_web_cpu_interval_active_failure();
}
int melee_web_audio_stream_snapshot_capture(MeleeWebAudio* audio,MeleeWebAudioStreamSnapshot** out,char* e,size_t n){
 if(!out)return fail(e,n,"HPS snapshot output is null");*out=NULL;
 if(!melee_web_audio_generation(audio))return fail(e,n,"HPS snapshot audio owner is stale");
 if(melee_web_cpu_interval_active())return fail(e,n,"HPS snapshots are captured before the CPU interval only");
 if(!active){if(e&&n)*e=0;return 1;}
 if(active->audio!=audio)return fail(e,n,"A different HPS stream owns the transport");
 MeleeWebAudioStream* s=active;
 if(!s->prepared||!snapshot_quiescent(s))return fail(e,n,"HPS snapshot requires prepared, naturally quiescent stream state");
 MeleeWebAudioStreamSnapshot* snapshot=calloc(1,sizeof(*snapshot));if(!snapshot)return fail(e,n,"HPS snapshot allocation failed");
 snapshot->audio=audio;snapshot->audio_generation=melee_web_audio_generation(audio);
 snapshot->stream_identity=s;snapshot->stream_generation=s->generation;snapshot->registry=s->input;
 snapshot->input_count=s->input_count;snapshot->io_identity=s->io;snapshot->selected_file=s->selected_file;
 memcpy(snapshot->headers,s->headers,sizeof(snapshot->headers));memcpy(snapshot->slots,s->slots,sizeof(snapshot->slots));
 memcpy(snapshot->slot_file,s->slot_file,sizeof(snapshot->slot_file));
 snapshot->next_id=s->next_id;snapshot->completed=s->completed;snapshot->revisited=s->revisited;
 snapshot->visited_identity=s->visited;snapshot->visited_count=stream_block_count(s);
 if(!snapshot->visited_count)goto failed;
 snapshot->visited_bytes=malloc(snapshot->visited_count);if(!snapshot->visited_bytes)goto failed;
 memcpy(snapshot->visited_bytes,s->visited,snapshot->visited_count);
 for(uint32_t i=0;i<s->input_count;i++){snapshot->file_handles[i]=s->files[i].handle;snapshot->file_inputs[i]=s->files[i].input;}
 snapshot->aux_handle=s->aux;snapshot->relay_handle=s->relay;
 if(!stream_buffer_pair(s,&snapshot->aux_identity,&snapshot->aux_size,
                        &snapshot->relay_identity,&snapshot->relay_size,e,n))goto failed;
 snapshot->aux_bytes=malloc(snapshot->aux_size);snapshot->relay_bytes=malloc(snapshot->relay_size);
 if(!snapshot->aux_bytes||!snapshot->relay_bytes)goto failed;
 memcpy(snapshot->aux_bytes,snapshot->aux_identity,snapshot->aux_size);
 memcpy(snapshot->relay_bytes,snapshot->relay_identity,snapshot->relay_size);
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  if((s->slots[slot]&&(!s->slot_file[slot]||s->slot_file[slot]>s->input_count||
       !block_pointer_in_file(file_at(s->slot_file[slot]),s->slots[slot])))||
     (!s->slots[slot]&&s->slot_file[slot]>s->input_count))goto failed;
  if(s->slot_channels[slot][side].frames>s->pcm_capacity[slot][side]||
     (s->slot_channels[slot][side].frames&&s->slot_channels[slot][side].pcm!=s->slot_pcm[slot][side])||
     (s->slot_channels[slot][side].pcm&&s->slot_channels[slot][side].pcm!=s->slot_pcm[slot][side]))goto failed;
  snapshot->pcm_identity[slot][side]=s->slot_pcm[slot][side];
  snapshot->pcm_capacity[slot][side]=s->pcm_capacity[slot][side];
  snapshot->pcm_bytes[slot][side]=malloc(s->pcm_capacity[slot][side]*sizeof(int16_t));
  if(!snapshot->pcm_bytes[slot][side])goto failed;
  memcpy(snapshot->pcm_bytes[slot][side],s->slot_pcm[slot][side],
         s->pcm_capacity[slot][side]*sizeof(int16_t));
  channel_state_capture(&snapshot->channels[slot][side],&s->slot_channels[slot][side]);
 }
 *out=snapshot;if(e&&n)*e=0;return 1;
failed:
 snapshot_free(snapshot);if(e&&n&&!*e)snprintf(e,n,"HPS stream snapshot storage allocation failed");return 0;
}
int melee_web_audio_stream_owner_info(MeleeWebAudio* audio,MeleeWebAudioStreamOwnerInfo* out){
 if(!out||!melee_web_audio_generation(audio))return 0;memset(out,0,sizeof(*out));
 out->audio_identity=audio;out->audio_generation=melee_web_audio_generation(audio);
 if(!active){return 1;}if(active->audio!=audio)return 0;
 out->present=1;out->prepared=(uint32_t)(active->prepared!=0);out->input_count=active->input_count;
 out->stream_generation=active->generation;out->stream_identity=active;out->registry_identity=active->input;out->io_identity=active->io;
 return 1;
}
int melee_web_audio_stream_snapshot_bind_interval(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* e,size_t n){
 if(!token||!melee_web_cpu_interval_active()||melee_web_cpu_interval_token()!=token||
    !melee_web_cpu_interval_admit(token)||!snapshot||!active||!snapshot_matches_live(audio,snapshot,active)||
    !snapshot_quiescent(active)||active->interval_snapshot){return fail(e,n,"HPS interval binding does not match a fresh prepared receipt");}
 active->interval_snapshot=snapshot;active->interval_token=token;if(e&&n)*e=0;return 1;
}
int melee_web_audio_stream_snapshot_pending_admit(MeleeWebAudio* audio,
 const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token){
 if(!active||active->audio!=audio||!snapshot||active->interval_snapshot!=snapshot||
    active->interval_token!=token||!stream_interval_bound()||active->pumping||
    !active->requests||melee_web_io_pending(active->io)!=0)return 0;
 /* Reuse the original pump's finite work budget. This proves ownership only;
  * it neither services the queue nor claims a snapshot-safe boundary. */
 unsigned budget=64;
 for(const Request* r=active->requests;r;r=r->next){
  if(!budget--)return 0;
  Request validated;
  if(validate_stream_request((int)r->file,r->src,r->dest,r->size,r->type,
       r->priority,r->callback,(void*)r->args,&validated)||
     (r->type!=0x21&&r->type!=0x23)||r->cancelled||
     validated.block!=r->block||validated.slot!=r->slot)return 0;
 }
 return 1;
}
int melee_web_audio_stream_snapshot_admit(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* e,size_t n){
 if(!snapshot||!active||!snapshot_matches_live(audio,snapshot,active)||!snapshot_quiescent(active))return fail(e,n,"HPS snapshot owner or quiescence changed");
 if(melee_web_cpu_interval_active()){
  if(!token||token!=active->interval_token||token!=melee_web_cpu_interval_token()||
     active->interval_snapshot!=snapshot||!melee_web_cpu_interval_admit(token))return fail(e,n,"HPS snapshot interval binding is stale or failed");
 }else if(token||active->interval_snapshot||active->interval_token)return fail(e,n,"HPS snapshot retains a stale interval binding");
 if(e&&n)*e=0;return 1;
}
int melee_web_audio_stream_snapshot_restore(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token,char* e,size_t n){
 if(!melee_web_audio_stream_snapshot_admit(audio,snapshot,token,e,n))return 0;
 MeleeWebAudioStream* s=active;void* aux=NULL,*relay=NULL;size_t aux_size=0,relay_size=0;
 if(!stream_buffer_pair(s,&aux,&aux_size,&relay,&relay_size,e,n))return 0;
 /* Every identity, destination, queue and interval predicate was checked
  * before the first write; the commit is allocation/callback-free. */
 memcpy(aux,snapshot->aux_bytes,aux_size);memcpy(relay,snapshot->relay_bytes,relay_size);
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  memcpy(s->slot_pcm[slot][side],snapshot->pcm_bytes[slot][side],
         s->pcm_capacity[slot][side]*sizeof(int16_t));
  channel_state_assign(&s->slot_channels[slot][side],&snapshot->channels[slot][side]);
 }
 s->selected_file=snapshot->selected_file;memcpy(s->slots,snapshot->slots,sizeof(s->slots));
 memcpy(s->slot_file,snapshot->slot_file,sizeof(s->slot_file));
 s->next_id=snapshot->next_id;s->completed=snapshot->completed;s->revisited=snapshot->revisited;
 memcpy(s->visited,snapshot->visited_bytes,snapshot->visited_count);
 if(e&&n)*e=0;return 1;
}
int melee_web_audio_stream_snapshot_end_interval(MeleeWebAudio* audio,const MeleeWebAudioStreamSnapshot* snapshot,uint64_t token){
 if(!token||!snapshot||!active||active->audio!=audio||active->interval_snapshot!=snapshot||active->interval_token!=token||
    snapshot->audio!=audio||snapshot->stream_identity!=active||snapshot->audio_generation!=melee_web_audio_generation(audio)||
    snapshot->stream_generation!=active->generation)return 0;
 active->interval_snapshot=NULL;active->interval_token=0;return 1;
}
int melee_web_audio_stream_snapshot_destroy(MeleeWebAudioStreamSnapshot* snapshot,char* e,size_t n){
 if(!snapshot){if(e&&n)*e=0;return 1;}
 if(active&&active->interval_snapshot==snapshot)return fail(e,n,"Cannot destroy an HPS receipt while its interval binding is active");
 snapshot_free(snapshot);if(e&&n)*e=0;return 1;
}
static int emit_stream_range(MeleeWebAudioStream* s,MeleeWebAudioStreamOwnedRange* out,
                             size_t* count,MeleeWebAudioStreamRangeKind kind,
                             uint32_t index,const void* address,size_t extent){
 if(*count>=MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX||(!address&&extent))return 0;
 out[*count]=(MeleeWebAudioStreamOwnedRange){kind,index,address,extent,s,s->generation};
 ++*count;return 1;
}
MeleeWebAudioStreamRangeVisitResult melee_web_audio_stream_visit_owned_ranges(
 MeleeWebAudio* audio,uint64_t audio_generation,size_t capacity,
 MeleeWebAudioStreamOwnedRangeVisitor visitor,void* context,size_t* required){
 if(required)*required=0;
 if(!visitor||!required)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_ARGUMENT;
 if(!melee_web_audio_generation(audio)||melee_web_audio_generation(audio)!=audio_generation)
  return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 if(!active){return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE;}
 if(active->audio!=audio)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 MeleeWebAudioStream* s=active;
 if(!s->prepared||!stream_quiescent(s))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_NOT_QUIESCENT;
 if(melee_web_cpu_interval_active()&&
    (!s->interval_snapshot||!s->interval_token||s->interval_token!=melee_web_cpu_interval_token()||
     !melee_web_cpu_interval_admit(s->interval_token)))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 void* aux=NULL,*relay=NULL;size_t aux_size=0,relay_size=0;
 if(!stream_buffer_pair(s,&aux,&aux_size,&relay,&relay_size,NULL,0))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 MeleeWebAudioStreamOwnedRange ranges[MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX];size_t count=0;
#define STREAM_RANGE(k,i,p,z) do{if(!emit_stream_range(s,ranges,&count,(k),(i),(p),(z)))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;}while(0)
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_SELECTED_FILE,0,&s->selected_file,sizeof(s->selected_file));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_HEADERS,0,s->headers,sizeof(s->headers));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_SLOTS,0,s->slots,sizeof(s->slots));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_SLOT_FILES,0,s->slot_file,sizeof(s->slot_file));
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  const unsigned index=slot*2+side;const MeleeWebAudioChannel* c=&s->slot_channels[slot][side];
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_PCM,index,&c->pcm,sizeof(c->pcm));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_FRAMES,index,&c->frames,sizeof(c->frames));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOP_PCM,index,&c->loop_pcm,sizeof(c->loop_pcm));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOP_AND_NIBBLES,index,&c->loop_frames,
               sizeof(c->loop_frames)+3*sizeof(uint32_t));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_COEFFICIENTS,index,c->coefficients,sizeof(c->coefficients));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_PREDICTORS,index,&c->predictor_scale,
               sizeof(c->predictor_scale)+sizeof(c->loop_predictor_scale));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_HISTORIES,index,&c->history1,
               sizeof(c->history1)+sizeof(c->history2)+sizeof(c->loop_history1)+sizeof(c->loop_history2));
  STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_CHANNEL_LOOPING,index,&c->looping,sizeof(c->looping));
 }
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_PCM_POINTERS,0,s->slot_pcm,sizeof(s->slot_pcm));
 for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){
  const size_t samples=s->pcm_capacity[slot][side];
  if(samples>SIZE_MAX/sizeof(int16_t))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
  if(s->slot_pcm[slot][side]&&samples)
   STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_PCM_BYTES,slot*2+side,s->slot_pcm[slot][side],
                samples*sizeof(int16_t));
 }
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_PCM_CAPACITIES,0,s->pcm_capacity,sizeof(s->pcm_capacity));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_PREPARED,0,&s->prepared,sizeof(s->prepared));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_VISITED_POINTER,0,&s->visited,sizeof(s->visited));
 const size_t visited_count=stream_block_count(s);
 if(!visited_count)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INVALID_OWNER;
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_VISITED_BYTES,0,s->visited,visited_count);
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_PROGRESS,0,&s->next_id,
              sizeof(s->next_id)+sizeof(s->completed)+sizeof(s->revisited));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_AUX_HANDLE,0,&s->aux,sizeof(s->aux));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_AUX_BYTES,0,aux,aux_size);
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_RELAY_HANDLE,0,&s->relay,sizeof(s->relay));
 STREAM_RANGE(MELEE_WEB_AUDIO_STREAM_RANGE_RELAY_BYTES,0,relay,relay_size);
#undef STREAM_RANGE
 *required=count;
 if(capacity<count)return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INSUFFICIENT_CAPACITY;
 for(size_t i=0;i<count;i++){
  if(!visitor(&ranges[i],context))return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_STOPPED;
 }
 return MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE;
}
int melee_web_audio_stream_end(MeleeWebAudioStream* s,char* e,size_t n){
    if(!s||active!=s)return fail(e,n,"HPS scope is not active");
    if(s->interval_snapshot){
     (void)hps_interval_guard("stream-retire");return fail(e,n,"HPS scope has a live CPU interval binding");
    }
    if(!hps_interval_guard("stream-retire"))return fail(e,n,"CPU interval forbids HPS scope retirement");

 if(s->pumping)return fail(e,n,"HPS scope is pumping");
 if(!melee_web_audio_stream_pump_for(s->audio,e,n))return 0;
 if(melee_web_audio_is_active(s->audio)){
  if(melee_web_audio_stream_source_loading())return fail(e,n,"Original HPS source remains loading");
  melee_web_audio_stream_source_stop();
 }
 if(!melee_web_io_destroy(s->io,e,n))return 0;melee_web_audio_stream_lb_end();active=NULL;for(unsigned slot=0;slot<3;slot++)for(unsigned side=0;side<2;side++){free(s->slot_pcm[slot][side]);free(s->decode_scratch[slot][side]);}free(s->visited);free(s);return 1;
}
