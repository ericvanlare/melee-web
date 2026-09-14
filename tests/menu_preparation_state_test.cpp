#include "menu_preparation_state.hpp"

#include <cassert>

int main()
{
    using melee_web::MenuPreparationState;
    using Phase = MenuPreparationState::Phase;

    MenuPreparationState state;
    assert(state.phase() == Phase::Idle);
    assert(!state.busy());
    assert(!state.suppress_source_draw());
    assert(!state.arm(true));
    assert(!MenuPreparationState::needs_live_render_settle(0));
    assert(MenuPreparationState::needs_live_render_settle(1));

    assert(state.request());
    assert(state.phase() == Phase::WaitingForAudio);
    assert(state.busy());
    assert(state.suppress_source_draw());
    assert(!state.request());
    assert(!state.begin_construction(false));
    assert(state.phase() == Phase::WaitingForAudio);
    assert(state.suppress_source_draw());

    assert(state.begin_construction(true));
    assert(state.phase() == Phase::Constructing);
    assert(state.suppress_source_draw());
    assert(!state.begin_construction(true));
    state.finish_construction(true);
    assert(state.phase() == Phase::Priming);
    assert(state.busy());
    assert(!state.suppress_source_draw());
    assert(!state.observe_render(false, 0, false));
    assert(state.phase() == Phase::Priming);
    assert(!state.observe_render(true, 3, true));
    assert(state.phase() == Phase::Settling);
    assert(!state.observe_render(true, 0, true));
    assert(!state.observe_render(true, 0, false));
    assert(state.phase() == Phase::Settling);
    assert(state.observe_render(true, 0, false));
    assert(state.phase() == Phase::Arming);
    // Quiet submission counters cannot release the source clock. A delayed
    // completion callback must hold the same state without additional draws.
    for (unsigned poll = 0; poll < 8; ++poll) {
        assert(!state.arm(false));
        assert(state.busy());
        assert(!state.warming());
        assert(state.suppress_source_draw());
        assert(!state.observe_render(false, 0, false));
        assert(state.phase() == Phase::Arming);
    }
    assert(state.arm(true));
    assert(state.phase() == Phase::Idle);
    assert(!state.busy());

    assert(state.request());
    assert(state.begin_construction(true));
    state.finish_construction(false);
    assert(state.phase() == Phase::Idle);
    assert(!state.suppress_source_draw());

    assert(state.request_render_settle());
    assert(state.phase() == Phase::Settling);
    assert(!state.suppress_source_draw());
    assert(!state.request_render_settle());
    assert(!state.observe_render(true, 1, true));
    assert(!state.observe_render(true, 0, false));
    assert(state.observe_render(true, 0, false));
    assert(!state.arm(false));
    state.reset(); // Unload/failure cancels readiness without arming a dead owner.
    assert(!state.arm(true));
    assert(!state.busy());

    // A final source draw can discover an undeclared pipeline after requesting
    // a transition. Its frozen settle must drain before that transition starts.
    assert(state.suppress_source_draw(true));
    assert(state.request_render_settle());
    assert(!state.request());
    assert(!state.suppress_source_draw(true));
    assert(!state.observe_render(true, 1, true));
    assert(!state.observe_render(true, 0, false));
    assert(!state.suppress_source_draw(true));
    assert(state.observe_render(true, 0, false));
    assert(state.suppress_source_draw(true));
    assert(!state.arm(false));
    assert(state.arm(true));
    assert(state.suppress_source_draw(true));
    assert(state.request());
    assert(state.begin_construction(true));
    state.finish_construction(false);

    state.request();
    state.reset();
    assert(state.phase() == Phase::Idle);
    assert(!state.busy());
    assert(!state.suppress_source_draw());
}
