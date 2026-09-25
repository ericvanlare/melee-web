#include "source_game_heap_context.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace melee_web::source_game_heap {
namespace {
constexpr std::uint32_t kInvalidAddress = 0xffffffffU;
}

bool Context::valid_span(Address lo, Address hi)
{
    return lo.value() != 0 && hi.value() > lo.value();
}

bool Context::valid_sum(Address start, std::uint32_t size, Address* end)
{
    const auto value = std::uint64_t(start.value()) + size;
    if (value > std::numeric_limits<std::uint32_t>::max()) return false;
    *end = Address(static_cast<std::uint32_t>(value));
    return true;
}

bool Context::valid_descriptor(const Descriptor& descriptor,
                              const std::vector<Descriptor>& seen) const
{
    if (descriptor.index < 2 || descriptor.index >= kHeapCount ||
        descriptor.type == 0 || descriptor.type > 4)
        return false;
    if (std::find_if(seen.begin(), seen.end(), [&](const Descriptor& other) {
            return other.index == descriptor.index;
        }) != seen.end())
        return false;
    if (descriptor.previous != kRootIndex &&
        std::find_if(seen.begin(), seen.end(), [&](const Descriptor& other) {
            return other.index == descriptor.previous;
        }) == seen.end())
        return false;
    return true;
}

Status Context::initialize(Bounds bounds, const std::vector<Descriptor>& descriptors)
{
    if (!valid_span(bounds.arena_lo, bounds.arena_hi) ||
        !valid_span(bounds.aram_lo, bounds.aram_hi) || descriptors.empty())
        return Status::invalid_layout;

    std::vector<Descriptor> checked;
    checked.reserve(descriptors.size());
    for (const auto& descriptor : descriptors) {
        if (!valid_descriptor(descriptor, checked)) return Status::invalid_layout;
        checked.push_back(descriptor);
    }

    std::array<Heap, kHeapCount> heaps{};
    // Keep the source reset values explicit.  In particular, the handle
    // sentinel is all-ones, while an absent start is zero.
    for (auto& heap : heaps) {
        heap.id = -1;
        heap.handle = Address(kInvalidAddress);
        heap.start = {};
        heap.size = 0;
        heap.type = 1;
        heap.transient = 1;
        heap.status = HeapStatus::destroy;
    }

    for (const auto& descriptor : checked) {
        auto& heap = heaps[descriptor.index];
        heap.type = descriptor.type;
        heap.size = descriptor.size;
        if (descriptor.previous == kRootIndex) {
            switch (descriptor.type) {
            case 1:
                heap.start = bounds.arena_lo;
                break;
            case 2:
                if (descriptor.size > bounds.arena_hi.value() - bounds.arena_lo.value())
                    return Status::invalid_layout;
                heap.start = Address(bounds.arena_hi.value() - descriptor.size);
                break;
            case 4:
                heap.start = bounds.aram_lo;
                break;
            case 3:
                break;
            default:
                return Status::invalid_layout;
            }
        } else {
            const auto& previous = heaps[descriptor.previous];
            Address end;
            switch (descriptor.type) {
            case 1:
            case 4:
                if (!valid_sum(previous.start, previous.size, &end))
                    return Status::invalid_layout;
                heap.start = end;
                break;
            case 2:
                if (previous.start.value() < descriptor.size)
                    return Status::invalid_layout;
                heap.start = Address(previous.start.value() - descriptor.size);
                break;
            case 3:
                break;
            default:
                return Status::invalid_layout;
            }
        }
    }

    bounds_ = bounds;
    descriptors_ = std::move(checked);
    heaps_ = heaps;
    initialized_ = true;
    rebuild_ = {};
    rebuild_.phase = RebuildState::Phase::done;
    return Status::ok;
}

void Context::clear()
{
    bounds_ = {};
    heaps_ = {};
    descriptors_.clear();
    initialized_ = false;
    rebuild_ = {};
    rebuild_.phase = RebuildState::Phase::done;
}

Snapshot Context::snapshot() const
{
    return {initialized_, bounds_, heaps_,
            rebuild_.phase != RebuildState::Phase::done,
            rebuild_.phase == RebuildState::Phase::done && initialized_};
}

const Heap& Context::heap(std::size_t index) const
{
    if (index >= kHeapCount) throw std::out_of_range("game heap index");
    return heaps_[index];
}

