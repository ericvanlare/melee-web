#include "source_frame_sequence.hpp"

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct SourceScene {
    unsigned next_step = 1;
    unsigned displayed_step = 1;
    std::vector<std::string> events;

    bool present() {
        events.emplace_back("draw");
        displayed_step = next_step;
        return true;
    }

    void step() {
        // The source draw updates the cull/magnifier input consumed by the
        // next simulation tick.  A batched callback must preserve this order.
        assert(displayed_step == next_step);
        events.emplace_back("step");
        ++next_step;
    }
};

std::vector<std::string> run_schedule(const std::vector<unsigned>& schedule) {
    melee_web::SourceFrameSequence sequence;
    SourceScene scene;
    for (const unsigned ticks : schedule) {
        for (unsigned tick = 0; tick < ticks; ++tick) {
            sequence.before_step([&scene] { return scene.present(); });
            scene.step();
            sequence.did_step();
        }
        sequence.finish([&scene] { return scene.present(); });
    }
    assert(sequence.steps() == 6);
    assert(sequence.draws() == 6);
    assert(!sequence.pending());
    return scene.events;
}

} // namespace

int main() {
    const std::vector<std::string> expected = {
        "step", "draw", "step", "draw", "step", "draw",
        "step", "draw", "step", "draw", "step", "draw",
    };
    for (const std::vector<unsigned>& schedule : {
             std::vector<unsigned>{6},
             std::vector<unsigned>{0, 6},
             std::vector<unsigned>{1, 5},
             std::vector<unsigned>{2, 4},
             std::vector<unsigned>{3, 3},
             std::vector<unsigned>{0, 1, 2, 3},
         }) {
        // Zero, one, two, and three ticks per callback all produce the same
        // step/draw order and exactly one source draw per consumed tick.
        assert(run_schedule(schedule) == expected);
    }

    // finish() is idempotent once the final pending draw has succeeded.
    melee_web::SourceFrameSequence sequence;
    sequence.did_step();
    unsigned presents = 0;
    const auto present = [&presents] {
        ++presents;
        return true;
    };
    sequence.finish(present);
    sequence.finish(present);
    assert(sequence.steps() == 1);
    assert(sequence.draws() == 1);
    assert(presents == 1);

    // A failed draw leaves the step pending and prevents simulation advance.
    melee_web::SourceFrameSequence failed;
    failed.did_step();
    bool throws = false;
    try {
        failed.finish([] { return false; });
    } catch (const std::runtime_error&) {
        throws = true;
    }
    assert(throws);
    assert(failed.steps() == 1);
    assert(failed.draws() == 0);
    assert(failed.pending());
    throws = false;
    try {
        failed.did_step();
    } catch (const std::runtime_error&) {
        throws = true;
    }
    assert(throws);
    assert(failed.steps() == 1);

    // A retry can flush the final draw after an early exit.
    failed.finish([] { return true; });
    assert(failed.draws() == 1);
    assert(!failed.pending());
    failed.finish([] { return false; });
    assert(failed.draws() == 1);
}
