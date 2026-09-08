#include "animation_clock.hpp"
#include <cassert>
#include <limits>

int main() {
    using melee_web::AnimationClock;
    for (auto policy : {AnimationClock::OverrunPolicy::Pause, AnimationClock::OverrunPolicy::CatchUp})
    for (unsigned hz : {30, 60, 120, 144}) {
        AnimationClock clock(policy);
        unsigned total = 0;
        for (unsigned i = 0; i <= hz * 10; ++i) {
            const auto tick = clock.tick(i * 1000.0 / hz, true);
            assert(!tick.stalled);
            total += tick.steps;
        }
        assert(total == 600);
    }
    AnimationClock clock;
    assert(clock.tick(0, true).steps == 0);
    assert(clock.tick(50, true).steps == 3);
    assert(clock.tick(60, false).steps == 0);
    assert(clock.tick(10000, true).steps == 0);
    assert(clock.tick(10050, true).steps == 3);
    const auto stalled = clock.tick(11000, true);
    assert(stalled.stalled && stalled.steps == 0);
    assert(clock.tick(11000, true).steps == 0);
    assert(clock.tick(10999, true).stalled);
    assert(clock.tick(std::numeric_limits<double>::infinity(), true).stalled);
    // A 200 ms presentation stall must retain all 12 source ticks, while no
    // individual callback can monopolize the browser with more than eight.
    AnimationClock recovering(AnimationClock::OverrunPolicy::CatchUp);
    recovering.tick(0, true);
    auto delayed = recovering.tick(200, true);
    assert(!delayed.stalled && delayed.steps == 8 && delayed.pending_steps == 4);
    auto drained = recovering.tick(200, true);
    assert(!drained.stalled && drained.steps == 4 && drained.pending_steps == 0);
    unsigned recovered_total = delayed.steps + drained.steps;
    for (unsigned i = 13; i <= 600; ++i) {
        auto tick = recovering.tick(i * 1000.0 / 60, true);
        assert(!tick.stalled && tick.steps <= 8);
        recovered_total += tick.steps;
    }
    assert(recovered_total == 600);
    // Real callbacks consume time too; preserve fractional debt while draining.
    recovering.reset(); recovering.tick(0, true);
    unsigned total = 0;
    for (double now : {155.0, 172.0, 189.0, 1000.0}) {
        auto tick = recovering.tick(now, true);
        assert(!tick.stalled && tick.steps <= 8); total += tick.steps;
    }
    for (unsigned i = 0; i < 8; ++i) {
        auto tick = recovering.tick(1000, true);
        assert(!tick.stalled && tick.steps <= 8); total += tick.steps;
    }
    assert(total == 60);
    recovering.reset(); recovering.tick(0, true);
    assert(recovering.tick(2000, true).stalled);
    recovering.tick(2000, true); recovering.tick(2150, true);
    recovering.tick(2151, false); // Focus/visibility pause clears old input debt.
    assert(recovering.tick(9000, true).steps == 0);
    assert(recovering.tick(9017, true).steps == 1);

}
