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

Registry::Node* Registry::find(Address descriptor)
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
        [descriptor](const Node& node) { return node.descriptor == descriptor; });
    return found == nodes_.end() ? nullptr : &*found;
}

const Registry::Node* Registry::find(Address descriptor) const
{
    const auto found = std::find_if(nodes_.begin(), nodes_.end(),
        [descriptor](const Node& node) { return node.descriptor == descriptor; });
    return found == nodes_.end() ? nullptr : &*found;
}

Status Registry::initialize(Address descriptor)
{
    if (!descriptor.value()) return Status::invalid_request;
    if (!find(descriptor)) nodes_.push_back({descriptor, {}});

    // removeAll(data): unlink this descriptor from the currently reachable
    // list, without touching any orphaned descriptor's stale next word.
    std::optional<Address> previous;
    std::optional<Address> cursor = head_;
    while (cursor) {
        Node* current = find(*cursor);
        if (!current) break; // Internal state cannot be repaired from a bad link.
        if (current->descriptor == descriptor) {
            if (previous) {
                Node* predecessor = find(*previous);
                if (predecessor) predecessor->next = current->next;
            } else {
                head_ = current->next;
            }
            break;
        }
        previous = cursor;
        cursor = current->next;
    }

    Node* node = find(descriptor);
    node->next = head_;
    head_ = descriptor;
    return Status::ok;
}

void Registry::forget_memory()
{
    // _HSD_ObjAllocForgetMemory intentionally does not walk descriptors.
    head_.reset();
}

std::optional<Address> Registry::next(Address descriptor) const
{
    const Node* node = find(descriptor);
    return node ? node->next : std::nullopt;
}

bool Registry::known(Address descriptor) const
{
    return find(descriptor) != nullptr;
}

Status ObjectPool::initialize(std::uint32_t size, std::uint32_t align,
                              bool dedicated_heap, bool number_limit, bool heap_limit)
{
    return configure(size, align, dedicated_heap, number_limit, heap_limit, true);
}

Status ObjectPool::reset(std::uint32_t size, std::uint32_t align,
                         bool dedicated_heap, bool number_limit, bool heap_limit)
{
    return configure(size, align, dedicated_heap, number_limit, heap_limit, false);
}

Status ObjectPool::configure(std::uint32_t size, std::uint32_t align,
                             bool dedicated_heap, bool number_limit, bool heap_limit,
                             bool require_new_generation)
{
    if (dedicated_heap || number_limit || heap_limit) return Status::unsupported_configuration;
    if (!heap_.initialized()) return Status::missing_context;
    if (require_new_generation && initialized_ &&
        initialized_generation_ == heap_.generation())
        return Status::invalid_context;
    if (size < 4 || !align || (align & (align - 1)))
        return Status::invalid_request;
    const auto rounded = (std::uint64_t(size) + align - 1) & ~std::uint64_t(align - 1);
    if (rounded > largest_cell - header) return Status::invalid_request;
    // HSD_ObjAllocInit is a descriptor reset, not a heap-generation test.
    // It is valid in place after allocations: old OS cells remain owned by
    // the source heap, while the descriptor forgets their object chains.
    state_ = {};
    state_.size = std::uint32_t(rounded);
    state_.align_mask = align - 1;
    initialized_ = true;
    initialized_generation_ = heap_.generation();
    // HSD_ObjAllocInit abandons the descriptor's object chains.  The source
    // OS heap still owns the old cells, represented by Heap::pool_backing_,
    // but this descriptor must not retain them as live ownership after a
    // reset.  In particular, a later heap recreation must be refillable after
    // this one reset rather than being blocked by an orphaned old generation.
    backings_.clear();
    return Status::ok;
}

bool ObjectPool::context_available() const
{
    if (!initialized_) return false;
    for (const auto& backing : backings_) {
        if (!backing.heap || !backing.heap->initialized() ||
            backing.heap->generation() != backing.generation)
            return false;
    }
    return true;
}

Status ObjectPool::add_free(std::uint32_t count)
{
    return add_free(count, heap_);
}

bool ObjectPool::context_available(Heap& selected_heap) const
{
    return context_available() && selected_heap.initialized();
}

Status ObjectPool::add_free(std::uint32_t count, Heap& selected_heap)
{
    if (!context_available(selected_heap)) return Status::missing_context;
    const auto bytes = std::uint64_t(state_.size) * count;
    if (!count || bytes > largest_cell - header) return Status::invalid_request;
    const auto allocation = selected_heap.allocate(std::uint32_t(bytes));
    if (allocation.status != Status::ok) return allocation.status;
    const auto status = link_backing(selected_heap, allocation.address, count);
    if (status != Status::ok) return status;
    return Status::ok;
}

Status ObjectPool::adopt_backing(Heap& selected_heap, Address backing,
                                 std::uint32_t count)
{
    if (!context_available(selected_heap)) return Status::missing_context;
    if (!backing.value() || !count) return Status::invalid_request;
    const auto bytes = std::uint64_t(state_.size) * count;
    if (bytes > largest_cell - header) return Status::invalid_request;
    const auto capacity = selected_heap.referent_size(backing);
    if (!capacity) return Status::unknown_allocation;
    if (*capacity < bytes) return Status::invalid_request;
    if (std::find(selected_heap.pool_backing_.begin(),
                  selected_heap.pool_backing_.end(), backing) !=
        selected_heap.pool_backing_.end())
        return Status::invalid_request;
    return link_backing(selected_heap, backing, count);
}

Status ObjectPool::link_backing(Heap& selected_heap, Address backing,
                                std::uint32_t count)
{
    const auto bytes = std::uint64_t(state_.size) * count;
    const auto capacity = selected_heap.referent_size(backing);
    if (!capacity || *capacity < bytes) return Status::invalid_request;
    selected_heap.pool_backing_.push_back(backing);
    backings_.push_back({&selected_heap, selected_heap.generation(), backing});
    std::vector<Address> added;
    added.reserve(count + state_.free.size());
    for (std::uint32_t i = 0; i < count; ++i)
        added.emplace_back(backing.value() + state_.size * i);
    added.insert(added.end(), state_.free.begin(), state_.free.end());
    state_.free = std::move(added);
    return Status::ok;
}

Allocation ObjectPool::allocate()
{
    return allocate(heap_);
}

Allocation ObjectPool::allocate(Heap& selected_heap)
{
    if (!context_available()) return {Status::missing_context, {}};
    if (state_.free.empty()) {
        const auto status = add_free(1, selected_heap);
        if (status != Status::ok) return {status, {}};
    }
    return allocate_existing();
}

Allocation ObjectPool::allocate_existing()
{
    if (!context_available()) return {Status::missing_context, {}};
    if (state_.free.empty()) return {Status::exhausted, {}};
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
