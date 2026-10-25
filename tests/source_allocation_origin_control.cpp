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
static bool deny_allocation;
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
static auto capture(){deny_allocation=false;auto c=melee_web_source_memory_payload_snapshot_capture();check(c.snapshot,"capture");return c.snapshot;}
static auto witness(MeleeWebSourceMemoryPayloadSnapshot* s){deny_allocation=true;auto c=melee_web_source_memory_payload_snapshot_compare_live(s);auto r=melee_web_source_memory_allocation_origin_witness(s,c);deny_allocation=false;return r;}
#endif
int main(int argc,char** argv){
 const char* mode=argc>1?argv[1]:"class";
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 check(melee_web_source_memory_begin(77,3),"begin");
#endif
 fail_next=!std::strcmp(mode,"assert-failure");
 unsigned side=64;auto* direct=static_cast<unsigned char*>(HSD_MemAlloc(side++));deny_allocation=false;
 check(side==65&&calls==1&&!assertions,"size side effect/original call once");
 check(HSD_MemAlloc(0)==nullptr&&HSD_MemAlloc(-1)==nullptr&&calls==1,"original nonpositive no OS call");
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto* s=capture();direct[5]^=1;auto r=witness(s);
 check(r.bytes==320&&r.status==1&&r.site_kind==1&&r.first_difference_matches&&r.line&&std::strstr(r.path,"source_allocation_origin_control.cpp"),"direct source token");
 if(!std::strcmp(mode,"conflict")){
  melee_web_source_memory_origin_event(direct,64,"conflict.c",1,1);r=witness(s);check(r.status==2&&r.reason==5,"conflict poison");
  auto* fresh=capture();direct[6]^=1;check(witness(fresh).status==2,"fresh snapshot poison");
  MeleeWebSourceMemorySnapshot* mirror=nullptr;check(!melee_web_source_memory_snapshot_capture(&mirror),"poison mirror capture");
  check(!melee_web_source_memory_snapshot_restore(mirror),"poison mirror restore");auto* after=capture();direct[7]^=1;
  check(witness(after).status==2&&witness(after).reason==5,"mirror cannot launder sticky origin refusal");
 }else if(!std::strcmp(mode,"foreign")){
  melee_web_source_memory_origin_event(direct+1,63,"foreign.c",1,1);r=witness(s);check(r.status==2&&r.reason==3,"interior foreign refuses");
 }else if(!std::strcmp(mode,"extent")){
  melee_web_source_memory_origin_event(direct,63,"extent.c",1,1);r=witness(s);check(r.status==2&&r.reason==3,"changed extent refuses");
 }else if(!std::strcmp(mode,"retired")){
  check(melee_web_source_memory_end(77),"retire");check(witness(s).reason==7,"retired snapshot refuses before pointer read");
 }else if(!std::strcmp(mode,"os")){
  auto* p=static_cast<unsigned char*>(OSAllocFromHeap(3,32));deny_allocation=false;
  // Explicit direct-OS hook model, not execution of initialize.c/GX services.
  melee_web_source_memory_origin_event(p,32,"modeled-direct-os.c",1,2);
  auto* fresh=capture();p[5]^=1;r=witness(fresh);check(r.status==1&&r.site_kind==2,"direct OS token kind distinct");
 }else if(!std::strcmp(mode,"bounds")){
  // New allocation has no existing outer token when called indirectly.
  void*(*function)(ssize_t)=&HSD_MemAlloc;auto* p=static_cast<unsigned char*>(function(32));deny_allocation=false;
  static char long_name[233];std::memset(long_name,'x',232);
  auto* bound_snapshot=capture();
  melee_web_source_memory_origin_event(p,32,long_name,1,1);p[5]^=1;r=witness(bound_snapshot);check(r.status==2&&r.reason==4,"path bound sticky");
 }else if(!std::strcmp(mode,"mirror")){
  MeleeWebSourceMemorySnapshot* mirror=nullptr;check(!melee_web_source_memory_snapshot_capture(&mirror),"mirror capture");
  check(!melee_web_source_memory_snapshot_restore(mirror),"mirror restore");auto* fresh=capture();direct[6]^=1;
  r=witness(fresh);check(!r.status&&r.reason==2,"mirror cannot simulate allocation event");
 }else if(!std::strcmp(mode,"reuse")){
  HSD_Free(direct);check(witness(s).reason==7,"old snapshot refused after free");
  cursor=0;void*(*function)(ssize_t)=&HSD_MemAlloc;auto* p=static_cast<unsigned char*>(function(64));deny_allocation=false;
  auto* fresh=capture();p[6]^=1;r=witness(fresh);check(!r.status&&r.reason==2,"fresh reused indirect call stays UNKNOWN");
 }else {
  direct[5]^=1;
  void*(*function)(ssize_t)=&HSD_MemAlloc;auto* p=static_cast<unsigned char*>(function(32));deny_allocation=false;
  auto* unknown=capture();p[5]^=1;r=witness(unknown);check(!r.status&&r.reason==2,"function pointer stays callable/UNKNOWN");p[5]^=1;
  auto* piece=static_cast<unsigned char*>(hsdAllocMemPiece(64));deny_allocation=false;auto count=calls;
  hsdFreeMemPiece(piece,64);auto* again=hsdAllocMemPiece(64);check(again==piece&&calls==count,"actual class reuse");
  auto* smaller=hsdAllocMemPiece(32);deny_allocation=false;check(smaller&&calls>=count,"actual class split/new entry");
  auto* snap=capture();piece[5]^=1;r=witness(snap);
  check(r.status==1&&r.site_kind==1&&std::strstr(r.path,"class.c"),"actual class backing chunk exact callsite (not class type)");
  EM_ASM({console.log('ACTUAL_ALLOCATION_ORIGIN_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+320)).toString('base64'));},&r);
  hsdFreeMemPiece(piece,64);hsdFreeMemPiece(smaller,32);
 }
#else
 auto* piece=hsdAllocMemPiece(64);deny_allocation=false;hsdFreeMemPiece(piece,64);check(hsdAllocMemPiece(64)==piece,"inactive actual class reuse");
 void*(*function)(ssize_t)=&HSD_MemAlloc;check(function(32)!=nullptr,"inactive function pointer");deny_allocation=false;
#endif
 std::puts("PASS: actual memory/class allocator; OS/assert leaves modeled");
}
