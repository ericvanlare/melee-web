#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"

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

int melee_web_net_local_capture_pending(void);
int melee_web_net_capture_local_input(std::uint64_t poll_serial,
                                      const PADStatus raw[4]);
const PADStatus* melee_web_net_before_step(std::uint32_t scene);
void melee_web_net_after_step(void);
void melee_web_net_reset(void);
}

namespace {
using Clock = melee_web::FixedTickClock;
using Tick = Clock::Tick;
using Reason = Clock::StallReason;
using SourceFrames = melee_web::SourceFrameSequence;

bool same_number(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

bool same_tick(const Tick& a, const Tick& b) {
    return a.steps == b.steps && a.stalled == b.stalled &&
        a.pending_steps == b.pending_steps && a.reason == b.reason &&
        same_number(a.triggering_value, b.triggering_value) &&
        same_number(a.threshold, b.threshold) &&
        same_number(a.interval_ms, b.interval_ms);
}

bool close_to(double actual, double expected) {
    return std::fabs(actual - expected) < 1e-9;
}

unsigned selected_budget() {
    return melee_web_net_local_capture_pending() ? 1u : 8u;
}

bool default_path_parity(Clock::OverrunPolicy policy) {
    Clock legacy_path(policy);
    Clock budgeted_path(policy);
    const double times[] = {0, 17, 34, 34, 51, 68, 150, 167, 184};
    for (double now : times) {
        const auto legacy = legacy_path.tick(now, true);
        const auto budgeted = budgeted_path.tick_with_budget(now, true, 8);
        if (!same_tick(legacy, budgeted) ||
            !same_number(legacy_path.pending_ticks(), budgeted_path.pending_ticks())) return false;
    }

    Clock observed_path(policy);
    Clock observed_budgeted_path(policy);
    unsigned observer_calls = 0, budgeted_observer_calls = 0;
    double pending_before_reset = -1, budgeted_pending_before_reset = -1;
    auto observe = [&](const Tick&) noexcept {
        ++observer_calls;
        pending_before_reset = observed_path.pending_ticks();
    };
    auto observe_budgeted = [&](const Tick&) noexcept {
        ++budgeted_observer_calls;
        budgeted_pending_before_reset = observed_budgeted_path.pending_ticks();
    };
    observed_path.tick(0, true);
    observed_budgeted_path.tick(0, true);
    const auto legacy_stall = observed_path.tick(150, true, observe);
    const auto budgeted_stall = observed_budgeted_path.tick_with_budget(
        150, true, 8, observe_budgeted);
    if (!same_tick(legacy_stall, budgeted_stall) ||
        observer_calls != budgeted_observer_calls ||
        pending_before_reset != budgeted_pending_before_reset ||
        !same_number(observed_path.pending_ticks(), observed_budgeted_path.pending_ticks()))
        return false;
    if (legacy_stall.stalled &&
        (!close_to(pending_before_reset, 9) ||
         !same_number(observed_path.pending_ticks(), 0))) return false;
    return true;
}

bool budget_api_controls() {
    if (!default_path_parity(Clock::OverrunPolicy::Pause) ||
        !default_path_parity(Clock::OverrunPolicy::CatchUp)) return false;

    Clock zero_budget;
    const auto zero_start = zero_budget.tick_with_budget(0, true, 0);
    const auto zero_due = zero_budget.tick_with_budget(34, true, 0);
    if (zero_start.steps != 0 || zero_due.steps != 0 || zero_due.stalled ||
        zero_due.pending_steps != 2 || zero_due.reason != Reason::None ||
        zero_due.triggering_value != 2 || zero_due.threshold != 8 ||
        zero_due.interval_ms != 34 || !close_to(zero_budget.pending_ticks(), 2.04)) return false;
    const auto same_time = zero_budget.tick_with_budget(34, true, 1);
    if (same_time.steps != 1 || same_time.stalled || same_time.pending_steps != 1 ||
        same_time.reason != Reason::None || same_time.triggering_value != 2 ||
        same_time.threshold != 8 || same_time.interval_ms != 0 ||
        !close_to(zero_budget.pending_ticks(), 1.04)) return false;

    Clock one_budget;
    one_budget.tick(0, true);
    const auto one_due = one_budget.tick_with_budget(34, true, 1);
    if (one_due.steps != 1 || one_due.pending_steps != 1 ||
        !close_to(one_budget.pending_ticks(), 1.04)) return false;
    const auto next_due = one_budget.tick_with_budget(51, true, 1);
    if (next_due.steps != 1 || next_due.pending_steps != 1 ||
        !close_to(one_budget.pending_ticks(), 1.06)) return false;

    Clock clamped_budget;
    clamped_budget.tick(0, true);
    const auto above_eight = clamped_budget.tick_with_budget(
        34, true, std::numeric_limits<unsigned>::max());
    if (above_eight.steps != 2 || above_eight.pending_steps != 0 ||
        !close_to(clamped_budget.pending_ticks(), 0.04)) return false;

    Clock pause_before_budget;
    pause_before_budget.tick(0, true);
    double observer_pending = -1;
    unsigned observer_calls = 0;
    const auto pause = pause_before_budget.tick_with_budget(150, true, 0,
        [&](const Tick& event) noexcept {
            ++observer_calls;
            observer_pending = pause_before_budget.pending_ticks();
            if (event.reason != Reason::Debt || event.triggering_value != 9 ||
                event.threshold != 8 || event.interval_ms != 150) observer_pending = -2;
        });
    if (!pause.stalled || pause.steps != 0 || pause.pending_steps != 0 ||
        pause.reason != Reason::Debt || pause.triggering_value != 9 ||
        pause.threshold != 8 || pause.interval_ms != 150 || observer_calls != 1 ||
        observer_pending != 9 || pause_before_budget.pending_ticks() != 0 ||
        pause_before_budget.tick_with_budget(150, true, 0).steps != 0) return false;

    Clock catch_up(Clock::OverrunPolicy::CatchUp);
    catch_up.tick(0, true);
    const auto catch_up_one = catch_up.tick_with_budget(1000, true, 1);
    if (catch_up_one.stalled || catch_up_one.steps != 1 ||
        catch_up_one.pending_steps != 59 || catch_up_one.reason != Reason::None ||
        catch_up_one.triggering_value != 60 || catch_up_one.threshold != 60 ||
        catch_up_one.interval_ms != 1000 || !close_to(catch_up.pending_ticks(), 59)) return false;
    const auto catch_up_zero = catch_up.tick_with_budget(1000, true, 0);
    if (catch_up_zero.stalled || catch_up_zero.steps != 0 ||
        catch_up_zero.pending_steps != 59 || !close_to(catch_up.pending_ticks(), 59)) return false;

    Clock catch_up_guard(Clock::OverrunPolicy::CatchUp);
    catch_up_guard.tick(0, true);
    double catch_up_observed_pending = -1;
    unsigned catch_up_observer_calls = 0;
    const auto catch_up_pause = catch_up_guard.tick_with_budget(1017, true, 0,
        [&](const Tick& event) noexcept {
            ++catch_up_observer_calls;
            catch_up_observed_pending = catch_up_guard.pending_ticks();
            if (event.reason != Reason::Debt || event.triggering_value != 61 ||
                event.threshold != 60 || event.interval_ms != 1017)
                catch_up_observed_pending = -2;
        });
    if (!catch_up_pause.stalled || catch_up_pause.reason != Reason::Debt ||
        catch_up_pause.steps != 0 || catch_up_pause.pending_steps != 0 ||
        catch_up_pause.triggering_value != 61 || catch_up_pause.threshold != 60 ||
        catch_up_pause.interval_ms != 1017 || catch_up_observer_calls != 1 ||
        !close_to(catch_up_observed_pending, 61.02) || catch_up_guard.pending_ticks() != 0) return false;

    Clock regression;
    regression.tick(0, true);
    regression.tick_with_budget(34, true, 1);
    double regression_pending = -1;
    unsigned regression_observers = 0;
    const auto backwards = regression.tick_with_budget(33, true, 1,
        [&](const Tick& event) noexcept {
            ++regression_observers;
            regression_pending = regression.pending_ticks();
            if (event.reason != Reason::ClockRegression || event.triggering_value != 33 ||
                event.threshold != 34 || event.interval_ms != 0) regression_pending = -2;
        });
    if (!backwards.stalled || backwards.reason != Reason::ClockRegression ||
        backwards.steps != 0 || backwards.pending_steps != 0 ||
        backwards.triggering_value != 33 || backwards.threshold != 34 ||
        backwards.interval_ms != 0 || regression_observers != 1 ||
        !close_to(regression_pending, 1.04) || regression.pending_ticks() != 0) return false;

    Clock nonfinite;
    nonfinite.tick(0, true);
    double nonfinite_pending = -1;
    unsigned nonfinite_observers = 0;
    const auto invalid = nonfinite.tick_with_budget(
        std::numeric_limits<double>::infinity(), true, 1,
        [&](const Tick& event) noexcept {
            ++nonfinite_observers;
            nonfinite_pending = nonfinite.pending_ticks();
            if (event.reason != Reason::NonFiniteClock || !std::isinf(event.triggering_value) ||
                event.threshold != 0 || event.interval_ms != 0) nonfinite_pending = -2;
        });
    if (!invalid.stalled || invalid.reason != Reason::NonFiniteClock ||
        invalid.steps != 0 || invalid.pending_steps != 0 ||
        !std::isinf(invalid.triggering_value) || invalid.threshold != 0 ||
        invalid.interval_ms != 0 || nonfinite_observers != 1 ||
        nonfinite_pending != 0 || nonfinite.pending_ticks() != 0) return false;

    Clock stopped;
    stopped.tick(0, true);
    stopped.tick_with_budget(34, true, 1);
    unsigned stopped_observers = 0;
    const auto stop = stopped.tick_with_budget(35, false, 1,
        [&](const Tick&) noexcept { ++stopped_observers; });
    if (!same_tick(stop, Tick{}) || stopped_observers || stopped.pending_ticks() != 0 ||
        stopped.tick_with_budget(10000, true, 1).steps != 0) return false;
    return true;
}

bool actual_clock_native_failure(unsigned port) {
    if (!a3_test_prepare_local_capture(port, 2) ||
        !melee_web_net_local_capture_pending() || selected_budget() != 1) return false;
    Clock clock;
    const auto callback_zero = clock.tick(0, true);
    const auto callback_34 = clock.tick(34, true);
    if (callback_zero.steps != 0 || callback_34.steps != 2 ||
        callback_34.stalled || a3_test_pushed() != 2) return false;

    SourceFrames source_frames;
    source_frames.begin_callback();
    unsigned draw_calls = 0;
    const auto present = [&]() { ++draw_calls; return true; };
    PADStatus raw[4]{};
    const std::uint64_t serial = port == 0 ? 204 : 203;
    unsigned consumed_steps = 0;
    bool saw_refusal = false;
    for (unsigned step = 0; step < callback_34.steps; ++step) {
        source_frames.before_step(present);
        if (!melee_web_net_capture_local_input(serial, raw)) {
            saw_refusal = true;
            break;
        }
        if (!melee_web_net_before_step(1)) return false;
        ++consumed_steps;
        melee_web_net_after_step();
        source_frames.did_step();
    }
    source_frames.finish(present);

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
        a3_test_wait_callbacks() == 0 && source_frames.steps() == 1 &&
        source_frames.draws() == 1 && draw_calls == 1;
    clock.reset();
    melee_web_net_reset();
    return reproduced;
}

bool budgeted_native_sampling_and_draws() {
    for (unsigned port = 0; port < 2; ++port) {
        if (!a3_test_prepare_local_capture(port, 6) || selected_budget() != 1) return false;
        Clock clock;
        PADStatus raw[4]{};
        if (clock.tick_with_budget(0, true, selected_budget()).steps != 0) return false;
        unsigned callback = 0, total_steps = 0, total_draws = 0;
        std::uint64_t serial = 400 + port * 10;
        for (double now : {34.0, 51.0, 68.0, 85.0}) {
            const unsigned budget = selected_budget();
            const auto tick = clock.tick_with_budget(now, true, budget);
            if (budget != 1 || tick.steps != 1 || tick.pending_steps != 1) return false;
            const double expected_debt = 1.04 + callback * 0.02;
            if (!close_to(clock.pending_ticks(), expected_debt)) return false;

            SourceFrames source_frames;
            source_frames.begin_callback();
            unsigned draw_calls = 0;
            const auto present = [&]() { ++draw_calls; return true; };
            for (unsigned step = 0; step < tick.steps; ++step) {
                source_frames.before_step(present);
                if (!melee_web_net_local_capture_pending() || selected_budget() != 1 ||
                    !melee_web_net_capture_local_input(serial++, raw) ||
                    melee_web_net_local_capture_pending() ||
                    !melee_web_net_before_step(1)) return false;
                melee_web_net_after_step();
                source_frames.did_step();
            }
            source_frames.finish(present);
            if (source_frames.steps() != 1 || source_frames.draws() != 1 || draw_calls != 1)
                return false;
            ++callback;
            ++total_steps;
            ++total_draws;
            if (callback < 4 &&
                (!melee_web_net_local_capture_pending() || selected_budget() != 1)) return false;
            if (callback == 4 &&
                (melee_web_net_local_capture_pending() || selected_budget() != 8)) return false;
        }
        if (callback != 4 || a3_test_publications() != 4 ||
            a3_test_capture_count() != 4 || a3_test_cursor() != 4 ||
            clock.pending_ticks() < 1 || selected_budget() != 8) return false;

        // After four captures, the actual query returns to the unchanged
        // default budget and the two agreed tail ticks require no new samples.
        const unsigned tail_budget = selected_budget();
        const auto tail = clock.tick_with_budget(102, true, tail_budget);
        if (tail_budget != 8 || tail.steps != 2 || tail.pending_steps != 0 ||
            !close_to(clock.pending_ticks(), 0.12)) return false;
        SourceFrames tail_frames;
        tail_frames.begin_callback();
        unsigned tail_draw_calls = 0;
        const auto tail_present = [&]() { ++tail_draw_calls; return true; };
        for (unsigned step = 0; step < tail.steps; ++step) {
            tail_frames.before_step(tail_present);
            if (melee_web_net_local_capture_pending() || selected_budget() != 8 ||
                !melee_web_net_capture_local_input(serial, raw) ||
                !melee_web_net_before_step(1)) return false;
            melee_web_net_after_step();
            tail_frames.did_step();
            ++total_steps;
        }
        tail_frames.finish(tail_present);
        total_draws += static_cast<unsigned>(tail_frames.draws());
        if (tail_frames.steps() != 2 || tail_frames.draws() != 2 || tail_draw_calls != 2 ||
            total_steps != 6 || total_draws != 6 || a3_test_publications() != 4 ||
            a3_test_capture_count() != 4 || a3_test_cursor() != 6 ||
            a3_test_ring_write() != 6 || a3_test_terminal_kind() != 0 ||
            a3_test_wait_callbacks() != 0 || selected_budget() != 8) return false;
        melee_web_net_reset();
        if (melee_web_net_local_capture_pending() || selected_budget() != 8) return false;
    }
    return true;
}

bool remote_wait_and_exception_resets() {
    if (!a3_test_prepare_local_capture(0, 1) || selected_budget() != 1) return false;
    Clock clock;
    PADStatus raw[4]{};
    if (clock.tick_with_budget(0, true, selected_budget()).steps != 0) return false;

    SourceFrames first_callback;
    first_callback.begin_callback();
    unsigned first_draws = 0;
    const auto first_present = [&]() { ++first_draws; return true; };
    const auto first_tick = clock.tick_with_budget(34, true, selected_budget());
    if (first_tick.steps != 1) return false;
    first_callback.before_step(first_present);
    if (!melee_web_net_capture_local_input(500, raw) ||
        !melee_web_net_before_step(1)) return false;
    melee_web_net_after_step();
    first_callback.did_step();
    first_callback.finish(first_present);
    if (first_callback.steps() != 1 || first_callback.draws() != 1 || first_draws != 1)
        return false;

    SourceFrames waiting_callback;
    waiting_callback.begin_callback();
    unsigned waiting_draws = 0;
    const auto waiting_present = [&]() { ++waiting_draws; return true; };
    const auto second_tick = clock.tick_with_budget(51, true, selected_budget());
    if (second_tick.steps != 1) return false;
    waiting_callback.before_step(waiting_present);
    if (!melee_web_net_local_capture_pending() ||
        !melee_web_net_capture_local_input(501, raw) ||
        melee_web_net_local_capture_pending()) return false;
    if (melee_web_net_before_step(1) != nullptr || a3_test_cursor() != 1 ||
        a3_test_wait_callbacks() != 1) return false;
    waiting_callback.finish(waiting_present);
    if (waiting_callback.steps() != 0 || waiting_callback.draws() != 0 || waiting_draws != 0)
        return false;
    raw[0].button = 0x100;
    if (!melee_web_net_capture_local_input(502, raw) || a3_test_publications() != 2 ||
        a3_test_capture_count() != 2 || melee_web_net_local_capture_pending()) return false;
    clock.reset(); // Existing network-wait reset intentionally discards this debt.
    if (clock.pending_ticks() != 0 || clock.tick_with_budget(1000, true, 8).steps != 0 ||
        clock.tick_with_budget(1017, true, 8).steps != 1) return false;
    melee_web_net_reset();

    Clock interrupted;
    interrupted.tick_with_budget(0, true, 1);
    const auto due = interrupted.tick_with_budget(34, true, 1);
    if (due.steps != 1 || !close_to(interrupted.pending_ticks(), 1.04)) return false;
    try {
        throw std::runtime_error("source step exception");
    } catch (const std::runtime_error&) {
        // The real caller faults and resets; no refund is applied while unwinding.
        interrupted.reset();
    }
    return interrupted.pending_ticks() == 0 &&
        interrupted.tick_with_budget(1000, true, 1).steps == 0;
}
} // namespace

extern "C" int a3_run_clock_native_reproducer(void) {
    return actual_clock_native_failure(0) && actual_clock_native_failure(1) ? 0 : 1;
}

extern "C" int a3_run_budgeted_clock_tests(void) {
    return budget_api_controls() && budgeted_native_sampling_and_draws() &&
        remote_wait_and_exception_resets() ? 0 : 1;
}
