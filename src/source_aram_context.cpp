#include "source_aram_context.hpp"

#include <limits>

namespace melee_web::source_aram {
namespace {
constexpr std::uint32_t kAlignment = 32;
}

Status Context::initialize(Layout layout)
{
    if (initialized_) return Status::already_initialized;
    if (!layout.initial_base.value() || layout.initial_base.value() % kAlignment ||
        layout.hardware_size < layout.initial_base.value())
        return Status::invalid_layout;

    layout_ = layout;
    hardware_size_ = layout.hardware_size;
    stack_pointer_ = layout.initial_base;
    lengths_.clear();
    initialized_ = true;
    return Status::ok;
}

void Context::clear()
{
    layout_ = {};
    stack_pointer_ = {};
    lengths_.clear();
    initialized_ = false;
    hardware_size_ = 0;
}

Result Context::allocate(std::uint64_t length)
{
    if (!initialized_) return {Status::missing_context, {}, 0};
    if (length > std::numeric_limits<std::uint32_t>::max() ||
        (length & (kAlignment - 1)) != 0)
        return {Status::invalid_request, {}, 0};
    if (lengths_.size() >= layout_.capacity)
        return {Status::exhausted, {}, 0};
    const auto current = std::uint64_t(stack_pointer_.value());
    if (current > hardware_size_ || length > std::uint64_t(hardware_size_) - current)
        return {Status::exhausted, {}, 0};
    const auto result = stack_pointer_;
    stack_pointer_ = Address(static_cast<std::uint32_t>(current + length));
    lengths_.push_back(static_cast<std::uint32_t>(length));
    return {Status::ok, result, static_cast<std::uint32_t>(length)};
}

Result Context::free()
{
    if (!initialized_) return {Status::missing_context, {}, 0};
    if (lengths_.empty()) return {Status::empty, {}, 0};
    const auto length = lengths_.back();
    const auto current = std::uint64_t(stack_pointer_.value());
    if (current < length) return {Status::invalid_layout, {}, 0};
    lengths_.pop_back();
    stack_pointer_ = Address(static_cast<std::uint32_t>(current - length));
    return {Status::ok, stack_pointer_, length};
}

const Snapshot Context::snapshot() const
{
    Snapshot result;
    result.initialized = initialized_;
    result.initial_base = layout_.initial_base;
    result.stack_pointer = stack_pointer_;
    result.capacity = layout_.capacity;
    result.free_blocks = layout_.capacity >= lengths_.size()
        ? layout_.capacity - static_cast<std::uint32_t>(lengths_.size()) : 0;
    result.hardware_size = hardware_size_;
    auto address = layout_.initial_base;
    for (const auto length : lengths_) {
        result.allocations.push_back({address, length});
        address = Address(address.value() + length);
    }
    return result;
}

const char* status_name(Status status)
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::missing_context: return "missing_context";
    case Status::invalid_layout: return "invalid_layout";
    case Status::invalid_request: return "invalid_request";
    case Status::already_initialized: return "already_initialized";
    case Status::exhausted: return "exhausted";
    case Status::empty: return "empty";
    }
    return "unknown_status";
}

} // namespace melee_web::source_aram
