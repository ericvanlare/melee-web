/* Actual original memory.c/class.c + tracked runtime; OS backing storage,
 * heap selection and assert reporting are explicit fixture leaves. */
#include "gameplay_source_memory_runtime.h"
extern "C" {
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/tobj.h>
#include <dolphin/os/OSAlloc.h>
}
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <emscripten.h>
static bool deny_allocation,fail_diagnostic_calloc;
extern "C" void* __real_calloc(std::size_t,std::size_t);
extern "C" void* __wrap_calloc(std::size_t n,std::size_t bytes){
 if(deny_allocation)std::abort();
 if(fail_diagnostic_calloc)return nullptr;
 return __real_calloc(n,bytes);
}
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
// Actual implementation included in this TU solely for injected exhaustion/
// cold-state controls; no production setter or extra export exists.
#include "../src/gameplay_source_memory_runtime.cpp"
#endif
void* operator new(std::size_t n){if(deny_allocation)std::abort();if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
alignas(32) static unsigned char arena[131072];static unsigned cursor,calls,assertions;
extern "C" int HSD_GetHeap(){return 3;}
extern "C" void* OSAllocFromHeap(int heap,u32 size){
 deny_allocation=false;++calls;if(size>sizeof(arena)-cursor)return nullptr;
 auto* p=arena+cursor;cursor+=(size+31)&~31u;std::memset(p,0,size);
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!melee_web_source_memory_alloc(heap,p,size))std::abort();
#endif
 deny_allocation=true;return p;
}
extern "C" void OSFreeToHeap(int heap,void* p){
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!melee_web_source_memory_free(heap,p))std::abort();
#else
 (void)heap;(void)p;
#endif
}
extern "C" void __assert(char*,u32,char*){++assertions;std::printf("ORIGINAL_ASSERT calls=%u assertions=%u\n",calls,assertions);std::exit(73);}
static void check(bool v,const char* s){if(!v){std::fprintf(stderr,"%s\n",s);std::exit(1);}}

#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
static auto snapshot(){deny_allocation=false;auto c=melee_web_source_memory_payload_snapshot_capture();check(c.snapshot,"payload capture");return c.snapshot;}
static uint64_t cell_hash(){uint64_t h=14695981039346656037ULL;
 const auto* p=reinterpret_cast<const unsigned char*>(piece_owner.cells);
 for(size_t i=0;i<size_t(piece_owner.count)*sizeof(PieceCell);++i){h^=p[i];h*=1099511628211ULL;}return h;}
