#include "source_address_context.hpp"

#include <algorithm>
#include <utility>

namespace melee_web::source {
namespace {
constexpr std::uint32_t alignment = 32, header = 32, minimum_cell = 64;
constexpr std::uint32_t largest_cell = 0x7fffffe0;
std::uint64_t end_of(const Cell& cell)
{
    return std::uint64_t(cell.start.value()) + cell.bytes;
}
bool valid_cell(const Cell& cell, const HeapState& state)
{
    return cell.start.value() >= state.arena_begin.value() &&
           cell.start.value() % alignment == 0 &&
           cell.bytes >= minimum_cell && cell.bytes <= largest_cell &&
           cell.bytes % alignment == 0 && end_of(cell) <= state.arena_end.value();
}
}

Status Heap::create_empty(Address begin, Address end)
{
    // OSCreateHeap rounds the supplied bounds inward. Use a wider temporary
    // to reject overflow instead of inheriting C pointer/integer UB.
    const auto first = (std::uint64_t(begin.value()) + 31) & ~std::uint64_t(31);
    const auto last = end.value() & ~std::uint32_t(31);
    if (!begin.value() || first >= last || last - first < minimum_cell ||
        last - first > largest_cell)
        return Status::invalid_context;
    HeapState candidate{Address(std::uint32_t(first)), Address(last),
                        std::uint32_t(last - first),
                        {{Address(std::uint32_t(first)), std::uint32_t(last - first)}}, {}};
    return restore(candidate);
}

Status Heap::restore(const HeapState& candidate)
{
    if (!candidate.arena_begin.value() ||
        candidate.arena_begin.value() >= candidate.arena_end.value() ||
        candidate.arena_begin.value() % alignment || candidate.arena_end.value() % alignment ||
        candidate.heap_bytes < minimum_cell || candidate.heap_bytes > largest_cell)
        return Status::invalid_context;
    std::uint64_t total = 0;
    std::vector<Cell> cells;
    for (const auto* list : {&candidate.free, &candidate.allocated}) {
        for (const Cell& cell : *list) {
            if (!valid_cell(cell, candidate)) return Status::invalid_context;
            total += cell.bytes;
            cells.push_back(cell);
        }
    }
    if (total != candidate.heap_bytes) return Status::invalid_context;
    for (std::size_t i = 1; i < candidate.free.size(); ++i) {
        // Adjacent free cells should already have coalesced in original DLInsert.
        if (end_of(candidate.free[i - 1]) >= candidate.free[i].start.value())
            return Status::invalid_context;
    }
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return a.start.value() < b.start.value();
    });
    for (std::size_t i = 1; i < cells.size(); ++i)
        if (end_of(cells[i - 1]) > cells[i].start.value()) return Status::invalid_context;
    state_ = candidate;
    pool_backing_.clear();
    initialized_ = true;
    ++generation_;
    return Status::ok;
}

void Heap::clear()
{
    initialized_ = false;
    state_ = {};
    pool_backing_.clear();
    ++generation_;
}

Allocation Heap::allocate(std::uint32_t requested)
{
    if (!initialized_) return {Status::missing_context, {}};
    const std::uint64_t rounded = (std::uint64_t(requested) + header + 31) & ~std::uint64_t(31);
    if (!requested || rounded > largest_cell) return {Status::invalid_request, {}};
    const auto bytes = std::uint32_t(rounded);
    auto found = std::find_if(state_.free.begin(), state_.free.end(), [bytes](const Cell& cell) {
        return cell.bytes >= bytes;
    });
    if (found == state_.free.end()) return {Status::exhausted, {}};
    Cell used = *found;
    const auto remainder = used.bytes - bytes;
    if (remainder < minimum_cell) {
        state_.free.erase(found);
    } else {
        used.bytes = bytes;
        *found = {Address(used.start.value() + bytes), remainder};
    }
    state_.allocated.insert(state_.allocated.begin(), used);
    return {Status::ok, Address(used.start.value() + header)};
}

