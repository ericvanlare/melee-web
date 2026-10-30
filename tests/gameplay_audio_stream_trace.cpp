#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_cpu_interval.h"
#include "original_startup_compat.h"
extern "C" {
#include <sysdolphin/baselib/devcom.h>
}
#include <fstream>
#include <cstdio>
#include <cmath>
#include <bit>
#include <cstdint>
#include <array>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <stdexcept>
#include <limits>
#define CHECK(c) do{if(!(c))throw std::runtime_error("HPS trace check: " #c);}while(0)
extern "C" int lbAudioAx_80023F28(int);
static std::vector<uint8_t> read(const char* path){std::ifstream f(path,std::ios::binary);CHECK(f.good());return {std::istreambuf_iterator<char>(f),{}};}
static const char* probe_kind(uint32_t kind){
 switch(kind){case MELEE_WEB_AUDIO_STREAM_PROBE_QUEUED:return "queued";case MELEE_WEB_AUDIO_STREAM_PROBE_SELECTED:return "selected";case MELEE_WEB_AUDIO_STREAM_PROBE_CALLBACK:return "callback";case MELEE_WEB_AUDIO_STREAM_PROBE_SLOT_REPLACED:return "slot-replaced";default:return "unknown";}
}
static void print_probe_event(const MeleeWebAudioStreamProbeEvent& e){
 printf("HPS_PROBE_EVENT {\"seq\":%u,\"kind\":\"%s\",\"id\":%u,\"type\":%u,\"priority\":%u,\"file\":%u,\"slot\":%u,\"block_offset\":%u,\"size\":%llu,\"src\":\"0x%llx\",\"dest\":\"0x%llx\",\"callback\":\"0x%llx\",\"args\":\"0x%llx\",\"previous_file\":%u,\"previous_block\":%u,\"current_file\":%u,\"current_block\":%u}\n",
  e.sequence,probe_kind(e.kind),e.id,e.type,e.priority,e.file,e.slot,e.block_offset,(unsigned long long)e.size,
  (unsigned long long)e.source,(unsigned long long)e.destination,(unsigned long long)e.callback,(unsigned long long)e.args,
  e.previous_file,e.previous_block_offset,e.current_file,e.current_block_offset);
}
static void print_slot(const char* label,const MeleeWebAudioStreamProbeSlot& s){
 printf("HPS_PROBE_SLOT {\"label\":\"%s\",\"valid\":%u,\"slot\":%u,\"file\":%u,\"block_offset\":%u,\"pcm0\":\"0x%llx\",\"frames0\":%llu,\"fnv0\":\"%016llx\",\"pcm1\":\"0x%llx\",\"frames1\":%llu,\"fnv1\":\"%016llx\"}\n",
  label,s.valid,s.slot,s.file,s.block_offset,(unsigned long long)s.pcm[0],(unsigned long long)s.frames[0],(unsigned long long)s.pcm_fnv1a[0],
  (unsigned long long)s.pcm[1],(unsigned long long)s.frames[1],(unsigned long long)s.pcm_fnv1a[1]);
}
static bool same_slot(const MeleeWebAudioStreamProbeSlot& a,const MeleeWebAudioStreamProbeSlot& b){
 if(a.valid!=b.valid||a.slot!=b.slot||a.file!=b.file||a.block_offset!=b.block_offset)return false;
 for(unsigned side=0;side<2;side++)if(a.pcm[side]!=b.pcm[side]||a.frames[side]!=b.frames[side]||a.pcm_fnv1a[side]!=b.pcm_fnv1a[side])return false;
 return true;
}
struct StreamProbeGuard {
 MeleeWebAudio* audio;bool armed=true;
 ~StreamProbeGuard(){if(armed)melee_web_audio_stream_probe_end(audio);}
 void end(){if(armed){CHECK(melee_web_audio_stream_probe_end(audio));armed=false;}}
};
struct StreamSnapshotDeleter {
 void operator()(MeleeWebAudioStreamSnapshot* snapshot) const {
  if(!snapshot)return;char error[192];
  if(!melee_web_audio_stream_snapshot_destroy(snapshot,error,sizeof(error)))std::abort();
 }
};
struct LeafRangeSet {
 std::array<MeleeWebAudioStreamOwnedRange,MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX> rows{};
 std::array<std::vector<uint8_t>,MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX> bytes{};
 size_t count=0,total_bytes=0;
};
struct LeafRangeMetadataCollector {
 std::array<MeleeWebAudioStreamOwnedRange,MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX>* rows;
 size_t count=0;bool invalid=false;
};
static int collect_leaf_range_metadata(const MeleeWebAudioStreamOwnedRange* row,void* context){
 auto* state=static_cast<LeafRangeMetadataCollector*>(context);
 if(!state||!row||!row->address||!row->byte_extent||!row->owner_identity||!row->generation||
    state->count>=state->rows->size()){
  if(state)state->invalid=true;return 0;
 }
 (*state->rows)[state->count++]=*row;return 1;
}
struct LeafRangeByteCollector {
 LeafRangeSet* set;size_t count=0;bool invalid=false;
};
static bool same_leaf_range_identity(const MeleeWebAudioStreamOwnedRange& a,
                                     const MeleeWebAudioStreamOwnedRange& b){
 return a.kind==b.kind&&a.index==b.index&&a.address==b.address&&
        a.byte_extent==b.byte_extent&&a.owner_identity==b.owner_identity&&
        a.generation==b.generation;
}
static int copy_leaf_range_bytes(const MeleeWebAudioStreamOwnedRange* row,void* context){
 auto* state=static_cast<LeafRangeByteCollector*>(context);
 if(!state||!row||state->count>=state->set->count||
    !same_leaf_range_identity(*row,state->set->rows[state->count])||
    state->set->bytes[state->count].size()!=row->byte_extent){
  if(state)state->invalid=true;return 0;
 }
 std::memcpy(state->set->bytes[state->count].data(),row->address,row->byte_extent);
 ++state->count;return 1;
}
static LeafRangeSet capture_stream_leaf_ranges(MeleeWebAudio* audio,uint64_t generation){
 LeafRangeSet set;LeafRangeMetadataCollector metadata{&set.rows};size_t required=0;
 const auto result=melee_web_audio_stream_visit_owned_ranges(audio,generation,
  MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX,collect_leaf_range_metadata,&metadata,&required);
 CHECK(result==MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE&&!metadata.invalid);
 CHECK(required==metadata.count&&required<=MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX);
 set.count=required;
 constexpr size_t kLeafEvidenceLimit=64u*1024u*1024u;
 for(size_t i=0;i<set.count;i++){
  CHECK(set.rows[i].byte_extent<=kLeafEvidenceLimit-set.total_bytes);
  set.total_bytes+=set.rows[i].byte_extent;set.bytes[i].resize(set.rows[i].byte_extent);
 }
 LeafRangeByteCollector bytes{&set};required=0;
 const auto copied=melee_web_audio_stream_visit_owned_ranges(audio,generation,
  MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX,copy_leaf_range_bytes,&bytes,&required);
 CHECK(copied==MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_COMPLETE&&!bytes.invalid);
 CHECK(required==set.count&&bytes.count==set.count);return set;
}
static bool same_stream_leaf_values(const LeafRangeSet& a,const LeafRangeSet& b){
 if(a.count!=b.count||a.total_bytes!=b.total_bytes)return false;
 for(size_t i=0;i<a.count;i++)
  if(!same_leaf_range_identity(a.rows[i],b.rows[i])||a.bytes[i]!=b.bytes[i])return false;
 return true;
}
static bool any_stream_leaf_value_changed(const LeafRangeSet& a,const LeafRangeSet& b){
 if(a.count!=b.count)return true;
 for(size_t i=0;i<a.count;i++)
  if(!same_leaf_range_identity(a.rows[i],b.rows[i])||a.bytes[i]!=b.bytes[i])return true;
 return false;
}
struct NoVisitCollector {size_t calls=0;};
static int count_only_stream_range(const MeleeWebAudioStreamOwnedRange*,void* context){
 ++static_cast<NoVisitCollector*>(context)->calls;return 1;
}
int main(int argc,char** argv){try{
 const bool first_refill_probe=argc==7&&std::strcmp(argv[1],"--first-refill-probe")==0;
 const bool stream_interval_leaf=argc==7&&std::strcmp(argv[1],"--stream-interval-leaf")==0;
 const bool stream_snapshot_leaf=argc==7&&std::strcmp(argv[1],"--stream-snapshot-leaf")==0;
 CHECK(first_refill_probe||stream_snapshot_leaf||stream_interval_leaf||argc==6||argc==7||argc==9);
 const int input_at=(first_refill_probe||stream_snapshot_leaf||stream_interval_leaf)?2:1;
 auto main=read(argv[input_at]),mario=read(argv[input_at+1]),sem=read(argv[input_at+2]),coef=read(argv[input_at+3]),hps=read(argv[input_at+4]);uint64_t first_hash=0;
 if(first_refill_probe){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);melee_web::GameplayAudioStream stream(bank.get(),"/audio/sp_end.hps",hps);
  CHECK(melee_web_audio_stream_probe_begin(bank.get()));StreamProbeGuard probe_guard{bank.get()};
  CHECK(lbAudioAx_80023F28(78)==0);CHECK(std::string(melee_web_audio_stream_path(bank.get()))=="/audio/sp_end.hps");
  char error[256];std::array<float,320> pcm{};constexpr uint32_t render_call_cap=2048;uint32_t render_calls=0;bool found=false;
  while(render_calls<render_call_cap){
   MeleeWebAudioStreamProbeRequest candidate{};MeleeWebAudioStreamProbeSlot before{};
   const int potential=melee_web_audio_stream_probe_next_refill(bank.get(),&candidate,&before);CHECK(potential>=0);
   MeleeWebAudioSnapshot* raw_snapshot=potential?melee_web_audio_snapshot_capture(bank.get()):nullptr;
   std::unique_ptr<MeleeWebAudioSnapshot,decltype(&melee_web_audio_snapshot_destroy)> snapshot(raw_snapshot,&melee_web_audio_snapshot_destroy);
   MeleeWebAudioStreamProbeBoundary pre_boundary{};uint32_t events_before=0,overflow=0,completed_before=0,revisited_before=0;
   CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&pre_boundary));
   CHECK(melee_web_audio_stream_probe_event_count(bank.get(),&events_before,&overflow));if(overflow)throw std::runtime_error("HPS first-refill probe event cap overflowed before a refill boundary");
   CHECK(melee_web_audio_stream_progress(bank.get(),&completed_before,&revisited_before));
   if(potential)CHECK(snapshot!=nullptr);
   if(!melee_web_audio_render(bank.get(),pcm.data(),160,error,sizeof(error)))throw std::runtime_error(error);
   for(float value:pcm)CHECK(std::isfinite(value));++render_calls;
   uint32_t events_after=0;CHECK(melee_web_audio_stream_probe_event_count(bank.get(),&events_after,&overflow));if(overflow)throw std::runtime_error("HPS first-refill probe event cap overflowed while observing a pump");
   int replacement_event=-1;
   if(potential){
    for(uint32_t i=events_before;i<events_after;i++){
     MeleeWebAudioStreamProbeEvent event{};CHECK(melee_web_audio_stream_probe_event(bank.get(),i,&event));
     if(event.kind==MELEE_WEB_AUDIO_STREAM_PROBE_SLOT_REPLACED&&event.slot==candidate.slot&&event.previous_file==before.file&&event.previous_block_offset==before.block_offset&&
        (event.current_file!=before.file||event.current_block_offset!=before.block_offset)){replacement_event=(int)i;break;}
    }
   }
   if(replacement_event<0)continue;

   MeleeWebAudioStreamProbeEvent replacement{};CHECK(melee_web_audio_stream_probe_event(bank.get(),(uint32_t)replacement_event,&replacement));
   MeleeWebAudioStreamProbeSlot after_render{},after_restore{};MeleeWebAudioStreamProbeBoundary post_boundary{};
   uint32_t completed_after_render=0,revisited_after_render=0,completed_after_restore=0,revisited_after_restore=0;
   CHECK(melee_web_audio_stream_probe_slot(bank.get(),candidate.slot,&after_render));CHECK(after_render.valid);
   CHECK(after_render.file==replacement.current_file&&after_render.block_offset==replacement.current_block_offset);
   CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&post_boundary));
   CHECK(melee_web_audio_stream_progress(bank.get(),&completed_after_render,&revisited_after_render));CHECK(completed_after_render>completed_before);
   CHECK(melee_web_audio_snapshot_restore(bank.get(),snapshot.get()));
   CHECK(melee_web_audio_stream_probe_slot(bank.get(),candidate.slot,&after_restore));CHECK(same_slot(after_render,after_restore));
   CHECK(melee_web_audio_stream_progress(bank.get(),&completed_after_restore,&revisited_after_restore));
   CHECK(completed_after_render==completed_after_restore&&revisited_after_render==revisited_after_restore);
   probe_guard.end();

   printf("HPS_FIRST_REFILL_PROBE {\"schema\":1,\"path\":\"/audio/sp_end.hps\",\"render_calls\":%u,\"rendered_samples\":%u,\"frames_per_call\":160,\"source_callback_boundaries\":%u,\"bounded_call_cap\":%u,\"prospective_request\":{\"id\":%u,\"type\":%u,\"priority\":%u,\"file\":%u,\"slot\":%u,\"block_offset\":%u,\"size\":%llu,\"src\":\"0x%llx\",\"dest\":\"0x%llx\",\"callback\":\"0x%llx\",\"args\":\"0x%llx\"},\"refill_request\":%u,\"pre_pending\":%u,\"pre_pumping\":%u,\"pre_source_loading\":%u,\"pre_quiescent\":%s,\"post_pending\":%u,\"post_pumping\":%u,\"post_source_loading\":%u,\"post_quiescent\":%s,\"completed_before\":%u,\"completed_after_render\":%u,\"completed_after_mixer_restore\":%u,\"revisited_before\":%u,\"revisited_after_render\":%u,\"revisited_after_mixer_restore\":%u,\"mixer_snapshot_restore\":\"success\",\"slot_unchanged_by_restore\":true,\"scope\":\"HPS stream owner state only; no playback-field or HSD-global equivalence claim\"}\n",
    render_calls,render_calls*160,render_calls,render_call_cap,candidate.id,candidate.type,candidate.priority,candidate.file,candidate.slot,candidate.block_offset,
    (unsigned long long)candidate.size,(unsigned long long)candidate.source,(unsigned long long)candidate.destination,(unsigned long long)candidate.callback,(unsigned long long)candidate.args,
    replacement.id,pre_boundary.pending_requests,pre_boundary.pumping,pre_boundary.source_loading,pre_boundary.pending_requests==0&&!pre_boundary.pumping&&!pre_boundary.source_loading?"true":"false",
    post_boundary.pending_requests,post_boundary.pumping,post_boundary.source_loading,post_boundary.pending_requests==0&&!post_boundary.pumping&&!post_boundary.source_loading?"true":"false",
    completed_before,completed_after_render,completed_after_restore,revisited_before,revisited_after_render,revisited_after_restore);
   print_slot("before",before);print_slot("after_render",after_render);print_slot("after_mixer_restore",after_restore);
   for(uint32_t i=0;i<events_after;i++){MeleeWebAudioStreamProbeEvent event{};CHECK(melee_web_audio_stream_probe_event(bank.get(),i,&event));print_probe_event(event);}
   found=true;break;
  }
  if(!found)throw std::runtime_error("No occupied-slot HPS refill reached within 2048 original 160-sample render calls");
  return 0;
 }
 if(stream_snapshot_leaf||stream_interval_leaf){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);
  melee_web::GameplayAudioStream stream(bank.get(),"/audio/sp_end.hps",hps);
  CHECK(melee_web_audio_stream_probe_begin(bank.get()));StreamProbeGuard probe_guard{bank.get()};
  char error[256];std::array<float,320> pcm{};uint32_t render_calls=0;
  CHECK(lbAudioAx_80023F28(78)==0);
  CHECK(std::string(melee_web_audio_stream_path(bank.get()))=="/audio/sp_end.hps");
  MeleeWebAudioStreamProbeBoundary boundary{};bool initial_slots_quiescent=false;
  while(render_calls<2048){
   bool all_slots_loaded=true;
   for(uint32_t slot=0;slot<3;slot++){
    MeleeWebAudioStreamProbeSlot state{};CHECK(melee_web_audio_stream_probe_slot(bank.get(),slot,&state));
    all_slots_loaded=all_slots_loaded&&state.valid;
   }
   CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&boundary));
   if(all_slots_loaded&&!boundary.pending_requests&&!boundary.pumping&&!boundary.source_loading){
    initial_slots_quiescent=true;break;
   }
   if(!melee_web_audio_render(bank.get(),pcm.data(),160,error,sizeof(error)))throw std::runtime_error(error);
   for(float value:pcm)CHECK(std::isfinite(value));++render_calls;
  }
  CHECK(initial_slots_quiescent);
  CHECK(melee_web_audio_stream_prepare_snapshot(bank.get(),error,sizeof(error)));
  MeleeWebAudioStreamOwnerInfo owner{};CHECK(melee_web_audio_stream_owner_info(bank.get(),&owner));
  CHECK(owner.present&&owner.prepared&&owner.stream_identity&&owner.stream_generation);
  const uint64_t audio_generation=melee_web_audio_generation(bank.get());CHECK(audio_generation);
  MeleeWebAudioStreamSnapshot* raw_snapshot=nullptr;
  CHECK(melee_web_audio_stream_snapshot_capture(bank.get(),&raw_snapshot,error,sizeof(error))&&raw_snapshot);
  std::unique_ptr<MeleeWebAudioStreamSnapshot,StreamSnapshotDeleter> snapshot(raw_snapshot);
  CHECK(melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),0,error,sizeof(error)));
  CHECK(!melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),1,error,sizeof(error)));
  uint64_t refill_token=0;
  if(stream_interval_leaf){
   refill_token=melee_web_cpu_interval_begin();CHECK(refill_token);
   CHECK(melee_web_audio_stream_snapshot_bind_interval(bank.get(),snapshot.get(),
     refill_token,error,sizeof(error)));
  }
  LeafRangeSet before=capture_stream_leaf_ranges(bank.get(),audio_generation);
  CHECK(before.count>=62&&before.count<=MELEE_WEB_AUDIO_STREAM_OWNED_RANGE_MAX);
  size_t pcm_rows=0;for(size_t i=0;i<before.count;i++)
   if(before.rows[i].kind==MELEE_WEB_AUDIO_STREAM_RANGE_PCM_BYTES)++pcm_rows;
  CHECK(pcm_rows<=6);
  NoVisitCollector no_visit;size_t short_required=0;
  CHECK(before.count>0&&melee_web_audio_stream_visit_owned_ranges(bank.get(),audio_generation,
    before.count-1,count_only_stream_range,&no_visit,&short_required)==
      MELEE_WEB_AUDIO_STREAM_RANGE_VISIT_INSUFFICIENT_CAPACITY);
  CHECK(short_required==before.count&&no_visit.calls==0);

  bool pending_capture_refused=false,refill_seen=false,post_quiescent=false;
  MeleeWebAudioStreamProbeRequest refill{};MeleeWebAudioStreamProbeSlot refill_before{},refill_after{};
  MeleeWebAudioStreamProbeBoundary pre_refill_boundary{},post_refill_boundary{};
  uint32_t replacement_id=0;
  while(render_calls<2048&&!refill_seen){
   MeleeWebAudioStreamProbeRequest candidate{};MeleeWebAudioStreamProbeSlot occupied{};
   const int potential=melee_web_audio_stream_probe_next_refill(bank.get(),&candidate,&occupied);
   CHECK(potential>=0);uint32_t events_before=0,overflow=0,events_after=0;
   CHECK(melee_web_audio_stream_probe_event_count(bank.get(),&events_before,&overflow));CHECK(!overflow);
   CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&pre_refill_boundary));
   if(potential){
    CHECK(pre_refill_boundary.pending_requests>0&&!pending_capture_refused);
    MeleeWebAudioStreamSnapshot* pending_snapshot=nullptr;
    CHECK(!melee_web_audio_stream_snapshot_capture(bank.get(),&pending_snapshot,error,sizeof(error)));
    CHECK(!pending_snapshot);
    CHECK(!melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),refill_token,error,sizeof(error)));
    CHECK(!melee_web_audio_stream_snapshot_restore(bank.get(),snapshot.get(),refill_token,error,sizeof(error)));
    if(stream_interval_leaf){
     CHECK(melee_web_audio_stream_snapshot_pending_admit(bank.get(),snapshot.get(),refill_token));
     CHECK(!melee_web_audio_stream_snapshot_pending_admit(bank.get(),snapshot.get(),refill_token==1?2:1));
     MeleeWebAudioStreamProbeBoundary unchanged{};
     CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&unchanged));
     CHECK(unchanged.pending_requests==pre_refill_boundary.pending_requests&&
       unchanged.pumping==pre_refill_boundary.pumping&&unchanged.source_loading==pre_refill_boundary.source_loading);
     MeleeWebAudioStreamProbeRequest same_request{};MeleeWebAudioStreamProbeSlot same_slot_state{};
     CHECK(melee_web_audio_stream_probe_next_refill(bank.get(),&same_request,&same_slot_state)==potential);
     CHECK(same_request.id==candidate.id&&same_request.type==candidate.type&&
       same_request.block_offset==candidate.block_offset&&same_request.slot==candidate.slot&&same_slot(occupied,same_slot_state));
    }
    pending_capture_refused=true;refill=candidate;refill_before=occupied;
   }
   if(!melee_web_audio_render(bank.get(),pcm.data(),160,error,sizeof(error)))throw std::runtime_error(error);
   for(float value:pcm)CHECK(std::isfinite(value));++render_calls;
   CHECK(melee_web_audio_stream_probe_event_count(bank.get(),&events_after,&overflow));CHECK(!overflow);
   if(!potential)continue;
   for(uint32_t index=events_before;index<events_after;index++){
    MeleeWebAudioStreamProbeEvent event{};CHECK(melee_web_audio_stream_probe_event(bank.get(),index,&event));
    if(event.kind==MELEE_WEB_AUDIO_STREAM_PROBE_SLOT_REPLACED&&event.slot==candidate.slot&&
       event.previous_file==occupied.file&&event.previous_block_offset==occupied.block_offset&&
       (event.current_file!=occupied.file||event.current_block_offset!=occupied.block_offset)){
     refill_seen=true;replacement_id=event.id;break;
    }
   }
  }
  CHECK(refill_seen&&pending_capture_refused&&render_calls<=2048);
  CHECK(melee_web_audio_stream_probe_slot(bank.get(),refill.slot,&refill_after));
  CHECK(refill_after.valid&&refill_after.file==refill.file&&refill_after.block_offset==refill.block_offset);
  CHECK(melee_web_audio_stream_probe_boundary(bank.get(),&post_refill_boundary));
  post_quiescent=!post_refill_boundary.pending_requests&&!post_refill_boundary.pumping&&
                 !post_refill_boundary.source_loading;
  CHECK(post_quiescent);
  if(stream_interval_leaf)CHECK(!melee_web_audio_stream_snapshot_pending_admit(bank.get(),snapshot.get(),refill_token));
  LeafRangeSet after_refill=capture_stream_leaf_ranges(bank.get(),audio_generation);
  CHECK(after_refill.count==before.count&&!same_stream_leaf_values(before,after_refill));
  CHECK(melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),refill_token,error,sizeof(error)));
  CHECK(melee_web_audio_stream_snapshot_restore(bank.get(),snapshot.get(),refill_token,error,sizeof(error)));
  LeafRangeSet after_restore=capture_stream_leaf_ranges(bank.get(),audio_generation);
  CHECK(same_stream_leaf_values(before,after_restore));
  if(stream_interval_leaf){
   CHECK(melee_web_cpu_interval_admit(refill_token));
   const uint64_t wrong_token=refill_token==1?2:1;
   CHECK(!melee_web_audio_stream_snapshot_end_interval(bank.get(),snapshot.get(),wrong_token));
   CHECK(melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),refill_token,error,sizeof(error)));
   /* A valid relay tuple remains forbidden: no queue, IDs or bytes change. */
   CHECK(HSD_DevComRequest((int)refill.file,0,0,128,0x22,1,
     +[](int,int,void*,melee_source_bool){std::abort();},nullptr)==-1);
   CHECK(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_HPS);
   CHECK(melee_web_audio_stream_snapshot_end_interval(bank.get(),snapshot.get(),refill_token));
   CHECK(melee_web_cpu_interval_end(refill_token));
   CHECK(same_stream_leaf_values(after_restore,capture_stream_leaf_ranges(bank.get(),audio_generation)));
   const uint64_t cancel_token=melee_web_cpu_interval_begin();CHECK(cancel_token);
   CHECK(melee_web_audio_stream_snapshot_bind_interval(bank.get(),snapshot.get(),cancel_token,error,sizeof(error)));
   CHECK(HSD_DevComCancelEx((int)replacement_id,0,nullptr,nullptr)==-1);
   CHECK(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_HPS);
   CHECK(melee_web_audio_stream_snapshot_end_interval(bank.get(),snapshot.get(),cancel_token));
   CHECK(melee_web_cpu_interval_end(cancel_token));
   CHECK(same_stream_leaf_values(after_restore,capture_stream_leaf_ranges(bank.get(),audio_generation)));
  }
  /* A failed interval must release its exact binding before normal cleanup. */
  const uint64_t interval_token=melee_web_cpu_interval_begin();CHECK(interval_token);
  CHECK(melee_web_audio_stream_snapshot_bind_interval(bank.get(),snapshot.get(),
    interval_token,error,sizeof(error)));
  const uint64_t wrong_token=interval_token==1?2:1;
  CHECK(!melee_web_audio_stream_snapshot_end_interval(bank.get(),snapshot.get(),wrong_token));
  CHECK(melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),interval_token,error,sizeof(error)));
  CHECK(!melee_web_cpu_interval_attempt(MELEE_WEB_CPU_EFFECT_HPS));
  CHECK(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_HPS);
  CHECK(!melee_web_audio_stream_snapshot_admit(bank.get(),snapshot.get(),interval_token,error,sizeof(error)));
  CHECK(melee_web_audio_stream_snapshot_end_interval(bank.get(),snapshot.get(),interval_token));
  CHECK(melee_web_cpu_interval_end(interval_token));
  CHECK(!melee_web_cpu_interval_active());
  snapshot.reset();
  probe_guard.end();
  printf("%s {\"schema\":1,\"path\":\"/audio/sp_end.hps\",\"render_calls\":%u,\"initial_slots_quiescent\":true,\"prepared\":true,\"pre_rows\":%zu,\"pre_bytes\":%zu,\"pcm_rows\":%zu,\"pending_capture_refused\":true,\"pending_restore_refused\":true,\"pending_interior_admitted_without_mutation\":%s,\"natural_next_block_pump\":true,\"wrong_token_refused\":true,\"insufficient_capacity_no_callbacks\":true,\"refill_request\":%u,\"refill_slot\":%u,\"refill_block\":%u,\"post_quiescent\":true,\"post_changed_stream_values\":true,\"restored_rows_exact\":true,\"failed_interval_binding_cleanup\":true,\"scope\":\"typed HPS stream and PCM leaf state only; source scheduler, AX/HSD globals, and replay output are not compared\"}\n",
   stream_interval_leaf?"HPS_STREAM_INTERVAL_LEAF":"HPS_STREAM_SNAPSHOT_LEAF",
   render_calls,before.count,before.total_bytes,pcm_rows,stream_interval_leaf?"true":"false",replacement_id,refill.slot,refill.block_offset);
  return 0;
 }
 if(argc==9){
  auto info1=read(argv[6]),info2=read(argv[7]),info3=read(argv[8]);
  const std::array<melee_web::GameplayAudioStreamFile,3> files{{
   {"/audio/s_info1.hps",info1},{"/audio/s_info2.hps",info2},{"/audio/s_info3.hps",info3}}};
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);melee_web::GameplayAudioStream stream(bank.get(),files);
  char error[256];std::vector<float> pcm(64000);
  for(const auto& selected:std::array<std::pair<int,const char*>,3>{{{0x43,"/audio/s_info1.hps"},{0x44,"/audio/s_info2.hps"},{0x45,"/audio/s_info3.hps"}}}){
   CHECK(lbAudioAx_80023F28(selected.first)==0);CHECK(std::string(melee_web_audio_stream_path(bank.get()))==selected.second);
   CHECK(melee_web_audio_render(bank.get(),pcm.data(),32000,error,sizeof(error)));
  }
  uint32_t completed,revisited;CHECK(melee_web_audio_stream_progress(bank.get(),&completed,&revisited));CHECK(completed>0);
  puts("Original HPS registry selected three authored files and preserved source changes");return 0;
 }
 if(argc==7){
  auto menu3=read(argv[6]);
  const std::array<melee_web::GameplayAudioStreamFile,2> files{{
   {"/audio/menu01.hps",hps},{"/audio/menu3.hps",menu3}}};
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);
  melee_web::GameplayAudioStream stream(bank.get(),files);
  char error[256];std::vector<float> pcm(64000);double energy=0;
  for(const auto& selected:std::array<std::pair<int,const char*>,2>{{
      {0x34,"/audio/menu01.hps"},{0x36,"/audio/menu3.hps"}}}){
   CHECK(lbAudioAx_80023F28(selected.first)==0);
   CHECK(std::string(melee_web_audio_stream_path(bank.get()))==selected.second);
   CHECK(melee_web_audio_render(bank.get(),pcm.data(),32000,error,sizeof(error)));
   for(float value:pcm){CHECK(std::isfinite(value));energy+=double(value)*value;}
  }
  uint32_t completed,revisited;CHECK(melee_web_audio_stream_progress(bank.get(),&completed,&revisited));
  CHECK(completed>0&&energy>1);
  puts("Original CSS/SSS BGM IDs selected and streamed menu01.hps/menu3.hps through one owner");return 0;
 }
 for(unsigned cycle=0;cycle<2;cycle++){
  melee_web::GameplayAudioBank bank(sem,{main,mario},coef);melee_web::GameplayAudioStream stream(bank.get(),"/audio/sp_end.hps",hps);
  CHECK(lbAudioAx_80023F28(78)==0);char error[256];std::vector<float> pcm(64000);double energy=0;uint64_t hash=14695981039346656037ULL;
  // Cross all fifty block boundaries and the original intro-to-loop transition.
  for(unsigned second=0;second<100;second++){
   if(!melee_web_audio_render(bank.get(),pcm.data(),32000,error,sizeof(error)))throw std::runtime_error(error);
   for(float v:pcm){CHECK(std::isfinite(v));energy+=double(v)*v;uint32_t bits=std::bit_cast<uint32_t>(v);for(unsigned b=0;b<4;b++){hash^=(bits>>(b*8))&255;hash*=1099511628211ULL;}}
  }
  uint32_t completed,revisited;CHECK(melee_web_audio_stream_progress(bank.get(),&completed,&revisited));CHECK(completed>50&&revisited>0);printf("HPS payloads%u revisited%u\n",completed,revisited);
  printf("HPS cycle%u PCM FNV-1a %016llx\n",cycle,(unsigned long long)hash);
  if(cycle)CHECK(hash==first_hash);else first_hash=hash;
  CHECK(energy>1);printf("Original HPS cycle%u 100 seconds, energy%.9f\n",cycle,energy);
 }
 puts("Original HPS three-slot scheduler, native PCM loop, and restart passed");
 }catch(const std::exception& e){puts(e.what());return 1;}return 0;}
