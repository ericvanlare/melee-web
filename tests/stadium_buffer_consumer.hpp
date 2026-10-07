#pragma once

#include "gameplay_source_memory_runtime.h"
#include "gameplay_source_preload.h"
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace melee_web::test::stadium_buffer {

inline bool owner_context_supported(
    MeleeWebSourceMemoryReadStatus status,
    const MeleeWebSourceMemoryContext& context, int os_current_heap,
    int hsd_heap, uint64_t world_generation)
{
    return status == MELEE_WEB_SOURCE_MEMORY_READ_OK && os_current_heap >= 0 &&
           hsd_heap == os_current_heap &&
           context.source_heap_handle == hsd_heap &&
           context.world_generation != 0 &&
           context.world_generation == world_generation &&
           context.allocation_generation_watermark <=
               std::numeric_limits<uint64_t>::max() - 2;
}

inline bool actual_layout_supported(
    uint32_t execution_pointer_bytes,
    const MeleeWebStadiumBufferLayout& layout)
{
    const uint64_t descriptor_end =
        uint64_t(layout.desc_offset) + layout.image_desc_bytes;
    const uint64_t flag_end =
        uint64_t(layout.flag_storage_offset) + layout.flag_storage_bytes;
    return execution_pointer_bytes == 4 && layout.pointer_bytes == 4 &&
           layout.image_desc_bytes == 0x18 && layout.wrapper_bytes > 0x1c &&
           layout.desc_offset == 0 && layout.flag_storage_offset == 0x18 &&
           layout.flag_storage_bytes == sizeof(uint16_t) &&
           layout.x1a_offset == 0x1a && layout.x1c_offset == 0x1c &&
           layout.constructor_allocation_bytes == 0x1c &&
           layout.written_prefix_bytes == flag_end &&
           descriptor_end <= flag_end && flag_end <= 0x1c &&
           uint64_t(layout.x1c_offset) + sizeof(uint16_t) <=
               layout.wrapper_bytes;
}

inline void pure_gate_checks(const MeleeWebStadiumBufferLayout& actual_layout)
{
    if (!actual_layout_supported(4, actual_layout))
        throw std::runtime_error(
            "Actual Stadium buffer source layout does not fit the checked Emscripten32 prefix");
    if (actual_layout_supported(8, actual_layout))
        throw std::runtime_error(
            "Unsupported host64 ABI passed the pure Stadium buffer gate");
    auto invalid_layout = actual_layout;
    invalid_layout.flag_storage_offset = 0x1c;
    if (actual_layout_supported(4, invalid_layout))
        throw std::runtime_error(
            "Flag storage outside the original 0x1c allocation passed the pure gate");
}

}  // namespace melee_web::test::stadium_buffer
