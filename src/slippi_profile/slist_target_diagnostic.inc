/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Temporary exact-row SList observer. It has no source-write or restore role. */
namespace {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
constexpr uint32_t slist_event_limit=32, slist_membership_limit=8, slist_walk_limit=512;
struct SListTraceEvent {
 uint32_t op,command,scope,frame,sample,changed,used_before,free_before,used_after,free_after;
 uintptr_t descriptor,node0,node1,input;
 uint32_t before[2],after[2];
};
struct SListMembership {
 uint32_t boundary,command,scope,frame,sample,current_status,free_status,current_count,free_count,used,free;
 uintptr_t descriptor,head,freehead;
};
struct SListTargetTrace {
 uint32_t state=0,arm_status=0,closed=0,overflow=0,refusal=0;
 uint32_t count=0,membership_count=0,membership_overflow=0,descriptor_set=0,canonical_verified=0;
 PoolAssociation retained_pool{};
 uint32_t stack_status=0,stack_bytes=0,stack_event=UINT32_MAX;
 uint32_t command=0,scope=0,frame=0,sample=0;
 uint32_t row=0,source=0,extent=0,offset=0;
 uintptr_t host=0,descriptor=0;
 uint64_t generation=0,epoch=0,world=0;
 char stack[2048]{};
 SListTraceEvent events[slist_event_limit]{};
 SListMembership memberships[slist_membership_limit]{};
} slist_trace;

bool slist_target_row_valid() {
 if(!slist_trace.state||!active||!healthy||owner_epoch!=slist_trace.epoch||
    world_generation!=slist_trace.world||slist_trace.row>=allocations.size())return false;
 const auto& row=allocations[slist_trace.row];
 return row.host==slist_trace.host&&row.source.value()==slist_trace.source&&
  row.requested==slist_trace.extent&&row.generation==slist_trace.generation&&
  slist_trace.offset<=row.requested&&8<=row.requested-slist_trace.offset;
}
bool slist_target_pool_valid(bool allow_retained) {
 const auto& t=slist_trace;
 if(!slist_target_row_valid()||!t.descriptor_set||!t.canonical_verified)return false;
 const auto& row=allocations[t.row];const auto& p=row.pool;
 if(!p.state)return allow_retained; // retained proof only for this exact captured row
 return p.state==1&&p.descriptor==t.descriptor&&p.generation==t.generation&&
  p.epoch==t.epoch&&p.world==t.world&&p.stride==8&&p.count==1&&
  p.alignment_mask==3&&p.pool_bytes==8;
}
bool slist_read_target(uint32_t out[2]) {
 if(!slist_target_pool_valid(true))return false;
 std::memcpy(out,reinterpret_cast<const void*>(slist_trace.host+slist_trace.offset),8);
 return true;
}
bool slist_pool_cell(uintptr_t pointer) {
 if(!pointer||!slist_trace.descriptor_set)return false;
 unsigned matches=0;
 for(const auto& row:allocations){
  if(row.pool.state!=1||row.pool.descriptor!=slist_trace.descriptor)continue;
  if(row.pool.generation!=row.generation||row.pool.epoch!=owner_epoch||
     row.pool.world!=world_generation||row.pool.stride!=8||row.pool.alignment_mask!=3||
     !row.pool.count||uint64_t(row.pool.count)*8!=row.pool.pool_bytes||
     row.requested!=row.pool.pool_bytes||pointer<row.host||pointer-row.host>=row.requested)continue;
  const uintptr_t offset=pointer-row.host;
  if((offset&7)||row.requested-offset<8)continue;
  ++matches;
 }
 return matches==1;
}
uint32_t slist_walk(uintptr_t head,uint32_t bound,bool free_chain,bool& found) {
 found=false;
 if(!slist_trace.descriptor_set||bound>slist_walk_limit)return UINT32_MAX;
 uintptr_t seen[slist_walk_limit]{};uint32_t count=0;
 uintptr_t node=head;
 while(node){
  if(count>=bound||count>=slist_walk_limit||!slist_pool_cell(node))return UINT32_MAX;
  for(uint32_t i=0;i<count;++i)if(seen[i]==node)return UINT32_MAX;
  seen[count++]=node;
  if(node==slist_trace.host+slist_trace.offset)found=true;
  uintptr_t next=0;std::memcpy(&next,reinterpret_cast<const void*>(node),sizeof(next));
  node=next;
 }
 if(free_chain&&count!=bound)return UINT32_MAX;
 return count;
}
void slist_stack_capture() {
 slist_trace.stack_status=0;slist_trace.stack_bytes=0;slist_trace.stack[0]=0;
#if defined(__EMSCRIPTEN__)
 int n=emscripten_get_callstack(EM_LOG_C_STACK,slist_trace.stack,sizeof(slist_trace.stack));
 if(n<0){slist_trace.stack_status=3;slist_trace.stack[0]=0;return;}
 slist_trace.stack[sizeof(slist_trace.stack)-1]=0;
 slist_trace.stack_bytes=static_cast<uint32_t>(std::strlen(slist_trace.stack));
 slist_trace.stack_status=n>=static_cast<int>(sizeof(slist_trace.stack))?2u:(n&&slist_trace.stack_bytes?1u:0u);
#endif
}
const char* slist_op_name(uint32_t op) {
 switch(op){case MELEE_WEB_SLIST_TRACE_ALLOC_ZERO:return "ALLOC_ZERO";
 case MELEE_WEB_SLIST_TRACE_ALLOC_APPEND_DATA:return "ALLOC_APPEND_DATA";
 case MELEE_WEB_SLIST_TRACE_ALLOC_PREPEND_DATA:return "ALLOC_PREPEND_DATA";
 case MELEE_WEB_SLIST_TRACE_APPEND_LIST:return "APPEND_LIST";
 case MELEE_WEB_SLIST_TRACE_PREPEND_LIST:return "PREPEND_LIST";
 case MELEE_WEB_SLIST_TRACE_REMOVE:return "REMOVE";
 case MELEE_WEB_SLIST_TRACE_OBJALLOC:return "OBJALLOC";
 case MELEE_WEB_SLIST_TRACE_OBJFREE:return "OBJFREE";default:return "UNKNOWN";}
}
void slist_escape(const char* in,char* out,size_t cap,bool& truncated) {
 size_t n=0;truncated=false;
 for(size_t i=0;in[i];++i){unsigned char c=in[i];const char* e=nullptr;
  if(c=='"')e="\\\"";else if(c=='\\')e="\\\\";else if(c=='\n')e="\\n";
  else if(c=='\r')e="\\r";else if(c=='\t')e="\\t";
  const size_t need=e?2u:c<32?6u:1u;if(n+need>=cap){truncated=true;break;}
  if(e){out[n++]=e[0];out[n++]=e[1];}else if(c<32){std::snprintf(out+n,7,"\\u%04x",unsigned(c));n+=6;}else out[n++]=c;
 }
 out[n]=0;
}
void slist_target_emit() {
 auto& t=slist_trace;
 const bool joined=piece_slab_trace.joined!=0;
 uint32_t boundary_mask=0;bool membership_known=true;
 for(uint32_t i=0;i<t.membership_count;++i){
  const auto& m=t.memberships[i];
  if(m.boundary>=MELEE_WEB_SLIST_TRACE_CAPTURE&&m.boundary<=MELEE_WEB_SLIST_TRACE_REPLAY_FINAL)
   boundary_mask|=1u<<(m.boundary-1);
  if(m.current_status>1||m.free_status>1)membership_known=false;
 }
 const bool complete=t.arm_status==1&&t.closed&&joined&&!t.overflow&&!t.membership_overflow&&
     !piece_slab_trace.overflow&&!t.refusal&&membership_known&&boundary_mask==0x1fu;
 std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"slist_summary\",\"schema\":1,\"arm_status\":%u,\"close_status\":%u,\"joined\":%s,\"complete\":%s,\"event_coverage_complete\":%s,\"membership_known\":%s,\"boundary_mask\":%u,\"overflow\":%s,\"membership_overflow\":%s,\"refusal\":%u,\"event_count\":%u,\"event_limit\":%u,\"membership_count\":%u,\"membership_limit\":%u,\"row\":%u,\"source\":\"0x%08x\",\"extent\":%u,\"offset\":%u,\"host\":\"0x%016llx\",\"generation\":\"0x%016llx\",\"epoch\":\"0x%016llx\",\"world\":\"0x%016llx\",\"descriptor\":\"0x%016llx\",\"stack_event\":%u,\"stack_status\":%u,\"stack_bytes\":%u}\n",
  t.arm_status,t.closed,joined?"true":"false",complete?"true":"false",
  (t.arm_status==1&&t.closed&&joined&&!t.overflow&&!t.refusal)?"true":"false",membership_known?"true":"false",boundary_mask,t.overflow?"true":"false",
  t.membership_overflow?"true":"false",t.refusal,t.count,slist_event_limit,t.membership_count,
  slist_membership_limit,t.row,t.source,t.extent,t.offset,static_cast<unsigned long long>(t.host),
  static_cast<unsigned long long>(t.generation),static_cast<unsigned long long>(t.epoch),
  static_cast<unsigned long long>(t.world),static_cast<unsigned long long>(t.descriptor),t.stack_event,t.stack_status,t.stack_bytes);
 for(uint32_t base=0;base<t.count;base+=4){
  const uint32_t end=std::min(base+4,t.count);
  std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"slist_events\",\"first\":%u,\"events\":[",base);
  for(uint32_t i=base;i<end;++i){const auto& e=t.events[i];
   char used_before[16]="null",free_before[16]="null";
   if(e.op==MELEE_WEB_SLIST_TRACE_OBJALLOC||e.op==MELEE_WEB_SLIST_TRACE_OBJFREE){
    std::snprintf(used_before,sizeof(used_before),"%u",e.used_before);
    std::snprintf(free_before,sizeof(free_before),"%u",e.free_before);
   }
   std::printf("%s{\"op\":\"%s\",\"command\":%u,\"scope\":%u,\"frame\":%u,\"sample\":%u,\"changed\":%s,\"descriptor\":\"0x%016llx\",\"node0\":\"0x%016llx\",\"node1\":\"0x%016llx\",\"input\":\"0x%016llx\",\"before\":[\"%08x\",\"%08x\"],\"after\":[\"%08x\",\"%08x\"],\"used_free\":[%s,%s,%u,%u]}",
    i==base?"":",",slist_op_name(e.op),e.command,e.scope,e.frame,e.sample,e.changed?"true":"false",
    static_cast<unsigned long long>(e.descriptor),static_cast<unsigned long long>(e.node0),static_cast<unsigned long long>(e.node1),static_cast<unsigned long long>(e.input),
    e.before[0],e.before[1],e.after[0],e.after[1],used_before,free_before,e.used_after,e.free_after);
  }
  std::printf("]}\n");
 }
 for(uint32_t i=0;i<t.membership_count;++i){const auto& m=t.memberships[i];
  std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"slist_membership\",\"boundary\":%u,\"command\":%u,\"scope\":%u,\"frame\":%u,\"sample\":%u,\"descriptor\":\"0x%016llx\",\"head\":\"0x%016llx\",\"freehead\":\"0x%016llx\",\"used\":%u,\"free\":%u,\"current_status\":%u,\"current_count\":%u,\"target_current\":%s,\"free_status\":%u,\"free_count\":%u,\"target_free\":%s}\n",
   m.boundary,m.command,m.scope,m.frame,m.sample,static_cast<unsigned long long>(m.descriptor),static_cast<unsigned long long>(m.head),
   static_cast<unsigned long long>(m.freehead),m.used,m.free,m.current_status,m.current_count,
   m.current_status==1?"true":m.current_status==0?"false":"null",m.free_status,m.free_count,
   m.free_status==1?"true":m.free_status==0?"false":"null");
 }
 if(t.stack_event!=UINT32_MAX){char escaped[2048];bool clipped=false;slist_escape(t.stack,escaped,sizeof(escaped),clipped);
  std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"slist_stack\",\"event\":%u,\"status\":%u,\"bytes\":%u,\"json_truncated\":%s,\"stack\":\"%s\"}\n",
    t.stack_event,t.stack_status,t.stack_bytes,clipped?"true":"false",escaped);
 }
 std::fflush(stdout);
}
#endif
}

