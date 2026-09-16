#pragma once

#include <cstddef>
#include <stdexcept>

namespace melee_web {

// Keeps source presentation ordered around simulation ticks.  A successful
// simulation step leaves one source draw pending. The default closes every
// tick; a captured-clock replay can keep a batch open across callbacks and
// flush exactly once when the batch closes.
class SourceFrameSequence {
public:
    template <typename Present>
    void before_step(Present&& present) {
        if (pending_ && batch_closed_) {
            flush(present);
        }
    }

    void did_step(bool closes_batch = true) {
        if (pending_ && batch_closed_) {
            throw std::runtime_error("source draw is pending before next step");
        }
        ++steps_;
        pending_ = true;
        batch_closed_ = closes_batch;
    }

    template <typename Present>
    void finish(Present&& present) {
        if (pending_ && batch_closed_) {
            flush(present);
        }
    }

    std::size_t steps() const noexcept { return steps_; }
    std::size_t draws() const noexcept { return draws_; }
    bool pending() const noexcept { return pending_; }
    void begin_callback() noexcept { steps_ = draws_ = 0; }

private:
    template <typename Present>
    void flush(Present& present) {
        if (!static_cast<bool>(present())) {
            throw std::runtime_error("source draw failed");
        }
        ++draws_;
        pending_ = false;
    }

    std::size_t steps_ = 0;
    std::size_t draws_ = 0;
    bool pending_ = false;
    bool batch_closed_ = true;
};

} // namespace melee_web
