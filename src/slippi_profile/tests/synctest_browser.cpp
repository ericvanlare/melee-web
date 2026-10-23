/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Draw-reference adapter for the same source-only observation fixture. The
 * adapter owns a real Aurora WebGPU context and never restores renderer state. */
#if defined(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)
#define MELEE_WEB_SYNCTEST_CPU_RESTORE 1
#endif
#include "../../../tests/gameplay_snapshot_probe.cpp"
#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_match_clock.h"
#include <emscripten/heap.h>
namespace { bool cpu_native_active(); }
#endif

#include "slippi_profile/profile.h"
#include "slippi_profile/draw_diagnostics.h"
#include "gameplay_bootstrap.h"
#include "gameplay_audio.h"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <emscripten.h>
#include <emscripten/threading.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <limits>

#if defined(MELEE_WEB_STAGE_ARENA_OWNER_INVENTORY)
extern "C" {
__attribute__((used, section(".bss.melee_web_synctest_stage_arena_diagnostic_storage"), aligned(4)))
uint32_t melee_web_synctest_stage_arena_diagnostic_storage[24]{};
}
static_assert(sizeof(melee_web_synctest_stage_arena_diagnostic_storage) == 96,
              "Stage-arena diagnostic storage must remain exactly 96 bytes");
#endif

static_assert(offsetof(MeleeWebMatchStats, player_slot) == 136,
              "v2 observation player-slot decoder offset changed");
static_assert(offsetof(MeleeWebMatchStats, stocks) == 148,
              "v2 observation stock decoder offset changed");
static_assert(offsetof(MeleeWebMatchStats, fighter_kind) == 152,
              "v2 observation fighter-kind decoder offset changed");

namespace {
bool renderer_ready = false;
AuroraBackend renderer_backend = BACKEND_AUTO;
uint64_t last_completed_frame_id = 0;
uint32_t renderer_error_count = 0;
std::array<unsigned char, 64 * 1024> fifo_buffer{};
std::string submission_json;

struct AudioRangeWireRow {
    uint32_t kind;
    uint32_t address;
    uint32_t extent;
    uint32_t owner;
    uint32_t generation_low;
    uint32_t generation_high;
};

struct AudioRangeWireInventory {
    uint32_t version;
    uint32_t size;
    uint32_t count;
    uint32_t row_size;
    uint32_t capacity;
    uint32_t status;
    uint32_t audio_owner;
    uint32_t generation_low;
    uint32_t generation_high;
    AudioRangeWireRow rows[MELEE_WEB_AUDIO_OWNED_RANGE_MAX];
};

struct MatchContextRangeWireRow {
    uint32_t kind;
    uint32_t address;
    uint32_t extent;
    uint32_t owner;
    uint32_t world_generation_low;
    uint32_t world_generation_high;
    uint32_t owner_epoch_low;
    uint32_t owner_epoch_high;
};

struct MatchContextRangeWireInventory {
    uint32_t version;
    uint32_t size;
    uint32_t count;
    uint32_t row_size;
    uint32_t capacity;
    uint32_t status;
    MatchContextRangeWireRow rows[1];
};

static_assert(sizeof(void *) == 4 && sizeof(size_t) == 4,
              "Browser audio inventory requires wasm32 pointers and size_t");
static_assert(sizeof(AudioRangeWireRow) == 24 &&
                  offsetof(AudioRangeWireRow, kind) == 0 &&
                  offsetof(AudioRangeWireRow, address) == 4 &&
                  offsetof(AudioRangeWireRow, extent) == 8 &&
                  offsetof(AudioRangeWireRow, owner) == 12 &&
                  offsetof(AudioRangeWireRow, generation_low) == 16 &&
                  offsetof(AudioRangeWireRow, generation_high) == 20,
              "Audio range wire row layout changed");
static_assert(offsetof(AudioRangeWireInventory, version) == 0 &&
                  offsetof(AudioRangeWireInventory, size) == 4 &&
                  offsetof(AudioRangeWireInventory, count) == 8 &&
                  offsetof(AudioRangeWireInventory, row_size) == 12 &&
                  offsetof(AudioRangeWireInventory, capacity) == 16 &&
                  offsetof(AudioRangeWireInventory, status) == 20 &&
                  offsetof(AudioRangeWireInventory, audio_owner) == 24 &&
                  offsetof(AudioRangeWireInventory, generation_low) == 28 &&
                  offsetof(AudioRangeWireInventory, generation_high) == 32 &&
                  offsetof(AudioRangeWireInventory, rows) == 36 &&
                  sizeof(AudioRangeWireInventory) == 252,
              "Audio range wire inventory layout changed");

static_assert(sizeof(MatchContextRangeWireRow) == 32 &&
                  offsetof(MatchContextRangeWireRow, kind) == 0 &&
                  offsetof(MatchContextRangeWireRow, address) == 4 &&
                  offsetof(MatchContextRangeWireRow, extent) == 8 &&
                  offsetof(MatchContextRangeWireRow, owner) == 12 &&
                  offsetof(MatchContextRangeWireRow, world_generation_low) == 16 &&
                  offsetof(MatchContextRangeWireRow, world_generation_high) == 20 &&
                  offsetof(MatchContextRangeWireRow, owner_epoch_low) == 24 &&
                  offsetof(MatchContextRangeWireRow, owner_epoch_high) == 28,
              "Match-context range wire row layout changed");
static_assert(offsetof(MatchContextRangeWireInventory, version) == 0 &&
                  offsetof(MatchContextRangeWireInventory, size) == 4 &&
                  offsetof(MatchContextRangeWireInventory, count) == 8 &&
                  offsetof(MatchContextRangeWireInventory, row_size) == 12 &&
                  offsetof(MatchContextRangeWireInventory, capacity) == 16 &&
                  offsetof(MatchContextRangeWireInventory, status) == 20 &&
                  offsetof(MatchContextRangeWireInventory, rows) == 24 &&
                  sizeof(MatchContextRangeWireInventory) == 56,
              "Match-context range wire inventory layout changed");

#if defined(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)
static_assert(sizeof(void *) == 4 && sizeof(size_t) == 4,
              "Aurora staging-shadow inventory requires wasm32");
static_assert(AURORA_BROWSER_QUIESCENCE_SCHEMA_VERSION == 5 &&
                  AURORA_BROWSER_STAGING_STREAM_COUNT == 5 &&
                  sizeof(AuroraBrowserQuiescenceDescriptor) == 120 &&
                  offsetof(AuroraBrowserQuiescenceDescriptor, stagingGeneration) == 72 &&
                  sizeof(AuroraBrowserFrameStagingDescriptor) == 464 &&
                  sizeof(AuroraBrowserSubmissionDescriptor) == 56,
              "Aurora staging-shadow schema 5 layout changed");
#endif

AudioRangeWireInventory audio_range_wire{};
MeleeWebAudio *audio_range_anchor = nullptr;
uint64_t audio_range_generation_anchor = 0;
MatchContextRangeWireInventory match_context_range_wire{};
uint32_t match_context_range_anchor_address = 0;
uint32_t match_context_range_anchor_extent = 0;
uint64_t match_context_range_anchor_world_generation = 0;
uint64_t match_context_range_anchor_owner_epoch = 0;
bool match_context_range_anchor_set = false;

struct AudioRangeCapture {
    AudioRangeWireRow rows[MELEE_WEB_AUDIO_OWNED_RANGE_MAX]{};
    uint32_t count = 0;
    bool valid = true;
};

bool wire_u32(const void *pointer, uint32_t &value)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (!pointer || address > UINT32_MAX) return false;
    value = static_cast<uint32_t>(address);
    return true;
}