extern "C" int melee_web_source_memory_slist_target_trace_arm(
 const MeleeWebSourceMemoryPayloadSnapshot* snapshot,MeleeWebClassPieceAnchor anchor,
 uint32_t source,uint32_t extent,uint32_t offset) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 const uint32_t command=piece_slab_trace.command,scope=piece_slab_trace.scope;
 const uint32_t frame=piece_slab_trace.frame,sample=piece_slab_trace.sample;
 slist_trace=SListTargetTrace{};slist_trace.command=command;slist_trace.scope=scope;
 slist_trace.frame=frame;slist_trace.sample=sample;slist_trace.arm_status=2;slist_trace.state=1;
 if(sizeof(uintptr_t)!=4||extent!=8||offset||!melee_web_source_memory_class_piece_slab_trace_arm(snapshot,anchor,source,extent,offset))return 0;
 const auto& p=piece_slab_trace;
 slist_trace.row=p.row;slist_trace.source=p.source;slist_trace.extent=p.requested;
 slist_trace.offset=p.target_offset;slist_trace.host=p.host;slist_trace.generation=p.generation;
 slist_trace.epoch=p.epoch;slist_trace.world=p.world;
 const auto& pool=allocations[p.row].pool;
 if(pool_refusal||pool.state!=1||!pool.descriptor||pool.stride!=8||pool.count!=1||pool.pool_bytes!=8||
    pool.alignment_mask!=3||pool.generation!=p.generation||pool.epoch!=p.epoch||pool.world!=p.world)return 0;
 slist_trace.retained_pool=pool;slist_trace.descriptor=pool.descriptor;slist_trace.descriptor_set=1;
 slist_trace.arm_status=1;slist_trace.state=1;
 return 1;