Status Context::set_transient(std::size_t index, std::int32_t value)
{
    if (!initialized_) return Status::missing_context;
    if (index >= kHeapCount) return Status::invalid_request;
    heaps_[index].transient = value;
    return Status::ok;
}

std::int32_t Context::transient(std::size_t index) const
{
    if (index >= kHeapCount) return -1;
    return heaps_[index].transient;
}

Status Context::begin_rebuild()
{
    if (!initialized_) return Status::missing_context;
    if (rebuild_.pending || rebuild_.phase != RebuildState::Phase::done)
        return Status::invalid_request;

    rebuild_ = {};
    rebuild_.phase = RebuildState::Phase::destroy;
    rebuild_.cursor = 2;
    rebuild_.adjusted = bounds_;
    const auto invalid_rebuild = [this]() {
        rebuild_ = {};
        rebuild_.phase = RebuildState::Phase::done;
        return Status::invalid_layout;
    };
    for (std::size_t index = 2; index < kHeapCount; ++index) {
        const auto& heap = heaps_[index];
        if (heap.transient != 0) continue;
        Address end;
        switch (heap.type) {
        case 1:
            if (!valid_sum(heap.start, heap.size, &end)) return invalid_rebuild();
            if (rebuild_.adjusted.arena_lo.value() < end.value())
                rebuild_.adjusted.arena_lo = end;
            break;
        case 2:
            if (rebuild_.adjusted.arena_hi.value() > heap.start.value())
                rebuild_.adjusted.arena_hi = heap.start;
            break;
        case 4:
            if (!valid_sum(heap.start, heap.size, &end)) return invalid_rebuild();
            if (rebuild_.adjusted.aram_lo.value() < end.value())
                rebuild_.adjusted.aram_lo = end;
            break;
        default:
            break;
        }
    }
    if (!valid_span(rebuild_.adjusted.arena_lo, rebuild_.adjusted.arena_hi) ||
        !valid_span(rebuild_.adjusted.aram_lo, rebuild_.adjusted.aram_hi)) {
        rebuild_ = {};
        rebuild_.phase = RebuildState::Phase::done;
        return Status::invalid_layout;
    }
    return Status::ok;
}

std::optional<Request> Context::make_request()
{
    for (;;) {
        switch (rebuild_.phase) {
        case RebuildState::Phase::destroy:
            while (rebuild_.cursor < kHeapCount) {
                const auto index = rebuild_.cursor++;
                auto& heap = heaps_[index];
                if (heap.transient != 1 || heap.status != HeapStatus::create)
                    continue;
                if (heap.type == 0) {
                    return Request{RequestKind::destroy_os, index, heap.id,
                                   Address(kInvalidAddress), {}, {}};
                }
                return Request{RequestKind::destroy_handle, index, -1,
                               heap.handle, {}, {}};
            }
            rebuild_.phase = RebuildState::Phase::main;
            continue;

        case RebuildState::Phase::main:
            rebuild_.phase = RebuildState::Phase::destroy_current;
            return Request{RequestKind::replace_hsd_main, 0, -1,
                           Address(kInvalidAddress), rebuild_.adjusted.arena_lo,
                           rebuild_.adjusted.arena_hi};

        case RebuildState::Phase::destroy_current:
            rebuild_.phase = RebuildState::Phase::current;
            return Request{RequestKind::destroy_current_handle, 1, -1,
                           Address(kInvalidAddress), {}, {}};

        case RebuildState::Phase::current:
            rebuild_.phase = RebuildState::Phase::create;
            rebuild_.cursor = 2;
            return Request{RequestKind::new_current_handle, 1, -1,
                           Address(kInvalidAddress), rebuild_.adjusted.aram_lo,
                           rebuild_.adjusted.aram_hi};

        case RebuildState::Phase::create:
            while (rebuild_.cursor < kHeapCount) {
                const auto index = rebuild_.cursor++;
                auto& heap = heaps_[index];
                if (heap.transient != 0 || heap.status != HeapStatus::destroy)
                    continue;
                const auto kind = heap.type == 0 ? RequestKind::new_os
                                                 : RequestKind::new_handle;
                return Request{kind, index, -1, Address(kInvalidAddress),
                               heap.start,
                               Address(heap.start.value() + heap.size)};
            }
            rebuild_.phase = RebuildState::Phase::done;
            return std::nullopt;

        case RebuildState::Phase::done:
            return std::nullopt;
        }
    }
}

