#include "stadium_live_image_consumer.hpp"
#include "stadium_buffer_consumer.hpp"
#include <iostream>
extern "C" {
#include <sysdolphin/baselib/initialize.h>
}
using namespace melee_web::test::stadium_screen;
namespace {
void source_observer_preflight()
{
    using namespace melee_web::test::stadium_buffer;
    MeleeWebSourceMemoryContext context{};
    const auto context_status = melee_web_source_memory_context_read(&context);
    const auto stats = melee_web_gameplay_stats();
    require(owner_context_supported(context_status, context, __OSCurrHeap,
                                    HSD_GetHeap(), stats.generation),
            "Actual source-memory context does not match the active OS/HSD/world owner");
    auto mismatched_heap = context;
    ++mismatched_heap.source_heap_handle;
    require(!owner_context_supported(context_status, mismatched_heap,
                                     __OSCurrHeap, HSD_GetHeap(),
                                     stats.generation),
            "Mismatched source heap passed the pure Stadium owner gate");
    auto mismatched_world = context;
    ++mismatched_world.world_generation;
    require(!owner_context_supported(context_status, mismatched_world,
                                     __OSCurrHeap, HSD_GetHeap(),
                                     stats.generation),
            "Mismatched source world passed the pure Stadium owner gate");
    auto exhausted_generations = context;
    exhausted_generations.allocation_generation_watermark =
        UINT64_MAX - 1;
    require(!owner_context_supported(context_status, exhausted_generations,
                                     __OSCurrHeap, HSD_GetHeap(),
                                     stats.generation),
            "Generation rollover risk passed the pure Stadium owner gate");

    require(melee_web_source_preload_observe(0x7D3) ==
                MELEE_WEB_SOURCE_PRELOAD_ABSENT,
            "Actual preload cache already matches Stadium entry 0x7D3");
    require(melee_web_source_preload_observe(-1) ==
                MELEE_WEB_SOURCE_PRELOAD_INVALID,
            "Invalid preload query was not rejected explicitly");
    require(!melee_web_source_preload_predicate(0, 1, 0x7D3, 0x7D3) &&
                !melee_web_source_preload_predicate(1, 0, 0x7D3, 0x7D3) &&
                !melee_web_source_preload_predicate(1, 1, 0x7D4, 0x7D3) &&
                melee_web_source_preload_predicate(1, 1, 0x7D3, 0x7D3),
            "Synthetic preload predicate controls differ from the source lookup rule");

    MeleeWebStadiumBufferLayout layout{};
    require(melee_web_stadium_buffer_layout_read(&layout),
            "Actual Stadium source translation unit did not report private layout facts");
    pure_gate_checks(layout);
    std::cout << "Stadium actual-TU layout: ptr=" << layout.pointer_bytes
              << " HSD_ImageDesc=" << layout.image_desc_bytes
              << " ImageDescWrapper=" << layout.wrapper_bytes
              << " desc@" << layout.desc_offset
              << " flag-storage@" << layout.flag_storage_offset << "+"
              << layout.flag_storage_bytes << " x1A@" << layout.x1a_offset
              << " x1C@" << layout.x1c_offset
              << " allocation=" << layout.constructor_allocation_bytes
              << " written-prefix=" << layout.written_prefix_bytes
              << "; ABI gate passed, constructor not called\n";
}
}
int main(){try{
    char error[256]{};
    require(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
    require(melee_web_native_world_enable(error,sizeof(error)),error);
    source_observer_preflight();
    synthetic_checks();
    live_source_consumer_checks();
    require(melee_web_gameplay_shutdown(error,sizeof(error)),error);
    std::cout<<"Screen roots synthetic canonical IMAGE, writable SIS, catalog negatives and two lifetimes passed\n";
    std::cout<<"Live Stadium IMAGE source hit/miss/remove passed twice\n";
    std::cout<<"Pure source-memory, preload predicate and Stadium ABI gate controls passed; no 2D78 constructor call\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
