#pragma once

#include <cstdint>

namespace melee_web {

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
