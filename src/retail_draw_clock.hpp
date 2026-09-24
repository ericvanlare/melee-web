#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace melee_web {

// Replay initialization only. Periods and relative epochs are captured in a
// common emulated CPU clock. No expected draw indexes enter this model.
struct RetailDrawClock {
    uint64_t pad_period = 0, vi_period = 0;
    uint64_t next_pad = 0, first_vi_poll = 0;
    uint32_t startup_draws = 0;

    std::vector<bool> boundaries(std::size_t ticks) const {
        if (pad_period != 8100000 || vi_period != 8108100 ||
            startup_draws != 2 || next_pad >= pad_period ||
            first_vi_poll <= next_pad || first_vi_poll > vi_period || ticks < 2)
            throw std::runtime_error("Unsupported captured PAD/VI clock context");
        std::vector<bool> result(ticks, false);
        result[0] = result[1] = true;
        uint64_t alarm = next_pad + pad_period;
        uint64_t poll = first_vi_poll;
        std::size_t cursor = 2;
        while (cursor < ticks) {
            unsigned count = 0;
            while (alarm <= poll) {
                ++count;
                alarm += pad_period;
            }
            if (!count) {
                count = 1;
                alarm += pad_period;
            }
            if (count > 2)
                throw std::runtime_error("Captured PAD clock queue exceeds supported batch");
            cursor += count < ticks - cursor ? count : ticks - cursor;
            result[cursor - 1] = true;
            poll += vi_period;
        }
        return result;
    }
};
} // namespace melee_web
