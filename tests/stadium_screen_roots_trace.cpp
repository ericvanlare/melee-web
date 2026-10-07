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
    const auto constructor_facts = actual_pre_call_facts(layout);
    require(original_constructor_pre_call_supported(constructor_facts),
            "Actual original-constructor pre-call facts did not pass the pure gate");
    auto rejected_facts = constructor_facts;
    rejected_facts.memory_status = MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Inactive source-memory owner passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.source_memory_healthy = false;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Unhealthy source-memory owner passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.os_current_heap += 1;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Mismatched OS heap passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.hsd_heap += 1;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Mismatched HSD heap passed the pure constructor gate");
    rejected_facts = constructor_facts;
    ++rejected_facts.world_generation;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Mismatched source-world generation passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.preload_status = MELEE_WEB_SOURCE_PRELOAD_PRESENT;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Present 0x7d3 preload match passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.execution_pointer_bytes = 8;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Unsupported host64 ABI passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.compiler_flag_access.matches_reviewed_actual_tu = false;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Unproven compiler flag access passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.compiler_flag_access.byte_width = 2;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Incorrect compiler flag access width passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.p_link_max = 0x11;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Invalid authored p_link bound passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.gx_link_max = UINT8_MAX;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "GX max-link sentinel overflow passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.gobj_tables_ready = false;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Unavailable GObj tables passed the pure constructor gate");
    rejected_facts = constructor_facts;
    rejected_facts.immediate_removal_context = false;
    require(!original_constructor_pre_call_supported(rejected_facts),
            "Deferred-removal/callback context passed the pure constructor gate");
    auto exhausted_pair = constructor_facts.memory_context;
    exhausted_pair.allocation_generation_watermark = UINT64_MAX - 4;
    require(!pair_generation_headroom_supported(exhausted_pair),
            "Insufficient source-generation headroom passed the two-lifetime gate");
    std::cout << "Stadium actual-TU layout: ptr=" << layout.pointer_bytes
              << " HSD_ImageDesc=" << layout.image_desc_bytes
              << " ImageDescWrapper=" << layout.wrapper_bytes
              << " desc@" << layout.desc_offset
              << " flag-container-candidate@"
              << layout.flag_container_candidate_offset << "+"
              << layout.flag_container_candidate_bytes << " x1A@" << layout.x1a_offset
              << " x1C@" << layout.x1c_offset
              << " allocation=" << layout.constructor_allocation_bytes
              << " layout-prefix-bound=" << layout.layout_prefix_bound_bytes
              << "; actual-layout and pure pre-call controls passed\n";
}
}
int main(){try{
    char error[256]{};
    require(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
    require(melee_web_native_world_enable(error,sizeof(error)),error);
    source_observer_preflight();
    melee_web::test::stadium_buffer::run_original_constructor_lifetimes([] {});
    synthetic_checks();
    live_source_consumer_checks();
    require(melee_web_gameplay_shutdown(error,sizeof(error)),error);
    std::cout<<"Screen roots synthetic canonical IMAGE, writable SIS, catalog negatives and two lifetimes passed\n";
    std::cout<<"Live Stadium IMAGE source hit/miss/remove passed twice\n";
    std::cout<<"Original Stadium auxiliary IMAGE constructor/remove passed twice without callback dispatch or stage entry\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
