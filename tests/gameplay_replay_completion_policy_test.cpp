#include "gameplay_replay_completion_policy.hpp"

#include <cassert>
#include <initializer_list>

namespace {

melee_web::ReplayCompletionState whole(
    melee_web::ReplayCompletionOwner final_owner)
{
    melee_web::ReplayCompletionState state;
    state.whole_session = true;
    state.input_consumed = true;
    state.final_input_drawn = true;
    state.final_input_owner = final_owner;
    return state;
}

} // namespace

int main()
{
    melee_web::DiagnosticPrefixProgress prefix;
    assert(!prefix.observe(9,10)); // a later scene cannot become cold match zero.
    assert(!prefix.observe(0,0)); // a PAD call alone proves no source traversal.
    for (unsigned tick=0; tick<60; ++tick) assert(prefix.observe(tick,tick+1));
    assert(prefix.first_source_tick==0 && prefix.last_source_tick==59);
    assert(prefix.ready(60,true,true,true,false,false,0));
    auto extra=prefix; assert(extra.observe(60,61));
    assert(extra.ready(61,true,true,true,false,false,0));
    assert(!extra.ready(60,true,true,true,false,false,0));
    assert(!prefix.ready(60,false,true,true,false,false,0));
    assert(!prefix.ready(60,true,false,true,false,false,0));
    assert(!prefix.ready(60,true,true,false,false,false,0));
    assert(!prefix.ready(60,true,true,true,true,false,0));
    assert(!prefix.ready(60,true,true,true,false,true,0));
    assert(!prefix.ready(60,true,true,true,false,false,1));
    melee_web::DiagnosticPrefixProgress missing;
    for(unsigned tick=0;tick<59;++tick)assert(missing.observe(tick,tick+1));
    assert(!missing.ready(60,true,true,true,false,false,0));
    assert(!missing.observe(60,61));

    // The old single-match route remains complete on its final source draw;
    // it has no whole-session return-to-CSS prerequisites.
    melee_web::ReplayCompletionState legacy;
    legacy.final_input_drawn = true;
    assert(melee_web::replay_completion_ready(legacy));

    // A v8 Results or Prize draw consumes the final recorded sample but is
    // not completion.  The owner must first perform the source transition
    // and enter a live CSS scene with preparation settled.
    for (const auto final_owner : {melee_web::ReplayCompletionOwner::Results,
                                   melee_web::ReplayCompletionOwner::Prize}) {
        auto state = whole(final_owner);
        assert(!melee_web::replay_completion_ready(state));

        state.final_results_or_prize_transitioned = true;
        assert(!melee_web::replay_completion_ready(state));

        state.live_css_entered = true;
        assert(!melee_web::replay_completion_ready(state));

        state.preparation_settled = true;
        assert(melee_web::replay_completion_ready(state));

        // Preparation may publish a CSS image, but an unrecorded source tick
        // or source draw after the final PAD sample invalidates completion.
        state.source_tick_after_input = true;
        assert(!melee_web::replay_completion_ready(state));
        state.source_tick_after_input = false;
        state.source_draw_after_input = true;
        assert(!melee_web::replay_completion_ready(state));
    }

    // A malformed timeline cannot complete from a Match/CSS owner, even when
    // all transition flags are set.
    auto wrong_owner = whole(melee_web::ReplayCompletionOwner::Match);
    wrong_owner.final_results_or_prize_transitioned = true;
    wrong_owner.live_css_entered = true;
    wrong_owner.preparation_settled = true;
    assert(!melee_web::replay_completion_ready(wrong_owner));

    auto missing_input = whole(melee_web::ReplayCompletionOwner::Results);
    missing_input.input_consumed = false;
    missing_input.final_results_or_prize_transitioned = true;
    missing_input.live_css_entered = true;
    missing_input.preparation_settled = true;
    assert(!melee_web::replay_completion_ready(missing_input));
}
