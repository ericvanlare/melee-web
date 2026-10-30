/* This include follows the actual payload implementation. All data below is
 * private diagnostic storage; no source/mirror snapshot copies or restores it. */
static_assert(sizeof(MeleeWebLObjWriterEvent)==128);
static_assert(offsetof(MeleeWebLObjWriterEvent,before)==96);
static_assert(offsetof(MeleeWebLObjWriterEvent,after)==108);
static_assert(offsetof(MeleeWebLObjWriterEvidence,events)==400);
static_assert(offsetof(MeleeWebLObjWriterEvidence,path)==160);
static_assert(sizeof(MeleeWebLObjWriterEvidence)==16784);
namespace {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
struct LObjWriterOwner {
 const MeleeWebSourceMemoryPayloadSnapshot* pre=nullptr;
 MeleeWebClassPieceAnchor anchor{};
 uint32_t command=0,busy=0;
 MeleeWebLObjWriterEvidence evidence{};
} lobj_writer;
uint64_t lobj_writer_session=0; // Nonrewindable; never HostAllocation metadata.
void writer_refuse(uint32_t why){auto& e=lobj_writer.evidence;if(!e.reason)e.reason=why;e.status=2;e.complete=0;}
bool writer_owner_live(){const auto& e=lobj_writer.evidence;
 return lobj_writer.pre&&active&&healthy&&owner_epoch==e.epoch&&world_generation==e.world&&
  topology_revision==e.topology_revision&&e.session==lobj_writer_session;
}
bool writer_row(uintptr_t field,size_t& index,const HostAllocation*& row){
 if(!writer_owner_live())return false;
 if(field>UINTPTR_MAX-12)return false;
 const auto* rows=payload_rows(lobj_writer.pre);index=SIZE_MAX;row=nullptr;
 for(size_t i=0;i<allocations.size();++i){const auto& a=allocations[i];
  if(field<a.host||field-a.host>a.requested||12>a.requested-(field-a.host))continue;
  if(index!=SIZE_MAX)return false;
  if(i>=lobj_writer.pre->row_count)return false;const auto& p=rows[i];
  if(a.host!=p.host||a.source!=p.source||a.requested!=p.requested||a.generation!=p.generation)return false;
  index=i;row=&a;
 }return row&&index<=UINT32_MAX;
}
// Read the immutable-at-arm table only under the already-reviewed historical
// arm/frozen conditions; never dereference a live class descriptor/header.
bool writer_pre_piece(MeleeWebLObjWriterEvent& e){
 const auto& a=lobj_writer.anchor;
 if(!a.ready||!piece_owner.cells||a.epoch!=owner_epoch||a.world!=world_generation||
    a.epoch!=piece_owner.epoch||a.world!=piece_owner.world||a.instance!=piece_owner.instance||
    a.serial!=piece_owner.capture)return false;
 const bool armed=piece_owner.state==3&&!piece_owner.reason&&piece_owner.serial==a.serial;
 const bool frozen=piece_owner.state==4&&piece_owner.reason==7&&a.serial!=UINT64_MAX&&
  piece_owner.first.serial==a.serial+1&&piece_owner.serial==piece_owner.first.serial;
 if(!armed&&!frozen)return false;
 const uint64_t begin=e.source,end=begin+e.requested;
 if(begin<piece_owner.begin||end>piece_owner.end||piece_owner.begin>=piece_owner.end||
    (piece_owner.begin&31)||(piece_owner.end&31)||piece_owner.count!=(piece_owner.end-piece_owner.begin)/32)return false;
 const uint32_t first=(begin-piece_owner.begin)/32,last=(end-piece_owner.begin+31)/32;
 if(last>piece_owner.count)return false;
 const uint64_t target=begin+e.field_offset;uint32_t found=UINT32_MAX;
 for(uint32_t i=first;i<last;++i){const auto& c=piece_owner.cells[i];if(!(c.geometry&piece_live))continue;
  const uint64_t start=uint64_t(piece_owner.begin)+uint64_t(i)*32,extent=piece_extent(c);
  if(!extent||start<begin||start>end||extent>end-start)return false;
  if(target>=start&&target-start<extent){if(found!=UINT32_MAX)return false;found=i;}
 }
 if(found==UINT32_MAX)return false;const auto& c=piece_owner.cells[found];
 const uint64_t start=uint64_t(piece_owner.begin)+uint64_t(found)*32;
 const uint32_t physical=piece_extent(c),requested=piece_requested(c),off=target-start;
 if(!c.generation||requested<72||off!=60||12>requested-off||(c.tag&3)!=2||!(c.tag&~3u)||
    e.object!=e.host+(start-begin)||physical<requested||physical-requested>31)return false;
 size_t saved=0;const auto* rows=payload_rows(lobj_writer.pre);
 for(size_t i=0;i<e.pre_row;++i)if(!add_size(saved,rows[i].requested,&saved))return false;
 if(!add_size(saved,size_t(start-begin),&saved)||lobj_writer.pre->payload_bytes<4||saved>lobj_writer.pre->payload_bytes-4)return false;
 uint32_t header=0;std::memcpy(&header,payload_bytes(lobj_writer.pre)+saved,4);
 if(header!=(c.tag&~3u))return false;
 e.piece_source=start;e.piece_offset=off;e.physical=physical;e.original_requested=requested;
 e.piece_generation=c.generation;e.class_info=c.tag&~3u;e.saved_class_info=header;
 e.flags=3; // CAPTURE_TIME_ONLY + live piece lifetime UNKNOWN, even if matched.
 return true;
}
#endif
}
extern "C" uint64_t melee_web_lobj_writer_arm(const MeleeWebSourceMemoryPayloadSnapshot* pre,MeleeWebClassPieceAnchor a,uint32_t command){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(lobj_writer.pre||lobj_writer_session>=UINT64_MAX/256||payload_snapshot_check(pre)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)return 0;
 lobj_writer={};lobj_writer.pre=pre;lobj_writer.anchor=a;lobj_writer.command=command;
 auto& e=lobj_writer.evidence;e.schema=1;e.bytes=sizeof(e);e.hook_active=1;e.epoch=owner_epoch;e.world=world_generation;
 e.session=++lobj_writer_session;e.piece_instance=a.instance;e.capture_serial=a.serial;e.topology_revision=topology_revision;
 e.post_count=e.drawn_count=e.matched_index=UINT32_MAX;e.site_kind=1;return e.session;
#else
 (void)pre;(void)a;(void)command;return 0;
#endif
}
extern "C" void melee_web_lobj_writer_phase(uint32_t command){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(lobj_writer.pre)lobj_writer.command=command;
#else
 (void)command;
#endif
}
extern "C" void melee_web_lobj_writer_mark(uint64_t session,uint32_t endpoint){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& e=lobj_writer.evidence;if(!session||session!=e.session)return;
 if(!writer_owner_live()||lobj_writer.busy){writer_refuse(4);return;}
 if(endpoint==1&&e.post_count==UINT32_MAX)e.post_count=e.count;
 else if(endpoint==2&&e.drawn_count==UINT32_MAX&&e.post_count!=UINT32_MAX)e.drawn_count=e.count;
 else writer_refuse(2);
#else
 (void)session;(void)endpoint;
#endif
}
extern "C" void melee_web_lobj_writer_disarm(uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(session&&session==lobj_writer.evidence.session){lobj_writer.pre=nullptr;lobj_writer.busy=0;}
#else
 (void)session;
#endif
}
extern "C" uint64_t melee_web_lobj_writer_before(const void* object,const void* field,uint32_t bytes,const char* file,uint32_t line){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!lobj_writer.pre)return 0;auto& e=lobj_writer.evidence;
 if(e.seen==UINT32_MAX){writer_refuse(3);e.overflow=1;return 0;}++e.seen;
 if(e.count==MELEE_WEB_LOBJ_WRITER_CAPACITY){e.overflow=1;writer_refuse(3);return 0;}
 if(lobj_writer.busy){writer_refuse(2);return 0;}
 if(!writer_owner_live()){writer_refuse(4);return 0;}
 const auto p=reinterpret_cast<uintptr_t>(object),f=reinterpret_cast<uintptr_t>(field);
 if(!p||p>UINTPTR_MAX-60||f!=p+60||bytes!=212){writer_refuse(5);return 0;}
 size_t index;const HostAllocation* row=nullptr;
 if(!writer_row(f,index,row)||p<row->host||p-row->host>row->requested||bytes>row->requested-(p-row->host)){writer_refuse(5);return 0;}
 if(!file||!line){writer_refuse(9);return 0;}size_t length=0;while(length<sizeof(e.path)&&file[length])++length;
 if(!length||length==sizeof(e.path)){writer_refuse(9);return 0;}
 if(!e.path_bytes){std::memcpy(e.path,file,length+1);e.path_bytes=length;e.line=line;}
 else if(e.path_bytes!=length||e.line!=line||std::memcmp(e.path,file,length)){writer_refuse(9);return 0;}
 auto& r=e.events[e.count];r.sequence=++e.count;r.command=lobj_writer.command;r.pre_row=index;r.source=row->source.value();r.requested=row->requested;
 r.field_offset=f-row->host;r.host=row->host;r.generation=row->generation;r.object=p;r.field=f;r.flags=2;
 if(!writer_pre_piece(r))r.reason=6;
 std::memcpy(r.before,field,12);lobj_writer.busy=r.sequence;
 return (e.session<<8)|r.sequence;
