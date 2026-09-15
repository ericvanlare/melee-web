#pragma once

#include "gameplay_match_session.hpp"
#include "gameplay_pad_state.h"
#include "retail_draw_clock.hpp"
#include <array>
#include <memory>
#include <span>
#include <vector>

namespace melee_web {

struct RetailReplayInput {
    std::array<PADStatus, 4> pads{};
    std::array<uint8_t, 44> bytes{};
};

// Input and initialization only. Expected observations never enter the runtime.
struct RetailReplayRecipe {
    uint32_t version = 0, seed = 0;
    std::array<uint8_t, 0x138> setup{};
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> pad_bytes{};
    MeleeWebMenuMatchSelection selection{};
    std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)>
        initial_input{nullptr, melee_web_pad_state_free};
    std::vector<RetailReplayInput> frames;
    std::vector<bool> draw_boundaries;
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
 * shared PAD/VI startup clock context; expected observations remain excluded. */
constexpr size_t kRetailReplayMaxBytes = 16 + 4 + 40 + 0x138 +
    MELEE_WEB_PAD_STATE_BYTES + 36000 * 44;
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
