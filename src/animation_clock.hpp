#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace melee_web {
// Fixed 60 Hz clock for source simulation and inspection playback.
// Callbacks execute at most eight ticks by default. A scoped caller budget can
// lower that count while gameplay retains the bounded backlog; inspection
// playback keeps its strict pause policy.
class FixedTickClock {
public:
    enum class OverrunPolicy { Pause, CatchUp };
    explicit FixedTickClock(OverrunPolicy policy = OverrunPolicy::Pause) : policy_(policy) {}
    enum class StallReason { None, Debt, NonFiniteClock, ClockRegression };
    struct Tick {
        unsigned steps = 0;
        bool stalled = false;
        unsigned pending_steps = 0;
        StallReason reason = StallReason::None;
        double triggering_value = 0;
        double threshold = 0;
        double interval_ms = 0;
    };
    void reset() noexcept { previous_.reset(); pending_ = 0; }
    double pending_ticks() const noexcept { return pending_; }
    Tick tick(double now_ms, bool running) noexcept {
        return tick_with_budget(now_ms, running, 8);
    }
    // The read-only receipt is delivered before reset destroys the clock debt.
    // Observers must not throw or alter source scheduling.
    template<class ObserveStall>
    Tick tick(double now_ms, bool running, ObserveStall observe_stall) noexcept {
        return tick_with_budget(now_ms, running, 8, observe_stall);
    }
    Tick tick_with_budget(double now_ms, bool running, unsigned max_steps) noexcept {
        return tick_with_budget(now_ms, running, max_steps,
                               [](const Tick&) noexcept {});
    }
    // Keep full-debt guards and stall receipts identical to tick(); the budget
    // limits only how many due steps this callback returns to its caller.
    template<class ObserveStall>
    Tick tick_with_budget(double now_ms, bool running, unsigned max_steps,
                          ObserveStall observe_stall) noexcept {
        if (!running) { reset(); return {}; }
        if (!std::isfinite(now_ms) || (previous_ && now_ms < *previous_)) {
            const Tick failure{0, true, 0,
                !std::isfinite(now_ms) ? StallReason::NonFiniteClock : StallReason::ClockRegression,
                now_ms, previous_.value_or(0)};
            observe_stall(failure);
            reset(); return failure;
        }
        if (!previous_) { previous_ = now_ms; return {}; }
        const double interval = now_ms - *previous_;
        pending_ += interval * (60.0 / 1000.0);
        previous_ = now_ms;
        const double whole = std::floor(pending_ + 1e-9);
        // A full second of debt indicates suspension or sustained overload. Keep
        // that explicit rather than trap the browser in endless catch-up.
        const double limit = policy_ == OverrunPolicy::CatchUp ? 60 : 8;
        if (whole > limit) {
            const Tick failure{0, true, 0, StallReason::Debt, whole, limit, interval};
            observe_stall(failure);
            reset(); return failure;
        }
        const unsigned steps = static_cast<unsigned>(
            std::min({whole, 8.0, static_cast<double>(max_steps)}));
        pending_ = std::max(0.0, pending_ - steps);
        return {steps, false, static_cast<unsigned>(whole) - steps,
                StallReason::None, whole, limit, interval};
    }
private:
    OverrunPolicy policy_;
    std::optional<double> previous_;
    double pending_ = 0;
};
using AnimationClock = FixedTickClock;
} // namespace melee_web
