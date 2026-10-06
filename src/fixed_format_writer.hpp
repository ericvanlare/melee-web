#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdio>

namespace melee_web {

// Formats printf fragments into one caller-owned fixed buffer.
//
// A sequence of add() calls writes the same bytes and reports the same
// result() as one std::snprintf() over the same buffer whose format is the
// concatenation of the fragments and whose arguments are the fragments'
// arguments in order. That includes truncation at the end of the buffer and
// the full untruncated length in result(). Observers use one call per JSON
// field so a new field is a single-line insertion.
class FixedFormatWriter {
public:
    FixedFormatWriter(char* buffer, size_t size) noexcept : buffer_(buffer), size_(size) {}
    FixedFormatWriter(const FixedFormatWriter&) = delete;
    FixedFormatWriter& operator=(const FixedFormatWriter&) = delete;

    [[gnu::format(printf, 2, 3)]] void add(const char* format, ...) noexcept {
        if (length_ < 0) return;
        const size_t used = static_cast<size_t>(length_);
        va_list arguments;
        va_start(arguments, format);
        // Once the buffer is full, keep counting like snprintf without writing.
        const int written = used < size_
            ? std::vsnprintf(buffer_ + used, size_ - used, format, arguments)
            : std::vsnprintf(nullptr, 0, format, arguments);
        va_end(arguments);
        length_ = written < 0 ? -1 : length_ + written;
    }

    // The value std::snprintf would return for the whole concatenated format.
    int result() const noexcept { return length_; }

private:
    char* buffer_;
    size_t size_;
    int length_ = 0;
};

}  // namespace melee_web