#else
 (void)snapshot;(void)anchor;(void)source;(void)extent;(void)offset;return 0;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_context(uint32_t command,uint32_t scope,uint32_t frame,uint32_t sample) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 slist_trace.command=command;slist_trace.scope=scope;slist_trace.frame=frame;slist_trace.sample=sample;
#else
 (void)command;(void)scope;(void)frame;(void)sample;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_before(uint32_t op,const void* descriptor,
 const void* node0,const void* node1,const void* input,MeleeWebSListTraceTicket* ticket) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!ticket)return;*ticket={};auto& t=slist_trace;
 if(!t.state||t.closed||t.arm_status!=1||!slist_target_row_valid())return;
 const uintptr_t d=reinterpret_cast<uintptr_t>(descriptor),n0=reinterpret_cast<uintptr_t>(node0),n1=reinterpret_cast<uintptr_t>(node1);
 if(!d||(!n0&&!n1)||(n0!=t.host+t.offset&&n1!=t.host+t.offset))return;
 if(!t.descriptor_set||t.descriptor!=d){t.refusal=1;return;}
 if(!slist_target_pool_valid(true)){t.refusal=2;return;}
 ticket->valid=1;ticket->operation=op;ticket->operand_mask=(n0==t.host+t.offset?1u:0u)|(n1==t.host+t.offset?2u:0u);
 ticket->command=t.command;ticket->scope=t.scope;ticket->frame=t.frame;ticket->sample=t.sample;
 ticket->descriptor=d;ticket->node0=n0;ticket->node1=n1;ticket->input=reinterpret_cast<uintptr_t>(input);
 (void)slist_read_target(ticket->before);