#else
 (void)object;(void)field;(void)bytes;(void)file;(void)line;return 0;
#endif
}
extern "C" void melee_web_lobj_writer_after(uint64_t ticket){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!ticket)return;auto& e=lobj_writer.evidence;const uint32_t n=ticket&255;
 if((ticket>>8)!=e.session||!n||n>e.count||lobj_writer.busy!=n){writer_refuse(2);return;}
 auto& r=e.events[n-1];lobj_writer.busy=0;size_t index;const HostAllocation* row=nullptr;
 if(!writer_row(r.field,index,row)||index!=r.pre_row||row->host!=r.host||row->generation!=r.generation){r.status=2;r.reason=4;writer_refuse(4);return;}
 std::memcpy(r.after,reinterpret_cast<const void*>(uintptr_t(r.field)),12);r.status=r.reason?2:1;
#else
 (void)ticket;
#endif
}
extern "C" void melee_web_lobj_writer_finalize(const MeleeWebSourceMemoryPayloadSnapshot* pre,const MeleeWebSourceMemoryPayloadSnapshot* post,MeleeWebSourcePayloadComparison original,MeleeWebClassPieceAnchor a,uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& e=lobj_writer.evidence;if(!session||session!=e.session)return;
 const auto w=melee_web_source_memory_payload_snapshot_witness(post);
 if(original.status!=2||w.status!=2||original.allocation_index!=w.row||original.byte_offset!=w.offset||original.host_address!=w.host_address||
    original.source_address!=w.source_address||original.requested_bytes!=w.requested_bytes||w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
    original.expected_byte!=w.expected[w.offset-w.window_offset]||original.actual_byte!=w.actual[w.offset-w.window_offset]){writer_refuse(10);return;}
 e.first_difference_matches=1;e.row=w.row;e.offset=w.offset;e.source=w.source_address;e.requested=w.requested_bytes;e.expected=original.expected_byte;e.actual=original.actual_byte;e.host=w.host_address;e.generation=w.allocation_generation;
 if(pre!=lobj_writer.pre||!writer_owner_live()||payload_snapshot_check(pre)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||payload_snapshot_check(post)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||
    a.epoch!=lobj_writer.anchor.epoch||a.world!=lobj_writer.anchor.world||a.instance!=lobj_writer.anchor.instance||a.serial!=lobj_writer.anchor.serial){writer_refuse(4);return;}
 if(e.reason||e.overflow||lobj_writer.busy||e.seen!=e.count){if(!e.reason)writer_refuse(2);return;}
 if(e.post_count==UINT32_MAX||e.post_count>e.count||e.drawn_count==UINT32_MAX||e.drawn_count<e.post_count||e.drawn_count>e.count){writer_refuse(8);return;}
 const auto* rows=payload_rows(post);size_t post_start=0;
 for(size_t i=0;i<w.row;++i)if(!add_size(post_start,rows[i].requested,&post_start)){writer_refuse(5);return;}
 for(uint32_t i=0;i<e.post_count;++i){const auto& r=e.events[i];
  if(r.status!=1||r.reason||r.flags!=3||r.pre_row>=pre->row_count||r.pre_row!=w.row||r.host!=w.host_address||r.generation!=w.allocation_generation||r.source!=w.source_address||r.requested!=w.requested_bytes||
     w.offset<r.field_offset||w.offset-r.field_offset>=12)continue;
  const auto& prior=payload_rows(pre)[r.pre_row];const auto& expected=rows[w.row];
  if(prior.host!=expected.host||prior.source!=expected.source||prior.requested!=expected.requested||prior.generation!=expected.generation)continue;
  size_t saved;if(!add_size(post_start,r.field_offset,&saved)||post->payload_bytes<12||saved>post->payload_bytes-12)continue;
  if(std::memcmp(payload_bytes(post)+saved,r.after,12))continue;
  // Capture-time join only. Even a matching writer is not live class ownership.
  ++e.matched_count;e.matched_index=i;
 }
 e.complete=1;
 if(e.matched_count){e.status=1;e.reason=0;}else {e.status=2;e.reason=7;}
#else
 (void)pre;(void)post;(void)original;(void)a;(void)session;
#endif
}
extern "C" const MeleeWebLObjWriterEvidence* melee_web_lobj_writer_evidence(uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 return session&&session==lobj_writer.evidence.session?&lobj_writer.evidence:nullptr;
#else
 (void)session;return nullptr;
#endif
}
