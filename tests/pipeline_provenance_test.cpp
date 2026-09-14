#include "pipeline_provenance.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
bool print_full_capture = false;

MeleeWebPipelineSourceContext context(uint32_t scene, uint32_t phase) {
    MeleeWebPipelineSourceContext value{};
    value.scene = scene;
    value.phase = phase;
    value.world_generation = scene == MELEE_WEB_PIPELINE_SCENE_BOOT ? 0 : 7;
    value.route_epoch = 11;
    value.source_tick = 19;
    value.coverage_case_id = 23;
    value.input_binding_sha256_present = 1;
    std::memset(value.input_binding_sha256, 0xab, sizeof(value.input_binding_sha256));
    value.stage = 37;
    value.ground = 38;
    value.hud_layout = 1;
    value.gobj_classifier = 3;
    value.gx_link = 1;
    value.render_pass = 4;
    value.active_player_count = scene == MELEE_WEB_PIPELINE_SCENE_BOOT ? 0 : 2;
    for (auto& player : value.players) {
        player.motion_id = -1;
        player.stocks = -1;
    }
    value.players[0].character = 0;
    value.players[0].fighter_kind = 2;
    value.players[0].costume = 1;
    value.players[0].subcolor = 0;
    value.players[0].effect_bank = 30;
    value.players[0].motion_id = 14;
    value.players[0].stocks = 4;
    value.players[1] = value.players[0];
    value.players[1].character = 1;
    value.players[1].costume = 2;
    for(unsigned i=value.active_player_count;i<4;++i){
        value.players[i].motion_id=-1;value.players[i].stocks=-1;
    }
    value.owner_kind = MELEE_WEB_PIPELINE_OWNER_ROUTE_COMPOSITE;
    value.owner_id = 99;
    value.owner_effect_bank = 30;
    return value;
}

MeleeWebPipelineRecorder* recorder_with(const MeleeWebPipelineSourceContext& boot,
                                        uint32_t max_records = 64,
                                        uint32_t max_descriptors = 16) {
    MeleeWebPipelineRecorderConfig config{};
    config.max_records = max_records;
    config.max_descriptors = max_descriptors;
    config.max_scopes = 4;
    config.max_json_bytes = 256 * 1024;
    config.boot_context = boot;
    char error[256]{};
    auto* recorder = melee_web_pipeline_recorder_create(&config, error, sizeof(error));
    assert(recorder && !error[0]);
    assert(melee_web_pipeline_capture_begin(recorder, &boot, error, sizeof(error)));
    assert(!error[0]);
    return recorder;
}

