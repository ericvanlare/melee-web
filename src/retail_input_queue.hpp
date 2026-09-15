#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace melee_web {
struct RetailQueueBatch {
    uint64_t poll_cpu_ticks;
    uint8_t available;
};

// Platform input fixture: a nonempty queue snapshot precedes consumption of its
// samples. This conditions gameplay replay on the recorded schedule; it does
// not predict the original CPU's scheduling or wait on the host wall clock.
inline std::vector<bool> retail_queue_boundaries(
    std::span<const RetailQueueBatch> batches, std::size_t frames) {
    if (!frames || frames > 36000 || batches.empty() || batches.size() > frames)
        throw std::runtime_error("Invalid recorded input-queue size");
    std::vector<bool> result(frames, false);
    std::size_t cursor = 0;
    uint64_t last = 0;
    for (std::size_t i = 0; i < batches.size(); ++i) {
        const auto& batch = batches[i];
        if ((!i && batch.poll_cpu_ticks != 0) ||
            (i && batch.poll_cpu_ticks <= last) || !batch.available ||
            batch.available > 5 || batch.available > frames - cursor)
            throw std::runtime_error("Invalid recorded input-queue event");
        last = batch.poll_cpu_ticks;
        cursor += batch.available;
        result[cursor - 1] = true;
    }
    if (cursor != frames)
        throw std::runtime_error("Recorded input queue does not cover all samples");
    return result;
}
} // namespace melee_web
