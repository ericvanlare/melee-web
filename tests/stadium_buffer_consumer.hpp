#pragma once

#include "gameplay_bootstrap.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_source_preload.h"
#include "stadium_live_image_consumer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

extern "C" {
#include <dolphin/gx.h>
#include <dolphin/os/OSAlloc.h>
#include <melee/gr/grpstadium.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/tobj.h>
}

namespace melee_web::test::stadium_buffer {

inline constexpr uint32_t kOriginalWrapperAllocationBytes = 0x1c;
inline constexpr uint32_t kOriginalPreloadEntry = 0x7d3;

/* Reviewed actual-TU Emscripten IR: generated grpstadium.c
 * dfd603804f36a8d53fa39a17be444b59df9d6586f887083c6ed40e44fcbeb23d,
 * full IR c09d5a2dcc55c829552aea71e26b5cfab44c34050d1eee6e028cac0e4891eff9.
 * grStadium_801D2D78 lowers its flag update to one i8 access at +0x18.
 * The identity supplement binds this proof to the pinned compiler/argv and the
 * exact dirty source state reviewed before this consumer was added. Recheck it
 * if the generated source, compiler, or target flags change. */
struct CompilerFlagAccessProof {
    uint32_t byte_offset;
    uint32_t byte_width;
    bool matches_reviewed_actual_tu;
};

inline constexpr CompilerFlagAccessProof kReviewedCompilerFlagAccess{
    0x18, 1, true};

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

inline bool pair_generation_headroom_supported(
    const MeleeWebSourceMemoryContext& context)
{
    /* The first GObj_Create may lazily add one backing block; immediate removal
     * returns its pool slot for the second GObj_Create. Each call also adds the
     * exact wrapper and fallback-image payload, so the pair needs at most five
     * source allocation generations in total. */
    return context.allocation_generation_watermark <=
           std::numeric_limits<uint64_t>::max() - 5;
}

inline bool actual_layout_supported(
    uint32_t execution_pointer_bytes,
    const MeleeWebStadiumBufferLayout& layout)
{
    const uint64_t descriptor_end =
        uint64_t(layout.desc_offset) + layout.image_desc_bytes;
    const uint64_t candidate_end =
        uint64_t(layout.flag_container_candidate_offset) +
        layout.flag_container_candidate_bytes;
    return execution_pointer_bytes == 4 && layout.pointer_bytes == 4 &&
           layout.image_desc_bytes == 0x18 && layout.wrapper_bytes > 0x1c &&
           layout.desc_offset == 0 &&
           layout.flag_container_candidate_offset == 0x18 &&
           layout.flag_container_candidate_bytes == sizeof(uint16_t) &&
           layout.x1a_offset == 0x1a && layout.x1c_offset == 0x1c &&
           layout.constructor_allocation_bytes == kOriginalWrapperAllocationBytes &&
           layout.layout_prefix_bound_bytes == candidate_end &&
           descriptor_end <= candidate_end && candidate_end <= 0x1c &&
           uint64_t(layout.x1c_offset) + sizeof(uint16_t) <=
               layout.wrapper_bytes;
}

inline bool compiler_access_supported(
    uint32_t execution_pointer_bytes,
    const MeleeWebStadiumBufferLayout& layout,
    const CompilerFlagAccessProof& proof)
{
    const uint64_t access_end =
        uint64_t(proof.byte_offset) + proof.byte_width;
    return proof.matches_reviewed_actual_tu &&
           actual_layout_supported(execution_pointer_bytes, layout) &&
           proof.byte_offset == 0x18 && proof.byte_width == 1 &&
           proof.byte_offset == layout.flag_container_candidate_offset &&
           access_end <= layout.layout_prefix_bound_bytes &&
           access_end <= layout.constructor_allocation_bytes;
}

struct ConstructorPreCallFacts {
    MeleeWebSourceMemoryReadStatus memory_status{};
    MeleeWebSourceMemoryContext memory_context{};
    int os_current_heap{-1};
    int hsd_heap{-1};
    uint64_t world_generation{};
    bool world_active{};
    bool source_memory_healthy{};
    MeleeWebSourcePreloadStatus preload_status{
        MELEE_WEB_SOURCE_PRELOAD_INVALID};
    uint32_t execution_pointer_bytes{};
    MeleeWebStadiumBufferLayout layout{};
    CompilerFlagAccessProof compiler_flag_access{};
    bool gobj_tables_ready{};
    uint32_t p_link_max{};
    uint32_t gx_link_max{};
    bool immediate_removal_context{};
};

inline bool original_constructor_pre_call_supported(
    const ConstructorPreCallFacts& facts)
{
    return facts.world_active && facts.source_memory_healthy &&
           owner_context_supported(facts.memory_status, facts.memory_context,
                                   facts.os_current_heap, facts.hsd_heap,
                                   facts.world_generation) &&
           facts.preload_status == MELEE_WEB_SOURCE_PRELOAD_ABSENT &&
           compiler_access_supported(facts.execution_pointer_bytes,
                                     facts.layout,
                                     facts.compiler_flag_access) &&
           facts.gobj_tables_ready && facts.p_link_max >= 0x12 &&
           facts.gx_link_max >= 3 && facts.gx_link_max < UINT8_MAX &&
           facts.immediate_removal_context;
}

inline bool exact_new_allocation_supported(
    MeleeWebSourceMemoryReadStatus status,
    const MeleeWebSourceMemoryAllocation& allocation,
    const MeleeWebSourceMemoryContext& before, uint32_t requested_bytes)
{
    return status == MELEE_WEB_SOURCE_MEMORY_READ_OK && allocation.live == 1 &&
           allocation.source_heap_handle == before.source_heap_handle &&
           allocation.world_generation == before.world_generation &&
           allocation.allocation_generation >
               before.allocation_generation_watermark &&
           allocation.requested_bytes == requested_bytes;
}

inline ConstructorPreCallFacts actual_pre_call_facts(
    const MeleeWebStadiumBufferLayout& layout)
{
    ConstructorPreCallFacts facts{};
    facts.memory_status =
        melee_web_source_memory_context_read(&facts.memory_context);
    const auto stats = melee_web_gameplay_stats();
    facts.os_current_heap = __OSCurrHeap;
    facts.hsd_heap = HSD_GetHeap();
    facts.world_generation = stats.generation;
    facts.world_active = melee_web_gameplay_world_exists() != 0;
    facts.source_memory_healthy =
        melee_web_source_memory_healthy() != 0;
    facts.preload_status = melee_web_source_preload_observe(
        static_cast<int>(kOriginalPreloadEntry));
    facts.execution_pointer_bytes = sizeof(void*);
    facts.layout = layout;
    facts.compiler_flag_access = kReviewedCompilerFlagAccess;
    facts.gobj_tables_ready = HSD_GObj_Entities && plinklow_gobjs &&
                              HSD_GObjGXLinkHead && HSD_GObj_804D7820;
    facts.p_link_max = HSD_GObjLibInitData.p_link_max;
    facts.gx_link_max = HSD_GObjLibInitData.gx_link_max;
    facts.immediate_removal_context =
        HSD_GObj_804D781C == nullptr && HSD_GObj_804D7818 == nullptr &&
        HSD_GObj_804D7814 == nullptr && HSD_GObj_804D7830 == nullptr &&
        HSD_GObj_804D7838 == nullptr && HSD_GObj_804CE3E4.flags == 0;
    return facts;
}

inline void pure_gate_checks(const MeleeWebStadiumBufferLayout& actual_layout)
{
    if (!actual_layout_supported(4, actual_layout))
        throw std::runtime_error(
            "Actual Stadium buffer source layout does not fit the checked Emscripten32 prefix");
    if (actual_layout_supported(8, actual_layout))
        throw std::runtime_error(
            "Unsupported host64 ABI passed the pure Stadium buffer gate");
    if (!compiler_access_supported(4, actual_layout,
                                  kReviewedCompilerFlagAccess))
        throw std::runtime_error(
            "Reviewed actual-TU flag access does not fit the original allocation");
    auto invalid_layout = actual_layout;
    invalid_layout.flag_container_candidate_offset = 0x1c;
    if (actual_layout_supported(4, invalid_layout))
        throw std::runtime_error(
            "Flag container candidate outside the original 0x1c allocation passed the pure gate");
}

namespace detail {

struct LeaseIdentity {
    const void* address{};
    uint64_t generation{};
};

inline bool current_owner_matches(
    const MeleeWebSourceMemoryContext& baseline)
{
    MeleeWebSourceMemoryContext current{};
    const auto status = melee_web_source_memory_context_read(&current);
    const auto stats = melee_web_gameplay_stats();
    return status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
           melee_web_source_memory_healthy() &&
           melee_web_gameplay_world_exists() &&
           current.source_heap_handle == baseline.source_heap_handle &&
           current.world_generation == baseline.world_generation &&
           stats.generation == baseline.world_generation &&
           __OSCurrHeap == baseline.source_heap_handle &&
           HSD_GetHeap() == baseline.source_heap_handle;
}

inline bool immediate_removal_is_safe(const HSD_GObj* candidate)
{
    return candidate && candidate != HSD_GObj_804D781C &&
           HSD_GObj_804D781C == nullptr && HSD_GObj_804D7818 == nullptr &&
           HSD_GObj_804D7814 == nullptr && HSD_GObj_804D7830 == nullptr &&
           HSD_GObj_804D7838 == nullptr && HSD_GObj_804CE3E4.flags == 0;
}

inline bool candidate_is_linked_once(const HSD_GObj* candidate,
                                     uint32_t used_objects)
{
    if (!candidate || !HSD_GObj_Entities || !plinklow_gobjs ||
        !HSD_GObjGXLinkHead || !HSD_GObj_804D7820 ||
        HSD_GObjLibInitData.p_link_max < 0x12 ||
        HSD_GObjLibInitData.gx_link_max < 3 ||
        HSD_GObjLibInitData.gx_link_max == UINT8_MAX)
        return false;

    const uint32_t p_link_max = HSD_GObjLibInitData.p_link_max;
    const uint32_t gx_link_max = HSD_GObjLibInitData.gx_link_max;
    auto** p_heads = reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    uint32_t p_occurrences = 0;
    for (uint32_t link = 0; link <= p_link_max; ++link) {
        HSD_GObj* tail = nullptr;
        uint32_t traversed = 0;
        for (HSD_GObj* current = p_heads[link]; current;
             current = current->next) {
            if (traversed >= used_objects || current->p_link != link)
                return false;
            ++traversed;
            tail = current;
            if (current == candidate) {
                ++p_occurrences;
                if (link != 0x12) return false;
            }
        }
        if (plinklow_gobjs[link] != tail) return false;
    }
    if (p_occurrences != 1) return false;

    const uint32_t max_link = gx_link_max + 1;
    uint32_t gx_occurrences = 0;
    for (uint32_t link = 0; link <= max_link; ++link) {
        HSD_GObj* tail = nullptr;
        uint32_t traversed = 0;
        for (HSD_GObj* current = HSD_GObjGXLinkHead[link]; current;
             current = current->next_gx) {
            if (traversed >= used_objects || current->gx_link != link)
                return false;
            ++traversed;
            tail = current;
            if (current == candidate) {
                ++gx_occurrences;
                if (link != max_link) return false;
            }
        }
        if (HSD_GObj_804D7820[link] != tail) return false;
    }
    return gx_occurrences == 1 && candidate->p_link == 0x12 &&
           candidate->gx_link == max_link;
}

inline bool rounded_buffer_request(const HSD_ImageDesc& desc,
                                   uint32_t* output)
{
    if (!output || desc.width != 0x280 || desc.height != 0x196 ||
        static_cast<uint32_t>(desc.format) != 4)
        return false;
    const uint32_t raw = GXGetTexBufferSize(
        desc.width, desc.height, desc.format, 0, 0);
    if (!raw || raw > UINT32_MAX - 0x1f) return false;
    *output = (raw + 0x1f) & ~uint32_t(0x1f);
    return *output != 0;
}

template <class Verify>
inline void run_one_constructor_lifetime(
    unsigned lifetime, Verify&& verify,
    std::array<LeaseIdentity, 4>& prior_leases, size_t& prior_count)
{
    namespace screen = melee_web::test::stadium_screen;
    const auto stats_before = melee_web_gameplay_stats();
    MeleeWebStadiumBufferLayout layout{};
    screen::require(melee_web_stadium_buffer_layout_read(&layout) != 0,
                    "Actual Stadium translation unit did not report private layout facts");
    const auto facts = actual_pre_call_facts(layout);
    screen::require(original_constructor_pre_call_supported(facts),
                    "Stadium constructor pre-call owner, preload, ABI, compiler, GObj-bound, or removal gate rejected");
    screen::require(stats_before.objects < UINT32_MAX,
                    "GObj used-count cannot represent one more source object");
    verify();

    const auto runtime_before = screen::runtime_roots_snapshot();
    const auto classes_before = screen::live_class_counts();
    const auto pools_before = screen::live_pool_counts();
    const uint32_t ids_before = HSD_ObjAllocGetUsing(HSD_IDGetAllocData());
    const MeleeWebGameplayBootstrapState bootstrap_before = [&] {
        MeleeWebGameplayBootstrapState state{};
        screen::require(melee_web_gameplay_bootstrap_state(
                            &state, sizeof(state)) != 0,
                        "Could not read the current gameplay lifecycle guards");
        return state;
    }();

    const MeleeWebSourceMemoryContext owner_before = facts.memory_context;
    HSD_GObj* gobj = grStadium_801D2D78();
    std::string first_failure;
    auto note = [&](bool condition, const char* message) {
        if (!condition && first_failure.empty()) first_failure = message;
    };
    auto note_verify = [&] {
        try {
            verify();
        } catch (const std::exception& error) {
            if (first_failure.empty()) first_failure = error.what();
        } catch (...) {
            if (first_failure.empty())
                first_failure = "Retained source guard callback failed";
        }
    };

    note(gobj != nullptr, "Original Stadium constructor returned a null GObj");
    void* wrapper = gobj ? HSD_GObjGetUserData(gobj) : nullptr;
    MeleeWebSourceMemoryAllocation wrapper_lease{};
    auto wrapper_status = wrapper
        ? melee_web_source_memory_allocation_read(wrapper, &wrapper_lease)
        : MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    const bool wrapper_lease_exact = wrapper &&
        exact_new_allocation_supported(wrapper_status, wrapper_lease,
                                       owner_before,
                                       kOriginalWrapperAllocationBytes);
    note(wrapper_lease_exact,
         "Constructor userdata is not the exact new 0x1c source allocation");

    HSD_ImageDesc* desc = wrapper_lease_exact
        ? static_cast<HSD_ImageDesc*>(wrapper)
        : nullptr;
    void* image_alias = desc ? desc->image_ptr : nullptr;
    const void* image_payload = image_alias;
    uint32_t rounded_image_bytes = 0;
    note(desc && desc->width == 0x280 && desc->height == 0x196 &&
             static_cast<uint32_t>(desc->format) == 4 && desc->mipmap == 0 &&
             desc->minLOD == 0.0f && desc->maxLOD == 0.0f,
         "Original constructor descriptor prefix differs from 640x406 format-4 source fields");
    note(desc && rounded_buffer_request(*desc, &rounded_image_bytes),
         "Original GX helper did not produce a bounded rounded Stadium image request");
    uint8_t flag_byte = 0;
    if (wrapper_lease_exact)
        flag_byte = static_cast<const uint8_t*>(wrapper)[
            kReviewedCompilerFlagAccess.byte_offset];
    note(wrapper_lease_exact && (flag_byte & 1u) != 0,
         "Reviewed compiler-proven flag byte does not contain the source-set low bit");

    MeleeWebSourceMemoryAllocation image_lease{};
    auto image_status = image_alias
        ? melee_web_source_memory_allocation_read(image_alias, &image_lease)
        : MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    const bool image_lease_exact = image_alias && rounded_image_bytes &&
        exact_new_allocation_supported(image_status, image_lease,
                                       owner_before, rounded_image_bytes) &&
        image_alias != wrapper;
    note(image_lease_exact,
         "Descriptor image pointer is not a distinct exact new GX-sized source allocation");
    note(melee_web_source_preload_observe(
             static_cast<int>(kOriginalPreloadEntry)) ==
             MELEE_WEB_SOURCE_PRELOAD_ABSENT,
         "Original constructor changed the actual 0x7d3 preload cache state");

    const auto stats_during = melee_web_gameplay_stats();
    note(stats_during.generation == stats_before.generation &&
             stats_during.ticks == stats_before.ticks &&
             stats_during.objects == stats_before.objects + 1 &&
             stats_during.processes == stats_before.processes,
         "Constructor did not add exactly one GObj without process or tick activity");
    note(gobj && gobj->classifier == 0x11 && gobj->p_link == 0x12 &&
             gobj->p_priority == 0 &&
             gobj->gx_link == HSD_GObjLibInitData.gx_link_max + 1 &&
             gobj->render_priority == 3 &&
             gobj->render_cb == grStadium_801D2FD0 &&
             gobj->user_data_kind == 3 &&
             gobj->user_data_remove_func == HSD_Free &&
             gobj->hsd_obj == nullptr && gobj->proc == nullptr,
         "Original constructor GObj/callback/userdata registration differs from its source setup");
    note(gobj && detail::candidate_is_linked_once(gobj, stats_during.objects),
         "Original GObj is not linked exactly once in its authored p_link and GX max list");
    note(detail::immediate_removal_is_safe(gobj),
         "Current GObj scheduler state would defer original removal or dispatch a callback");
    note(detail::current_owner_matches(owner_before),
         "Source heap/world owner changed across original Stadium construction");
    note_verify();

    /* Never free an image pointer that is borrowed, interior, stale, or not an
     * exact generation newer than this lifetime's source-memory watermark. */
    bool image_freed = false;
    if (image_lease_exact && detail::current_owner_matches(owner_before)) {
        MeleeWebSourceMemoryAllocation before_free{};
        const auto before_free_status =
            melee_web_source_memory_allocation_read(image_alias, &before_free);
        const bool lease_unchanged = exact_new_allocation_supported(
            before_free_status, before_free, owner_before,
            rounded_image_bytes) &&
            before_free.allocation_generation ==
                image_lease.allocation_generation;
        note(lease_unchanged,
             "Exact fallback image lease changed before original HSD_Free");
        if (lease_unchanged) {
            HSD_Free(image_alias);
            image_alias = nullptr;
            image_freed = true;
        }
    }
    note(image_freed,
         "Owned fallback image was not safely released through original HSD_Free");
    if (image_freed) {
        MeleeWebSourceMemoryAllocation stale_image{};
        const auto stale_status = melee_web_source_memory_allocation_read(
            image_payload,
            &stale_image);
        note(stale_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                 stale_image.live == 0,
             "Freed exact image payload still has a live source lease");
    }
    note_verify();

    bool wrapper_removed = false;
    const bool gobj_removal_safe = wrapper_lease_exact && gobj &&
        gobj->user_data == wrapper && gobj->user_data_kind == 3 &&
        gobj->user_data_remove_func == HSD_Free && gobj->hsd_obj == nullptr &&
        gobj->proc == nullptr &&
        detail::candidate_is_linked_once(gobj, melee_web_gameplay_stats().objects) &&
        detail::immediate_removal_is_safe(gobj) &&
        detail::current_owner_matches(owner_before);
    if (gobj_removal_safe) {
        MeleeWebSourceMemoryAllocation before_remove{};
        const auto before_remove_status =
            melee_web_source_memory_allocation_read(wrapper, &before_remove);
        const bool wrapper_lease_unchanged = exact_new_allocation_supported(
            before_remove_status, before_remove, owner_before,
            kOriginalWrapperAllocationBytes) &&
            before_remove.allocation_generation ==
                wrapper_lease.allocation_generation;
        note(wrapper_lease_unchanged,
             "Exact wrapper lease changed before original immediate GObj removal");
        if (wrapper_lease_unchanged) {
            HSD_GObjPLink_80390228(gobj);
            gobj = nullptr;
            wrapper_removed = true;
        }
    }
    note(wrapper_removed,
         "Exact wrapper was not safely released by original immediate GObj removal");
    if (wrapper_removed) {
        MeleeWebSourceMemoryAllocation stale_wrapper{};
        const auto stale_status = melee_web_source_memory_allocation_read(
            wrapper, &stale_wrapper);
        note(stale_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                 stale_wrapper.live == 0,
             "Removed GObj wrapper still has a live source lease");
    }
    note_verify();

    MeleeWebSourceMemoryAllocation final_image{};
    const auto final_image_status = melee_web_source_memory_allocation_read(
        image_payload,
        &final_image);
    note(final_image_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
             final_image.live == 0,
         "Image payload lease was not absent after constructor teardown");
    note(detail::current_owner_matches(owner_before),
         "Source heap/world owner changed during Stadium buffer teardown");
    note(melee_web_source_preload_observe(
             static_cast<int>(kOriginalPreloadEntry)) ==
             MELEE_WEB_SOURCE_PRELOAD_ABSENT,
         "Stadium buffer teardown changed the actual 0x7d3 preload cache state");
    note(melee_web_gameplay_stats().ticks == stats_before.ticks &&
             melee_web_gameplay_stats().objects == stats_before.objects &&
             melee_web_gameplay_stats().processes == stats_before.processes &&
             melee_web_gameplay_stats().generation == stats_before.generation,
         "Original Stadium teardown did not restore source GObj/process/tick usage");
    note(screen::runtime_roots_snapshot() == runtime_before,
         "Original Stadium teardown did not restore GObj/process/GX lists and StageInfo");
    note(screen::live_class_counts() == classes_before &&
             screen::live_pool_counts() == pools_before &&
             HSD_ObjAllocGetUsing(HSD_IDGetAllocData()) == ids_before,
         "Original Stadium teardown did not restore existing HSD object/pool usage");
    MeleeWebGameplayBootstrapState bootstrap_after{};
    note(melee_web_gameplay_bootstrap_state(&bootstrap_after,
                                            sizeof(bootstrap_after)) != 0 &&
             std::memcmp(&bootstrap_before, &bootstrap_after,
                         sizeof(bootstrap_before)) == 0,
         "Original Stadium constructor changed gameplay bootstrap owner guards");
    note(gobj == nullptr && HSD_GObj_804D781C == nullptr &&
             HSD_GObj_804D7818 == nullptr &&
             HSD_GObj_804D7814 == nullptr && HSD_GObj_804CE3E4.flags == 0,
         "Original Stadium callback/removal guards did not return to idle");
    note_verify();

    const auto remember_lease = [&](const void* address, uint64_t generation) {
        if (!address || !generation) return;
        for (size_t i = 0; i < prior_count; ++i) {
            if (prior_leases[i].address == address)
                note(generation > prior_leases[i].generation,
                     "Reused source payload address did not receive a newer generation");
        }
        if (prior_count < prior_leases.size())
            prior_leases[prior_count++] = {address, generation};
        else
            note(false, "Stadium two-lifetime lease identity buffer overflowed");
    };
    if (wrapper_lease_exact)
        remember_lease(wrapper, wrapper_lease.allocation_generation);
    if (image_lease_exact)
        remember_lease(image_payload, image_lease.allocation_generation);
    if (!first_failure.empty()) {
        throw std::runtime_error("Stadium constructor lifetime " +
                                 std::to_string(lifetime) + ": " +
                                 first_failure);
    }
}

}  // namespace detail

/* Verify is observation-only: it may read retained owners and guards, but must
 * not allocate, mutate, reset, or otherwise change source/runtime state. */
template <class Verify>
inline void run_original_constructor_lifetimes(Verify&& verify)
{
    namespace screen = melee_web::test::stadium_screen;
    MeleeWebStadiumBufferLayout layout{};
    screen::require(melee_web_stadium_buffer_layout_read(&layout) != 0,
                    "Actual Stadium translation unit did not report private layout facts");
    const auto initial_facts = actual_pre_call_facts(layout);
    screen::require(original_constructor_pre_call_supported(initial_facts),
                    "Stadium constructor pre-call owner, preload, ABI, compiler, GObj-bound, or removal gate rejected");
    screen::require(pair_generation_headroom_supported(
                        initial_facts.memory_context),
                    "Source allocation-generation watermark cannot safely cover both bounded lifetimes");

    std::array<detail::LeaseIdentity, 4> prior_leases{};
    size_t prior_count = 0;
    for (unsigned lifetime = 0; lifetime < 2; ++lifetime)
        detail::run_one_constructor_lifetime(lifetime, verify, prior_leases,
                                             prior_count);
}

}  // namespace melee_web::test::stadium_buffer