void check_full_capture() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder = recorder_with(boot);
    char error[256]{};
    const uint8_t descriptor[] = {0x01, 0x02, 0x03, 0x04};
    assert(melee_web_pipeline_device_begin(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_begin(recorder, 10, error, sizeof(error)));
    assert(melee_web_pipeline_frame_begin(recorder, 9, error, sizeof(error)));
    assert(melee_web_pipeline_packet_begin(recorder, 11, error, sizeof(error)));

    auto match = context(MELEE_WEB_PIPELINE_SCENE_MATCH,
                         MELEE_WEB_PIPELINE_PHASE_INTERACTIVE);
    MeleeWebPipelineSourceToken source{};
    assert(melee_web_pipeline_source_scope_begin(recorder, &match, &source,
                                                 error, sizeof(error)));
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_LAST_REF,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_LAST_REF,
                                      error, sizeof(error)));
    /* Repeated typed uses share one dictionary entry.  A different type/version
     * remains a distinct descriptor even when its bytes match. */
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_READY,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_READY,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_PENDING,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_PENDING,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_CREATE,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_CREATED,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_observe(recorder,
                                      MELEE_WEB_PIPELINE_EVENT_DRAW_CACHE_REUSE,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_REUSED,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_MERGE,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_MERGED,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_observe_at(recorder,
                                      MELEE_WEB_PIPELINE_EVENT_PACKET_USE,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_USED, 9, 11,
                                      error, sizeof(error)));

    /* Closing the source scope before worker consumption is valid. */
    assert(melee_web_pipeline_source_scope_end(recorder, &source,
                                               error, sizeof(error)));
    MeleeWebPipelineExecutionToken execution{};
    assert(melee_web_pipeline_execution_push(recorder, &source, &execution,
                                             error, sizeof(error)));
    assert(melee_web_pipeline_observe_at(recorder, MELEE_WEB_PIPELINE_EVENT_READY,
                                      4, 0x10, 2, descriptor, sizeof(descriptor),
                                      MELEE_WEB_PIPELINE_OUTCOME_READY, 9, 11,
                                      error, sizeof(error)));
    assert(melee_web_pipeline_execution_pop(recorder, &execution,
                                             error, sizeof(error)));
    assert(melee_web_pipeline_packet_end(recorder, 11, error, sizeof(error)));
    assert(melee_web_pipeline_frame_end(recorder, 9, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_end(recorder, 10, error, sizeof(error)));
    assert(melee_web_pipeline_device_end(recorder, 1, error, sizeof(error)));

    /* Import is allowed using the copied BOOT context with no source scope;
     * observe_at IDs are deliberately ignored for this background event. */
    assert(melee_web_pipeline_observe_at(recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT,
                                         8, 0x20, 3, descriptor, sizeof(descriptor),
                                         MELEE_WEB_PIPELINE_OUTCOME_IMPORTED,
                                         999, 998, error, sizeof(error)));
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    char json[256 * 1024]{};
    size_t written = 0;
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(written && std::strstr(json, MELEE_WEB_PIPELINE_PROVENANCE_SCHEMA));
    assert(std::strstr(json, "\"descriptor_sha256\":\""));
    assert(std::strstr(json, "\"kind\":\"scope_begin\",\"scope_id\":1"));
    assert(std::strstr(json, "\"kind\":\"scope_end\",\"scope_id\":1"));
    assert(std::strstr(json, "\"kind\":\"import\",\"scope_id\":0,\"thread_id\":1,\"context_index\":1"));
    assert(std::strstr(json, "\"kind\":\"import\",\"scope_id\":0,\"thread_id\":1,\"context_index\":1") &&
           std::strstr(json, "\"device_generation\":0,\"frame_id\":0,\"packet_id\":0"));
    MeleeWebPipelineStatus status{};
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(status.final && status.valid && status.descriptor_count == 2);
    if(print_full_capture){std::fwrite(json,1,written,stdout);std::fputc('\n',stdout);}
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_missing_context_and_scope_pair() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder = recorder_with(boot);
    char error[256]{};
    const uint8_t byte = 1;
    assert(!melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_READY,
                                       1, 1, 1, &byte, 1,
                                       MELEE_WEB_PIPELINE_OUTCOME_READY,
                                       error, sizeof(error)));
    MeleeWebPipelineStatus status{};
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_MISSING_CONTEXT);
    /* A separate valid capture checks the end-pair latch without leaving an
     * active scope behind. */
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));

    recorder = recorder_with(boot);
    auto match = context(MELEE_WEB_PIPELINE_SCENE_MATCH,
                         MELEE_WEB_PIPELINE_PHASE_INTERACTIVE);
    MeleeWebPipelineSourceToken token{};
    assert(melee_web_pipeline_source_scope_begin(recorder, &match, &token,
                                                 error, sizeof(error)));
    assert(melee_web_pipeline_source_scope_end(recorder, &token,
                                               error, sizeof(error)));
    assert(!melee_web_pipeline_source_scope_end(recorder, &token,
                                                error, sizeof(error)));
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_UNPAIRED_SCOPE);
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_overflow_and_stale_token() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    char error[256]{};
    auto* recorder = recorder_with(boot, 1);
    const uint8_t byte = 1;
    assert(melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT,
                                      1, 1, 1, &byte, 1,
                                      MELEE_WEB_PIPELINE_OUTCOME_IMPORTED,
                                      error, sizeof(error)));
    assert(!melee_web_pipeline_observe(recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT,
                                       1, 2, 1, &byte, 1,
                                       MELEE_WEB_PIPELINE_OUTCOME_IMPORTED,
                                       error, sizeof(error)));
    MeleeWebPipelineStatus status{};
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid && status.dropped == 1 &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_OVERFLOW);
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    char json[64 * 1024]{};
    size_t written = 0;
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(written && std::strstr(json, "\"valid\":false"));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));

    recorder = recorder_with(boot);
    assert(melee_web_pipeline_device_begin(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_begin(recorder, 1, error, sizeof(error)));
    auto match = context(MELEE_WEB_PIPELINE_SCENE_MATCH,
                         MELEE_WEB_PIPELINE_PHASE_INTERACTIVE);
    MeleeWebPipelineSourceToken token{};
    assert(melee_web_pipeline_source_scope_begin(recorder, &match, &token,
                                                 error, sizeof(error)));
    assert(melee_web_pipeline_source_scope_end(recorder, &token,
                                               error, sizeof(error)));
    assert(melee_web_pipeline_renderer_end(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_device_end(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_device_begin(recorder, 2, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_begin(recorder, 2, error, sizeof(error)));
    MeleeWebPipelineExecutionToken stale{};
    assert(!melee_web_pipeline_execution_push(recorder, &token, &stale,
                                              error, sizeof(error)));
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_STALE_DEVICE);
    assert(melee_web_pipeline_renderer_end(recorder, 2, error, sizeof(error)) == 0);
    /* The sticky failure is intentionally visible; lifecycle cleanup is not
     * allowed to turn it into a successful capture. */
}

void check_final_snapshot_preserves_unpaired_evidence() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder = recorder_with(boot);
    char error[256]{};
    auto match = context(MELEE_WEB_PIPELINE_SCENE_MATCH,
                         MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    MeleeWebPipelineSourceToken token{};
    assert(melee_web_pipeline_source_scope_begin(recorder, &match, &token,
                                                 error, sizeof(error)));
    assert(!melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    char json[64 * 1024]{};
    size_t written = 0;
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(written && std::strstr(json, "\"valid\":false") &&
           std::strstr(json, "\"open_scopes\":1"));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_thread_local_deferred_context_and_nonce() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder = recorder_with(boot);
    char error[256]{};
    const uint8_t descriptor[] = {0x09, 0x08};
    assert(melee_web_pipeline_device_begin(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_begin(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_frame_begin(recorder, 9, error, sizeof(error)));
    assert(melee_web_pipeline_packet_begin(recorder, 11, error, sizeof(error)));

    auto source_context = context(MELEE_WEB_PIPELINE_SCENE_MATCH,
                                  MELEE_WEB_PIPELINE_PHASE_INTERACTIVE);
    MeleeWebPipelineSourceToken copied{};
    assert(melee_web_pipeline_source_scope_begin(recorder, &source_context,
                                                 &copied, error, sizeof(error)));
    assert(melee_web_pipeline_source_scope_end(recorder, &copied,
                                               error, sizeof(error)));

    /* The producer thread keeps a different source scope open while a worker
     * consumes the closed copied token.  Each thread must retain its own stack. */
    auto producer_context = context(MELEE_WEB_PIPELINE_SCENE_CSS,
                                    MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    std::atomic<bool> allow_worker{false};
    std::atomic<bool> worker_pushed{false};
    std::atomic<bool> worker_continue{false};
    std::atomic<bool> worker_push_ok{false};
    char worker_error[256]{};
    std::thread worker([&] {
        while (!allow_worker.load()) std::this_thread::yield();
        MeleeWebPipelineExecutionToken execution{};
        const int pushed = melee_web_pipeline_execution_push(
            recorder, &copied, &execution, worker_error, sizeof(worker_error));
        worker_push_ok.store(pushed != 0);
        worker_pushed.store(true);
        while (!worker_continue.load()) std::this_thread::yield();
        if (pushed) {
            assert(melee_web_pipeline_observe_at(
                recorder, MELEE_WEB_PIPELINE_EVENT_READY, 4, 0x10, 2,
                descriptor, sizeof(descriptor), MELEE_WEB_PIPELINE_OUTCOME_READY,
                9, 11, worker_error, sizeof(worker_error)));
            assert(melee_web_pipeline_execution_pop(recorder, &execution,
                                                    worker_error, sizeof(worker_error)));
        }
    });

    MeleeWebPipelineSourceToken producer_scope{};
    assert(melee_web_pipeline_source_scope_begin(
        recorder, &producer_context, &producer_scope, error, sizeof(error)));
    assert(melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_READY, 4, 0x20, 2, descriptor,
        sizeof(descriptor), MELEE_WEB_PIPELINE_OUTCOME_READY, error,
        sizeof(error)));
    allow_worker.store(true);
    while (!worker_pushed.load()) std::this_thread::yield();
    assert(worker_push_ok.load());
    assert(melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_LAST_REF, 4, 0x20, 2, descriptor,
        sizeof(descriptor), MELEE_WEB_PIPELINE_OUTCOME_LAST_REF, error,
        sizeof(error)));
    worker_continue.store(true);
    worker.join();
    assert(melee_web_pipeline_source_scope_end(recorder, &producer_scope,
                                               error, sizeof(error)));
    assert(melee_web_pipeline_packet_end(recorder, 11, error, sizeof(error)));
    assert(melee_web_pipeline_frame_end(recorder, 9, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_end(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_device_end(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    char json[256 * 1024]{};
    size_t written = 0;
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(written && std::strstr(json, "\"thread_id\":1") &&
           std::strstr(json, "\"thread_id\":2"));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));

    /* A copied token with altered context must fail nonce validation after its
     * source scope has closed, without any historical scope registry. */
    recorder = recorder_with(boot);
    assert(melee_web_pipeline_device_begin(recorder, 1, error, sizeof(error)));
    assert(melee_web_pipeline_renderer_begin(recorder, 1, error, sizeof(error)));
    MeleeWebPipelineSourceToken original{};
    assert(melee_web_pipeline_source_scope_begin(
        recorder, &source_context, &original, error, sizeof(error)));
    assert(melee_web_pipeline_source_scope_end(recorder, &original,
                                               error, sizeof(error)));
    MeleeWebPipelineSourceToken altered = original;
    ++altered.context.source_tick;
    MeleeWebPipelineExecutionToken rejected{};
    assert(!melee_web_pipeline_execution_push(recorder, &altered, &rejected,
                                              error, sizeof(error)));
    MeleeWebPipelineStatus status{};
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_INVALID_REQUEST);
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_descriptor_memoization() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder = recorder_with(boot, 64, 8);
    char error[256]{};
    uint8_t descriptor[] = {0x01, 0x02, 0x03, 0x04};

    auto import = [&](uint32_t type, uint64_t pipeline_ref,
                      uint32_t config_version) {
        return melee_web_pipeline_observe(
            recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, type, pipeline_ref,
            config_version, descriptor, sizeof(descriptor),
            MELEE_WEB_PIPELINE_OUTCOME_IMPORTED, error, sizeof(error));
    };

    /* Repeated key and identical bytes hit the private memo.  Mutating the
     * same source buffer under the same key must recompute its digest and
     * leave both versions observable in the typed dictionary. */
    assert(import(7, 0x55, 9));
    assert(import(7, 0x55, 9));
    descriptor[0] = 0x09;
    assert(import(7, 0x55, 9));
    /* Type and config version are part of the memo key and descriptor identity. */
    assert(import(8, 0x55, 9));
    assert(import(7, 0x55, 10));

    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    char json[128 * 1024]{};
    size_t written = 0;
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    const std::string output(json, written);
    const auto count = [](const std::string& haystack, const char* needle) {
        size_t found = 0;
        for (size_t offset = 0;;) {
            offset = haystack.find(needle, offset);
            if (offset == std::string::npos) return found;
            ++found;
            offset += std::strlen(needle);
        }
    };
    /* The unchanged key contributes two events plus one dictionary row; the
     * mutated digest contributes three events plus three typed rows. */
    assert(count(output,
                 "9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a") ==
           3);
    assert(count(output,
                 "66396eb26f8f188cca1e86936688d2c3a148a6ab0369aa8015b181563761b306") ==
           6);
    assert(output.find("\"descriptor_count\":4") != std::string::npos);
    MeleeWebPipelineStatus status{};
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(status.descriptor_count == 4 && status.total_records == 5);
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));

    /* Capture begin clears the byte memo and the exported catalog together. */
    recorder = recorder_with(boot, 8, 1);
    assert(import(7, 0x55, 9));
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(std::strstr(json, "\"descriptor_count\":1"));
    assert(melee_web_pipeline_capture_begin(recorder, &boot, error, sizeof(error)));
    // A different key fits the one-entry limit only if capture begin cleared
    // the previous memo on this same recorder instance.
    assert(import(8, 0x66, 10));
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_json_drain(recorder, json, sizeof(json), &written,
                                         1, error, sizeof(error)));
    assert(std::strstr(json, "\"descriptor_count\":1"));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_descriptor_memo_bounds() {
    const auto boot = context(MELEE_WEB_PIPELINE_SCENE_BOOT,
                              MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    char error[256]{};
    MeleeWebPipelineStatus status{};
    uint8_t byte = 0x01;

    auto* recorder = recorder_with(boot, 8, 2);
    assert(melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, 1, 1, 1, &byte, 1,
        MELEE_WEB_PIPELINE_OUTCOME_IMPORTED, error, sizeof(error)));
    assert(melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, 1, 2, 1, &byte, 1,
        MELEE_WEB_PIPELINE_OUTCOME_IMPORTED, error, sizeof(error)));
    assert(!melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, 1, 3, 1, &byte, 1,
        MELEE_WEB_PIPELINE_OUTCOME_IMPORTED, error, sizeof(error)));
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW);
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));

    /* Sixteen 1 MiB entries exactly fill the private aggregate byte bound;
     * the next distinct key must invalidate instead of growing the cache. */
    recorder = recorder_with(boot, 32, 17);
    std::vector<uint8_t> block(1024 * 1024, 0x5a);
    for (uint64_t i = 0; i != 16; ++i) {
        assert(melee_web_pipeline_observe(
            recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, 2, 0x100 + i, 1,
            block.data(), block.size(), MELEE_WEB_PIPELINE_OUTCOME_IMPORTED,
            error, sizeof(error)));
    }
    assert(!melee_web_pipeline_observe(
        recorder, MELEE_WEB_PIPELINE_EVENT_IMPORT, 2, 0x200, 1, &byte, 1,
        MELEE_WEB_PIPELINE_OUTCOME_IMPORTED, error, sizeof(error)));
    assert(melee_web_pipeline_status(recorder, &status, error, sizeof(error)));
    assert(!status.valid &&
           status.invalid_reason == MELEE_WEB_PIPELINE_INVALID_DESCRIPTOR_OVERFLOW);
    assert(melee_web_pipeline_capture_end(recorder, error, sizeof(error)));
    assert(melee_web_pipeline_recorder_destroy(recorder, error, sizeof(error)));
}

