#pragma once

#include <cstddef>
#include <stdexcept>

namespace melee_web {

// Keeps source presentation ordered around simulation ticks.  A successful
// simulation step leaves one source draw pending; before the next step or at
// the end of a callback that draw is flushed exactly once.
class SourceFrameSequence {
public:
    template <typename Present>
    void before_step(Present&& present) {
        if (pending_) {
            flush(present);
        }
    }

    void did_step() {
        if (pending_) {
            throw std::runtime_error("source draw is pending before next step");
        }
        ++steps_;
        pending_ = true;
    }

    template <typename Present>
    void finish(Present&& present) {
        if (pending_) {
            flush(present);
        }
    }

    std::size_t steps() const noexcept { return steps_; }
    std::size_t draws() const noexcept { return draws_; }
    bool pending() const noexcept { return pending_; }

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
};

} // namespace melee_web
