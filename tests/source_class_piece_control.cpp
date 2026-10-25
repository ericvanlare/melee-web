/* Actual original memory.c/class.c + tracked runtime; OS backing storage,
 * heap selection and assert reporting are explicit fixture leaves. */
#include "gameplay_source_memory_runtime.h"
extern "C" {
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/class.h>
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
alignas(32) static unsigned char arena[131072];static unsigned cursor,calls,assertions;static bool fail_next;
extern "C" int HSD_GetHeap(){return 3;}
extern "C" void* OSAllocFromHeap(int heap,u32 size){
 deny_allocation=false;++calls;if(fail_next||size>sizeof(arena)-cursor)return nullptr;
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
  auto* p=hsdAllocMemPiece(48);check(p,"inactive raw");hsdFreeMemPiece(p,48);return 0;
 }
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto* raw=static_cast<unsigned char*>(hsdAllocMemPiece(160));check(raw,"raw allocation");
 hsdFreeMemPiece(raw,160);auto* small=static_cast<unsigned char*>(hsdAllocMemPiece(32));
 auto* next=static_cast<unsigned char*>(hsdAllocMemPiece(32));
 check(small==raw&&next==raw+32,"actual larger free split");
 hsdFreeMemPiece(small,32);check(hsdAllocMemPiece(32)==small,"actual exact reuse");
 if(!std::strcmp(mode,"oom")){
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