#if defined(MELEE_WEB_STAGE_ARENA_OWNER_INVENTORY)
struct StageArenaPreflight {
    uint32_t next_ordinal = 0;
    uint64_t total_bytes = 0;
    uint64_t diagnostic_begin = 0;
    uint64_t diagnostic_end = 0;
    bool valid = true;
};

bool stage_arena_preflight_row(const melee_web::NativeDatAllocationRange& row,
                               void* context) noexcept
{
    auto* const preflight = static_cast<StageArenaPreflight*>(context);
    if (!preflight || !preflight->valid || row.ordinal != preflight->next_ordinal ||
        !row.address || !row.bytes || row.bytes > UINT32_MAX) {
        if (preflight) preflight->valid = false;
        return false;
    }
    uint32_t address = 0;
    if (!wire_u32(row.address, address) ||
        static_cast<uint64_t>(row.bytes) > (uint64_t{1} << 32) - address ||
        row.bytes > melee_web::native_dat_allocation_bytes_max - preflight->total_bytes) {
        preflight->valid = false;
        return false;
    }
    const uint64_t end = static_cast<uint64_t>(address) + row.bytes;
    if (address < preflight->diagnostic_end && preflight->diagnostic_begin < end) {
        preflight->valid = false;
        return false;
    }
    preflight->total_bytes += row.bytes;
    ++preflight->next_ordinal;
    return true;
}

bool stage_arena_wire_anchor_matches(const uint32_t* anchor,
                                    const melee_web::GameplayWorldStageArenaInventory& owner,
                                    uint32_t world_address, uint32_t arena_address,
                                    uint32_t token_address) noexcept
{
    return anchor[0] == 1 && anchor[1] == static_cast<uint32_t>(owner.world_generation) &&
        anchor[2] == static_cast<uint32_t>(owner.world_generation >> 32) &&
        anchor[3] == world_address && anchor[4] == arena_address &&
        anchor[5] == token_address && anchor[6] == owner.arena.allocation_count &&
        anchor[7] == owner.arena.total_bytes;
}
#endif

int capture_audio_range(const MeleeWebAudioOwnedRange *range, void *context)
{
    auto *capture = static_cast<AudioRangeCapture *>(context);
    if (!capture || !range || capture->count >= MELEE_WEB_AUDIO_OWNED_RANGE_MAX ||
        range->kind < MELEE_WEB_AUDIO_RANGE_AUDIO_OWNER ||
        range->kind > MELEE_WEB_AUDIO_RANGE_EFFECTS_DELAY_HEAP ||
        !range->byte_extent || range->byte_extent > UINT32_MAX ||
        range->generation == 0 || !wire_u32(range->address,
                                             capture->rows[capture->count].address) ||
        !wire_u32(range->owner_identity,
                  capture->rows[capture->count].owner)) {
        if (capture) capture->valid = false;
        return 0;
    }
    const uint32_t address = capture->rows[capture->count].address;
    if (static_cast<uint64_t>(range->byte_extent) + address >
        static_cast<uint64_t>(UINT32_MAX) + 1u) {
        capture->valid = false;
        return 0;
    }
    AudioRangeWireRow &row = capture->rows[capture->count++];
    row.kind = static_cast<uint32_t>(range->kind);
    row.extent = static_cast<uint32_t>(range->byte_extent);
    row.generation_low = static_cast<uint32_t>(range->generation);
    row.generation_high = static_cast<uint32_t>(range->generation >> 32);
    return 1;
}

void renderer_log(AuroraLogLevel level, const char *module,
                  const char *message, unsigned length)
{
    if (level >= LOG_ERROR) ++renderer_error_count;
    std::fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s] %.*s\n",
                 module, static_cast<int>(length), message);
}
}