void check_descriptor_digest_vectors() {
    // Independently generated SHA-256 vectors cover short inputs, both
    // padding branches, a block boundary, and a real GX descriptor length.
    struct Vector { size_t bytes; const char* sha256; };
    const Vector vectors[] = {
        {3, "a497359a7a01fd57ac3b4895f289bcfc496bd168996dc3b0351b4acf07c81f83"},
        {55, "c40dbcd18dfdb34a5c52a791f09286986b5e9d61f867278c03adda227684e897"},
        {56, "ca6c4804e2170c368f94d2da606adf4552eb0b1d35bc2ad1dc12887627a028eb"},
        {64, "30a1b375b49e0f1d5e00b6868bbfd471cbe5387806456cc11fd153c689bfe747"},
        {2772, "ad25eb427fb0688587f8545c723e2147e747cce533b188f64c86b41f9e8ae844"},
    };
    auto boot=context(MELEE_WEB_PIPELINE_SCENE_BOOT,MELEE_WEB_PIPELINE_PHASE_PREPARATION);
    auto* recorder=recorder_with(boot);
    char error[256]{};
    for(const auto& vector:vectors){
        std::string bytes(vector.bytes, '\0');
        for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<char>((i*17+3)&255);
        assert(melee_web_pipeline_observe(recorder,MELEE_WEB_PIPELINE_EVENT_IMPORT,
            1,vector.bytes,1,bytes.data(),bytes.size(),MELEE_WEB_PIPELINE_OUTCOME_IMPORTED,
            error,sizeof(error)));
    }
    assert(melee_web_pipeline_capture_end(recorder,error,sizeof(error)));
    char json[64*1024]{};size_t written=0;
    assert(melee_web_pipeline_json_drain(recorder,json,sizeof(json),&written,1,error,sizeof(error)));
    for(const auto& vector:vectors)assert(std::strstr(json,vector.sha256));
    assert(melee_web_pipeline_recorder_destroy(recorder,error,sizeof(error)));
}

} // namespace

int main(int argc, char** argv) {
    print_full_capture=argc==2&&std::strcmp(argv[1],"--json")==0;
    check_descriptor_digest_vectors();
    check_full_capture();
    check_missing_context_and_scope_pair();
    check_overflow_and_stale_token();
    check_final_snapshot_preserves_unpaired_evidence();
    check_thread_local_deferred_context_and_nonce();
    check_descriptor_memoization();
    check_descriptor_memo_bounds();
    return 0;
}