Status Heap::release(Address payload)
{
    if (!initialized_) return Status::missing_context;
    const auto found = std::find_if(state_.allocated.begin(), state_.allocated.end(),
        [payload](const Cell& cell) { return cell.start.value() + header == payload.value(); });
    if (found == state_.allocated.end()) return Status::unknown_allocation;
    if (std::find(pool_backing_.begin(), pool_backing_.end(), payload) != pool_backing_.end())
        return Status::invalid_request;
    const Cell released = *found;
    state_.allocated.erase(found);
    auto where = std::lower_bound(state_.free.begin(), state_.free.end(), released,
        [](const Cell& a, const Cell& b) { return a.start.value() < b.start.value(); });
    auto index = std::size_t(where - state_.free.begin());
    state_.free.insert(where, released);
    if (index + 1 < state_.free.size() &&
        end_of(state_.free[index]) == state_.free[index + 1].start.value()) {
        state_.free[index].bytes += state_.free[index + 1].bytes;
        state_.free.erase(state_.free.begin() + index + 1);
    }
    if (index && end_of(state_.free[index - 1]) == state_.free[index].start.value()) {
        state_.free[index - 1].bytes += state_.free[index].bytes;
        state_.free.erase(state_.free.begin() + index);
    }
    return Status::ok;
}

std::optional<std::uint32_t> Heap::free_bytes() const
{
    if (!initialized_) return {};
    std::uint32_t result = 0;
    for (const auto& cell : state_.free) result += cell.bytes - header;
    return result;
}

std::optional<std::uint32_t> Heap::referent_size(Address payload) const
{
    if (!initialized_) return {};
    for (const auto& cell : state_.allocated)
        if (cell.start.value() + header == payload.value()) return cell.bytes - header;
    return {};
}

Status ObjectPool::initialize(std::uint32_t size, std::uint32_t align,
                              bool dedicated_heap, bool number_limit, bool heap_limit)
{
    if (dedicated_heap || number_limit || heap_limit) return Status::unsupported_configuration;
    if (initialized_ && heap_generation_ == heap_.generation()) return Status::invalid_context;
    if (!heap_.initialized()) return Status::missing_context;
    if (size < 4 || !align || (align & (align - 1)))
        return Status::invalid_request;
    const auto rounded = (std::uint64_t(size) + align - 1) & ~std::uint64_t(align - 1);
    if (rounded > largest_cell - header) return Status::invalid_request;
    state_ = {};
    state_.size = std::uint32_t(rounded);
    state_.align_mask = align - 1;
    initialized_ = true;
    heap_generation_ = heap_.generation();
    return Status::ok;
}

bool ObjectPool::context_available() const
{
    return initialized_ && heap_.initialized() && heap_generation_ == heap_.generation();
}

Status ObjectPool::add_free(std::uint32_t count)
{
    if (!context_available()) return Status::missing_context;
    const auto bytes = std::uint64_t(state_.size) * count;
    if (!count || bytes > largest_cell - header) return Status::invalid_request;
    const auto allocation = heap_.allocate(std::uint32_t(bytes));
    if (allocation.status != Status::ok) return allocation.status;
    heap_.pool_backing_.push_back(allocation.address);
    std::vector<Address> added;
    added.reserve(count + state_.free.size());
    for (std::uint32_t i = 0; i < count; ++i)
        added.emplace_back(allocation.address.value() + state_.size * i);
    added.insert(added.end(), state_.free.begin(), state_.free.end());
    state_.free = std::move(added);
    return Status::ok;
}

Allocation ObjectPool::allocate()
{
    if (!context_available()) return {Status::missing_context, {}};
    if (state_.free.empty()) {
        const auto status = add_free(1);
        if (status != Status::ok) return {status, {}};
    }
    const Address result = state_.free.front();
    state_.free.erase(state_.free.begin());
    state_.live.push_back(result);
    state_.used += 1;
    state_.peak = std::max(state_.peak, state_.used);
    return {Status::ok, result};
}

Status ObjectPool::release(Address object)
{
    if (!context_available()) return Status::missing_context;
    const auto found = std::find(state_.live.begin(), state_.live.end(), object);
    if (found == state_.live.end()) return Status::unknown_allocation;
    state_.live.erase(found);
    state_.free.insert(state_.free.begin(), object);
    state_.used -= 1;
    return Status::ok;
}

RegisterWord RegisterWord::from_source_address(Address address)
{
    return from_scalar(address.value());
}
RegisterWord RegisterWord::from_scalar(std::uint32_t value)
{
    RegisterWord result;
    result.word_ = value;
    return result;
}
std::optional<int> RegisterWord::signed_low_byte() const
{
    if (!word_) return {};
    const auto byte = int(*word_ & 0xffU);
    return byte < 128 ? byte : byte - 256;
}
std::optional<StickCarry> resolve_stick_carry(RegisterWord r5, RegisterWord r30)
{
    const auto x = r5.signed_low_byte(), y = r30.signed_low_byte();
    if (!x || !y) return {};
    return StickCarry{*x, *y};
}
} // namespace melee_web::source
