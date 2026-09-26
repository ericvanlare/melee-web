#include "gameplay_retail_recipe.hpp"
#include "gameplay_cpu_observation.h"
#include "gameplay_menu.h"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string_view>

extern "C" int melee_web_retail_setup(const uint8_t*, uint32_t,
    MeleeWebMenuMatchSelection*, char*, size_t);
extern "C" void melee_web_retail_state(void);
extern "C" uint32_t melee_web_retail_rng(void);
extern "C" uint32_t gm_GetFrameCount(void);
extern "C" uint32_t gm_8016AEEC(void);
extern "C" uint16_t gm_8016AEFC(void);
extern "C" int melee_web_match_source_result(void);
extern "C" int melee_web_match_end_state(void);

namespace melee_web {
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t cursor = 0;
    uint8_t u8() {
        check(cursor < bytes.size(), "Reference input is truncated");
        return bytes[cursor++];
    }
    uint16_t u16() { const auto hi = u8(); const auto lo = u8(); return uint16_t(hi) << 8 | lo; }
    uint32_t u32() { const auto hi = u16(); const auto lo = u16(); return uint32_t(hi) << 16 | lo; }
    uint64_t u64() { const auto hi = u32(); const auto lo = u32(); return uint64_t(hi) << 32 | lo; }
};
void hex(std::span<const uint8_t> bytes, std::ostream& out = std::cout) {
    static constexpr char digits[] = "0123456789abcdef";
    for (auto byte : bytes) out << digits[byte >> 4] << digits[byte & 15];
}
bool timer_audit_active = false;
bool whole_session_cpu_observation_requested = false;
bool whole_session_cpu_observation_started = false;
bool whole_session_cpu_observation_finished = false;
void timer_state(const char* record, size_t index = 0) {
    if (!timer_audit_active) return;
    // Separate diagnostic stream: never add fields to an older state schema,
    // and never execute this observer in a performance replay.
    std::cerr << "TIMER_AUDIT {\"record\":\"" << record << "\"";
    if (std::string_view(record) == "frame") std::cerr << ",\"index\":" << index;
    std::cerr << ",\"match_frame\":" << gm_GetFrameCount()
        << ",\"seconds\":" << gm_8016AEEC()
        << ",\"subframe\":" << gm_8016AEFC()
        << ",\"outcome\":" << melee_web_match_source_result()
        << ",\"end_state\":" << melee_web_match_end_state() << "}\n";
}
void history(const RetailReplayRecipe& recipe) {
    if (recipe.version < 2) return;
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> bytes{};
    melee_web_pad_state_capture(bytes.data());
    std::cout << ",\"pad_state_hex\":\""; hex(bytes); std::cout << "\"";
}
}

