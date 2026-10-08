#include "stadium_live_image_consumer.hpp"
#include "stadium_buffer_consumer.hpp"
#include "stadium_ground_owner_contract.hpp"
#include <array>
#include <iostream>
#include <string>
extern "C" {
#include <sysdolphin/baselib/initialize.h>
}
using namespace melee_web::test::stadium_screen;
namespace {
void owner_contract_pure_controls()
{
    using namespace melee_web::test::stadium_buffer;
    const std::array<melee_web::test::stadium_ground::SourceMarkerRow, 1>
        authored_marker_rows{{{0x100, {{2, 0x87}}}}};
    const auto different_root_selection =
        melee_web::test::stadium_ground::select_source_markers_for_root(
            authored_marker_rows, 0x200);
    require(different_root_selection.authored_pair_count == 1 &&
                different_root_selection.matched_row_count == 0 &&
                different_root_selection.map_bindings.empty(),
            "Different authored marker root incorrectly matched the selected map");
    const auto exact_root_selection =
        melee_web::test::stadium_ground::select_source_markers_for_root(
            authored_marker_rows, 0x100);
    require(exact_root_selection.authored_pair_count == 1 &&
                exact_root_selection.matched_row_count == 1 &&
                exact_root_selection.map_bindings.size() == 1 &&
                exact_root_selection.map_bindings[0].joint_index == 2 &&
                exact_root_selection.map_bindings[0].marker_id == 0x87,
            "Exact authored marker root did not select its original pair");
    const std::array<melee_web::test::stadium_ground::SourceMarkerRow, 2>
        duplicate_root_rows{{{0x100, {{2, 0x87}}},
                             {0x100, {{3, 0x88}}}}};
    const auto first_root_selection =
        melee_web::test::stadium_ground::select_source_markers_for_root(
            duplicate_root_rows, 0x100);
    require(first_root_selection.authored_pair_count == 2 &&
                first_root_selection.matched_row_count == 1 &&
                first_root_selection.map_bindings.size() == 1 &&
                first_root_selection.map_bindings[0].joint_index == 2 &&
                first_root_selection.map_bindings[0].marker_id == 0x87,
            "Duplicate marker roots did not preserve original first-row selection");

    const MeleeWebSourceMemoryContext before_free{7, 19, 24};
    const MeleeWebSourceMemoryAllocation live_allocation{7, 64, 19, 24, 1};
    require(exact_live_allocation_matches_context(
                MELEE_WEB_SOURCE_MEMORY_READ_OK, live_allocation,
                before_free, 64, 24),
            "Exact live payload in its current owner context was rejected");
    const MeleeWebSourceMemoryAllocation retired_allocation{7, 0, 19, 0, 0};
    require(exact_retired_allocation_supported(
                MELEE_WEB_SOURCE_MEMORY_READ_OK, retired_allocation,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, before_free,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, before_free),
            "Exact retired payload with a stable generation watermark was rejected");
    MeleeWebSourceMemoryAllocation reused_live_allocation{7, 64, 19, 25, 1};
    const MeleeWebSourceMemoryContext after_reuse{7, 19, 25};
    require(!exact_retired_allocation_supported(
                MELEE_WEB_SOURCE_MEMORY_READ_OK, reused_live_allocation,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, before_free,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, after_reuse),
            "A reused exact address passed the retired-payload check");
    require(!exact_retired_allocation_supported(
                MELEE_WEB_SOURCE_MEMORY_READ_OK, retired_allocation,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, before_free,
                MELEE_WEB_SOURCE_MEMORY_READ_OK, after_reuse),
            "A freed payload with a newer reuse generation passed retirement");
    MeleeWebSourceMemoryAllocation changed_generation_allocation{7, 64, 19, 25, 1};
    require(!exact_live_allocation_matches_context(
                MELEE_WEB_SOURCE_MEMORY_READ_OK, changed_generation_allocation,
                after_reuse, 64, 24),
            "A newer lease at the same address passed the original-generation check");
}

void source_observer_preflight()
{
    using namespace melee_web::test::stadium_buffer;
    owner_contract_pure_controls();
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
int main(int argc, char** argv){try{
    if (argc == 2 && std::string(argv[1]) == "--pure-owner-controls") {
        owner_contract_pure_controls();
        std::cout << "Stadium Ground owner pure selector/lease controls passed\n";
        return 0;
    }
    if (argc != 1)
        throw std::runtime_error("Expected optional --pure-owner-controls");
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