static auto capture_query(MeleeWebSourceMemoryPayloadSnapshot* pre,MeleeWebSourceMemoryPayloadSnapshot* post,MeleeWebClassPieceAnchor a){
 deny_allocation=true;auto c=melee_web_source_memory_payload_snapshot_compare_live(post);
 auto w=melee_web_source_memory_class_piece_capture_witness(pre,post,c,a);deny_allocation=false;return w;
}
static auto query(MeleeWebSourceMemoryPayloadSnapshot* s,MeleeWebClassPieceAnchor a){
 deny_allocation=true;auto c=melee_web_source_memory_payload_snapshot_compare_live(s);
 auto w=melee_web_source_memory_class_piece_witness(s,c,a);deny_allocation=false;return w;
}
#endif
static int fail_init(HSD_Class*){return -1;} // explicitly modeled constructor failure
int main(int argc,char** argv){
 const char* mode=argc>1?argv[1]:"class";
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 fail_diagnostic_calloc=!std::strcmp(mode,"oom");
 check(melee_web_source_memory_begin(77,3),"begin");
 fail_diagnostic_calloc=false;
#endif
 if(!std::strcmp(mode,"inactive")){
  auto* p=hsdAllocMemPieceAt(48,0xffffffffu);check(p,"inactive raw");hsdFreeMemPiece(p,48);return 0;
 }
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 if(!std::strncmp(mode,"origin",6)){
  static_assert(sizeof(HSD_Tlut)==16,"pinned wasm HSD_Tlut request size");
  const auto before=piece_owner.serial;
  const uint32_t site=!std::strcmp(mode,"origin-unknown")?12:MELEE_WEB_CLASS_PIECE_SITE_TLUT;
  auto* p=static_cast<unsigned char*>(hsdAllocMemPieceAt(sizeof(HSD_Tlut),site));
  check(p,"caller wrapper returns original allocator pointer");
  const auto* row=find_host_allocation(reinterpret_cast<uintptr_t>(p));
  check(row,"wrapper result maps to existing backing row");
  const auto source=row->source.value()+reinterpret_cast<uintptr_t>(p)-row->host;
  const auto index=(source-piece_owner.begin)/32;
  auto& cell=piece_owner.cells[index];
  check(!cell.tag,"raw wrapper allocation remains untyped");
  if(!std::strcmp(mode,"origin-unknown")){
   check(piece_owner.reason==12&&!cell.origin_site&&piece_owner.serial>before,"unknown site refuses provenance without labeling the allocation");
  }else if(!std::strcmp(mode,"origin-duplicate")){
   check(!piece_owner.reason&&cell.origin_site==MELEE_WEB_CLASS_PIECE_SITE_TLUT,"first exact site label");
   const auto generation=cell.generation;
   const auto serial=piece_owner.serial;
   melee_web_class_piece_origin(p,sizeof(HSD_Tlut),MELEE_WEB_CLASS_PIECE_SITE_TLUT);
   check(piece_owner.reason==12&&piece_owner.serial==serial&&cell.generation==generation&&cell.origin_site==MELEE_WEB_CLASS_PIECE_SITE_TLUT,"duplicate site refuses without relabel");
  }else{
   check(!piece_owner.reason&&piece_owner.serial>before&&cell.origin_site==MELEE_WEB_CLASS_PIECE_SITE_TLUT,"successful allocation gets one caller label");
   const auto generation=cell.generation;
   hsdFreeMemPiece(p,sizeof(HSD_Tlut));check(!cell.origin_site,"free clears origin");
   auto* reused=static_cast<unsigned char*>(hsdAllocMemPieceAt(sizeof(HSD_Tlut),MELEE_WEB_CLASS_PIECE_SITE_MOBJ_PEDESC));
   check(reused==p&&cell.generation>generation&&cell.origin_site==MELEE_WEB_CLASS_PIECE_SITE_MOBJ_PEDESC&&!piece_owner.reason,"same-address reuse gets fresh generation and caller");
  }
  return 0;
 }
 auto* raw=static_cast<unsigned char*>(hsdAllocMemPiece(160));check(raw,"raw allocation");
 hsdFreeMemPiece(raw,160);auto* small=static_cast<unsigned char*>(hsdAllocMemPiece(32));
 auto* next=static_cast<unsigned char*>(hsdAllocMemPiece(32));
 check(small==raw&&next==raw+32,"actual larger free split");
 hsdFreeMemPiece(small,32);check(hsdAllocMemPiece(32)==small,"actual exact reuse");
 if(!std::strncmp(mode,"capture-",8)){
  ClassInfoInit(&hsdClass);HSD_ClassInfo parent{},a{},b{};
  hsdInitClassInfo(&parent,&hsdClass,(char*)"fixture",(char*)"parent",sizeof(parent),48);
  hsdInitClassInfo(&a,&parent,(char*)"fixture",(char*)"a",sizeof(a),48);
  hsdInitClassInfo(&b,&parent,(char*)"fixture",(char*)"b",sizeof(b),48);
  auto* p=static_cast<unsigned char*>(hsdNew(&a));check(p,"capture actual class");
  // Consume actual split pieces until target and the unrelated32 piece have
  // distinct backing ledger rows. Bound is the fixture arena's byte capacity,
  // not an authored allocator/table bound or inferred object type.
  for(size_t n=0;find_host_allocation(reinterpret_cast<uintptr_t>(p))==find_host_allocation(reinterpret_cast<uintptr_t>(next));++n){
   check(n<sizeof(arena)/64,"fixture distinct-row construction exhausted");p=static_cast<unsigned char*>(hsdNew(&a));check(p,"capture distinct class row");
  }
  auto* raw_target=static_cast<unsigned char*>(p);
  size_t target_offset=8;
  if(!std::strcmp(mode,"capture-raw")){
   raw_target=nullptr;
   for(size_t n=0;;++n){
    check(n<sizeof(arena)/16,"fixture raw target row construction exhausted");
    raw_target=static_cast<unsigned char*>(hsdAllocMemPieceAt(sizeof(HSD_Tlut),MELEE_WEB_CLASS_PIECE_SITE_TLUT));
    check(raw_target,"actual raw TLUT-sized piece");
    if(find_host_allocation(reinterpret_cast<uintptr_t>(raw_target))!=find_host_allocation(reinterpret_cast<uintptr_t>(next)))break;
   }
   target_offset=0;
  }
  // A preexisting free32 piece ensures the first event is a piece allocation,
  // not a new outer heap topology event. Class48 uses a distinct backing row.
  hsdFreeMemPiece(next,32);
  auto* pre=snapshot();auto* post=snapshot();auto anchor=melee_web_class_piece_arm();
  check(anchor.ready,"capture ready");const uint64_t frozen=cell_hash();
  raw_target[target_offset]^=1;
  if(!std::strcmp(mode,"capture-armed")){
   auto w=capture_query(pre,post,anchor);check(w.status==1&&w.live_reason==0&&w.scope==1&&w.class_info==reinterpret_cast<uintptr_t>(&a),"armed capture-only");
  }else if(!std::strcmp(mode,"capture-disarm")){
   melee_web_class_piece_disarm(anchor);check(capture_query(pre,post,anchor).reason==9,"clean disarm not historical proof");
  }else if(!std::strcmp(mode,"capture-duplicate")){
   melee_web_class_piece_arm();check(capture_query(pre,post,anchor).reason==9,"duplicate arm refuses");
  }else if(!std::strcmp(mode,"capture-topology")){
   check(HSD_MemAlloc(32),"outer source allocation");check(cell_hash()==frozen,"armed clear_row no cell write");
   check(capture_query(pre,post,anchor).status!=1&&capture_query(pre,post,anchor).reason==9,"topology invalidates POST comparison before attribution");
  }else if(!std::strcmp(mode,"capture-descriptor")){
   melee_web_class_piece_descriptor(&a);check(cell_hash()==frozen,"descriptor no cell write");check(capture_query(pre,post,anchor).reason==11,"descriptor refusal not relaxed");
  }else if(!std::strcmp(mode,"capture-amnesia")){
   melee_web_class_piece_amnesia();check(cell_hash()==frozen,"amnesia no cell write");check(capture_query(pre,post,anchor).reason==8,"amnesia refuses");
  }else if(!std::strcmp(mode,"capture-serial")){
   piece_owner.serial=UINT64_MAX;check(hsdAllocMemPiece(32),"overflow source allocation");check(cell_hash()==frozen,"overflow table frozen");check(capture_query(pre,post,anchor).reason==6,"serial overflow refuses");
  }else {
   auto* unrelated=hsdAllocMemPiece(32);check(unrelated==next,"actual unrelated32 reuse");
   check(find_host_allocation(reinterpret_cast<uintptr_t>(unrelated))!=find_host_allocation(reinterpret_cast<uintptr_t>(p)),"distinct outer rows");
   check(piece_owner.reason==7&&piece_owner.serial==anchor.serial+1&&cell_hash()==frozen,"postarm event froze table before update");
   check(query(post,anchor).status==2&&query(post,anchor).reason==7,"existing live witness stays refused");
   // POST expected endpoint follows the unrelated event. Its allocator-link
   // writes must not be silently substituted for the intended later mismatch.
   raw_target[target_offset]^=1;melee_web_source_memory_payload_snapshot_destroy(post);post=snapshot();raw_target[target_offset]^=1;
   if(!std::strcmp(mode,"capture-reuse")){
    // Actual free and constructor overwrite the target's live header. Only
    // PRE saved header remains a valid interpretation of the frozen cell.
    hsdFreeMemPiece(p,48);auto* reused=hsdNew(&b);check(reused==p,"actual post-refusal target reuse");
    uint32_t header=0;std::memcpy(&header,reused,4);check(header==reinterpret_cast<uintptr_t>(&b),"new live class differs");
   }else if(!std::strcmp(mode,"capture-wrong-header")){
    auto* bytes=const_cast<unsigned char*>(payload_bytes(pre));size_t offset=0;
    for(size_t i=0;i<pre->row_count;++i){const auto& row=payload_rows(pre)[i];if(reinterpret_cast<uintptr_t>(p)>=row.host&&reinterpret_cast<uintptr_t>(p)-row.host<row.requested){offset+=reinterpret_cast<uintptr_t>(p)-row.host;break;}offset+=row.requested;}
    bytes[offset]^=1;
   }else if(!std::strcmp(mode,"capture-stale-row")){const_cast<HostAllocation*>(payload_rows(pre))[0].generation++;}
   else if(!std::strcmp(mode,"capture-extent")){const_cast<HostAllocation*>(payload_rows(pre))[0].requested++;}
   else if(!std::strcmp(mode,"capture-anchor")){anchor.instance++;}
   else if(!std::strcmp(mode,"capture-cold")){piece_owner.state=1;}
   else if(!std::strcmp(mode,"capture-driftserial")){piece_owner.serial++;}
   else if(!std::strcmp(mode,"capture-bounds")){piece_owner.count--;}
   else if(!std::strcmp(mode,"capture-null-class")){
    // Injected malformed diagnostic cell; no production setter or hook change.
    const auto* row=find_host_allocation(reinterpret_cast<uintptr_t>(p));const auto source=row->source.value()+reinterpret_cast<uintptr_t>(p)-row->host;
    piece_owner.cells[(source-piece_owner.begin)/32].tag=2;
    auto* bytes=const_cast<unsigned char*>(payload_bytes(pre));size_t offset=0;
    for(size_t i=0;i<pre->row_count;++i){const auto& r=payload_rows(pre)[i];if(r.host==row->host){offset+=reinterpret_cast<uintptr_t>(p)-r.host;break;}offset+=r.requested;}
    std::memset(bytes+offset,0,4);
   }else if(!std::strcmp(mode,"capture-pending")){
    // Injected metadata negative, no production setter: PENDING is UNKNOWN.
    const auto* row=find_host_allocation(reinterpret_cast<uintptr_t>(p));const auto source=row->source.value()+reinterpret_cast<uintptr_t>(p)-row->host;
    piece_owner.cells[(source-piece_owner.begin)/32].tag=reinterpret_cast<uintptr_t>(&a)|1;
   }else if(!std::strcmp(mode,"capture-missing")){
    const auto* row=find_host_allocation(reinterpret_cast<uintptr_t>(p));const auto source=row->source.value()+reinterpret_cast<uintptr_t>(p)-row->host;
    piece_owner.cells[(source-piece_owner.begin)/32]={};
   }else if(!std::strcmp(mode,"capture-mirror")){
    deny_allocation=false;MeleeWebSourceMemorySnapshot* mirror=nullptr;check(!melee_web_source_memory_snapshot_capture(&mirror),"mirror capture");
    check(!melee_web_source_memory_snapshot_restore(mirror),"mirror restore afterreason7");check(cell_hash()==frozen,"mirror never copies diagnostic table");
   }else if(!std::strcmp(mode,"capture-retired")||!std::strcmp(mode,"capture-recreated")){
    check(melee_web_source_memory_end(77),"retire");
    if(!std::strcmp(mode,"capture-recreated")){deny_allocation=false;check(melee_web_source_memory_begin(78,3),"recreate");}
   }
   auto w=capture_query(pre,post,anchor);
   if(!std::strcmp(mode,"capture-frozen")||!std::strcmp(mode,"capture-reuse")){
    check(w.status==1&&w.scope==1&&w.live_reason==7&&w.coverage_state==4&&w.record_state==3&&w.class_info==reinterpret_cast<uintptr_t>(&a)&&w.saved_class_info==w.class_info,"historical PRE class only");
    check(cell_hash()==frozen,"all later actual events leave frozen cells unchanged");
    check(query(post,anchor).reason==7,"existing live witness still UNKNOWN after historical query");
    if(!std::strcmp(mode,"capture-frozen")){
     auto original=melee_web_source_memory_payload_snapshot_compare_live(post);original.expected_byte^=1;deny_allocation=true;
     check(melee_web_source_memory_class_piece_capture_witness(pre,post,original,anchor).reason==9,"actual mismatch cross-check");deny_allocation=false;
     // No failure means no witness, even with valid snapshots/anchor.
     auto good=melee_web_source_memory_payload_snapshot_compare_live(pre);good.status=0;
     check(melee_web_source_memory_class_piece_capture_witness(pre,post,good,anchor).reason==9,"no fabricated difference");
     EM_ASM({console.log('ACTUAL_CLASS_CAPTURE_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+160)).toString('base64'));},&w);
    }
   }else if(!std::strcmp(mode,"capture-null-class")){
    check(w.status==2&&w.reason==10&&!w.class_info&&!w.saved_class_info,"malformed confirmed null pointer refuses");
    EM_ASM({console.log('ACTUAL_CLASS_CAPTURE_NULL_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+160)).toString('base64'));},&w);
   }else if(!std::strcmp(mode,"capture-raw")){
    check(w.status==1&&w.record_state==1&&!w.class_info&&w.origin_site==MELEE_WEB_CLASS_PIECE_SITE_TLUT&&w.origin_status==1,"actual raw TLUT-sized caller capture stays type UNKNOWN");
    EM_ASM({console.log('ACTUAL_CLASS_CAPTURE_RAW_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+160)).toString('base64'));},&w);
   }
   else if(!std::strcmp(mode,"capture-pending")||!std::strcmp(mode,"capture-missing")){check(!w.status&&w.reason==4,"pending/missing UNKNOWN");}
   else {check(w.status!=1&&w.reason,"capture invalid identity/header/lifecycle refuses");}
  }
 }else if(!std::strcmp(mode,"oom")){
  check(melee_web_source_memory_healthy(),"diagnostic OOM does not source fail");
  auto* snap=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;auto w=query(snap,a);
  check(!a.ready&&w.reason==1&&!w.class_info,"diagnostic OOM UNKNOWN without source NULL");
 }else if(!std::strcmp(mode,"topology")){
  auto* old=snapshot();auto a=melee_web_class_piece_arm();check(HSD_MemAlloc(32),"source allocation still succeeds");
  check(query(old,a).reason==9,"old payload topology refused");auto* fresh=snapshot();small[10]^=1;auto w=query(fresh,a);
  check(w.status==2&&w.reason==8&&w.first_event.kind==8&&w.event_serial>a.serial,"topology event sticky serial");
 }else if(!std::strcmp(mode,"disarm")){
  auto* snap=snapshot();auto a=melee_web_class_piece_arm();melee_web_class_piece_disarm(a);
  check(hsdAllocMemPiece(64),"ordinary source allocation after diagnostic disarm");
  auto* fresh=snapshot();auto b=melee_web_class_piece_arm();small[10]^=1;check(query(fresh,b).status==1,"ordinary tracker can rearm after clean disarm");
  (void)snap;
 }else if(!std::strcmp(mode,"raw")){
  auto* s=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;auto w=query(s,a);
  check(w.status==1&&w.record_state==1&&!w.class_info&&w.physical==32&&w.piece_offset==10&&w.piece_generation,"raw covering live piece");
 }else if(!std::strcmp(mode,"drift")){
  auto* s=snapshot();auto a=melee_web_class_piece_arm();hsdFreeMemPiece(next,32);small[10]^=1;auto w=query(s,a);
  check(w.status==2&&w.reason==7&&w.first_event.kind==3,"first free drift");
  hsdAllocMemPiece(32);auto* fresh=snapshot();small[11]^=1;auto q=query(fresh,a);
  check(q.status==2&&q.reason==7&&q.first_event.serial==w.first_event.serial,"fresh snapshot cannot launder drift");
 }else if(!std::strcmp(mode,"missing")){
  auto* p=static_cast<unsigned char*>(HSD_MemAlloc(64));auto* s=snapshot();auto a=melee_web_class_piece_arm();p[10]^=1;
  check(query(s,a).status==0&&query(s,a).reason==4,"unobserved direct row stays UNKNOWN");
 }else if(!std::strcmp(mode,"mirror")){
  deny_allocation=false;
  MeleeWebSourceMemorySnapshot* mirror=nullptr;check(!melee_web_source_memory_snapshot_capture(&mirror),"mirror capture");
  auto* s=snapshot();auto a=melee_web_class_piece_arm();check(!melee_web_source_memory_snapshot_restore(mirror),"mirror restore");check(query(s,a).reason==9,"old payload snapshot refuses mirror topology before byte reads");
  auto* fresh=snapshot();small[10]^=1;
  check(query(fresh,a).status==2&&query(fresh,a).reason==8,"fresh payload cannot launder mirror invalidation");
 }else if(!std::strcmp(mode,"retired")){
  auto* s=snapshot();auto a=melee_web_class_piece_arm();check(melee_web_source_memory_end(77),"retire");
  check(query(s,a).reason==9,"retired refusal before pointer");
 }else if(!std::strcmp(mode,"overflow")){
  piece_owner.next_generation=UINT32_MAX;auto* p=hsdAllocMemPiece(64);check(p,"original alloc still succeeds on diagnostic exhaustion");
  auto* fresh=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;auto w=query(fresh,a);
  check(w.status==2&&w.reason==6,"injected piece generation exhaustion refuses");
 }else if(!std::strcmp(mode,"serial")){
  piece_owner.serial=UINT64_MAX;check(hsdAllocMemPiece(64),"source success at diagnostic serial exhaustion");
  auto* fresh=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;check(query(fresh,a).reason==6,"injected serial exhaustion refuses");
 }else if(!std::strcmp(mode,"noncold")){
  piece_owner.state=1;melee_web_class_piece_cold(reinterpret_cast<void*>(32),32);
  auto* fresh=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;check(query(fresh,a).reason==2,"noncold does not invent coverage");
 }else if(!std::strcmp(mode,"extent")){
  melee_web_class_piece_free(small,31);auto* fresh=snapshot();auto a=melee_web_class_piece_arm();small[10]^=1;
  check(query(fresh,a).reason==4,"wrong extent cannot retire live piece");
 }else if(!std::strcmp(mode,"promotion")){
  // Force GetMemoryEntry pointer table expansion from32 to64 buckets.
  auto* big=static_cast<unsigned char*>(hsdAllocMemPiece(1100));check(big,"pointer-table promotion path");
  auto* s=snapshot();auto a=melee_web_class_piece_arm();big[10]^=1;auto w=query(s,a);
  check(w.status==1&&w.physical==1120&&w.original_requested==1100,"old memory-list promotion did not poison coverage");
 }else {
  ClassInfoInit(&hsdClass);HSD_ClassInfo parent{},a{},b{},bad{};
  hsdInitClassInfo(&parent,&hsdClass,(char*)"fixture",(char*)"parent",sizeof(parent),48);
  hsdInitClassInfo(&a,&parent,(char*)"fixture",(char*)"a",sizeof(a),48);
  hsdInitClassInfo(&b,&parent,(char*)"fixture",(char*)"b",sizeof(b),48);
  hsdInitClassInfo(&bad,&parent,(char*)"fixture",(char*)"bad",sizeof(bad),80);
  auto* p=static_cast<unsigned char*>(hsdNew(&a));check(p,"actual constructor success");
  check(hsdChangeClass(p,&b),"actual compatible class change");check(!hsdChangeClass(p,&bad),"incompatible class unchanged");
  a.init=fail_init;check(!hsdNew(&a),"actual failed constructor destroys/retire");
  auto* s=snapshot();auto anchor=melee_web_class_piece_arm();p[8]^=1;
  auto w=query(s,anchor);
  if(!std::strcmp(mode,"descriptor")){
   hsdInitClassInfo(&b,&parent,(char*)"fixture",(char*)"b",sizeof(b),48);
   w=query(s,anchor);check(w.status==2&&w.reason==11,"descriptor identity cannot be reused");
  }else{
   check(w.status==1&&w.record_state==3&&w.confirmed&&w.class_info==reinterpret_cast<uintptr_t>(&b)&&
    w.class_obj_size==48&&w.physical==64&&w.original_requested==48&&w.piece_offset==8,"class metadata/physical exact");
   EM_ASM({console.log('ACTUAL_CLASS_PIECE_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+208)).toString('base64'));},&w);
  }
 }
 std::printf("CLASS_PIECE_PASS %s actual memory/class/runtime; OS storage/assert and failed-init leaf modeled\n",mode);
#endif
 return 0;
}