RetailReplayRecipe read_retail_replay(std::span<const uint8_t> bytes) {
    check(bytes.size() >= 16 + 0x138 && bytes.size() <= kRetailReplayMaxBytes,
          "Reference input size is outside its bounds");
    Reader input{bytes};
    check(input.u32() == 0x4d575243, "Unsupported reference input format");
    RetailReplayRecipe result;
    result.version = input.u32();
    check(result.version >= 1 && result.version <= kRetailReplayVersion,
          "Unsupported reference input version");
    const auto max_frames = result.version == 8
        ? kRetailReplayWholeSessionMaxFrames : kRetailReplayLegacyMaxFrames;
    /* Version 7 was emitted by the provisional producer before the runtime
     * could install the source's first-CSS context.  Accepting its bytes and
     * silently entering CSS would make the declared PAD/RNG context inert, so
     * fail at the transport boundary with an actionable migration error. */
    check(result.version != 7,
          "Whole-session MWRC v7 is unsupported: re-export with v8 first-CSS context");
    result.seed = input.u32();
    const auto count = input.u32();
    const size_t profile_bytes = result.version >= 4 ? 4 : 0;
    uint16_t unlocked_characters = 0;
    uint16_t unlocked_stages = 0;
    if (result.version >= 4) {
        /* Save profile belongs to the transport envelope, not StartMeleeData. */
        unlocked_characters = input.u16();
        unlocked_stages = input.u16();
    }
    size_t clock_bytes = result.version == 5 ? 40 : 0;
    const size_t context_bytes = result.version == 8
        ? kRetailReplayContextHeaderBytes + kRetailReplayContextBytes : 0;
    if (result.version == 5) {
        RetailDrawClock clock;
        clock.pad_period = input.u64(); clock.vi_period = input.u64();
        clock.next_pad = input.u64(); clock.first_vi_poll = input.u64();
        clock.startup_draws = input.u32();
        check(input.u32() == 0, "Unsupported clock context flags");
        check(count && count <= max_frames,
              "Reference input frame count is outside its bounds");
        result.draw_boundaries = clock.boundaries(count);
    }
    if (result.version == 6) {
        const auto batches = input.u32();
        check(input.u32() == 0, "Unsupported recorded input-queue flags");
        check(count && count <= max_frames && batches && batches <= count,
              "Invalid recorded input-queue size");
        clock_bytes = 8 + size_t(batches) * 9;
        check(bytes.size() == 20 + clock_bytes + 0x138 + MELEE_WEB_PAD_STATE_BYTES + size_t(count) * 44,
              "Recorded input-queue size disagrees with transport");
        std::vector<RetailQueueBatch> events;
        events.reserve(batches);
        for (uint32_t i = 0; i < batches; ++i) {
            const auto poll = input.u64();
            const auto available = input.u8();
            events.push_back({poll, available});
        }
        result.draw_boundaries = retail_queue_boundaries(events, count);
    }
    const size_t envelope_bytes = 16 + profile_bytes + clock_bytes + context_bytes + 0x138 +
        (result.version >= 2 ? MELEE_WEB_PAD_STATE_BYTES : 0) + size_t(count) * 44;
    if (result.version == 8) {
        check(input.u16() == kRetailReplayContextVersion && input.u16() == 0,
              "Unsupported whole-session first-CSS context header");
        check(input.u32() == kRetailReplayContextBytes,
              "Whole-session first-CSS context size disagrees with its transport");
        result.initial_css = std::make_unique<RetailReplayInitialCssContext>();
        for (auto& byte : result.initial_css->game_rules) byte = input.u8();
        for (auto& byte : result.initial_css->save_data) byte = input.u8();
        for (auto& byte : result.initial_css->css_data) byte = input.u8();
        for (auto& byte : result.initial_css->ko_counts) byte = input.u8();
        check((uint16_t(result.initial_css->save_data[0]) << 8 |
               result.initial_css->save_data[1]) == unlocked_characters &&
              (uint16_t(result.initial_css->save_data[2]) << 8 |
               result.initial_css->save_data[3]) == unlocked_stages,
              "Whole-session profile masks disagree with first-CSS SaveData");
    }
    if (result.version == 8)
        // The whole-session span table follows the frames; its own length is
        // validated once the table has been read.
        check(count && count <= max_frames && bytes.size() >= envelope_bytes + 2,
              "Whole-session input size disagrees with its transport");
    else
        check(count && count <= max_frames && bytes.size() == envelope_bytes,
              "Reference input frame count disagrees with its size");
    for (auto& byte : result.setup) byte = input.u8();
    char error[256]{};
    check(melee_web_retail_setup(result.setup.data(), result.seed, &result.selection,
                                error, sizeof(error)), error);
    check(result.version >= 3 || result.selection.player_count == 2,
          "Multiplayer input requires reference version 3");
    check(result.version < 3 ||
          (result.selection.player_count >= MELEE_WEB_MENU_MIN_PLAYERS &&
           result.selection.player_count <= MELEE_WEB_MENU_MAX_PLAYERS),
          "Reference version 3/4 requires two through four active players");
    if (result.version >= 4) {
        result.selection.unlocked_characters = unlocked_characters;
        result.selection.unlocked_stages = unlocked_stages;
        result.selection.save_profile_present = 1;
    }
    if (result.version >= 2) {
        for (auto& byte : result.pad_bytes) byte = input.u8();
        result.initial_input.reset(melee_web_pad_state_decode(result.pad_bytes.data(),
            result.pad_bytes.size(), error, sizeof(error)));
        check(bool(result.initial_input), error);
    }
    if (result.version < 4) {
        std::cerr << "MWRC warning: save profile was not recorded in legacy input "
                  << "version " << result.version
                  << "; no all-unlocked profile is assumed\n";
    }
    result.frames.resize(count);
    for (auto& frame : result.frames) {
        const auto offset = input.cursor;
        for (auto& pad : frame.pads) {
            pad.button = input.u16();
            pad.stickX = std::bit_cast<int8_t>(input.u8());
            pad.stickY = std::bit_cast<int8_t>(input.u8());
            pad.substickX = std::bit_cast<int8_t>(input.u8());
            pad.substickY = std::bit_cast<int8_t>(input.u8());
            pad.triggerLeft = input.u8(); pad.triggerRight = input.u8();
            pad.analogA = input.u8(); pad.analogB = input.u8();
            pad.err = std::bit_cast<int8_t>(input.u8());
        }
        std::copy_n(bytes.data() + offset, frame.bytes.size(), frame.bytes.data());
    }
    if (result.version == 8) {
        const auto span_count = input.u16();
        check(span_count >= 1 && span_count <= kRetailReplayMaxSpans,
              "Whole-session span count is outside its bounds");
        check(bytes.size() == envelope_bytes + 2 + size_t(span_count) * kRetailReplaySpanBytes,
              "Whole-session span table disagrees with its transport");
        result.spans.reserve(span_count);
        uint32_t next_frame = 0;
        for (uint32_t index = 0; index < span_count; ++index) {
            RetailReplaySpan span;
            span.scene = input.u8();
            check(input.u8() == 0 && input.u16() == 0, "Unsupported whole-session span flags");
            span.first_frame = input.u32();
            span.last_frame = input.u32();
            check(span.scene >= kRetailReplayCss && span.scene <= kRetailReplayPrize,
                  "Whole-session span scene is not an admitted scene");
            check(span.first_frame < count && span.last_frame < count,
                  "Whole-session span frame index is outside its bounds");
            check(span.first_frame == next_frame && span.last_frame >= span.first_frame,
                  "Whole-session spans must be ordered and contiguous");
            next_frame = span.last_frame + 1;
            result.spans.push_back(span);
        }
        check(next_frame == count, "Whole-session spans must cover every input frame");
        check(result.spans.front().scene == kRetailReplayCss,
              "Whole-session timeline must start in CSS");
        check(result.spans.back().scene == kRetailReplayResults ||
              result.spans.back().scene == kRetailReplayPrize,
              "Whole-session timeline must end in Results or Prize");
    }
    return result;
}

