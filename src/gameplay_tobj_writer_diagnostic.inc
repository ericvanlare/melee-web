/* This include follows the actual payload implementation. All data below is
 * private diagnostic storage; no source/mirror snapshot copies or restores it. */
static_assert(sizeof(MeleeWebTObjWriterEvent)==216);
static_assert(offsetof(MeleeWebTObjWriterEvent,before)==96);
static_assert(offsetof(MeleeWebTObjWriterEvent,after)==144);
static_assert(offsetof(MeleeWebTObjWriterEvidence,events)==400);
static_assert(offsetof(MeleeWebTObjWriterEvidence,path)==160);
static_assert(offsetof(MeleeWebTObjWriterEvidence,canonical_class)==140);
static_assert(offsetof(MeleeWebTObjWriterEvidence,pre_frame)==392);
static_assert(offsetof(MeleeWebTObjWriterEvent,flags_before)==192);
static_assert(offsetof(MeleeWebTObjWriterEvent,outer_epoch)==208);
static_assert(sizeof(MeleeWebTObjWriterEvidence)==28048);
namespace {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
struct TObjWriterOwner {
 const MeleeWebSourceMemoryPayloadSnapshot* pre=nullptr;
 MeleeWebClassPieceAnchor anchor{};
 uint32_t command=0,busy=0,outer_frame=0,outer_phase=0; uint64_t outer_epoch=0;
 MeleeWebTObjWriterEvidence evidence{};
} tobj_writer;
uint64_t tobj_writer_session=0; // Nonrewindable; never HostAllocation metadata.
void tobj_writer_refuse(uint32_t why){auto& e=tobj_writer.evidence;if(!e.reason)e.reason=why;e.status=2;e.complete=0;}
bool tobj_writer_owner_live(){const auto& e=tobj_writer.evidence;
 return tobj_writer.pre&&active&&healthy&&owner_epoch==e.epoch&&world_generation==e.world&&
  topology_revision==e.topology_revision&&e.session==tobj_writer_session&&
  payload_snapshot_check(tobj_writer.pre)==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;
}
bool tobj_writer_row(uintptr_t field,size_t& index,const HostAllocation*& row){
 if(!tobj_writer_owner_live())return false;
 if(field>UINTPTR_MAX-48)return false;
 const auto* rows=payload_rows(tobj_writer.pre);index=SIZE_MAX;row=nullptr;
 for(size_t i=0;i<allocations.size();++i){const auto& a=allocations[i];
  if(field<a.host||field-a.host>a.requested||48>a.requested-(field-a.host))continue;
  if(index!=SIZE_MAX)return false;
  if(i>=tobj_writer.pre->row_count)return false;const auto& p=rows[i];
  if(a.host!=p.host||a.source!=p.source||a.requested!=p.requested||a.generation!=p.generation)return false;
  index=i;row=&a;
 }return row&&index<=UINT32_MAX;
}
// Read the immutable-at-arm table only under the already-reviewed historical
// arm/frozen conditions; never dereference a live class descriptor/header.
bool tobj_writer_pre_piece(MeleeWebTObjWriterEvent& e){
 const auto& a=tobj_writer.anchor;
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
 if(!c.generation||requested!=172||off!=116||48>requested-off||(c.tag&3)!=2||!(c.tag&~3u)||
    e.object!=e.host+(start-begin)||physical<requested||physical-requested>31)return false;
 size_t saved=0;const auto* rows=payload_rows(tobj_writer.pre);
 for(size_t i=0;i<e.pre_row;++i)if(!add_size(saved,rows[i].requested,&saved))return false;
 if(!add_size(saved,size_t(start-begin),&saved)||tobj_writer.pre->payload_bytes<4||saved>tobj_writer.pre->payload_bytes-4)return false;
 uint32_t header=0;std::memcpy(&header,payload_bytes(tobj_writer.pre)+saved,4);
 if(header!=(c.tag&~3u)||header!=tobj_writer.evidence.canonical_class)return false;
 e.piece_source=start;e.piece_offset=off;e.physical=physical;e.original_requested=requested;
 e.piece_generation=c.generation;e.class_info=c.tag&~3u;e.saved_class_info=header;
 e.flags=3; // CAPTURE_TIME_ONLY + live piece lifetime UNKNOWN, even if matched.
 return true;
}
#endif
}
extern "C" uint64_t melee_web_tobj_writer_arm(const MeleeWebSourceMemoryPayloadSnapshot* pre,MeleeWebClassPieceAnchor a,uint32_t command,uint32_t pre_frame){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(tobj_writer.pre||tobj_writer_session>=UINT64_MAX/256||payload_snapshot_check(pre)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)return 0;
 tobj_writer={};tobj_writer.pre=pre;tobj_writer.anchor=a;tobj_writer.command=command;
 auto& e=tobj_writer.evidence;e.schema=1;e.bytes=sizeof(e);e.hook_active=1;e.epoch=owner_epoch;e.world=world_generation;
 e.session=++tobj_writer_session;e.piece_instance=a.instance;e.capture_serial=a.serial;e.topology_revision=topology_revision;
 e.post_count=e.drawn_count=e.matched_index=UINT32_MAX;e.site_kind=1;e.pre_frame=pre_frame;return e.session;
#else
 (void)pre;(void)a;(void)command;(void)pre_frame;return 0;
#endif
}
extern "C" void melee_web_tobj_writer_phase(uint32_t command){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(tobj_writer.pre){tobj_writer.command=command;tobj_writer.outer_phase=0;tobj_writer.outer_frame=0;}
#else
 (void)command;
#endif
}
extern "C" void melee_web_tobj_writer_boundary(uint32_t phase,uint32_t frame){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!tobj_writer.pre)return;
 if(!phase){tobj_writer.outer_phase=0;tobj_writer.outer_frame=0;return;}
 auto& e=tobj_writer.evidence;
 if(!tobj_writer_owner_live()||tobj_writer.busy||phase>2||tobj_writer.outer_epoch==UINT64_MAX||
    e.pre_frame>UINT32_MAX-2||tobj_writer.command!=8+phase||frame!=e.pre_frame+phase||
    tobj_writer.outer_epoch+1!=phase){tobj_writer_refuse(11);return;}
 tobj_writer.outer_phase=phase;tobj_writer.outer_frame=frame;++tobj_writer.outer_epoch;
