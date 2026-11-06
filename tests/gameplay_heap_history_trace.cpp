#include <aurora/aurora.h>
namespace aurora { AuroraConfig g_config{}; }
// Including the isolated gameplay heap TU keeps the original allocator's
// private descriptor/cell types in this focused control. No substitute heap is
// linked; the TU itself includes the pinned SDK OSAlloc.cpp.
#include "../src/gameplay_heap.cpp"
#include "../src/gameplay_source_memory_runtime.h"
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
#include "original_startup_compat.h"
extern "C" {
#include <sysdolphin/baselib/jobj.h>
// Link-only root for unrelated legacy JObj tracing, which this control never calls.
HSD_JObjInfo hsdJObj{};
}
#endif

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace { long allocation_budget=-1; }
void* operator new(std::size_t bytes)
{
    if (allocation_budget==0) throw std::bad_alloc();
    if (allocation_budget>0) --allocation_budget;
    if (void* p=std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }

namespace {
constexpr size_t kArenaBytes = 65536;
constexpr uint32_t kAnchorBytes = 96;
constexpr uint32_t kTrialBytes = 128;
char error[192];
[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s (%s)\n", message, error);
    std::exit(1);
}
void check(bool value, const char* message)
{
    if (!value) fail(message);
}
void fill(unsigned char* dst, size_t count, unsigned char seed)
{
    for (size_t i=0;i<count;++i) dst[i]=static_cast<unsigned char>(seed+i*17u);
}
void same_bytes(const void* a,const void* b,size_t n,const char* message)
{
    check(std::memcmp(a,b,n)==0,message);
}
struct Rows { const void* payloads[8]{}; uint32_t capacities[8]{}; uint64_t count=0; };
int collect_row(void* opaque,uint64_t ordinal,const void* payload,uint32_t capacity)
{
    auto* rows=static_cast<Rows*>(opaque);
    if(ordinal>=8)return 0;
    rows->payloads[ordinal]=payload;
    rows->capacities[ordinal]=capacity;
    ++rows->count;
    return 1;
}
}
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
static void paired_restore()
{
    constexpr uint64_t world=17;
    auto* arena=static_cast<unsigned char*>(std::malloc(kArenaBytes));
    check(arena!=nullptr,"allocate paired arena");
    std::memset(arena,0xA7,kArenaBytes);
    void* start=melee_web_gameplay_heap_initialize(arena,kArenaBytes,error,sizeof(error));
    check(start!=nullptr,"initialize paired SDK arena");
    const auto heap=OSCreateHeap(start,arena+kArenaBytes);
    check(heap==0&&OSCheckHeap(heap)>0,"create paired SDK heap");
    OSSetCurrentHeap(heap);
    check(melee_web_source_memory_begin(world,heap),"begin source shadow owner");
    melee_web_class_piece_cold(nullptr,0);
    auto* anchor=static_cast<unsigned char*>(OSAllocFromHeap(heap,kAnchorBytes));
    check(anchor!=nullptr,"paired PRE payload");
    fill(anchor,kAnchorBytes,0x32);
    melee_web_class_piece_alloc(anchor,32,32);
    melee_web_class_piece_origin(anchor,32,1);
    const std::vector<unsigned char> pre(arena,arena+kArenaBytes);
    const auto sdk=melee_web_gameplay_heap_history_capture();
    const auto shadow=melee_web_source_memory_history_capture();
    const auto payload=melee_web_source_memory_payload_snapshot_capture();
    check(sdk.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&sdk.snapshot&&
          shadow.status==MELEE_WEB_SOURCE_MEMORY_HISTORY_OK&&shadow.pre&&payload.snapshot,
          "paired capture of SDK and active shadow");
    const auto initial=melee_web_source_memory_history_observe(shadow.pre);
    check(initial.exact_pre&&initial.pre_piece_watermark,"paired PRE has tracked class piece");
    uint64_t prior=initial.current_watermark;
    const void* host_place=nullptr;
    uint32_t source_place=0;
    for(unsigned cycle=0;cycle<2;++cycle){
        auto* trial=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
        check(trial!=nullptr,"paired speculative allocation");
        MeleeWebSourceMemoryAllocation trial_identity{};
        check(melee_web_source_memory_allocation_read(trial,&trial_identity)==MELEE_WEB_SOURCE_MEMORY_READ_OK&&
              trial_identity.live&&trial_identity.allocation_generation>prior,
              "paired speculative allocation mints a fresh ID");
        fill(trial,kTrialBytes,0x49);
        OSFreeToHeap(heap,trial);
        auto* reused=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
        check(reused==trial,"SDK reuses the same physical address");
        fill(reused,kTrialBytes,0xA2);
        melee_web_class_piece_alloc(reused,32,32);
        const auto header=melee_web_source_memory_payload_current_endpoint_header(payload.snapshot);
        check(header.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&header.row_count==2,
              "selected shadow observes both live allocations");
        const auto row=melee_web_source_memory_payload_current_endpoint_row(payload.snapshot,&header,1);
        check(row.status==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK&&
              row.host_address==reinterpret_cast<uintptr_t>(reused)&&
              row.allocation_generation>trial_identity.allocation_generation,
              "paired replay mints a fresh allocation ID");
        prior=row.allocation_generation;
        if(cycle==0){host_place=reused;source_place=row.source_address;}
        else check(host_place==reused&&source_place==row.source_address,
                   "replay preserves physical and source placement");
        const auto a=melee_web_gameplay_heap_history_prepare(sdk.snapshot);
        const auto b=melee_web_source_memory_history_prepare(shadow.pre);
        check(a.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&b.status==MELEE_WEB_SOURCE_MEMORY_HISTORY_OK,
              "prepare both actual owners");
        const auto selected=melee_web_source_memory_history_observe(shadow.pre);
        check(!selected.exact_pre,"selected shadow differs from PRE");
        check(melee_web_gameplay_heap_history_admit(a.prepared)==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
              melee_web_source_memory_history_admit(b.prepared)==MELEE_WEB_SOURCE_MEMORY_HISTORY_OK,
              "final admit both actual owners before either write");
        allocation_budget=0;
        melee_web_gameplay_heap_history_commit_prevalidated(a.prepared);
        const auto status=melee_web_source_memory_history_commit_prevalidated(b.prepared);
        allocation_budget=-1;
        check(status==MELEE_WEB_SOURCE_MEMORY_HISTORY_OK,"paired no-allocation commit");
        const auto restored_sdk=melee_web_gameplay_heap_history_observe(sdk.snapshot);
        const auto restored=melee_web_source_memory_history_observe(shadow.pre);
        check(restored_sdk.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&restored_sdk.exact_equal&&
              restored_sdk.compared_bytes==kArenaBytes&&restored.exact_pre&&
              restored.current_revision==selected.current_revision+1&&
              restored.current_watermark==selected.current_watermark&&
              restored.current_piece_revision==selected.current_piece_revision+1&&
              restored.current_piece_watermark==selected.current_piece_watermark,
              "paired exact PRE with advancing revisions and preserved high-water marks");
        same_bytes(arena,pre.data(),kArenaBytes,"all actual arena bytes restored");
        check(melee_web_source_memory_payload_snapshot_admit(payload.snapshot)==
                  MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED,
              "legacy PRE token remains stale after exact restore");
        melee_web_gameplay_heap_history_prepared_destroy(a.prepared);
        melee_web_source_memory_history_prepared_destroy(b.prepared);
    }

    const auto a=melee_web_gameplay_heap_history_prepare(sdk.snapshot);
    const auto b=melee_web_source_memory_history_prepare(shadow.pre);
    check(a.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&b.status==MELEE_WEB_SOURCE_MEMORY_HISTORY_OK,
          "prepare stale-control pair");
    void* transient=OSAllocFromHeap(heap,kTrialBytes);
    check(transient!=nullptr,"stale-control real allocation");
    OSFreeToHeap(heap,transient);
    anchor[0]^=1;
    const std::vector<unsigned char> selected(arena,arena+kArenaBytes);
    const auto before=melee_web_source_memory_history_observe(shadow.pre);
    check(melee_web_gameplay_heap_history_admit(a.prepared)==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE&&
          melee_web_source_memory_history_admit(b.prepared)==MELEE_WEB_SOURCE_MEMORY_HISTORY_STALE_PREPARE,
          "both owners refuse stale prepare before either writer");
    same_bytes(arena,selected.data(),kArenaBytes,"stale pair leaves physical bytes unchanged");
    const auto after=melee_web_source_memory_history_observe(shadow.pre);
    check(after.exact_pre==before.exact_pre&&after.current_revision==before.current_revision&&
          after.current_watermark==before.current_watermark&&
          after.current_piece_revision==before.current_piece_revision&&
          after.current_piece_watermark==before.current_piece_watermark,
          "stale pair leaves shadow and piece fences unchanged");
    melee_web_gameplay_heap_history_prepared_destroy(a.prepared);
    melee_web_source_memory_history_prepared_destroy(b.prepared);
    melee_web_source_memory_payload_snapshot_destroy(payload.snapshot);
    melee_web_source_memory_history_destroy(shadow.pre);
    melee_web_gameplay_heap_history_snapshot_destroy(sdk.snapshot);
    OSFreeToHeap(heap,anchor);
    check(melee_web_source_memory_end(world),"retire paired shadow owner");
    OSSetCurrentHeap(-1);
    OSDestroyHeap(heap);
    check(melee_web_gameplay_heap_release(arena,error,sizeof(error)),"release paired SDK arena");
    std::free(arena);
    std::puts("Actual SDK and active shadow paired restore: passed two cycles");
}
#endif

int main(int argc,char** argv)
{
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(argc==2&&std::strcmp(argv[1],"joint")==0){paired_restore();return 0;}
#else
    (void)argc;(void)argv;
#endif
    void* arena_storage=std::malloc(kArenaBytes+64u);
    check(arena_storage!=nullptr,"allocate backing for an offset SDK arena");
    const uintptr_t storage_address=reinterpret_cast<uintptr_t>(arena_storage);
    const uintptr_t wanted_mod=alignof(HeapDesc);
    const uintptr_t delta=(wanted_mod+32u-(storage_address&31u))&31u;
    auto* arena=reinterpret_cast<void*>(storage_address+delta);
    check(wanted_mod&&wanted_mod<32&&
          (reinterpret_cast<uintptr_t>(arena)&31u)==wanted_mod&&
          (reinterpret_cast<uintptr_t>(arena)%alignof(HeapDesc))==0,
          "arena satisfies SDK descriptor alignment without 32-byte base alignment");
    void* start=melee_web_gameplay_heap_initialize(arena,kArenaBytes,error,sizeof(error));
    check(start&&melee_web_gameplay_heap_owns(arena),"claim stable gameplay arena");
    OSHeapHandle heap=OSCreateHeap(start,static_cast<u8*>(arena)+kArenaBytes);
    check(heap==0&&OSCheckHeap(heap)>0,"create actual original one-heap descriptor");
    OSSetCurrentHeap(heap);
    auto* anchor=static_cast<unsigned char*>(OSAllocFromHeap(heap,kAnchorBytes));
    check(anchor&&OSReferentSize(anchor)>=kAnchorBytes,"allocate real SDK PRE payload");
    fill(anchor,kAnchorBytes,0x21);
    const s32 free_pre=OSCheckHeap(heap);
    std::vector<unsigned char> pre(static_cast<unsigned char*>(arena),static_cast<unsigned char*>(arena)+kArenaBytes);

    // Wrong selected-heap shape refuses before touching bytes.
    OSSetCurrentHeap(-1);
    auto wrong_selected=melee_web_gameplay_heap_history_capture();
    check(wrong_selected.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER&&
          !wrong_selected.snapshot,"capture rejects nonselected owner tuple");
    OSSetCurrentHeap(heap);
    same_bytes(arena,pre.data(),kArenaBytes,"wrong selection leaves arena intact");

    // Corrupt only a real private SDK root link. The validator must reject its
    // out-of-arena address before calling OSCheckHeap or dereferencing it.
    Cell* const free_head=sHeapArray[heap].freeList;
    sHeapArray[heap].freeList=reinterpret_cast<Cell*>(
        reinterpret_cast<uintptr_t>(arena)+kArenaBytes+32u);
    std::vector<unsigned char> malformed(static_cast<unsigned char*>(arena),
        static_cast<unsigned char*>(arena)+kArenaBytes);
    auto invalid_capture=melee_web_gameplay_heap_history_capture();
    check(invalid_capture.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE&&
          !invalid_capture.snapshot,"capture refuses malformed live SDK link");
    uint64_t invalid_visit_count=99;
    check(melee_web_gameplay_heap_history_visit_allocations(nullptr,collect_row,nullptr,
              &invalid_visit_count)==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT&&
          invalid_visit_count==0,"invalid visitor arguments clear count and refuse");
    same_bytes(arena,malformed.data(),kArenaBytes,
               "invalid capture does not further mutate malformed owner");
    sHeapArray[heap].freeList=free_head;
    same_bytes(arena,pre.data(),kArenaBytes,"restore test-only corrupted link");

    auto captured=melee_web_gameplay_heap_history_capture();
    check(captured.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&captured.snapshot,
          "capture exact real PRE arena bytes and owner identity");

    const auto equal_observation=melee_web_gameplay_heap_history_observe(captured.snapshot);
    check(equal_observation.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          equal_observation.schema==1&&equal_observation.bytes==64&&
          equal_observation.exact_equal==1&&equal_observation.selected_heap==heap&&
          equal_observation.owner_epoch==captured.snapshot->epoch&&
          equal_observation.arena_base==reinterpret_cast<uintptr_t>(arena)&&
          equal_observation.arena_bytes==kArenaBytes&&
          equal_observation.compared_bytes==kArenaBytes&&
          equal_observation.first_difference==UINT64_MAX,
          "equal observer result preserves complete bytes and exact owner receipt");
    const size_t observer_offsets[]={0,kArenaBytes/2,kArenaBytes-1,3};
    for(const size_t offset:observer_offsets){
        const u8 saved=captured.snapshot->image[offset];
        captured.snapshot->image[offset]=static_cast<u8>(saved^0xFFu);
        const auto difference=melee_web_gameplay_heap_history_observe(captured.snapshot);
        check(difference.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
              difference.schema==1&&difference.bytes==64&&!difference.exact_equal&&
              difference.selected_heap==heap&&
              difference.owner_epoch==captured.snapshot->epoch&&
              difference.arena_base==reinterpret_cast<uintptr_t>(arena)&&
              difference.arena_bytes==kArenaBytes&&
              difference.first_difference==offset&&
              difference.compared_bytes==offset+1,
              "observer reports exact first, middle, last, and unaligned differences");
        captured.snapshot->image[offset]=saved;
    }
    const uint64_t saved_epoch=captured.snapshot->epoch;
    ++captured.snapshot->epoch;
    const auto refused_observation=melee_web_gameplay_heap_history_observe(captured.snapshot);
    captured.snapshot->epoch=saved_epoch;
    check(refused_observation.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER&&
          refused_observation.schema==0&&refused_observation.bytes==0&&
          refused_observation.arena_base==0&&refused_observation.arena_bytes==0&&
          refused_observation.compared_bytes==0,
          "observer preserves stale-owner refusal before comparing arena bytes");

    auto* first=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
    check(first,"branch original allocation succeeds");
    const uintptr_t first_address=reinterpret_cast<uintptr_t>(first);
    fill(first,kTrialBytes,0x53);
    OSFreeToHeap(heap,first);
    auto* reused=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
    check(reused&&reinterpret_cast<uintptr_t>(reused)==first_address,
          "original SDK free list reuses same branch address");
    fill(reused,kTrialBytes,0xC1);
    std::vector<unsigned char> branch(static_cast<unsigned char*>(arena),
        static_cast<unsigned char*>(arena)+kArenaBytes);
    check(OSCheckHeap(heap)<free_pre,"branch allocation changes actual SDK heap");

    // Preparation rejects malformed live allocator metadata without mutation.
    Cell* const branch_free_head=sHeapArray[heap].freeList;
    sHeapArray[heap].freeList=reinterpret_cast<Cell*>(
        reinterpret_cast<uintptr_t>(arena)+kArenaBytes+64u);
    std::vector<unsigned char> malformed_branch(static_cast<unsigned char*>(arena),
        static_cast<unsigned char*>(arena)+kArenaBytes);
    auto failed_prepare=melee_web_gameplay_heap_history_prepare(captured.snapshot);
    check(failed_prepare.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE&&
          !failed_prepare.prepared,"prepare refuses malformed branch allocator graph");
    check(melee_web_gameplay_heap_history_current_admit(captured.snapshot)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_STATE,
          "selected-current proof refuses malformed live SDK graph");
    same_bytes(arena,malformed_branch.data(),kArenaBytes,
               "failed preparation leaves malformed live bytes unchanged");
    sHeapArray[heap].freeList=branch_free_head;
    same_bytes(arena,branch.data(),kArenaBytes,"restore test-only branch link");

    auto prepared_result=melee_web_gameplay_heap_history_prepare(captured.snapshot);
    check(prepared_result.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          prepared_result.prepared,"prepare immutable PRE image and full branch byte fence");
    auto* prepared=prepared_result.prepared;
    same_bytes(arena,branch.data(),kArenaBytes,"prepare leaves selected END unchanged");
    check(melee_web_gameplay_heap_history_commit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_NOT_ADMITTED,
          "checked commit refuses before final admission");
    same_bytes(arena,branch.data(),kArenaBytes,"pre-admission refusal is nonmutating");
    allocation_budget=0;
    const auto branch_current=melee_web_gameplay_heap_history_current_admit(captured.snapshot);
    const auto branch_observation=melee_web_gameplay_heap_history_observe(captured.snapshot);
    allocation_budget=-1;
    check(branch_current==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          branch_observation.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          branch_observation.schema==1&&branch_observation.bytes==64&&
          branch_observation.selected_heap==heap&&
          branch_observation.owner_epoch&&
          branch_observation.arena_base==reinterpret_cast<uintptr_t>(arena)&&
          branch_observation.arena_bytes==kArenaBytes&&
          !branch_observation.exact_equal&&
          branch_observation.first_difference<kArenaBytes&&
          branch_observation.compared_bytes==branch_observation.first_difference+1&&
          branch_observation.compared_bytes<kArenaBytes,
          "no-allocation selected-current proof reports truthful partial mismatch count");
    Rows rows{}; uint64_t visited=0;
    allocation_budget=0;
    const auto visit_status=melee_web_gameplay_heap_history_visit_allocations(
        captured.snapshot,collect_row,&rows,&visited);
    allocation_budget=-1;
    check(visit_status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&visited==2&&rows.count==2,
          "allocation-free visitor enumerates every live original SDK allocation");
    bool saw_anchor=false,saw_reused=false;
    for(uint64_t i=0;i<visited;++i){
        if(rows.payloads[i]==anchor){saw_anchor=true;check(rows.capacities[i]>=kAnchorBytes,
            "visitor reports original SDK payload capacity for anchor");}
        if(rows.payloads[i]==reused){saw_reused=true;check(rows.capacities[i]>=kTrialBytes,
            "visitor reports original SDK payload capacity for branch row");}
    }
    check(saw_anchor&&saw_reused,
          "visitor's exact live host pointers cover anchor and branch allocation");

    // Mutate one allocated payload byte after preparation. Admission and the
    // standalone checked commit must each refuse without changing that byte.
    const unsigned char saved_payload_byte=reused[7];
    reused[7]^=0x55;
    std::vector<unsigned char> changed_after_prepare(static_cast<unsigned char*>(arena),
        static_cast<unsigned char*>(arena)+kArenaBytes);
    check(melee_web_gameplay_heap_history_admit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE,
          "final admission detects selected-END payload mutation after prepare");
    same_bytes(arena,changed_after_prepare.data(),kArenaBytes,
               "failed final admission preserves current branch bytes");
    reused[7]=saved_payload_byte;
    same_bytes(arena,branch.data(),kArenaBytes,"re-establish prepared byte fence");
    check(melee_web_gameplay_heap_history_admit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK,
          "exact unchanged branch re-admits after the no-source-work fence");
    reused[7]^=0x11;
    std::vector<unsigned char> changed_after_admit(static_cast<unsigned char*>(arena),
        static_cast<unsigned char*>(arena)+kArenaBytes);
    check(melee_web_gameplay_heap_history_commit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_LIVE_STATE,
          "standalone checked commit catches mutation after final admission");
    same_bytes(arena,changed_after_admit.data(),kArenaBytes,
               "checked-commit refusal preserves live owner");
    reused[7]=saved_payload_byte;
    same_bytes(arena,branch.data(),kArenaBytes,"restore exact selected-END fence");
    check(melee_web_gameplay_heap_history_admit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          melee_web_gameplay_heap_history_commit(prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK,
          "checked standalone commit restores the admitted arena image");
    same_bytes(arena,pre.data(),kArenaBytes,"restored actual arena bytes equal captured PRE");
    auto restored_observation=melee_web_gameplay_heap_history_observe(captured.snapshot);
    check(restored_observation.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          restored_observation.exact_equal&&
          restored_observation.arena_base==reinterpret_cast<uintptr_t>(arena)&&
          restored_observation.compared_bytes==kArenaBytes&&
          restored_observation.first_difference==UINT64_MAX,
          "observation proves full actual restored arena equals captured PRE");
    check(OSCheckHeap(heap)==free_pre&&OSReferentSize(anchor)>=kAnchorBytes,
          "restored original allocator descriptor and PRE allocation validate");

    // Replay identical original SDK operations and compare the complete arena,
    // not just a returned host pointer.
    auto* replay_first=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
    check(replay_first&&reinterpret_cast<uintptr_t>(replay_first)==first_address,
          "first replay allocation matches branch address");
    fill(replay_first,kTrialBytes,0x53);
    OSFreeToHeap(heap,replay_first);
    auto* replay=static_cast<unsigned char*>(OSAllocFromHeap(heap,kTrialBytes));
    check(replay&&reinterpret_cast<uintptr_t>(replay)==first_address,
          "same-address reuse replay matches branch address");
    fill(replay,kTrialBytes,0xC1);
    same_bytes(arena,branch.data(),kArenaBytes,
               "same alloc/free/alloc replay restores complete branch arena bytes");
    check(OSCheckHeap(heap)>0,"replayed heap remains checkable");

    // Exercise the aggregate-only prevalidated leaf: all owners have passed
    // admission; a preceding aggregate owner is permitted to write into a PRE
    // arena payload before this slot, and this leaf performs only the fixed
    // restore copy without a late payload-fence refusal.
    auto second_prepare=melee_web_gameplay_heap_history_prepare(captured.snapshot);
    check(second_prepare.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          second_prepare.prepared,"prepare second aggregate leaf");
    check(melee_web_gameplay_heap_history_admit(second_prepare.prepared)==
              MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK,
          "admit second aggregate leaf before transaction writes");
    anchor[3]^=0x77;
    melee_web_gameplay_heap_history_commit_prevalidated(second_prepare.prepared);
    same_bytes(arena,pre.data(),kArenaBytes,
               "prevalidated aggregate leaf restores exact PRE after earlier owner write");
    restored_observation=melee_web_gameplay_heap_history_observe(captured.snapshot);
    check(restored_observation.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK&&
          restored_observation.exact_equal&&
          restored_observation.compared_bytes==kArenaBytes,
          "post-aggregate observation uses actual live arena bytes");

    // Same base and handle do not revive a snapshot after owner retirement.
    // The prevalidated copy already removed the branch-only replay allocation.
    OSFreeToHeap(heap,anchor);
    OSSetCurrentHeap(-1);
    OSDestroyHeap(heap);
    check(melee_web_gameplay_heap_release(arena,error,sizeof(error)),
          "retire exact first SDK arena owner");
    start=melee_web_gameplay_heap_initialize(arena,kArenaBytes,error,sizeof(error));
    check(start,"reinitialize same physical arena under new owner epoch");
    heap=OSCreateHeap(start,static_cast<u8*>(arena)+kArenaBytes);
    check(heap==0,"create same public heap handle under new owner");
    OSSetCurrentHeap(heap);
    auto stale=melee_web_gameplay_heap_history_prepare(captured.snapshot);
    check(stale.status==MELEE_WEB_GAMEPLAY_HEAP_HISTORY_STALE_OWNER&&
          !stale.prepared,"retired epoch rejects snapshot at reused same arena address");
    melee_web_gameplay_heap_history_prepared_destroy(second_prepare.prepared);
    melee_web_gameplay_heap_history_prepared_destroy(prepared);
    melee_web_gameplay_heap_history_snapshot_destroy(captured.snapshot);
    OSSetCurrentHeap(-1);
    OSDestroyHeap(heap);
    check(melee_web_gameplay_heap_release(arena,error,sizeof(error)),
          "release reinitialized SDK owner");
    std::free(arena_storage);
    std::printf("SDK arena history leaf controls: passed; arena_base_mod32=%llu sdk_heapdesc_align=%zu sdk_heapdesc_size=%zu branch_compared=%llu arena_bytes=%llu first_difference=%llu\n",
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(arena)&31u),
        alignof(HeapDesc),sizeof(HeapDesc),
        static_cast<unsigned long long>(branch_observation.compared_bytes),
        static_cast<unsigned long long>(branch_observation.arena_bytes),
        static_cast<unsigned long long>(branch_observation.first_difference));
    return 0;
}
