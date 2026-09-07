#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace melee_web {
// Inspection playback only. Simulation scheduling remains a separate game
// integration task. A long stall pauses playback explicitly instead of dropping
// animation frames or doing unbounded catch-up work on the browser thread.
class AnimationClock {
public:
    struct Tick { unsigned steps = 0; bool stalled = false; };
    void reset() noexcept { previous_.reset(); pending_ = 0; }
    Tick tick(double now_ms, bool running) noexcept {
        if (!running) { reset(); return {}; }
        if (!std::isfinite(now_ms) || (previous_ && now_ms < *previous_)) {
            reset(); return {0, true};
        }
        if (!previous_) { previous_ = now_ms; return {}; }
        pending_ += (now_ms - *previous_) * (60.0 / 1000.0);
        previous_ = now_ms;
        const double whole = std::floor(pending_ + 1e-9);
        if (whole > 8) { reset(); return {0, true}; }
        pending_ = std::max(0.0, pending_ - whole);
        return {static_cast<unsigned>(whole), false};
    }
private:
    std::optional<double> previous_;
    double pending_ = 0;
};
} // namespace melee_web