void retail_replay_session_initial(const RetailReplayRecipe& recipe) {
    check(recipe.whole_session(), "Session diagnostics require MWRC v8");
    timer_audit_active = false;
    whole_session_cpu_observation_requested = melee_web_cpu_observation_available() != 0;
    whole_session_cpu_observation_started = false;
    whole_session_cpu_observation_finished = false;
    std::cout << "{\"record\":\"header\",\"schema\":\"melee-web-port-session-diagnostic\","
        "\"version\":1,\"frames_requested\":" << recipe.frames.size()
        << ",\"comparison\":\"not_run\",\"cpu_observations\":\""
        << (whole_session_cpu_observation_requested ? "first_match_only" : "not_captured") << "\","
        "\"draw_state\":\"not_captured\"}\n";
}

void retail_replay_initial(const RetailReplayRecipe& recipe, bool source_drawing) {
    if (recipe.whole_session()) {
        std::cout << "{\"record\":\"session_match_enter_complete\",";
        melee_web_retail_state(); history(recipe); std::cout << "}\n";
        if (whole_session_cpu_observation_requested &&
            !whole_session_cpu_observation_started &&
            !whole_session_cpu_observation_finished) {
            melee_web_cpu_observation_begin(recipe.setup.data(), recipe.frames.size(),
                                             source_drawing);
            whole_session_cpu_observation_started = true;
        }
        return;
    }
    timer_audit_active = recipe.selection.start.rules.timer_enabled;
    std::cout << "{\"record\":\"header\",\"schema\":\"melee-web-port-replay-candidate\",\"version\":"
        << (recipe.version >= 3 ? 3 : recipe.version)
        << ",\"frames_requested\":" << recipe.frames.size();
    if (recipe.version >= 3) std::cout << ",\"active_player_count\":" << recipe.selection.player_count;
    std::cout << ",\"phase\":\"after_source_tick_before_audio_transport\",\"rendering\":\""
        << (source_drawing ? "source_draws" : "excluded") << "\",\"comparison\":\"not_run\"}\n";
    std::cout << "{\"record\":\"match_enter\",\"rng\":" << recipe.seed << ",\"start_melee_hex\":\"";
    hex(recipe.setup); std::cout << "\"";
    if (recipe.version >= 2) { std::cout << ",\"pad_state_hex\":\""; hex(recipe.pad_bytes); std::cout << "\""; }
    std::cout << "}\n{\"record\":\"match_enter_complete\",";
    melee_web_retail_state(); history(recipe); std::cout << "}\n";
    if (recipe.version >= 3)
        melee_web_cpu_observation_begin(recipe.setup.data(), recipe.frames.size(), source_drawing);
    if (timer_audit_active) {
        std::cerr << "TIMER_AUDIT {\"record\":\"header\",\"schema\":\"melee-web-match-timer-audit\",\"version\":1,\"frames_requested\":"
            << recipe.frames.size() << ",\"setup_hex\":\"";
        hex(recipe.setup, std::cerr);
        std::cerr << "\",\"phase\":\"after_source_tick_before_audio_transport\"}\n";
        timer_state("initial");
    }
}

