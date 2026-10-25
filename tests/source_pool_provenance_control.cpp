/* Actual objalloc.c and source-memory runtime. Original memory/OS allocator
 * leaves are explicit models; no real source-world/type attribution claimed. */
#include "gameplay_source_memory_runtime.h"
extern "C" {
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/initialize.h>
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <limits>
#include <new>
#include <emscripten.h>
static bool deny_allocation;
void* operator new(std::size_t n){if(deny_allocation)std::abort();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
alignas(32) unsigned char pool[128];
static unsigned allocation_calls;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
static unsigned wrapper_calls;
#endif
// Parenthesized original symbol remains a real callable allocator model.
extern "C" void* (HSD_MemAlloc)(ssize_t bytes){
    ++allocation_calls;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(!melee_web_source_memory_alloc(3,pool,bytes))std::abort();
#else
    (void)bytes;
#endif
    deny_allocation=true; // actual source hook/linking after this return must not allocate
    return pool;
}
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
/* Model the production memory.c wrapper, not a success stub. The actual
 * objalloc TU keeps its private callsite macro enabled and supplies its token. */
extern "C" void* melee_web_HSD_MemAllocAt(ssize_t bytes,const char* file,unsigned line){
    ++wrapper_calls;
    void* result=(HSD_MemAlloc)(bytes);
    if(result)melee_web_source_memory_origin_event(result,static_cast<uint32_t>(bytes),file,line,1);
    return result;
}
#endif
extern "C" int HSD_GetHeap(){return 3;}
extern "C" s32 OSCheckHeap(int){return 65536;}
extern "C" void __assert(char*,u32,char*){std::abort();}
static void check(bool v,const char* message){if(!v){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
static MeleeWebSourcePayloadFailureWitness witness(MeleeWebSourceMemoryPayloadSnapshot* s){
 const bool old=deny_allocation;deny_allocation=true;
 auto original=melee_web_source_memory_payload_snapshot_compare_live(s);
 const auto result=melee_web_source_memory_payload_failure_witness(s,original);
 deny_allocation=old;return result;
}
static MeleeWebSourceMemoryPayloadSnapshot* capture(){auto c=melee_web_source_memory_payload_snapshot_capture();check(c.snapshot,"capture");return c.snapshot;}
#endif
int main(int argc,char** argv){
 const char* mode=argc>1?argv[1]:"success";
 HSD_ObjAllocData descriptor{};
 check(sizeof(descriptor)==0x2c,"actual wasm32 source descriptor ABI0x2c");
 std::memset(pool,7,sizeof(pool));
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 check(melee_web_source_memory_begin(912,3),"begin");
#endif
 HSD_ObjAllocInit(&descriptor,32,std::strcmp(mode,"bad-mask")==0?3:4);
 check(descriptor.size==32&&descriptor.align==(std::strcmp(mode,"bad-mask")==0?2u:3u),"original init exact stride/mask");
 check(HSD_ObjAllocAddFree(&descriptor,2)==2,"original AddFree result");
 deny_allocation=false;
 check(allocation_calls==1&&descriptor.free==2&&descriptor.used==0&&descriptor.peak==0&&
       reinterpret_cast<void*>(descriptor.freehead)==pool&&
       reinterpret_cast<void*>(descriptor.freehead->next)==pool+32&&
       descriptor.freehead->next->next==nullptr,"original allocator call/link/accounting exact");
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 auto* snapshot=capture();pool[20]^=1;auto first=witness(snapshot);
 const auto original=melee_web_source_memory_payload_snapshot_compare_live(snapshot);
 const auto origin=melee_web_source_memory_allocation_origin_witness(snapshot,original);
 check(wrapper_calls==1&&allocation_calls==1&&origin.status==1&&origin.site_kind==1&&origin.line&&
       std::strstr(origin.path,"objalloc.c"),"actual ObjAlloc macro invokes modeled production wrapper/original once with source token");
 check(first.bytes==256&&first.schema==1&&first.original_status==2&&first.first_difference_matches&&
       first.window.status==2&&first.window.offset==20&&first.hook_active==1,"actual first-difference crosscheck");
 if(!std::strcmp(mode,"bad-mask")){check(first.attribution==2&&first.reason==4,"actual invalid alignment mask refuses attribution");}
 else {
 check(first.attribution==1&&!first.reason&&first.descriptor==reinterpret_cast<uintptr_t>(&descriptor)&&
       first.stride==32&&first.count==2&&first.alignment_mask==3&&first.pool_bytes==64&&
       first.cell_index==0&&first.cell_offset==20&&first.allocation_generation==first.window.allocation_generation,
       "actual AddFree association exact observed descriptor/stride/count/mask/generation");
 if(!std::strcmp(mode,"success"))EM_ASM({console.log("ACTUAL_POOL_FAILURE_WITNESS_BASE64="+
     Buffer.from(HEAPU8.subarray($0,$0+$1)).toString("base64"));},&first,sizeof(first));
 auto malformed=melee_web_source_memory_payload_snapshot_compare_live(snapshot);++malformed.byte_offset;
 auto cross=melee_web_source_memory_payload_failure_witness(snapshot,malformed);
 check(!cross.first_difference_matches&&!cross.attribution,"original-first-difference mismatch cannot publish association");
 if(!std::strcmp(mode,"conflict")||!std::strcmp(mode,"foreign")||!std::strcmp(mode,"overflow")||!std::strcmp(mode,"internal")){
  MeleeWebSourceMemorySnapshot* mirror=nullptr;check(melee_web_source_memory_snapshot_capture(&mirror)==0,"mirror before conflict");
  HSD_ObjAllocData other{};
  const bool foreign=!std::strcmp(mode,"foreign"),overflow=!std::strcmp(mode,"overflow"),internal=!std::strcmp(mode,"internal");
  melee_web_source_memory_objalloc_event(foreign?pool+1:pool,&other,overflow?UINT32_MAX:32,overflow?UINT32_MAX:2,0,64,internal?1:0);
  const unsigned reason=foreign?3:overflow?5:internal?7:6;
  auto bad=witness(snapshot);check(bad.attribution==2&&bad.reason==reason&&!bad.descriptor,"sticky rejection hides prior valid identity");
  auto* fresh=capture();pool[21]^=1;check(witness(fresh).attribution==2&&witness(fresh).reason==reason,"fresh snapshot cannot launder conflict");
  check(melee_web_source_memory_snapshot_restore(mirror)==0,"mirror restore");
  auto* after=capture();pool[22]^=1;check(witness(after).attribution==2&&witness(after).reason==reason,"mirror restore cannot clear aggregate poison");
  check(melee_web_source_memory_free(3,pool)&&melee_web_source_memory_alloc(3,pool,64),"free/reuse after refusal");
  melee_web_source_memory_objalloc_event(pool,&descriptor,32,2,3,64,0);
  auto* reused=capture();pool[21]^=1;check(witness(reused).attribution==2&&witness(reused).reason==reason,"reuse/event cannot clear first refusal");
  melee_web_source_memory_payload_snapshot_destroy(reused);melee_web_source_memory_payload_snapshot_destroy(after);
  melee_web_source_memory_payload_snapshot_destroy(fresh);melee_web_source_memory_snapshot_destroy(mirror);
 }else if(!std::strcmp(mode,"mirror")){
  MeleeWebSourceMemorySnapshot* mirror=nullptr;check(melee_web_source_memory_snapshot_capture(&mirror)==0,"capture provenance in mirror");
  check(melee_web_source_memory_snapshot_restore(mirror)==0,"restore mirror without source event");
  auto* fresh=capture();pool[21]^=1;auto unknown=witness(fresh);
  check(unknown.first_difference_matches&&!unknown.attribution&&unknown.reason==2&&!unknown.descriptor,"copied mirror fields cannot revive allocating event");
  melee_web_source_memory_payload_snapshot_destroy(fresh);melee_web_source_memory_snapshot_destroy(mirror);
 }else if(!std::strcmp(mode,"reuse")){
  check(melee_web_source_memory_free(3,pool)&&melee_web_source_memory_alloc(3,pool,64),"new allocation generation");
  auto* fresh=capture();pool[21]^=1;auto unknown=witness(fresh);
  check(unknown.first_difference_matches&&!unknown.attribution&&unknown.reason==2,"fresh allocation does not inherit association");
  melee_web_source_memory_payload_snapshot_destroy(fresh);
 }else{
  const auto before=first;
  check(melee_web_source_memory_payload_snapshot_restore(snapshot)==0,"ordinary payload restore");
  pool[20]^=1;auto after=witness(snapshot);
  check(!std::memcmp(&before,&after,sizeof(before)),"payload restore leaves actual association immutable");
 }
 }
 melee_web_source_memory_payload_snapshot_destroy(snapshot);
 check(melee_web_source_memory_healthy()&&melee_web_source_memory_end(912),"diagnostic failure never poisons source owner");
 check(melee_web_source_memory_begin(912,3),"new owner resets diagnostic refusal");
 check(melee_web_source_memory_alloc(3,pool,64),"new owner allocation");
 melee_web_source_memory_objalloc_event(pool,&descriptor,32,2,3,64,0);
 auto* final=capture();pool[20]^=1;check(witness(final).attribution==1,"new owner genuine event accepted");
 melee_web_source_memory_payload_snapshot_destroy(final);check(melee_web_source_memory_end(912),"end");
 std::printf("Actual source AddFree/provenance %s passed (memory/OS leaves modeled)\n",mode);
#else
 std::puts("Actual inactive objalloc links without source-memory implementation and preserves original result");
#endif
}
