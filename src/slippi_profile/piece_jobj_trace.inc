/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Temporary exact-target JObj diagnostic. No ownership or restore authority. */
namespace {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED

#if UINTPTR_MAX==UINT32_MAX
static_assert(sizeof(HSD_JObj)==0x88 && offsetof(HSD_JObj,mtx)==0x44,
              "diagnostic requires actual Wasm32 JObj layout");
#endif
void piece_jobj_stack(char* stack,uint32_t& status,uint32_t& bytes) {
 status=0;bytes=0;stack[0]=0;
#if defined(__EMSCRIPTEN__)
 int n=emscripten_get_callstack(EM_LOG_C_STACK,stack,2048);
 if(n<0){status=3;stack[0]=0;return;}
 stack[2047]=0;bytes=static_cast<uint32_t>(std::strlen(stack));
 if(!n||!bytes){status=0;return;}
 status=n>=2048?2u:1u;
#endif
}
// Events already contain actual row identities. Never infer a live incarnation
// from the separate frozen PieceOwner table or just a reused object pointer.
bool piece_jobj_event_matches(const PieceSlabTraceEvent& e) {
 const auto& t=piece_slab_trace;
 if(sizeof(HSD_JObj)!=0x88||offsetof(HSD_JObj,mtx)!=0x44)return false;
 return e.row==t.row&&e.row_source==t.source&&e.row_requested==t.requested&&
  e.row_host==t.host&&e.generation==t.generation&&e.epoch==t.epoch&&e.world==t.world&&
  e.inside_row&&e.requested==sizeof(HSD_JObj)&&
  uint64_t(e.event_source)+offsetof(HSD_JObj,mtx)==t.target_source;
}
int piece_jobj_latest_allocation() {
 const auto& t=piece_slab_trace;
 for(uint32_t i=t.count;i;){const auto& e=t.events[--i];
  if(e.kind==3||e.kind==2)return -1;
  if(e.kind==1)return piece_jobj_event_matches(e)?static_cast<int>(i):-1;
 }
 return -1;
}
void piece_jobj_creator(uint32_t index) {
 auto& t=piece_slab_trace;const auto& e=t.events[index];
 if(e.class_info!=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&hsdJObj.parent.parent))||
    !piece_jobj_event_matches(e))return;
 const int alloc=piece_jobj_latest_allocation();
 if(alloc<0){t.jobj_refused=1;return;}
 for(uint32_t i=0;i<t.jobj_count;++i)if(t.jobjs[i].allocation_event==uint32_t(alloc))return;
 if(t.jobj_count==2){t.jobj_overflow=1;return;}
 auto& c=t.jobjs[t.jobj_count++];c.allocation_event=alloc;c.label_event=index;
 // Stage1 is before original class initialization: read no object bytes here.
 piece_jobj_stack(c.stack,c.stack_status,c.stack_bytes);
}
bool piece_jobj_live(void* object,uint32_t creator) {
 const auto& t=piece_slab_trace;
 if(t.state!=2||t.closed||t.overflow||creator>=t.jobj_count||!active||!healthy||
    owner_epoch!=t.epoch||world_generation!=t.world)return false;
 const auto& c=t.jobjs[creator];const int alloc=piece_jobj_latest_allocation();
 if(alloc<0||uint32_t(alloc)!=c.allocation_event)return false;
 const auto& e=t.events[alloc];const uintptr_t p=reinterpret_cast<uintptr_t>(object);
 if(p!=e.event_host)return false;
 const auto* row=find_host_allocation(p);
 if(!row||row->host!=t.host||row->requested!=t.requested||row->source.value()!=t.source||
    row->generation!=t.generation||p<row->host||p-row->host>row->requested||
    sizeof(HSD_JObj)>row->requested-(p-row->host))return false;
 bool initialized=false;
 for(uint32_t i=c.label_event+1;i<t.count;++i){const auto& label=t.events[i];
  if(label.kind==5&&label.stage==2&&label.event_host==p&&piece_jobj_event_matches(label)&&
     label.class_info==static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&hsdJObj.parent.parent)))initialized=true;
 }
 if(!initialized)return false;
 // Full containing extent and actual constructor are checked before this read.
 return static_cast<HSD_JObj*>(object)->object.parent.class_info==&hsdJObj.parent.parent;
}
// JSON escaped stack text is separately capped so every emitted line is below
// 4096 bytes, even if the original stack contains control characters.
void piece_jobj_escape(const char* input,char (&out)[2049],bool& truncated) {
 size_t n=0;truncated=false;
 for(size_t i=0;input[i];++i){unsigned char c=input[i];const char* escape=nullptr;
  if(c=='"')escape="\\\"";else if(c=='\\')escape="\\\\";else if(c=='\n')escape="\\n";
  else if(c=='\r')escape="\\r";else if(c=='\t')escape="\\t";
  const size_t need=escape?2u:c<32?6u:1u;
  if(n+need>2048){truncated=true;break;}
  if(escape){out[n++]=escape[0];out[n++]=escape[1];}
  else if(c<32){std::snprintf(out+n,7,"\\u%04x",unsigned(c));n+=6;}
  else out[n++]=c;
 }
 out[n]=0;
}
void piece_jobj_trace_emit() {
 const auto& t=piece_slab_trace;uint32_t writers=0;
 for(uint32_t i=0;i<t.jobj_count;++i){const auto& c=t.jobjs[i];const auto& e=t.events[c.allocation_event];
  char escaped[2049];bool clipped=false;piece_jobj_escape(c.stack,escaped,clipped);
  std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"jobj_creator\",\"index\":%u,\"allocation_event\":%u,\"label_event\":%u,\"command\":%u,\"scope\":\"%s\",\"frame\":%u,\"sample\":%u,\"source\":\"0x%08x\",\"host\":\"0x%016llx\",\"generation\":\"0x%016llx\",\"epoch\":\"0x%016llx\",\"world\":\"0x%016llx\",\"stack_status\":%u,\"stack_bytes\":%u,\"stack_json_truncated\":%s,\"stack\":\"%s\"}\n",
   i,c.allocation_event,c.label_event,e.command,e.scope==1?"STEP":e.scope==2?"DRAW":"COMMAND",e.frame,e.sample,e.event_source,
   static_cast<unsigned long long>(e.event_host),static_cast<unsigned long long>(e.generation),
   static_cast<unsigned long long>(e.epoch),static_cast<unsigned long long>(e.world),c.stack_status,c.stack_bytes,clipped?"true":"false",escaped);
  for(uint32_t k=0;k<c.writer_count;++k){const auto& w=c.writers[k];++writers;piece_jobj_escape(w.stack,escaped,clipped);
   std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"jobj_writer\",\"creator\":%u,\"index\":%u,\"leaf\":\"%s\",\"command\":%u,\"scope\":\"%s\",\"frame\":%u,\"sample\":%u,\"begin_sequence\":%u,\"end_sequence\":%u,\"flags_before\":\"%08x\",\"flags_after\":\"%08x\",\"before\":[",
    i,k,w.leaf==1?"HSD_JObjMakeMatrix":"HSD_JObjSetupMatrixSub",w.command,w.scope==1?"STEP":w.scope==2?"DRAW":"COMMAND",w.frame,w.sample,w.begin_sequence,w.end_sequence,w.flags_before,w.flags_after);
   for(unsigned z=0;z<12;++z)std::printf("%s\"%08x\"",z?",":"",w.before[z]);
   std::printf("],\"after\":[");for(unsigned z=0;z<12;++z)std::printf("%s\"%08x\"",z?",":"",w.after[z]);
   std::printf("],\"stack_status\":%u,\"stack_bytes\":%u,\"stack_json_truncated\":%s,\"stack\":\"%s\"}\n",w.stack_status,w.stack_bytes,clipped?"true":"false",escaped);
  }
 }
 std::printf("MELEE_CLASS_PIECE_SLAB_TRACE {\"type\":\"jobj_summary\",\"creator_count\":%u,\"writer_count\":%u,\"overflow\":%s,\"refused\":%s,\"identity_joined\":%s}\n",t.jobj_count,writers,t.jobj_overflow?"true":"false",t.jobj_refused?"true":"false",t.joined?"true":"false");
}
#endif
}
extern "C" int melee_web_piece_jobj_begin(void* object,uint32_t leaf,MeleeWebPieceJObjTicket* ticket) {
 if(!ticket)return 0;
 *ticket={};
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto& t=piece_slab_trace;if(leaf<1||leaf>2)return 0;
 bool target=false;
 for(uint32_t i=0;i<t.jobj_count;++i){
  target=target||reinterpret_cast<uintptr_t>(object)==t.events[t.jobjs[i].allocation_event].event_host;
  if(!piece_jobj_live(object,i))continue;
  if(t.jobj_sequence==UINT32_MAX){t.jobj_refused=1;return 0;}
  ticket->begin_sequence=++t.jobj_sequence;ticket->flags_before=static_cast<HSD_JObj*>(object)->flags;
  ticket->valid=1;ticket->creator=i;ticket->allocation_event=t.jobjs[i].allocation_event;ticket->leaf=leaf;
  std::memcpy(ticket->before,static_cast<HSD_JObj*>(object)->mtx,48);return 1;
 }
 if(target&&t.state==2&&!t.closed)t.jobj_refused=1;
#else
 (void)object;(void)leaf;
#endif
 return 0;
}
extern "C" void melee_web_piece_jobj_end(void* object,const MeleeWebPieceJObjTicket* ticket) {
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!ticket||!ticket->valid)return;auto& t=piece_slab_trace;
 if(!piece_jobj_live(object,ticket->creator)||t.jobjs[ticket->creator].allocation_event!=ticket->allocation_event){t.jobj_refused=1;return;}
 if(t.jobj_sequence==UINT32_MAX){t.jobj_refused=1;return;}
 const uint32_t end_sequence=++t.jobj_sequence;
 uint32_t after[12];std::memcpy(after,static_cast<HSD_JObj*>(object)->mtx,48);
 if(!std::memcmp(after,ticket->before,48))return;
 auto& c=t.jobjs[ticket->creator];if(c.writer_count==4){t.jobj_overflow=1;return;}
 auto& w=c.writers[c.writer_count++];w.begin_sequence=ticket->begin_sequence;w.end_sequence=end_sequence;
 w.flags_before=ticket->flags_before;w.flags_after=static_cast<HSD_JObj*>(object)->flags;w.leaf=ticket->leaf;w.command=t.command;w.scope=t.scope;w.frame=t.frame;w.sample=t.sample;
 std::memcpy(w.before,ticket->before,48);std::memcpy(w.after,after,48);piece_jobj_stack(w.stack,w.stack_status,w.stack_bytes);
#else
 (void)object;(void)ticket;
#endif
}