#else
 (void)phase;(void)frame;
#endif
}
extern "C" void melee_web_tobj_writer_mark(uint64_t session,uint32_t endpoint){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& e=tobj_writer.evidence;if(!session||session!=e.session)return;
 if(!tobj_writer_owner_live()||tobj_writer.busy){tobj_writer_refuse(4);return;}
 if(endpoint==1&&e.post_count==UINT32_MAX)e.post_count=e.count;
 else if(endpoint==2&&e.drawn_count==UINT32_MAX&&e.post_count!=UINT32_MAX)e.drawn_count=e.count;
 else tobj_writer_refuse(2);
#else
 (void)session;(void)endpoint;
#endif
}
extern "C" void melee_web_tobj_writer_disarm(uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(session&&session==tobj_writer.evidence.session){tobj_writer.pre=nullptr;tobj_writer.busy=0;}
#else
 (void)session;
#endif
}
extern "C" uint64_t melee_web_tobj_writer_before(const void* object,const void* field,uint32_t bytes,const void* canonical_info,const char* file,uint32_t line){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!tobj_writer.pre)return 0;auto& e=tobj_writer.evidence;
 if(e.seen==UINT32_MAX){tobj_writer_refuse(3);e.overflow=1;return 0;}++e.seen;
 if(e.count==MELEE_WEB_TOBJ_WRITER_CAPACITY){e.overflow=1;tobj_writer_refuse(3);return 0;}
 if(tobj_writer.busy){tobj_writer_refuse(2);return 0;}
 if(!tobj_writer_owner_live()){tobj_writer_refuse(4);return 0;}
 const auto p=reinterpret_cast<uintptr_t>(object),f=reinterpret_cast<uintptr_t>(field);
 if(!p||p>UINTPTR_MAX-116||f!=p+116||bytes!=172){tobj_writer_refuse(5);return 0;}
 size_t index;const HostAllocation* row=nullptr;
 if(!tobj_writer_row(f,index,row)||p<row->host||p-row->host>row->requested||bytes>row->requested-(p-row->host)){tobj_writer_refuse(5);return 0;}
 if(!file||!line){tobj_writer_refuse(9);return 0;}size_t length=0;while(length<sizeof(e.path)&&file[length])++length;
 if(!length||length==sizeof(e.path)){tobj_writer_refuse(9);return 0;}
 if(!e.path_bytes){std::memcpy(e.path,file,length+1);e.path_bytes=length;e.line=line;}
 else if(e.path_bytes!=length||e.line!=line||std::memcmp(e.path,file,length)){tobj_writer_refuse(9);return 0;}
 auto& r=e.events[e.count];r.sequence=++e.count;r.command=tobj_writer.command;r.pre_row=index;r.source=row->source.value();r.requested=row->requested;
 r.field_offset=f-row->host;r.host=row->host;r.generation=row->generation;r.object=p;r.field=f;r.flags=2;
 const auto canonical=reinterpret_cast<uintptr_t>(canonical_info);
 if(!canonical||canonical>UINT32_MAX||(canonical&3)){r.reason=12;}
 else if(e.canonical_class&&e.canonical_class!=canonical){r.reason=12;}
 else {e.canonical_class=canonical;uint32_t current=0;std::memcpy(&current,object,4);if(current!=canonical)r.reason=12;}
 if(!r.reason&&!tobj_writer_pre_piece(r))r.reason=6;
 r.outer_frame=tobj_writer.outer_frame;r.outer_phase=tobj_writer.outer_phase;r.outer_epoch=tobj_writer.outer_epoch;
 if(!r.reason&&(!r.outer_phase||r.outer_phase>2||r.command!=8+r.outer_phase||r.outer_epoch!=r.outer_phase||
    e.pre_frame>UINT32_MAX-2||r.outer_frame!=e.pre_frame+r.outer_phase))r.reason=11;
 std::memcpy(&r.flags_before,reinterpret_cast<const void*>(p+76),4);
 std::memcpy(r.before,field,48);tobj_writer.busy=r.sequence;
 return (e.session<<8)|r.sequence;
