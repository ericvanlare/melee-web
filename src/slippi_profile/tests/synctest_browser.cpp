/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Draw-reference adapter for the same source-only observation fixture. The
 * adapter owns a real Aurora WebGPU context and never restores renderer state. */
#include "../../../tests/gameplay_snapshot_probe.cpp"

#include "slippi_profile/profile.h"
#include "slippi_profile/draw_diagnostics.h"
#include "gameplay_bootstrap.h"
#include "gameplay_audio.h"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <emscripten.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>

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
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_decision(int tick_kind)
{
    return slippi_profile_source_draw_decision(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5,
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
            SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5,
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

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_renderer_shutdown(void)
{
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
