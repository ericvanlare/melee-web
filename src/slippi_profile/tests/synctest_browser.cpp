/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Draw-reference adapter for the same source-only observation fixture. The
 * adapter owns a real Aurora WebGPU context and never restores renderer state. */
#include "../../../tests/gameplay_snapshot_probe.cpp"

#include "slippi_profile/profile.h"
#include "slippi_profile/draw_diagnostics.h"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx.h>
#include <emscripten.h>

#include <array>
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

void renderer_log(AuroraLogLevel level, const char *module,
                  const char *message, unsigned length)
{
    if (level >= LOG_ERROR) ++renderer_error_count;
    std::fprintf(level >= LOG_ERROR ? stderr : stdout, "[%s] %.*s\n",
                 module, static_cast<int>(length), message);
}
}

extern "C" {
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
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2);
}

EMSCRIPTEN_KEEPALIVE int melee_web_synctest_draw_decision(int tick_kind)
{
    return slippi_profile_source_draw_decision(
        SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2,
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
            SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_V2,
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