std::optional<Request> Context::next_request()
{
    if (!initialized_ || rebuild_.phase == RebuildState::Phase::done)
        return std::nullopt;
    if (!rebuild_.pending) rebuild_.pending = make_request();
    return rebuild_.pending;
}

Status Context::apply_result(const Request& request, const RequestResult& result)
{
    if (result.status != Status::ok) return result.status;
    auto& heap = heaps_[request.heap_index];
    switch (request.kind) {
    case RequestKind::destroy_os:
        heap.id = -1;
        heap.status = HeapStatus::destroy;
        break;
    case RequestKind::destroy_handle:
        heap.handle = Address(kInvalidAddress);
        heap.status = HeapStatus::destroy;
        break;
    case RequestKind::replace_hsd_main:
        if (result.id < 0) return Status::backend_failure;
        heap.id = result.id;
        heap.start = request.lo;
        heap.size = request.hi.value() - request.lo.value();
        heap.type = 0;
        heap.status = HeapStatus::create;
        break;
    case RequestKind::destroy_current_handle:
        break;
    case RequestKind::new_current_handle:
        if (result.handle.value() == 0 || result.handle.value() == kInvalidAddress)
            return Status::backend_failure;
        heap.handle = result.handle;
        heap.start = request.lo;
        heap.size = request.hi.value() - request.lo.value();
        heap.type = 3;
        heap.status = HeapStatus::create;
        break;
    case RequestKind::new_os:
        if (result.id < 0) return Status::backend_failure;
        heap.id = result.id;
        heap.status = HeapStatus::create;
        break;
    case RequestKind::new_handle:
        if (result.handle.value() == 0 || result.handle.value() == kInvalidAddress)
            return Status::backend_failure;
        heap.handle = result.handle;
        heap.status = HeapStatus::create;
        break;
    }
    return Status::ok;
}

Status Context::complete_request(const RequestResult& result)
{
    if (!initialized_) return Status::missing_context;
    if (!rebuild_.pending) return Status::invalid_request;
    const auto request = *rebuild_.pending;
    rebuild_.pending.reset();
    const auto status = apply_result(request, result);
    if (status != Status::ok) {
        rebuild_ = {};
        rebuild_.phase = RebuildState::Phase::done;
        return status;
    }
    return Status::ok;
}

Status Context::rebuild(Backend& backend)
{
    auto status = begin_rebuild();
    if (status != Status::ok) return status;
    for (;;) {
        const auto request = next_request();
        if (!request) return Status::ok;
        RequestResult result;
        switch (request->kind) {
        case RequestKind::destroy_os:
            backend.destroy_os(request->id);
            break;
        case RequestKind::destroy_handle:
            backend.destroy_handle(request->handle);
            break;
        case RequestKind::replace_hsd_main:
            result.id = backend.replace_hsd_main(request->lo, request->hi);
            break;
        case RequestKind::destroy_current_handle:
            backend.destroy_current_handle();
            break;
        case RequestKind::new_current_handle:
            result.handle = backend.new_current_handle(request->lo, request->hi);
            break;
        case RequestKind::new_os:
            result.id = backend.new_os(request->lo, request->hi);
            break;
        case RequestKind::new_handle:
            result.handle = backend.new_handle(request->lo, request->hi);
            break;
        }
        status = complete_request(result);
        if (status != Status::ok) return status;
    }
}

const char* status_name(Status status)
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::missing_context: return "missing_context";
    case Status::invalid_layout: return "invalid_layout";
    case Status::invalid_request: return "invalid_request";
    case Status::backend_failure: return "backend_failure";
    }
    return "unknown_status";
}

const char* request_kind_name(RequestKind kind)
{
    switch (kind) {
    case RequestKind::destroy_os: return "destroy_os";
    case RequestKind::destroy_handle: return "destroy_handle";
    case RequestKind::replace_hsd_main: return "replace_hsd_main";
    case RequestKind::destroy_current_handle: return "destroy_current_handle";
    case RequestKind::new_current_handle: return "new_current_handle";
    case RequestKind::new_os: return "new_os";
    case RequestKind::new_handle: return "new_handle";
    }
    return "unknown_request";
}

} // namespace melee_web::source_game_heap
