#pragma once

#include <array>
#include <cstddef>

namespace melee_web {

struct ResultsSourcePadEvent {
    unsigned source_frame = 0;
    unsigned port = 0;
    unsigned buttons = 0;
    unsigned duration = 0;
};

class ResultsSourcePadSchedule {
public:
    static constexpr std::size_t capacity = 8;
    enum class Boundary { waiting, due, missed };

    bool enqueue(ResultsSourcePadEvent event) noexcept
    {
        if (cursor_ != 0 || count_ == events_.size() ||
            event.source_frame > 8191 || event.port != 0 ||
            event.buttons != 0x1000 || event.duration == 0 ||
            event.duration > 120 ||
            (count_ && event.source_frame <= events_[count_ - 1].source_frame))
            return false;
        events_[count_++] = event;
        return true;
    }

    Boundary before_tick(unsigned source_frame, ResultsSourcePadEvent& event) noexcept
    {
        if (cursor_ == count_) return Boundary::waiting;
        const auto& next = events_[cursor_];
        if (source_frame < next.source_frame) return Boundary::waiting;
        if (source_frame > next.source_frame) return Boundary::missed;
        event = next;
        ++cursor_;
        return Boundary::due;
    }

    void clear() noexcept
    {
        events_ = {};
        count_ = 0;
        cursor_ = 0;
    }

    bool all_consumed() const noexcept { return cursor_ == count_; }
    bool started() const noexcept { return cursor_ != 0; }
    bool full() const noexcept { return count_ == events_.size(); }
    std::size_t size() const noexcept { return count_; }

private:
    std::array<ResultsSourcePadEvent, capacity> events_{};
    std::size_t count_ = 0;
    std::size_t cursor_ = 0;
};

// Development-only host control used to stop immediately before a declared
// Results source sample. It never supplies PAD or mutates Results state; the
// browser can deliver a trusted keyboard edge while source stepping is held.
class ResultsSourceFramePauseSchedule {
public:
    static constexpr std::size_t capacity = 8;
    enum class Boundary { waiting, due, missed };

    bool enqueue(unsigned source_frame) noexcept
    {
        if (cursor_ != 0 || count_ == frames_.size() || source_frame > 8191 ||
            (count_ && source_frame <= frames_[count_ - 1]))
            return false;
        frames_[count_++] = source_frame;
        return true;
    }

    Boundary before_tick(unsigned source_frame) noexcept
    {
        if (cursor_ == count_) return Boundary::waiting;
        const unsigned target = frames_[cursor_];
        if (source_frame < target) return Boundary::waiting;
        if (source_frame > target) return Boundary::missed;
        ++cursor_;
        return Boundary::due;
    }

    void clear() noexcept
    {
        frames_ = {};
        count_ = 0;
        cursor_ = 0;
    }

    bool all_consumed() const noexcept { return cursor_ == count_; }
    bool started() const noexcept { return cursor_ != 0; }
    bool full() const noexcept { return count_ == frames_.size(); }
    std::size_t size() const noexcept { return count_; }

private:
    std::array<unsigned, capacity> frames_{};
    std::size_t count_ = 0;
    std::size_t cursor_ = 0;
};

} // namespace melee_web
