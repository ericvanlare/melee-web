#pragma once

#include <cstdint>

namespace melee_web {

// This diagnostic counts actual original scheduler boundaries, not PAD rows or
// the gameplay timer (which stays stopped during Entry/Ready).
// gmMain's authored PAD capacity is five: a passive qualifying draw can
// retain at most cutoff + capacity - 1 observations. Browser preparation must
// separately reject batching that its existing per-tick draws cannot replay.
struct DiagnosticPrefixProgress {
    static constexpr std::uint32_t comparison_ticks = 60, authored_queue_capacity = 5;
    static constexpr std::uint32_t maximum_ticks = comparison_ticks + authored_queue_capacity - 1;
    std::uint32_t observations = 0, first_source_tick = 0, last_source_tick = 0;
    bool observe(std::uint32_t before, std::uint32_t after) noexcept {
        if (observations >= maximum_ticks || (!observations && before != 0) ||
            before == UINT32_MAX || after != before + 1 ||
            (observations && before != last_source_tick + 1)) return false;
        if (!observations) first_source_tick = before;
        last_source_tick = before;
        ++observations;
        return true;
    }
    bool ready(std::uint32_t bound_observations, bool input_consumed, bool final_draw, bool live_match,
               bool pending, bool complete, int outcome) const noexcept {
        return bound_observations >= comparison_ticks && bound_observations <= maximum_ticks &&
               observations == bound_observations && input_consumed && final_draw && live_match &&
               !pending && !complete && outcome == 0;
    }
};

// The v8 timeline ends at the final Results/Prize input.  Returning to CSS is
// a native owner transition after that input, so its preparation draw is not a
// replay PAD step and must not be counted as one.
enum class ReplayCompletionOwner : std::uint8_t {
    Unknown,
    Css,
    Sss,
    Match,
    Results,
    Prize,
};

struct ReplayCompletionState {
    bool whole_session = false;
    bool input_consumed = false;
    bool final_input_drawn = false;
    ReplayCompletionOwner final_input_owner = ReplayCompletionOwner::Unknown;
    bool final_results_or_prize_transitioned = false;
    bool live_css_entered = false;
    bool preparation_settled = false;
    bool source_tick_after_input = false;
    bool source_draw_after_input = false;
};

constexpr bool replay_completion_ready(const ReplayCompletionState& state) noexcept
{
    // Preserve the legacy replay's final-source-draw contract.  The caller's
    // input_consumed bit is still meaningful for v8, whose owner continues
    // after the final replay frame without consuming another PAD sample.
    if (!state.whole_session)
        return state.final_input_drawn;

    if (!state.input_consumed || !state.final_input_drawn ||
        (state.final_input_owner != ReplayCompletionOwner::Results &&
         state.final_input_owner != ReplayCompletionOwner::Prize))
        return false;

    // A CSS preparation draw is allowed between the final input and
    // publication.  A source simulation tick or source draw after the input
    // would fabricate an unrecorded PAD step and is never admissible.
    return state.final_results_or_prize_transitioned && state.live_css_entered &&
           state.preparation_settled && !state.source_tick_after_input &&
           !state.source_draw_after_input;
}

} // namespace melee_web