#else
 (void)op;(void)descriptor;(void)node0;(void)node1;(void)input;(void)ticket;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_after(const MeleeWebSListTraceTicket* ticket,uint32_t used,uint32_t free) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!ticket||!ticket->valid)return;auto& t=slist_trace;uint32_t after[2]{};
 if(!slist_target_row_valid()||!slist_read_target(after)){t.refusal=2;return;}
 if(t.count==slist_event_limit){t.overflow=1;return;}
 const bool changed=std::memcmp(ticket->before,after,8)!=0;auto& e=t.events[t.count];
 e={ticket->operation,ticket->command,ticket->scope,ticket->frame,ticket->sample,changed?1u:0u,
   ticket->used_before,ticket->free_before,used,free,ticket->descriptor,ticket->node0,ticket->node1,ticket->input,
   {ticket->before[0],ticket->before[1]},{after[0],after[1]}};
 if(changed&&t.stack_event==UINT32_MAX){t.stack_event=t.count;slist_stack_capture();}
 ++t.count;
#else
 (void)ticket;(void)used;(void)free;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_allocator_before(uint32_t op,const void* descriptor,
 const void* node,const void* freehead,uint32_t used,uint32_t free,MeleeWebSListTraceTicket* ticket) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 melee_web_source_memory_slist_trace_before(op,descriptor,node,freehead,nullptr,ticket);
 if(ticket&&ticket->valid){ticket->used_before=used;ticket->free_before=free;}