void retail_replay_frame(const RetailReplayRecipe& recipe, size_t index, unsigned scene) {
    check(index < recipe.frames.size(), "Reference observation index is outside the timeline");
    const auto& frame = recipe.frames[index];
    if (recipe.whole_session()) {
        check(scene >= kRetailReplayCss && scene <= kRetailReplayPrize,
              "Session diagnostics require a live source scene");
        std::cout << "{\"record\":\"session_frame\",\"scene\":" << scene;
    } else {
        std::cout << "{\"record\":\"frame\"";
    }
    std::cout << ",\"index\":" << index << ",\"supplied_inputs\":[";
    for (unsigned port = 0; port < 4; ++port) {
        if (port) std::cout << ",";
        std::cout << "\""; hex(std::span(frame.bytes).subspan(port * 11, 11)); std::cout << "\"";
    }
    std::cout << "],";
    if (!recipe.whole_session() || scene == kRetailReplayMatch)
        melee_web_retail_state();
    else
        std::cout << "\"rng\":" << melee_web_retail_rng();
    history(recipe); std::cout << "}\n";
    if (recipe.whole_session()) {
        if (whole_session_cpu_observation_started) {
            if (scene == kRetailReplayMatch) {
                melee_web_cpu_observation_tick(index);
            } else {
                melee_web_cpu_observation_end(index);
                whole_session_cpu_observation_started = false;
                whole_session_cpu_observation_finished = true;
            }
        }
    } else {
        timer_state("frame", index);
        if (recipe.version >= 3) melee_web_cpu_observation_tick(index);
    }
}
void retail_replay_draw(const RetailReplayRecipe& recipe, size_t index) {
    if (!recipe.whole_session() && recipe.version >= 3) melee_web_cpu_observation_draw(index);
}
void retail_replay_preparation_draw(const RetailReplayRecipe& recipe) {
    if (!recipe.whole_session() && recipe.version >= 3) melee_web_cpu_observation_preparation_draw();
}
void retail_replay_end(size_t frames, bool whole_session) {
    if (whole_session) {
        if (whole_session_cpu_observation_started)
            melee_web_cpu_observation_end(frames);
        whole_session_cpu_observation_requested = false;
        whole_session_cpu_observation_started = false;
        whole_session_cpu_observation_finished = false;
    } else {
        melee_web_cpu_observation_end(frames);
    }
    std::cout << "{\"record\":\"end\",\"frames\":" << frames << ",\"status\":\"captured\"}\n";
    if (timer_audit_active) {
        std::cerr << "TIMER_AUDIT {\"record\":\"end\",\"frames\":" << frames << ",\"status\":\"captured\"}\n";
        timer_audit_active = false;
    }
}
} // namespace melee_web
