#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace melee_web {
// Fixed 60 Hz clock for source simulation and inspection playback.
// Each callback executes at most eight ticks. Gameplay can retain a bounded
// backlog across callbacks; inspection playback keeps its strict pause policy.
class FixedTickClock {
public:
    enum class OverrunPolicy { Pause, CatchUp };
    explicit FixedTickClock(OverrunPolicy policy = OverrunPolicy::Pause) : policy_(policy) {}
    struct Tick { unsigned steps = 0; bool stalled = false; unsigned pending_steps = 0; };
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
        // A full second of debt indicates suspension or sustained overload. Keep
        // that explicit rather than trap the browser in endless catch-up.
        const double limit = policy_ == OverrunPolicy::CatchUp ? 60 : 8;
        if (whole > limit) { reset(); return {0, true}; }
        const unsigned steps = static_cast<unsigned>(std::min(whole, 8.0));
        pending_ = std::max(0.0, pending_ - steps);
        return {steps, false, static_cast<unsigned>(whole) - steps};
    }
private:
    OverrunPolicy policy_;
    std::optional<double> previous_;
    double pending_ = 0;
};
using AnimationClock = FixedTickClock;
} // namespace melee_web