#else
 (void)op;(void)descriptor;(void)node;(void)freehead;(void)used;(void)free;(void)ticket;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_allocator_after(const MeleeWebSListTraceTicket* ticket,
 const void* descriptor,const void* node,const void* freehead,uint32_t used,uint32_t free) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(ticket&&ticket->valid){
  if(reinterpret_cast<uintptr_t>(descriptor)!=ticket->descriptor){slist_trace.refusal=1;return;}
  if(ticket->operation==MELEE_WEB_SLIST_TRACE_OBJALLOC&&reinterpret_cast<uintptr_t>(node)!=slist_trace.host+slist_trace.offset&&
     reinterpret_cast<uintptr_t>(freehead)!=slist_trace.host+slist_trace.offset)return;
  melee_web_source_memory_slist_trace_after(ticket,used,free);
 }
#else
 (void)ticket;(void)descriptor;(void)node;(void)freehead;(void)used;(void)free;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_membership(uint32_t boundary,const void* head,
 const void* descriptor,const void* freehead,uint32_t used,uint32_t free) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& t=slist_trace;if(!t.state||t.closed)return;
 if(t.membership_count==slist_membership_limit){t.membership_overflow=1;return;}
 SListMembership m{};m.boundary=boundary;m.command=t.command;m.scope=t.scope;m.frame=t.frame;m.sample=t.sample;
 m.descriptor=reinterpret_cast<uintptr_t>(descriptor);m.head=reinterpret_cast<uintptr_t>(head);
 m.freehead=reinterpret_cast<uintptr_t>(freehead);m.used=used;m.free=free;
 if(!slist_target_row_valid()||!descriptor||!active||!healthy||owner_epoch!=t.epoch||world_generation!=t.world){
  m.current_status=m.free_status=2;t.memberships[t.membership_count++]=m;return;}
 const uintptr_t d=reinterpret_cast<uintptr_t>(descriptor);
 if(t.arm_status!=1||!t.descriptor_set||t.descriptor!=d){t.refusal=1;m.current_status=m.free_status=2;t.memberships[t.membership_count++]=m;return;}
 // This provider is called by original lobj.c with HSD_SListGetAllocData().
 // It authenticates the retained allocator event to the canonical descriptor.
 if(boundary==MELEE_WEB_SLIST_TRACE_CAPTURE)t.canonical_verified=1;
 uint64_t pool_cells=0;bool pool_ok=pool_refusal==0;
 for(const auto& row:allocations)if(row.pool.state==1&&row.pool.descriptor==d){
  if(row.pool.generation!=row.generation||row.pool.epoch!=t.epoch||row.pool.world!=t.world||
     row.pool.stride!=8||row.pool.alignment_mask!=3||uint64_t(row.pool.count)*8!=row.requested||row.requested!=row.pool.pool_bytes){pool_ok=false;break;}
  pool_cells+=row.pool.count;
 }
 if(!pool_ok||pool_cells!=uint64_t(used)+free||!slist_pool_cell(t.host+t.offset)){
  m.current_status=m.free_status=2;t.memberships[t.membership_count++]=m;return;}
 bool current=false,free_target=false;uint32_t current_count=slist_walk(m.head,used,false,current);
 uint32_t free_count=slist_walk(m.freehead,free,true,free_target);
 if(current_count==UINT32_MAX||free_count==UINT32_MAX||(current&&free_target)){
  m.current_status=m.free_status=2;
 }else{
  m.current_status=current?1u:0u;m.free_status=free_target?1u:0u;
  m.current_count=current_count;m.free_count=free_count;
 }
 t.memberships[t.membership_count++]=m;
#else
 (void)boundary;(void)head;(void)descriptor;(void)freehead;(void)used;(void)free;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_early_close(void) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(slist_trace.state&&!slist_trace.closed){slist_trace.closed=1;slist_trace.refusal=3;}
#endif
}
extern "C" int melee_web_source_memory_slist_trace_active(void) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 return slist_trace.state!=0;
#else
 return 0;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_finalize(void) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(slist_trace.state)slist_trace.closed=1;
#endif
}
extern "C" void melee_web_source_memory_slist_trace_emit(void) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(slist_trace.state)slist_target_emit();
#endif
}
