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
    assert(!state.arm());

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
    assert(state.phase() == Phase::Arming);
    assert(state.busy());
    // The arm callback is the first point where a source draw is permitted.
    assert(!state.suppress_source_draw());
    assert(state.arm());
    assert(state.phase() == Phase::Idle);
    assert(!state.busy());

    assert(state.request());
    assert(state.begin_construction(true));
    state.finish_construction(false);
    assert(state.phase() == Phase::Idle);
    assert(!state.suppress_source_draw());

    state.request();
    state.reset();
    assert(state.phase() == Phase::Idle);
    assert(!state.busy());
    assert(!state.suppress_source_draw());
}
