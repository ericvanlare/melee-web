#include "animation_clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

extern "C" {
typedef struct {
    std::uint16_t button;
    std::int8_t stickX, stickY, substickX, substickY;
    std::uint8_t triggerLeft, triggerRight, analogA, analogB;
    std::int8_t err;
} PADStatus;

int a3_test_prepare_local_capture(unsigned port, unsigned prefix_count);
unsigned a3_test_publications(void);
unsigned a3_test_capture_count(void);
std::uint32_t a3_test_cursor(void);
unsigned a3_test_terminal_kind(void);
unsigned a3_test_terminal_is_protocol(void);
std::uint32_t a3_test_terminal_tick(void);
unsigned a3_test_terminal_channel(void);
unsigned a3_test_capture_failure_is_poll_serial(void);
std::uint32_t a3_test_capture_failure_cursor(void);
std::uint32_t a3_test_capture_failure_count(void);
std::uint64_t a3_test_capture_failure_serial(void);
std::uint64_t a3_test_capture_failure_last_serial(void);
unsigned a3_test_capture_failure_port(void);
std::uint32_t a3_test_ring_write(void);
std::uint32_t a3_test_wait_callbacks(void);
std::uint32_t a3_test_pushed(void);

int melee_web_net_capture_local_input(std::uint64_t poll_serial,
                                      const PADStatus raw[4]);
const PADStatus* melee_web_net_before_step(std::uint32_t scene);
void melee_web_net_after_step(void);
void melee_web_net_reset(void);
}

namespace {
using Clock = melee_web::FixedTickClock;

bool close_to(double actual, double expected) {
    return std::fabs(actual - expected) < 1e-9;
}

/* Test-only candidate for review: cap returned work before debiting pending
 * debt. This is not wired into the runtime or the production clock API. */
class BudgetedClockCandidate {
public:
    using OverrunPolicy = Clock::OverrunPolicy;
    using StallReason = Clock::StallReason;
    using Tick = Clock::Tick;

    explicit BudgetedClockCandidate(OverrunPolicy policy = OverrunPolicy::Pause)
        : policy_(policy) {}

    void reset() noexcept {
        previous_.reset();
        pending_ = 0;
    }

    double pending_ticks() const noexcept { return pending_; }
    std::optional<double> previous_ms() const noexcept { return previous_; }

