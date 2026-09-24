#pragma once

#include "gameplay_match_session.hpp"
#include "gameplay_pad_state.h"
#include "retail_draw_clock.hpp"
#include "retail_input_queue.hpp"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace melee_web {

struct RetailReplayInput {
    std::array<PADStatus, 4> pads{};
    std::array<uint8_t, 44> bytes{};
};

// Scene codes for the whole-session span table. They name the owner that must
// be active for the frames inside a span, so a whole-session replay cannot
// silently drive the wrong scene with a valid-looking pad stream.
enum RetailReplayScene : uint8_t {
    kRetailReplayCss = 1,
    kRetailReplaySss = 2,
    kRetailReplayMatch = 3,
    kRetailReplayResults = 4,
    kRetailReplayPrize = 5,
};

struct RetailReplaySpan {
    uint8_t scene = 0;
    uint32_t first_frame = 0;
    uint32_t last_frame = 0;
};

/* The bytes in this block are copied from the observer's PowerPC guest
 * ranges.  They stay explicitly endian tagged at this boundary; the menu
 * owner translates the source layouts before installing them in its native
 * backing.  Keeping the ranges here rather than passing a guest pointer is
 * what makes a replay safe across scene teardown. */
struct RetailReplayInitialCssContext {
    std::array<uint8_t, 0x18> game_rules{};
    std::array<uint8_t, 0x55E8> save_data{};
    std::array<uint8_t, 0x148> css_data{};
    std::array<uint8_t, GM_MAX_PLAYERS> ko_counts{};
};

// Controller input, initialization and (v6) recorded platform queue inputs.
// Expected game-state observations never enter the runtime.
struct RetailReplayRecipe {
    uint32_t version = 0, seed = 0;
    std::array<uint8_t, 0x138> setup{};
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> pad_bytes{};
    MeleeWebMenuMatchSelection selection{};
    std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)>
        initial_input{nullptr, melee_web_pad_state_free};
    std::vector<RetailReplayInput> frames;
    std::vector<bool> draw_boundaries;
    std::vector<RetailReplaySpan> spans;
    std::unique_ptr<RetailReplayInitialCssContext> initial_css;
    // Version 8 is the opt-in whole-session form: one continuous pad history
    // with a declared scene span table and copied first-CSS source context. It
    // retains one source arena instead of requiring a fresh application, and
    // it is the only version that may span the menu chain around a match.
    bool whole_session() const { return version == 8; }
    unsigned scheduling_mode() const { return version == 6 ? 2 : version == 5 ? 1 : 0; }
    std::size_t expected_draws() const {
        if (draw_boundaries.empty()) return frames.size();
        std::size_t count = 0;
        for (bool closes : draw_boundaries) count += closes;
        return count;
    }
    bool closes_draw_batch(std::size_t index) const {
        return draw_boundaries.empty() || draw_boundaries.at(index);
    }
};

/* MWRC v4 adds save masks after the fixed header. V5 then adds 40 bytes of
 * shared PAD/VI startup clock context. V6 replaces that context with u32 batch
 * count, u32 zero flags, then (u64 relative CPU poll time, u8 available samples)
 * per input-queue snapshot. No expected game states or draw indexes are stored.
 * V7 was the provisional whole-session envelope and is intentionally rejected
 * by the reader because it cannot carry first-CSS source context. V8 keeps
 * the v4 envelope, then adds a fixed context header (u16 schema=2, u16
 * flags=0, u32 context_bytes), the source GameRules, SaveData, complete
 * CSSData entry object and six-byte KO array, and finally the whole-session
 * span table. Schema 1 is rejected because it cannot initialize CSS. */
constexpr uint32_t kRetailReplayVersion = 8;
constexpr uint16_t kRetailReplayContextVersion = 2;
constexpr size_t kRetailReplayGameRulesBytes = 0x18;
constexpr size_t kRetailReplaySaveDataBytes = 0x55E8;
constexpr size_t kRetailReplayCssDataBytes = 0x148;
constexpr size_t kRetailReplayKoCountsBytes = GM_MAX_PLAYERS;
constexpr size_t kRetailReplayContextHeaderBytes = 8;
constexpr size_t kRetailReplayContextBytes = kRetailReplayGameRulesBytes +
    kRetailReplaySaveDataBytes + kRetailReplayCssDataBytes +
    kRetailReplayKoCountsBytes;
constexpr size_t kRetailReplayMaxSpans = 32;
constexpr size_t kRetailReplaySpanBytes = 12;
constexpr size_t kRetailReplayMaxBytes = 16 + 4 + 8 + 36000 * 9 + 0x138 +
    kRetailReplayContextHeaderBytes + kRetailReplayContextBytes +
    MELEE_WEB_PAD_STATE_BYTES + 36000 * 44 + 2 +
    kRetailReplayMaxSpans * kRetailReplaySpanBytes;
RetailReplayRecipe read_retail_replay(std::span<const uint8_t>);

// Diagnostic JSON output; callers must disable this instrumentation for timing
// acceptance. Initial follows construction, frame precedes audio/drawing, and
// end is emitted only after the complete input timeline and successful teardown.
void retail_replay_initial(const RetailReplayRecipe&, bool source_drawing);
void retail_replay_frame(const RetailReplayRecipe&, size_t index);
void retail_replay_draw(const RetailReplayRecipe&, size_t index);
void retail_replay_preparation_draw(const RetailReplayRecipe&);
void retail_replay_end(size_t frames);

} // namespace melee_web