#else
 (void)object;(void)field;(void)bytes;(void)canonical_info;(void)file;(void)line;return 0;
#endif
}
extern "C" void melee_web_tobj_writer_after(uint64_t ticket){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!ticket)return;auto& e=tobj_writer.evidence;const uint32_t n=ticket&255;
 if((ticket>>8)!=e.session||!n||n>e.count||tobj_writer.busy!=n){tobj_writer_refuse(2);return;}
 auto& r=e.events[n-1];tobj_writer.busy=0;size_t index;const HostAllocation* row=nullptr;
 if(!tobj_writer_row(r.field,index,row)||index!=r.pre_row||row->host!=r.host||row->generation!=r.generation){r.status=2;r.reason=4;tobj_writer_refuse(4);return;}
 std::memcpy(r.after,reinterpret_cast<const void*>(uintptr_t(r.field)),48);
 std::memcpy(&r.flags_after,reinterpret_cast<const void*>(uintptr_t(r.object)+76),4);
 uint32_t current=0;std::memcpy(&current,reinterpret_cast<const void*>(uintptr_t(r.object)),4);
 if(!r.reason&&current!=e.canonical_class)r.reason=12;
 if(!r.reason&&(r.outer_phase!=tobj_writer.outer_phase||r.outer_frame!=tobj_writer.outer_frame||r.outer_epoch!=tobj_writer.outer_epoch))r.reason=11;
 r.status=r.reason?2:1;
