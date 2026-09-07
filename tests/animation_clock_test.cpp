#include "animation_clock.hpp"
#include <cassert>
#include <limits>

int main() {
    using melee_web::AnimationClock;
    for (unsigned hz : {30, 60, 120, 144}) {
        AnimationClock clock;
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
}