extern "C" {
EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_gameplay_arena_base(void)
{
    const void *base = nullptr;
    size_t bytes = 0;
    if (!melee_web_gameplay_active_arena_view(&base, &bytes) || !bytes)
        return 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(base);
    return address <= UINT32_MAX ? static_cast<uint32_t>(address) : 0;
}

EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_gameplay_arena_bytes(void)
{
    const void *base = nullptr;
    size_t bytes = 0;
    if (!melee_web_gameplay_active_arena_view(&base, &bytes) ||
        !base || !bytes || bytes > UINT32_MAX)
        return 0;
    return static_cast<uint32_t>(bytes);
}

#if defined(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)
EMSCRIPTEN_KEEPALIVE uint32_t melee_web_synctest_aurora_staging_shadow_inventory_query(void)
{
    const void* observed = aurora_browser_quiescence_observe_descriptor();
    uint32_t address = 0;
    return observed && wire_u32(observed, address) ? address : 0;
}
#endif

EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_audio_range_inventory(void)
{
    return &audio_range_wire;
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_audio_range_inventory_query(void)
{
    std::memset(&audio_range_wire, 0, sizeof(audio_range_wire));
    audio_range_wire.version = 1;
    audio_range_wire.size = sizeof(audio_range_wire);
    audio_range_wire.row_size = sizeof(AudioRangeWireRow);
    audio_range_wire.capacity = MELEE_WEB_AUDIO_OWNED_RANGE_MAX;
    if (!match) {
        audio_range_wire.status = MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER;
        return 0;
    }

    MeleeWebAudio *const current_audio = match->audio();
    const uint64_t current_generation = melee_web_audio_generation(current_audio);
    if (!current_audio || !current_generation ||
        (audio_range_anchor &&
         (current_audio != audio_range_anchor ||
          current_generation != audio_range_generation_anchor))) {
        audio_range_wire.status = MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_OWNER;
        return 0;
    }

    AudioRangeCapture capture{};
    const uint64_t expected_generation = audio_range_anchor ?
        audio_range_generation_anchor : current_generation;
    const MeleeWebAudioRangeVisitResult result =
        melee_web_audio_visit_owned_ranges(current_audio, expected_generation,
            MELEE_WEB_AUDIO_OWNED_RANGE_MAX, capture_audio_range, &capture);
    audio_range_wire.status = static_cast<uint32_t>(result);
    if (result != MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE || !capture.valid ||
        (capture.count != 6 && capture.count != 9)) {
        if (!capture.valid || (capture.count != 6 && capture.count != 9))
            audio_range_wire.status = MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_ARGUMENT;
        return 0;
    }

    uint32_t audio_owner_address = 0;
    if (!wire_u32(current_audio, audio_owner_address)) {
        audio_range_wire.status = MELEE_WEB_AUDIO_RANGE_VISIT_INVALID_ARGUMENT;
        return 0;
    }
    audio_range_wire.count = capture.count;
    audio_range_wire.audio_owner = audio_owner_address;
    audio_range_wire.generation_low = static_cast<uint32_t>(expected_generation);
    audio_range_wire.generation_high = static_cast<uint32_t>(expected_generation >> 32);
    std::memcpy(audio_range_wire.rows, capture.rows,
                capture.count * sizeof(AudioRangeWireRow));
    if (!audio_range_anchor) {
        audio_range_anchor = current_audio;
        audio_range_generation_anchor = expected_generation;
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_match_context_range_inventory(void)
{
    return &match_context_range_wire;
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_match_context_range_inventory_query(void)
{
    std::memset(&match_context_range_wire, 0, sizeof(match_context_range_wire));
    match_context_range_wire.version = 1;
    match_context_range_wire.size = sizeof(match_context_range_wire);
    match_context_range_wire.row_size = sizeof(MatchContextRangeWireRow);
    match_context_range_wire.capacity = 1;
    if (!match) {
        match_context_range_wire.status = MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER;
        return 0;
    }

    char error[256]{};
    MeleeWebMatchContextRange current{};
    const MeleeWebMatchContextRangeResult result =
        match->diagnostic_match_context_range(&current, error, sizeof(error));
    match_context_range_wire.status = static_cast<uint32_t>(result);
    if (result != MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE ||
        !current.address ||
        current.byte_extent == 0 || current.byte_extent > UINT32_MAX ||
        current.world_generation == 0 || current.owner_epoch == 0) {
        if (result == MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE) {
            match_context_range_wire.status = MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER;
        }
        return 0;
    }

    MatchContextRangeWireRow row{};
    if (!wire_u32(current.address, row.address) ||
        !wire_u32(current.address, row.owner) ||
        static_cast<uint64_t>(current.byte_extent) + row.address >
            static_cast<uint64_t>(UINT32_MAX) + 1u) {
        match_context_range_wire.status = MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT;
        return 0;
    }
    row.kind = 1;
    row.extent = static_cast<uint32_t>(current.byte_extent);
    if (match_context_range_anchor_set &&
        (row.address != match_context_range_anchor_address ||
         row.extent != match_context_range_anchor_extent ||
         current.world_generation != match_context_range_anchor_world_generation ||
         current.owner_epoch != match_context_range_anchor_owner_epoch)) {
        match_context_range_wire.status = MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER;
        return 0;
    }
    row.world_generation_low = static_cast<uint32_t>(current.world_generation);
    row.world_generation_high = static_cast<uint32_t>(current.world_generation >> 32);
    row.owner_epoch_low = static_cast<uint32_t>(current.owner_epoch);
    row.owner_epoch_high = static_cast<uint32_t>(current.owner_epoch >> 32);
    match_context_range_wire.rows[0] = row;
    match_context_range_wire.count = 1;
    if (!match_context_range_anchor_set) {
        match_context_range_anchor_address = row.address;
        match_context_range_anchor_extent = row.extent;
        match_context_range_anchor_world_generation = current.world_generation;
        match_context_range_anchor_owner_epoch = current.owner_epoch;
        match_context_range_anchor_set = true;
    }
    return 1;
}

#if defined(MELEE_WEB_STAGE_ARENA_OWNER_INVENTORY)
EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_stage_arena_inventory_query(
    uint32_t index)
{
#if defined(__EMSCRIPTEN_PTHREADS__)
    (void)index;
    return nullptr;
#else
    constexpr uint32_t metadata_index = UINT32_MAX;
    constexpr uint32_t wire_words = 16;
    constexpr uint32_t anchor_words = 8;
    constexpr uint32_t owner_kind_stage_arena = 1;
    constexpr size_t diagnostic_bytes = 96;
    auto* const storage = melee_web_synctest_stage_arena_diagnostic_storage;
    auto* const anchor = storage;
    auto* const wire = storage + anchor_words;
    uint32_t wire_address = 0, storage_address = 0, anchor_end = 0;
    if (!wire_u32(storage, storage_address) ||
        static_cast<uint64_t>(storage_address) + diagnostic_bytes >= (uint64_t{1} << 32) ||
        !wire_u32(wire, wire_address) || wire_address != storage_address + 32u)
        return nullptr;
    anchor_end = static_cast<uint32_t>(static_cast<uint64_t>(storage_address) + diagnostic_bytes);

    if (!match) return nullptr;
    if (index == metadata_index) {
        StageArenaPreflight preflight{};
        preflight.diagnostic_begin = storage_address;
        preflight.diagnostic_end = anchor_end;
        auto owner = match->diagnostic_stage_arena_allocations(
            0, 0, nullptr, nullptr);
        if (owner.status != melee_web::NativeDatAllocationVisitStatus::Complete ||
            !owner.world_generation || !owner.world_storage ||
            !owner.arena.arena_storage || !owner.arena.identity_token ||
            owner.arena.allocation_count > melee_web::native_dat_allocation_count_max ||
            owner.arena.total_bytes > melee_web::native_dat_allocation_bytes_max)
            return nullptr;
        uint32_t world_address = 0, arena_address = 0, token_address = 0;
        if (!wire_u32(owner.world_storage, world_address) ||
            !wire_u32(owner.arena.arena_storage, arena_address) ||
            !wire_u32(owner.arena.identity_token, token_address)) return nullptr;
        preflight.next_ordinal = 0;
        if (owner.arena.allocation_count != 0) {
            owner = match->diagnostic_stage_arena_allocations(
                0, owner.arena.allocation_count, stage_arena_preflight_row, &preflight);
            if (owner.status != melee_web::NativeDatAllocationVisitStatus::Complete ||
                !preflight.valid || preflight.next_ordinal != owner.arena.allocation_count ||
                preflight.total_bytes != owner.arena.total_bytes) return nullptr;
        }
        auto verify = match->diagnostic_stage_arena_allocations(0, 0, nullptr, nullptr);
        if (verify.status != melee_web::NativeDatAllocationVisitStatus::Complete ||
            verify.world_generation != owner.world_generation ||
            verify.world_storage != owner.world_storage ||
            verify.arena.arena_storage != owner.arena.arena_storage ||
            verify.arena.identity_token != owner.arena.identity_token ||
            verify.arena.allocation_count != owner.arena.allocation_count ||
            verify.arena.total_bytes != owner.arena.total_bytes) return nullptr;
        bool empty_anchor = anchor[0] == 0;
        if (empty_anchor) {
            for (uint32_t index = 1; index < anchor_words; ++index) {
                if (anchor[index] != 0) return nullptr;
            }
        } else if (!stage_arena_wire_anchor_matches(
                       anchor, owner, world_address, arena_address, token_address)) {
            return nullptr;
        }
        uint32_t next_anchor[anchor_words] = {
            1u, static_cast<uint32_t>(owner.world_generation),
            static_cast<uint32_t>(owner.world_generation >> 32), world_address,
            arena_address, token_address,
            static_cast<uint32_t>(owner.arena.allocation_count),
            static_cast<uint32_t>(owner.arena.total_bytes)};
        uint32_t next_wire[wire_words] = {
            1u, 64u, 1u, owner_kind_stage_arena,
            next_anchor[1], next_anchor[2], world_address, arena_address,
            token_address, next_anchor[6], next_anchor[7], metadata_index, 0u, 0u, 0u, 0u};
        if (empty_anchor) std::memcpy(anchor, next_anchor, sizeof(next_anchor));
        std::memcpy(wire, next_wire, sizeof(next_wire));
        return wire;
    }

    if (anchor[0] != 1 || index >= anchor[6]) return nullptr;
    struct OneRow { melee_web::NativeDatAllocationRange value{}; bool seen = false; };
    OneRow observed{};
    const auto one_row = [](const melee_web::NativeDatAllocationRange& row,
                            void* context) noexcept -> bool {
        auto* const captured = static_cast<OneRow*>(context);
        if (!captured || captured->seen) return false;
        captured->value = row;
        captured->seen = true;
        return true;
    };
    auto owner = match->diagnostic_stage_arena_allocations(index, 1, one_row, &observed);
    if (owner.status != melee_web::NativeDatAllocationVisitStatus::Complete ||
        !observed.seen || owner.world_generation == 0 || !owner.world_storage ||
        !owner.arena.arena_storage || !owner.arena.identity_token) return nullptr;
    uint32_t world_address = 0, arena_address = 0, token_address = 0, allocation_address = 0;
    if (!wire_u32(owner.world_storage, world_address) ||
        !wire_u32(owner.arena.arena_storage, arena_address) ||
        !wire_u32(owner.arena.identity_token, token_address) ||
        !wire_u32(observed.value.address, allocation_address) || !observed.value.bytes ||
        observed.value.bytes > UINT32_MAX ||
        static_cast<uint64_t>(observed.value.bytes) > (uint64_t{1} << 32) - allocation_address ||
        (allocation_address < anchor_end && storage_address <
         static_cast<uint64_t>(allocation_address) + observed.value.bytes) ||
        !stage_arena_wire_anchor_matches(anchor, owner, world_address, arena_address, token_address) ||
        observed.value.ordinal != index) return nullptr;
    uint32_t next_wire[wire_words] = {
        1u, 64u, 1u, owner_kind_stage_arena,
        anchor[1], anchor[2], world_address, arena_address, token_address,
        anchor[6], anchor[7], index, allocation_address,
        static_cast<uint32_t>(observed.value.bytes), 0u, 0u};
    std::memcpy(wire, next_wire, sizeof(next_wire));
    return wire;
#endif
}
#endif

EMSCRIPTEN_KEEPALIVE const void *melee_web_synctest_draw_diagnostics(void)
{
    return slippi_profile_draw_diagnostics_snapshot();
}

EMSCRIPTEN_KEEPALIVE unsigned melee_web_synctest_draw_diagnostics_size(void)
{
    return sizeof(SlippiProfileDrawDiagnostics);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_motion_boundary(int motion_id)
{
    return slippi_profile_draw_motion_boundary(motion_id);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_is_mario_kind(int fighter_kind)
{
    return slippi_profile_draw_is_mario_kind(fighter_kind);
}

EMSCRIPTEN_KEEPALIVE const char *melee_web_synctest_profile_identity(void)
{
    return slippi_profile_identity_name(
        SLIPPI_PROFILE_ACTIVE_IDENTITY);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_decision(int tick_kind)
{
    return slippi_profile_source_draw_decision(
        SLIPPI_PROFILE_ACTIVE_IDENTITY,
        static_cast<SlippiTickKind>(tick_kind));
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_renderer_initialize(void)
{
    return checked([] {
        require(!renderer_ready, "Aurora renderer was already initialized");
        renderer_error_count = 0;
        AuroraConfig config{};
        config.appName = "Slippi-compatible rules (unofficial) — dependency diagnostic";
        config.cachePath = "/melee-render-cache";
        config.desiredBackend = BACKEND_WEBGPU;
        config.windowWidth = 640;
        config.windowHeight = 480;
        config.msaa = 1;
        config.vsync = true;
        config.logCallback = renderer_log;
        config.logLevel = LOG_INFO;
        char arg0[] = "melee-slippi-profile-synctest";
        char *argv[] = {arg0, nullptr};
        const AuroraInfo info = aurora_initialize(1, argv, &config);
        require(info.backend == BACKEND_WEBGPU,
                "Aurora did not initialize the required WebGPU backend");
        renderer_backend = info.backend;
        GXInit(fifo_buffer.data(), static_cast<u32>(fifo_buffer.size()));
        renderer_ready = true;
    });
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_present(void)
{
    return checked([] {
        require(renderer_ready, "Aurora renderer is not initialized");
        require(match != nullptr, "Source match is not initialized");
        const AuroraStats *stats_before = aurora_get_stats();
        require(stats_before != nullptr, "Aurora stats are unavailable before source draw");
        const uint64_t previous_begin_id = stats_before->lastBeginFrameId;
        const auto decision = slippi_profile_source_draw_decision(
            SLIPPI_PROFILE_ACTIVE_IDENTITY,
            SLIPPI_TICK_PRESENT);
        require(decision == SLIPPI_SOURCE_DRAW_ALLOW,
                "Slippi present tick did not permit the original source draw");
        require(aurora_begin_frame(), "Aurora did not begin the draw-reference frame");
        const AuroraStats *stats_after_begin = aurora_get_stats();
        require(stats_after_begin != nullptr, "Aurora stats disappeared at frame begin");
        try {
            GXSetCopyClear(GXColor{16, 20, 30, 255}, GX_MAX_Z24);
            match->draw();
        } catch (...) {
            aurora_end_frame();
            throw;
        }
        aurora_end_frame();
        const AuroraStats *stats_after = aurora_get_stats();
        require(stats_after != nullptr, "Aurora stats disappeared after source draw");
        require(stats_after->drawCallCount > 0,
                "Actual source match draw submitted no Aurora draw calls");
        require(stats_after->lastBeginFrameId > previous_begin_id &&
                    stats_after->lastEndFrameId >= stats_after->lastBeginFrameId &&
                    stats_after->lastEndFrameId > last_completed_frame_id,
                "Aurora draw did not advance and finish its frame identity");
        last_completed_frame_id = stats_after->lastEndFrameId;
        const uint32_t pcm_frames = observation.pcm_frames;
        observe();
        observation.pcm_frames = pcm_frames;
    });
}

EMSCRIPTEN_KEEPALIVE const char *melee_web_synctest_submission_json(void)
{
    if (!renderer_ready) return "{\"initialized\":false}";
    const AuroraBrowserSubmissionStatus status =
        aurora_browser_submission_status();
    std::ostringstream out;
    out << "{\"initialized\":true,\"backend\":"
        << static_cast<unsigned>(renderer_backend)
        << ",\"pendingFramePackets\":"
        << status.pendingFramePackets
        << ",\"pendingStagingBuffers\":" << status.pendingStagingBuffers
        << ",\"workerBusy\":" << status.workerBusy
        << ",\"rendererErrorCount\":" << renderer_error_count;
    if (const AuroraStats *stats = aurora_get_stats()) {
        out << ",\"drawCallCount\":" << stats->drawCallCount
            << ",\"lastBeginFrameId\":" << stats->lastBeginFrameId
            << ",\"lastEndFrameId\":" << stats->lastEndFrameId
            << ",\"createdPipelines\":" << stats->createdPipelines
            << ",\"queuedPipelines\":" << stats->queuedPipelines;
    }
    out << '}';
    submission_json = out.str();
    return submission_json.c_str();
}

// BEGIN MELEE_WEB_SOURCE_PAYLOAD_ADMISSION_ADAPTER
extern "C" int melee_web_synctest_renderer_shutdown(void);

EM_JS_DEPS(melee_web_payload_admission_asyncify_deps, "$Asyncify");
EM_JS(int, melee_web_payload_admission_asyncify_idle, (), {
  try {
    return typeof Asyncify === "object" && Asyncify !== null &&
      typeof Asyncify.State === "object" && Asyncify.State !== null &&
      typeof Asyncify.State.Normal === "number" &&
      Asyncify.state === Asyncify.State.Normal && Asyncify.currData === null ? 1 : 0;
  } catch (_) {
    return 0;
  }
});

namespace {
enum class PayloadAdmissionState : uint8_t {
    Idle, Captured, Stepped, DrawSubmitted, Complete, Failed, Closed
};
enum PayloadAdmissionFailure : uint32_t {
    PayloadBadOrder = 1,
    PayloadUnsafeCallBoundary = 2,
    PayloadOwnerUnavailable = 3,
    PayloadTransferBusy = 4,
    PayloadAuroraUnavailable = 5,
    PayloadAuroraBusy = 6,
    PayloadFrameMismatch = 7,
    PayloadCaptureFailure = 8,
    PayloadStepFailure = 9,
    PayloadDrawFailure = 10,
    PayloadAdmissionFailureCode = 11,
    PayloadCleanupFailure = 12,
    PayloadUnknownCommand = 13,
};
constexpr uint32_t kPayloadPrimitiveFailureBase = 0x100;
constexpr uint32_t kPayloadAdapterFailureBase = 0x200;
MeleeWebSourceMemoryPayloadSnapshot *payload_admission_snapshot = nullptr;
PayloadAdmissionState payload_admission_state = PayloadAdmissionState::Idle;
uint32_t payload_admission_first_failure = 0;
char payload_admission_first_message[256]{};
uint32_t payload_admission_capture_source_frame = 0;
uint64_t payload_admission_capture_begin_frame = 0;
uint64_t payload_admission_capture_end_frame = 0;
bool payload_admission_cleanup_done = false;
bool payload_admission_inflight = false;
bool payload_admission_close_requested = false;
bool payload_admission_shutdown_requested = false;

uint32_t payload_admission_latch(uint32_t status, const char *message)
{
    if (!payload_admission_first_failure) {
        payload_admission_first_failure = status;
        std::snprintf(payload_admission_first_message,
                      sizeof(payload_admission_first_message), "%s", message);
        std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
        payload_admission_state = PayloadAdmissionState::Failed;
    }
    return payload_admission_first_failure;
}

uint32_t payload_admission_fail_adapter(PayloadAdmissionFailure failure,
                                        const char *message)
{
    return payload_admission_latch(kPayloadAdapterFailureBase + failure, message);
}

uint32_t payload_admission_fail_primitive(
    MeleeWebSourceMemoryPayloadSnapshotStatus status, const char *operation)
{
    char message[256]{};
    std::snprintf(message, sizeof(message), "%s returned source-payload status %u",
                  operation, static_cast<unsigned>(status));
    return payload_admission_latch(kPayloadPrimitiveFailureBase +
                                       static_cast<uint32_t>(status), message);
}

uint32_t payload_admission_runtime_boundary()
{
#if defined(__EMSCRIPTEN_PTHREADS__)
    return PayloadUnsafeCallBoundary;
#else
    if (emscripten_has_asyncify() != 1 ||
        !emscripten_is_main_runtime_thread() ||
        !emscripten_is_main_browser_thread() ||
        melee_web_payload_admission_asyncify_idle() != 1) {
        return PayloadUnsafeCallBoundary;
    }
    return 0;
#endif
}

uint32_t payload_admission_source_boundary()
{
    if (payload_admission_runtime_boundary() != 0) return PayloadUnsafeCallBoundary;
    if (!renderer_ready || renderer_error_count != 0 || !match ||
        !match->ready() || match->complete() || match->paused() ||
        !melee_web_source_memory_healthy()) {
        return PayloadOwnerUnavailable;
    }
    if (melee_web_snapshot_quiescent() != 1) return PayloadTransferBusy;
    if (match->source_frames() != observation.frame || observation.ready != 1 ||
        observation.complete != 0 || observation.paused != 0) {
        return PayloadFrameMismatch;
    }
    return 0;
}

uint32_t payload_admission_boundary(bool require_new_completed_draw)
{
    if (const uint32_t failure = payload_admission_source_boundary()) return failure;
#if defined(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)
    const auto *descriptor = static_cast<const AuroraBrowserQuiescenceDescriptor *>(
        aurora_browser_quiescence_observe_descriptor());
    if (!descriptor) return PayloadAuroraUnavailable;
    const size_t descriptor_bytes = aurora_browser_quiescence_descriptor_bytes();
    if (descriptor->schemaVersion != AURORA_BROWSER_QUIESCENCE_SCHEMA_VERSION ||
        descriptor_bytes < sizeof(AuroraBrowserQuiescenceDescriptor) ||
        descriptor_bytes > UINT32_MAX || descriptor->frameSlotCount == 0 ||
        descriptor->stagingSlotCount == 0 ||
        descriptor->frameDescriptorCount != descriptor->frameSlotCount ||
        descriptor->submissionDescriptorCount != descriptor->stagingSlotCount ||
        descriptor->frameDescriptorOffset != sizeof(AuroraBrowserQuiescenceDescriptor) ||
        descriptor->frameDescriptorStride != sizeof(AuroraBrowserFrameStagingDescriptor) ||
        descriptor->submissionDescriptorStride != sizeof(AuroraBrowserSubmissionDescriptor)) {
        return PayloadAuroraUnavailable;
    }
    // byteSize covers the header and both authored row arrays, not just the header.
    // uint32 counts times these fixed row sizes fit uint64; refuse a wasm32 extent overflow.
    const uint64_t submission_offset = sizeof(AuroraBrowserQuiescenceDescriptor) +
        uint64_t{descriptor->frameDescriptorCount} * sizeof(AuroraBrowserFrameStagingDescriptor);
    const uint64_t total_bytes = submission_offset +
        uint64_t{descriptor->submissionDescriptorCount} * sizeof(AuroraBrowserSubmissionDescriptor);
    if (total_bytes > UINT32_MAX || descriptor->submissionDescriptorOffset != submission_offset ||
        descriptor->byteSize != total_bytes || descriptor_bytes != total_bytes) {
        return PayloadAuroraUnavailable;
    }
    if (descriptor->pendingFramePackets != 0 ||
        descriptor->pendingStagingBuffers != 0 || descriptor->workerBusy != 0 ||
        descriptor->lastBeginFrameId != descriptor->lastEndFrameId ||
        descriptor->lastEndFrameId != last_completed_frame_id) {
        return PayloadAuroraBusy;
    }
    if (require_new_completed_draw &&
        (descriptor->lastBeginFrameId <= payload_admission_capture_begin_frame ||
         descriptor->lastEndFrameId <= payload_admission_capture_end_frame)) {
        return PayloadAuroraBusy;
    }
    return 0;
#else
    (void)require_new_completed_draw;
    return PayloadAuroraUnavailable;
#endif
}

uint32_t payload_admission_submission_boundary()
{
    if (const uint32_t failure = payload_admission_source_boundary()) return failure;
    if (last_completed_frame_id <= payload_admission_capture_end_frame) {
        return PayloadFrameMismatch;
    }
    return 0;
}

uint32_t payload_admission_check_boundary_failure(uint32_t failure)
{
    if (!failure) return 0;
    switch (failure) {
    case PayloadUnsafeCallBoundary:
        return payload_admission_fail_adapter(PayloadUnsafeCallBoundary,
            "Payload command requires the configured synchronous main-thread target");
    case PayloadOwnerUnavailable:
        return payload_admission_fail_adapter(PayloadOwnerUnavailable,
            "Payload command requires a live ready source match and renderer");
    case PayloadTransferBusy:
        return payload_admission_fail_adapter(PayloadTransferBusy,
            "Payload command requires source transfer quiescence");
    case PayloadAuroraUnavailable:
        return payload_admission_fail_adapter(PayloadAuroraUnavailable,
            "Payload command requires the exact Aurora quiescence descriptor");
    case PayloadAuroraBusy:
        return payload_admission_fail_adapter(PayloadAuroraBusy,
            "Payload command requires an idle completed Aurora frame boundary");
    default:
        return payload_admission_fail_adapter(PayloadFrameMismatch,
            "Payload command source-frame observation is inconsistent");
    }
}

uint32_t payload_admission_check_boundary(bool require_new_completed_draw)
{
    return payload_admission_check_boundary_failure(
        payload_admission_boundary(require_new_completed_draw));
}

uint32_t payload_admission_check_submission_boundary()
{
    return payload_admission_check_boundary_failure(payload_admission_submission_boundary());
}

uint32_t payload_admission_close()
{
    if (payload_admission_state == PayloadAdmissionState::Idle) {
        payload_admission_cleanup_done = true;
        payload_admission_state = PayloadAdmissionState::Closed;
        return 0;
    }
    if (payload_admission_state == PayloadAdmissionState::Closed) {
        if (payload_admission_first_failure) {
            std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
        }
        return payload_admission_first_failure;
    }
    if (payload_admission_state != PayloadAdmissionState::Complete &&
        payload_admission_state != PayloadAdmissionState::Failed) {
        payload_admission_fail_adapter(PayloadBadOrder,
            "Payload admission closed before the complete dry-run sequence");
    }
    if (payload_admission_snapshot && !payload_admission_cleanup_done) {
        melee_web_source_memory_payload_snapshot_destroy(payload_admission_snapshot);
        payload_admission_snapshot = nullptr;
        payload_admission_cleanup_done = true;
    }
    payload_admission_state = PayloadAdmissionState::Closed;
    if (payload_admission_first_failure) {
        std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
    }
    return payload_admission_first_failure;
}

bool payload_admission_shutdown_fallback()
{
    if (payload_admission_inflight) {
        payload_admission_close_requested = true;
        payload_admission_shutdown_requested = true;
        payload_admission_fail_adapter(PayloadUnsafeCallBoundary,
            "Renderer shutdown reentered an active payload command");
        return false;
    }
    if (payload_admission_state == PayloadAdmissionState::Idle ||
        payload_admission_state == PayloadAdmissionState::Closed) return true;
    if (payload_admission_runtime_boundary() != 0) {
        payload_admission_fail_adapter(PayloadUnsafeCallBoundary,
            "Renderer shutdown requires a normal main-thread Asyncify boundary");
        return false;
    }
    if (payload_admission_state != PayloadAdmissionState::Complete &&
        payload_admission_state != PayloadAdmissionState::Failed) {
        payload_admission_fail_adapter(PayloadCleanupFailure,
            "Renderer shutdown interrupted the payload admission sequence");
    }
    if (payload_admission_snapshot && !payload_admission_cleanup_done) {
        melee_web_source_memory_payload_snapshot_destroy(payload_admission_snapshot);
        payload_admission_snapshot = nullptr;
        payload_admission_cleanup_done = true;
    }
    payload_admission_state = PayloadAdmissionState::Closed;
    if (payload_admission_first_failure) {
        std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
    }
    return true;
}

uint32_t payload_admission_execute(uint32_t command)
{
    switch (command) {
    case 1: { // CAPTURE
        if (payload_admission_state != PayloadAdmissionState::Idle) {
            return payload_admission_fail_adapter(PayloadBadOrder,
                "Payload CAPTURE is valid only once at IDLE");
        }
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        payload_admission_capture_source_frame = match->source_frames();
#if defined(MELEE_WEB_AURORA_STAGING_SHADOW_INVENTORY)
        const auto *descriptor = static_cast<const AuroraBrowserQuiescenceDescriptor *>(
            aurora_browser_quiescence_observe_descriptor());
        if (!descriptor) return payload_admission_fail_adapter(PayloadAuroraUnavailable,
            "Aurora quiescence descriptor disappeared before payload capture");
        payload_admission_capture_begin_frame = descriptor->lastBeginFrameId;
        payload_admission_capture_end_frame = descriptor->lastEndFrameId;
#else
        return payload_admission_fail_adapter(PayloadAuroraUnavailable,
            "Aurora quiescence descriptor is not enabled for payload capture");
#endif
        const auto captured = melee_web_source_memory_payload_snapshot_capture();
        payload_admission_snapshot = captured.snapshot;
        if (payload_admission_first_failure) return payload_admission_first_failure;
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        if (captured.status != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) {
            if (payload_admission_snapshot) {
                melee_web_source_memory_payload_snapshot_destroy(payload_admission_snapshot);
                payload_admission_snapshot = nullptr;
                payload_admission_cleanup_done = true;
            }
            return payload_admission_fail_primitive(captured.status, "payload capture");
        }
        if (!payload_admission_snapshot) {
            return payload_admission_fail_adapter(PayloadCaptureFailure,
                "Payload capture reported success without a snapshot");
        }
        const auto admitted = melee_web_source_memory_payload_snapshot_admit(
            payload_admission_snapshot);
        if (payload_admission_first_failure) return payload_admission_first_failure;
        if (admitted != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) {
            return payload_admission_fail_primitive(admitted, "payload admission after capture");
        }
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        if (!match || match->source_frames() != payload_admission_capture_source_frame ||
            observation.frame != payload_admission_capture_source_frame) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Source frame changed during payload capture");
        }
        payload_admission_state = PayloadAdmissionState::Captured;
        return 0;
    }
    case 2: { // STEP_AND_ADMIT
        if (payload_admission_state != PayloadAdmissionState::Captured ||
            !payload_admission_snapshot) {
            return payload_admission_fail_adapter(PayloadBadOrder,
                "Payload STEP_AND_ADMIT requires CAPTURED state");
        }
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        if (match->source_frames() != payload_admission_capture_source_frame ||
            payload_admission_capture_source_frame == UINT32_MAX) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Source frame no longer matches the captured payload boundary");
        }
        if (melee_web_snapshot_step(300) != 1) {
            return payload_admission_fail_adapter(PayloadStepFailure,
                error[0] ? error : "The actual sample-300 source step failed");
        }
        if (payload_admission_first_failure) return payload_admission_first_failure;
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        if (match->source_frames() != payload_admission_capture_source_frame + 1u ||
            observation.frame != payload_admission_capture_source_frame + 1u) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Actual sample-300 step did not advance the observed source frame exactly once");
        }
        const auto admitted = melee_web_source_memory_payload_snapshot_admit(
            payload_admission_snapshot);
        if (payload_admission_first_failure) return payload_admission_first_failure;
        if (admitted != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) {
            return payload_admission_fail_primitive(admitted, "payload admission after source step");
        }
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        payload_admission_state = PayloadAdmissionState::Stepped;
        return 0;
    }
    case 3: { // DRAW_ONCE
        if (payload_admission_state != PayloadAdmissionState::Stepped ||
            !payload_admission_snapshot) {
            return payload_admission_fail_adapter(PayloadBadOrder,
                "Payload DRAW_ONCE requires STEPPED state");
        }
        if (const uint32_t failure = payload_admission_check_boundary(false)) return failure;
        if (match->source_frames() != payload_admission_capture_source_frame + 1u) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Source frame changed before the single actual draw");
        }
        if (melee_web_synctest_draw_present() != 1) {
            return payload_admission_fail_adapter(PayloadDrawFailure,
                error[0] ? error : "The actual source draw failed");
        }
        if (payload_admission_first_failure) return payload_admission_first_failure;
        // The draw has returned with a newer submitted frame, but Aurora may
        // still own packets or staging buffers. Check the completed native
        // call/source boundary now; the runner's existing wait owns idle
        // admission, which command 4 verifies with the full descriptor.
        if (const uint32_t failure = payload_admission_check_submission_boundary()) return failure;
        if (match->source_frames() != payload_admission_capture_source_frame + 1u ||
            observation.frame != payload_admission_capture_source_frame + 1u ||
            last_completed_frame_id <= payload_admission_capture_end_frame) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Actual draw changed the source frame or failed to submit a newer frame");
        }
        payload_admission_state = PayloadAdmissionState::DrawSubmitted;
        return 0;
    }
    case 4: { // POSTDRAW_ADMIT, only after the host's existing idle wait
        if (payload_admission_state != PayloadAdmissionState::DrawSubmitted ||
            !payload_admission_snapshot) {
            return payload_admission_fail_adapter(PayloadBadOrder,
                "Payload POSTDRAW_ADMIT requires DRAW_SUBMITTED state");
        }
        if (const uint32_t failure = payload_admission_check_boundary(true)) return failure;
        if (match->source_frames() != payload_admission_capture_source_frame + 1u) {
            return payload_admission_fail_adapter(PayloadFrameMismatch,
                "Source frame changed while awaiting post-draw admission");
        }
        const auto admitted = melee_web_source_memory_payload_snapshot_admit(
            payload_admission_snapshot);
        if (payload_admission_first_failure) return payload_admission_first_failure;
        if (admitted != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) {
            return payload_admission_fail_primitive(admitted, "payload admission after draw");
        }
        if (const uint32_t failure = payload_admission_check_boundary(true)) return failure;
        payload_admission_state = PayloadAdmissionState::Complete;
        return 0;
    }
    default:
        return payload_admission_fail_adapter(PayloadUnknownCommand,
            "Unknown source-payload admission command");
    }
}

uint32_t payload_admission_run(uint32_t command)
{
#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
    if(cpu_native_active())return payload_admission_fail_adapter(PayloadBadOrder,
        "CPU restore and payload dry run are mutually exclusive");
#endif
    if (payload_admission_state == PayloadAdmissionState::Closed) {
        if (payload_admission_first_failure) {
            std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
            return payload_admission_first_failure;
        }
        return command == 5 ? 0 : kPayloadAdapterFailureBase + PayloadBadOrder;
    }
    if (payload_admission_inflight) {
        if (command == 5) payload_admission_close_requested = true;
        return payload_admission_fail_adapter(PayloadUnsafeCallBoundary,
            "Reentrant source-payload command refused while an Asyncify call is active");
    }
    if (command != 5 && payload_admission_first_failure) {
        std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
        return payload_admission_first_failure;
    }
    if (payload_admission_runtime_boundary() != 0) {
        return payload_admission_fail_adapter(PayloadUnsafeCallBoundary,
            "Payload command requires a normal Asyncify main-runtime browser boundary");
    }
    if (command == 5) return payload_admission_close();
    payload_admission_inflight = true;
    const uint32_t result = payload_admission_execute(command);
    payload_admission_inflight = false;
    const bool close_requested = payload_admission_close_requested;
    const bool shutdown_requested = payload_admission_shutdown_requested;
    if (close_requested || shutdown_requested) {
        payload_admission_close_requested = false;
        (void)payload_admission_close();
    }
    if (shutdown_requested) {
        payload_admission_shutdown_requested = false;
        (void)melee_web_synctest_renderer_shutdown();
        if (payload_admission_first_failure) {
            std::snprintf(error, sizeof(error), "%s", payload_admission_first_message);
        }
    }
    return payload_admission_first_failure ? payload_admission_first_failure : result;
}
} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE uint32_t
melee_web_synctest_payload_admission_command(uint32_t command)
{
    return payload_admission_run(command);
}
// END MELEE_WEB_SOURCE_PAYLOAD_ADMISSION_ADAPTER

#include "cpu_restore_adapter.inc"

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_renderer_shutdown(void)
{
#if defined(MELEE_WEB_SYNCTEST_CPU_RESTORE)
    if(cpu_restore.inflight){cpu_restore.close_requested=true;
        cpu_fail(0x498u,"renderer shutdown reentered CPU command");return 0;}
    if(cpu_restore.capsule){cpu_fail(0x49bu,"retire source owners before renderer shutdown");return 0;}
#endif
    if (!payload_admission_shutdown_fallback()) return 0;
    return checked([] {
        if (!renderer_ready) return;
        const AuroraBrowserSubmissionStatus status =
            aurora_browser_submission_status();
        require(status.pendingFramePackets == 0 &&
                    status.pendingStagingBuffers == 0 && status.workerBusy == 0,
                "Aurora renderer shutdown requires completed browser submissions");
        aurora_shutdown();
        renderer_ready = false;
    });
}
}