    Tick tick(double now_ms, bool running, unsigned executable_budget) noexcept {
        if (!running) {
            reset();
            return {};
        }
        if (!std::isfinite(now_ms) || (previous_ && now_ms < *previous_)) {
            const Tick failure{0, true, 0,
                !std::isfinite(now_ms) ? StallReason::NonFiniteClock : StallReason::ClockRegression,
                now_ms, previous_.value_or(0)};
            reset();
            return failure;
        }
        if (!previous_) {
            previous_ = now_ms;
            return {};
        }
        const double interval = now_ms - *previous_;
        pending_ += interval * (60.0 / 1000.0);
        previous_ = now_ms;
        const double whole = std::floor(pending_ + 1e-9);
        const double limit = policy_ == OverrunPolicy::CatchUp ? 60 : 8;
        if (whole > limit) {
            const Tick failure{0, true, 0, StallReason::Debt, whole, limit, interval};
            reset();
            return failure;
        }
        const unsigned steps = static_cast<unsigned>(
            std::min({whole, 8.0, static_cast<double>(executable_budget)}));
        pending_ = std::max(0.0, pending_ - steps);
        return {steps, false, static_cast<unsigned>(whole) - steps,
                StallReason::None, whole, limit, interval};
    }

private:
    OverrunPolicy policy_;
    std::optional<double> previous_;
    double pending_ = 0;
};

bool actual_clock_native_failure(unsigned port) {
    if (!a3_test_prepare_local_capture(port, 2)) return false;
    Clock clock;
    const auto callback_zero = clock.tick(0, true);
    const auto callback_34 = clock.tick(34, true);
    if (callback_zero.steps != 0 || callback_34.steps != 2 ||
        callback_34.stalled || a3_test_pushed() != 2) return false;

    PADStatus raw[4]{};
    const std::uint64_t serial = port == 0 ? 204 : 203;
    unsigned consumed_steps = 0;
    bool saw_refusal = false;
    for (unsigned step = 0; step < callback_34.steps; ++step) {
        if (!melee_web_net_capture_local_input(serial, raw)) {
            saw_refusal = true;
            break;
        }
        if (!melee_web_net_before_step(1)) return false;
        ++consumed_steps;
        melee_web_net_after_step();
    }

    // The second source contribution in this one callback repeats its single
    // PAD poll serial at cursor 1. The native refusal is the expected result.
    const bool reproduced = saw_refusal && consumed_steps == 1 &&
        a3_test_cursor() == 1 && a3_test_capture_count() == 1 &&
        a3_test_publications() == 1 && a3_test_capture_failure_is_poll_serial() &&
        a3_test_capture_failure_cursor() == 1 && a3_test_capture_failure_count() == 1 &&
        a3_test_capture_failure_serial() == serial &&
        a3_test_capture_failure_last_serial() == serial &&
        a3_test_capture_failure_port() == port &&
        a3_test_terminal_is_protocol() && a3_test_terminal_tick() == 1 &&
        a3_test_terminal_channel() == port && a3_test_ring_write() == 1 &&
        a3_test_wait_callbacks() == 0;
    clock.reset(); // The current caller resets its clock after native capture refusal.
    melee_web_net_reset();
    return reproduced;
}

bool baseline_parity() {
    Clock production;
    BudgetedClockCandidate candidate;
    const double times[] = {0, 17, 34, 34, 51, 68, 201, 218, 235};
    for (double now : times) {
        const auto a = production.tick(now, true);
        const auto b = candidate.tick(now, true, 8);
        if (a.steps != b.steps || a.stalled != b.stalled ||
            a.pending_steps != b.pending_steps || a.reason != b.reason ||
            !close_to(production.pending_ticks(), candidate.pending_ticks())) return false;
    }
    return true;
}

bool candidate_preserves_fractional_debt() {
    BudgetedClockCandidate clock;
    if (clock.tick(0, true, 1).steps != 0) return false;
    const auto at_34 = clock.tick(34, true, 1);
    if (at_34.steps != 1 || at_34.pending_steps != 1 ||
        !close_to(clock.pending_ticks(), 1.04) || clock.previous_ms() != 34) return false;
    const auto at_51 = clock.tick(51, true, 1);
    if (at_51.steps != 1 || at_51.pending_steps != 1 ||
        !close_to(clock.pending_ticks(), 1.06) || clock.previous_ms() != 51) return false;

    BudgetedClockCandidate zero_steps;
    if (zero_steps.tick(0, true, 1).steps != 0 ||
        zero_steps.tick(0, true, 1).steps != 0 ||
        zero_steps.tick(17, true, 1).steps != 1 ||
        !close_to(zero_steps.pending_ticks(), 0.02) ||
        zero_steps.tick(17, true, 1).steps != 0 ||
        !close_to(zero_steps.pending_ticks(), 0.02)) return false;

    BudgetedClockCandidate callbacks;
    if (callbacks.tick(0, true, 1).steps != 0) return false;
    for (double now : {17.0, 34.0, 51.0})
        if (callbacks.tick(now, true, 1).steps != 1) return false;
    return close_to(callbacks.pending_ticks(), 0.06);
}

bool candidate_composes_with_native_input() {
    for (unsigned port = 0; port < 2; ++port) {
        if (!a3_test_prepare_local_capture(port, 6)) return false;
        BudgetedClockCandidate clock;
        PADStatus raw[4]{};
        if (clock.tick(0, true, 1).steps != 0) return false;
        unsigned callback = 0;
        std::uint64_t serial = 400 + port * 10;
        for (double now : {34.0, 51.0, 68.0, 85.0}) {
            const auto tick = clock.tick(now, true, 1);
            if (tick.steps != 1) return false;
            const double expected_debt = 1.04 + callback * 0.02;
            if (!close_to(clock.pending_ticks(), expected_debt)) return false;
            if (!melee_web_net_capture_local_input(serial++, raw) ||
                a3_test_capture_count() != callback + 1 ||
                !melee_web_net_before_step(1)) return false;
            melee_web_net_after_step();
            ++callback;
        }
        if (a3_test_publications() != 4 || a3_test_capture_count() != 4 ||
            a3_test_cursor() != 4 || clock.previous_ms() != 85) return false;

        // Once four local samples have been published, ordinary agreed-input
        // tail steps need no new acquisition even when the budget drains debt.
        const auto tail = clock.tick(102, true, 8);
        if (tail.steps != 2 || !close_to(clock.pending_ticks(), 0.12)) return false;
        for (unsigned step = 0; step < tail.steps; ++step) {
            if (!melee_web_net_capture_local_input(serial, raw) ||
                !melee_web_net_before_step(1)) return false;
            melee_web_net_after_step();
        }
        if (a3_test_publications() != 4 || a3_test_capture_count() != 4 ||
            a3_test_cursor() != 6 || a3_test_ring_write() != 6 ||
            a3_test_terminal_kind() != 0 || a3_test_wait_callbacks() != 0) return false;
        melee_web_net_reset();
    }
    return true;
}

bool candidate_exception_and_remote_wait_controls() {
    BudgetedClockCandidate interrupted;
    interrupted.tick(0, true, 1);
    if (interrupted.tick(34, true, 1).steps != 1 ||
        !close_to(interrupted.pending_ticks(), 1.04)) return false;
    try {
        throw std::runtime_error("source step exception");
    } catch (const std::runtime_error&) {
        // The selected step is already debited. The candidate has no deferred
        // reservation destructor that can refund it a second time on unwind.
    }
    if (!close_to(interrupted.pending_ticks(), 1.04) ||
        interrupted.previous_ms() != 34) return false;

    if (!a3_test_prepare_local_capture(0, 1)) return false;
    BudgetedClockCandidate remote_wait;
    PADStatus raw[4]{};
    if (remote_wait.tick(0, true, 1).steps != 0 ||
        remote_wait.tick(34, true, 1).steps != 1 ||
        !melee_web_net_capture_local_input(500, raw) ||
        !melee_web_net_before_step(1)) return false;
    melee_web_net_after_step();
    if (remote_wait.tick(51, true, 1).steps != 1 ||
        !melee_web_net_capture_local_input(501, raw) ||
        melee_web_net_before_step(1) != nullptr || a3_test_cursor() != 1 ||
        a3_test_wait_callbacks() != 1) return false;
    remote_wait.reset(); // Existing native network-wait policy intentionally discards debt.
    if (remote_wait.pending_ticks() != 0 || remote_wait.previous_ms()) return false;
    if (remote_wait.tick(1000, true, 1).steps != 0 ||
        remote_wait.tick(1017, true, 1).steps != 1 ||
        !close_to(remote_wait.pending_ticks(), 0.02)) return false;
    melee_web_net_reset();
    return true;
}

bool candidate_keeps_original_pause_guard() {
    BudgetedClockCandidate clock;
    Clock production;
    if (clock.tick(0, true, 1).steps != 0) return false;
    if (production.tick(0, true).steps != 0) return false;
    const auto production_pause = production.tick(150, true);
    if (!production_pause.stalled || production_pause.reason != Clock::StallReason::Debt ||
        production_pause.triggering_value != 9 || production_pause.threshold != 8) return false;
    for (unsigned callback = 1; callback <= 7; ++callback) {
        const auto tick = clock.tick(callback * 34.0, true, 1);
        if (tick.stalled || tick.steps != 1) return false;
    }
    const auto pause = clock.tick(8 * 34.0, true, 1);
    return pause.stalled && pause.reason == Clock::StallReason::Debt &&
        pause.triggering_value == 9 && pause.threshold == 8 &&
        clock.pending_ticks() == 0 && !clock.previous_ms();
}
} // namespace

extern "C" int a3_run_clock_native_reproducer(void) {
    return actual_clock_native_failure(0) && actual_clock_native_failure(1) ? 0 : 1;
}

extern "C" int a3_run_budgeted_clock_candidate(void) {
    return baseline_parity() && candidate_preserves_fractional_debt() &&
        candidate_composes_with_native_input() &&
        candidate_exception_and_remote_wait_controls() &&
        candidate_keeps_original_pause_guard() ? 0 : 1;
}
