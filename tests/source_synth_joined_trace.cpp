/* Retain the original allocation fixture's checked boot/heap construction and
 * continue in the same fresh process through the joined audio service owner. */
#define main melee_web_source_hsd_prefix_main
#include "original_startup_alloc_trace.cpp"
#undef main
#include "source_synth_joined_services.h"
#include <climits>
#include <vector>

#if defined(MELEE_WEB_SOURCE_POST_AUDIO_STARTUP)
#include "source_post_audio_allocations_accessors.h"
extern "C" {
#include <melee/lb/lbheap.h>
void lbMemory_8001564C(void);
}

static void run_post_audio()
{
    unsigned stack_before, free_before;
    melee_web_source_synth_aram_state(&stack_before, &free_before);
    const auto main_free_before = OSCheckHeap(HSD_GetHeap());
    const auto audio_free_before = OSCheckHeap(HSD_Synth_804D6018);
    if (main_free_before < 0 || audio_free_before < 0) fail("pre-memory heap integrity failed");
    lbMemory_8001564C();
    MeleeWebSourceLBMemorySnapshot memory{};
    if (melee_web_source_lbmemory_snapshot(&memory) != 1 ||
        memory.arena_lo != stack_before || memory.arena_hi <= memory.arena_lo ||
        memory.root_matches_arena != 1 || memory.root_handle_index != 0 ||
        memory.root_next != 0 || memory.root_next_pool != 0 ||
        memory.root_prev != 0 || memory.root_prev_pool != 0 ||
        memory.root_span_bytes != memory.arena_hi - memory.arena_lo ||
        memory.free_mem_head_index != 0 || memory.free_heap_head_index != 1 ||
        memory.num_allocs != 0 || memory.max_num_allocs != 0 || memory.manager_size != 0 ||
        memory.mem_entry_count != MELEE_WEB_SOURCE_LB_MEMORY_ENTRY_COUNT ||
        memory.heap_handle_count != MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT)
        fail("original lbMemory root or free-list state mismatch");
    lbHeap_80015F3C();
    MeleeWebSourceLBHeapSnapshot heap{};
    if (melee_web_source_lbheap_snapshot(&heap) != 1 ||
        heap.aram_lo != memory.arena_lo || heap.aram_hi != memory.arena_hi ||
        heap.heap_count != MELEE_WEB_SOURCE_LB_HEAP_HANDLE_COUNT ||
        heap.descriptor_count != MELEE_WEB_SOURCE_LB_HEAP_DESCRIPTOR_COUNT)
        fail("original lbHeap bounds or authored counts mismatch");
    void* arena_lo = nullptr;
    void* arena_hi = nullptr;
    HSD_GetNextArena(&arena_lo, &arena_hi);
    if (heap.arena_lo != reinterpret_cast<uintptr_t>(arena_lo) ||
        heap.arena_hi != reinterpret_cast<uintptr_t>(arena_hi))
        fail("original lbHeap MEM1 bounds lost their HSD owner");
    for (unsigned i = 0; i < heap.heap_count; ++i) {
        const auto& entry = heap.heaps[i];
        if (entry.index != i || entry.id != -1 || entry.handle != UINT32_MAX ||
            entry.transient != 1 || entry.status != LbHeapStatus_Destroy)
            fail("lbHeap descriptor initialization created an unexpected live heap");
    }
    for (unsigned i = 0; i + 1 < heap.descriptor_count; ++i) {
        const auto& desc = heap.descriptors[i];
        if (desc.index >= heap.heap_count ||
            (desc.previous_index != heap.heap_count && desc.previous_index >= desc.index))
            fail("original heap descriptor relationship is invalid");
        const auto& entry = heap.heaps[desc.index];
        if (entry.type != static_cast<int32_t>(desc.type) || entry.size != desc.size)
            fail("original heap entry differs from its authored descriptor");
    }
    if (heap.descriptors[heap.descriptor_count-1].index != heap.heap_count)
        fail("original heap descriptor sentinel changed");
    unsigned stack_after, free_after;
    melee_web_source_synth_aram_state(&stack_after, &free_after);
    if (stack_after != stack_before || free_after != free_before ||
        OSCheckHeap(HSD_GetHeap()) != main_free_before ||
        OSCheckHeap(HSD_Synth_804D6018) != audio_free_before)
        fail("memory descriptor startup changed retained AR or HSD allocation ownership");
    std::printf("{\"kind\":\"source_post_audio_startup\",\"aram_stack\":%u,\"aram_free_blocks\":%u,\"root_handle_index\":%u,\"root_handle_words\":[%u,%u,%u,%u],\"game_heap_words\":[%u,%u,%u,%u",
                stack_after, free_after, memory.root_handle_index,
                memory.root_next, memory.root_lo, memory.root_hi, memory.root_prev,
                static_cast<unsigned>(source_address(arena_lo)),
                static_cast<unsigned>(source_address(arena_hi)), heap.aram_lo, heap.aram_hi);
    for (unsigned i = 0; i < heap.heap_count; ++i) {
        const auto& entry = heap.heaps[i];
        const unsigned start = entry.type == 4 ? entry.start :
            static_cast<unsigned>(source_address(reinterpret_cast<void*>(static_cast<uintptr_t>(entry.start))));
        std::printf(",%u,%u,%u,%u,%u,%u,%u", static_cast<unsigned>(entry.id),
                    entry.handle, start, entry.size, static_cast<unsigned>(entry.type),
                    static_cast<unsigned>(entry.transient), static_cast<unsigned>(entry.status));
    }
    std::printf("],\"free_mem_index\":%u,\"free_heap_index\":%u,\"runtime_claim\":false}\n",
                memory.free_mem_head_index, memory.free_heap_head_index);
}
#endif

int main(int argc, char** argv)
{
    if (argc < 7) fail("expected mode, four source-derived driver arguments, SRAM settings path, then boot argument pairs");
    int driver[4];
    for (unsigned i = 0; i < 4; ++i) {
        const u32 value = number(argv[i + 2], "source-derived audio argument");
        if (value > INT_MAX) fail("audio argument is outside original signed-int range");
        driver[i] = static_cast<int>(value);
    }
    unsigned char settings[64];
    FILE* input = std::fopen(argv[6], "rb");
    if (!input) fail("owned SRAM settings file is unavailable");
    const size_t bytes = std::fread(settings, 1, sizeof(settings), input);
    const int tail = std::fgetc(input);
    const bool read_failed = std::ferror(input) != 0;
    std::fclose(input);
    if (bytes != sizeof(settings) || tail != EOF || read_failed)
        fail("SRAM settings must be exactly 64 independently owned bytes");
    std::vector<char*> prefix_arguments{argv[0]};
    for (int i = 7; i < argc; ++i) prefix_arguments.push_back(argv[i]);
    const int prefix_argc = static_cast<int>(prefix_arguments.size());
    prefix_arguments.push_back(nullptr);
    const int status = melee_web_source_hsd_prefix_main(prefix_argc, prefix_arguments.data());
    if (status) return status;
    const int audio_status = melee_web_source_synth_joined_run(driver[0], driver[1], driver[2], driver[3],
                                                              settings, sizeof(settings), argv[1]);
    if (audio_status) return audio_status;
#if defined(MELEE_WEB_SOURCE_POST_AUDIO_STARTUP)
    run_post_audio();
#endif
    return 0;
}