#else
 (void)ticket;
#endif
}
extern "C" void melee_web_tobj_writer_finalize(const MeleeWebSourceMemoryPayloadSnapshot* pre,const MeleeWebSourceMemoryPayloadSnapshot* post,MeleeWebSourcePayloadComparison original,MeleeWebClassPieceAnchor a,uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& e=tobj_writer.evidence;if(!session||session!=e.session)return;
 const auto w=melee_web_source_memory_payload_snapshot_witness(post);
 if(original.status!=2||w.status!=2||original.allocation_index!=w.row||original.byte_offset!=w.offset||original.host_address!=w.host_address||
    original.source_address!=w.source_address||original.requested_bytes!=w.requested_bytes||w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
    original.expected_byte!=w.expected[w.offset-w.window_offset]||original.actual_byte!=w.actual[w.offset-w.window_offset]){tobj_writer_refuse(10);return;}
 e.first_difference_matches=1;e.row=w.row;e.offset=w.offset;e.source=w.source_address;e.requested=w.requested_bytes;e.expected=original.expected_byte;e.actual=original.actual_byte;e.host=w.host_address;e.generation=w.allocation_generation;
 if(pre!=tobj_writer.pre||!tobj_writer_owner_live()||payload_snapshot_check(pre)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||payload_snapshot_check(post)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||
    a.epoch!=tobj_writer.anchor.epoch||a.world!=tobj_writer.anchor.world||a.instance!=tobj_writer.anchor.instance||a.serial!=tobj_writer.anchor.serial){tobj_writer_refuse(4);return;}
 if(e.reason||e.overflow||tobj_writer.busy||e.seen!=e.count){if(!e.reason)tobj_writer_refuse(2);return;}
 if(e.post_count==UINT32_MAX||e.post_count>e.count||e.drawn_count==UINT32_MAX||e.drawn_count<e.post_count||e.drawn_count>e.count){tobj_writer_refuse(8);return;}
 const auto* rows=payload_rows(post);size_t post_start=0;
 for(size_t i=0;i<w.row;++i)if(!add_size(post_start,rows[i].requested,&post_start)){tobj_writer_refuse(5);return;}
 for(uint32_t i=0;i<e.post_count;++i){const auto& r=e.events[i];
  if(r.status!=1||r.reason||r.flags!=3||r.outer_phase!=1||r.outer_epoch!=1||r.command!=9||e.pre_frame==UINT32_MAX||r.outer_frame!=e.pre_frame+1||r.class_info!=e.canonical_class||r.pre_row>=pre->row_count||r.pre_row!=w.row||r.host!=w.host_address||r.generation!=w.allocation_generation||r.source!=w.source_address||r.requested!=w.requested_bytes||
     w.offset<r.field_offset||w.offset-r.field_offset>=48)continue;
  const auto& prior=payload_rows(pre)[r.pre_row];const auto& expected=rows[w.row];
  if(prior.host!=expected.host||prior.source!=expected.source||prior.requested!=expected.requested||prior.generation!=expected.generation)continue;
  size_t saved;if(!add_size(post_start,r.field_offset,&saved)||post->payload_bytes<48||saved>post->payload_bytes-48)continue;
  if(std::memcmp(payload_bytes(post)+saved,r.after,48))continue;
  // Full actual POST48 equality above remains native authority. Capture-time join only. Even a matching writer is not live class ownership.
  ++e.matched_count;e.matched_index=i;
 }
 e.complete=1;
 if(e.matched_count){e.status=1;e.reason=0;}else {e.status=2;e.reason=7;}
#else
 (void)pre;(void)post;(void)original;(void)a;(void)session;
#endif
}
extern "C" const MeleeWebTObjWriterEvidence* melee_web_tobj_writer_evidence(uint64_t session){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 return session&&session==tobj_writer.evidence.session?&tobj_writer.evidence:nullptr;
#else
 (void)session;return nullptr;
#endif
}
