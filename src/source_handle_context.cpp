#include "source_handle_context.hpp"

#include <algorithm>
#include <limits>

namespace melee_web::source_handle {
namespace {
constexpr std::uint64_t kAddressLimit = std::uint64_t{0x100000000};
constexpr std::uint32_t kAlign = 32;
}

bool Context::valid_address_span(Address start, std::uint64_t bytes) const
{
    return start.value() != 0 && std::uint64_t(start.value()) + bytes <= kAddressLimit;
}

namespace {
bool overlaps(Address first, std::uint64_t first_bytes,
              Address second, std::uint64_t second_bytes)
{
    const auto first_start = std::uint64_t(first.value());
    const auto second_start = std::uint64_t(second.value());
    return first_start < second_start + second_bytes &&
           second_start < first_start + first_bytes;
}
}

Context::Record* Context::find(Address identity)
{
    const auto it = std::find_if(records_.begin(), records_.end(),
        [identity](const Record& record) { return record.identity == identity; });
    return it == records_.end() ? nullptr : &*it;
}

const Context::Record* Context::find(Address identity) const
{
    const auto it = std::find_if(records_.begin(), records_.end(),
        [identity](const Record& record) { return record.identity == identity; });
    return it == records_.end() ? nullptr : &*it;
}

Context::Record* Context::active_heap(Address identity)
{
    auto* record = find(identity);
    return record && record->active && record->heap_descriptor ? record : nullptr;
}

Status Context::initialize(Layout layout, Arena arena)
{
    if (!valid_address_span(layout.mem_entries, kMemEntries * kMemEntryStride) ||
        !valid_address_span(layout.heap_handles, kHeapHandles * kHeapHandleStride) ||
        !valid_address_span(layout.current_handle_slot, sizeof(std::uint32_t)) ||
        !layout.allocator.value() || !layout.current_handle_slot.value() ||
        layout.mem_entries.value() % 4 || layout.heap_handles.value() % 4 ||
        layout.current_handle_slot.value() % 4 || !arena.lo.value() ||
        arena.lo.value() >= arena.hi.value() ||
        overlaps(layout.mem_entries, kMemEntries * kMemEntryStride,
                 layout.heap_handles, kHeapHandles * kHeapHandleStride) ||
        overlaps(layout.mem_entries, kMemEntries * kMemEntryStride,
                 layout.current_handle_slot, sizeof(std::uint32_t)) ||
        overlaps(layout.heap_handles, kHeapHandles * kHeapHandleStride,
                 layout.current_handle_slot, sizeof(std::uint32_t)))
        return Status::invalid_layout;

    std::vector<Record> records;
    records.reserve(kMemEntries + kHeapHandles);
    for (std::size_t i = 0; i < kMemEntries; ++i) {
        records.push_back({Address(layout.mem_entries.value() +
                                   std::uint32_t(i * kMemEntryStride)), {}, {}, {}, {}, false, false});
    }
    for (std::size_t i = 0; i < kHeapHandles; ++i) {
        records.push_back({Address(layout.heap_handles.value() +
                                   std::uint32_t(i * kHeapHandleStride)), {}, {}, {}, {}, true, false});
    }

    layout_ = layout;
    arena_ = arena;
    records_ = std::move(records);
    free_mem_ = layout.mem_entries;
    for (std::size_t i = 0; i + 1 < kMemEntries; ++i)
        records_[i].next = records_[i + 1].identity;
    free_heap_ = layout.heap_handles;
    for (std::size_t i = 0; i + 1 < kHeapHandles; ++i)
        records_[kMemEntries + i].next = records_[kMemEntries + i + 1].identity;
    allocations_ = 0;
    max_allocations_ = 0;
    current_ = {};
    initialized_ = true;

    const auto root = new_current(arena.lo, arena.hi);
    if (root.status != Status::ok) {
        clear();
        return root.status == Status::descriptor_exhausted ? Status::invalid_layout : root.status;
    }
    return Status::ok;
}

void Context::clear()
{
    layout_ = {};
    arena_ = {};
    records_.clear();
    free_mem_ = {};
    free_heap_ = {};
    current_ = {};
    allocations_ = 0;
    max_allocations_ = 0;
    initialized_ = false;
}

Address Context::pop_mem_entry()
{
    const auto identity = free_mem_;
    auto* record = find(identity);
    if (!record) return {};
    free_mem_ = record->next;
    record->next = {};
    record->active = true;
    record->prev = {};
    return identity;
}

Address Context::pop_heap_handle()
{
    const auto identity = free_heap_;
    auto* record = find(identity);
    if (!record) return {};
    free_heap_ = record->next;
    record->next = {};
    record->active = true;
    record->prev = {};
    return identity;
}

void Context::push_mem_entry(Record& record)
{
    record.next = free_mem_;
    free_mem_ = record.identity;
    record.active = false;
}

void Context::push_heap_handle(Record& record)
{
    record.next = free_heap_;
    free_heap_ = record.identity;
    record.active = false;
}

HandleResult Context::new_handle(Address lo, Address hi)
{
    if (!initialized_) return {Status::missing_context, {}, {}, 0};
    if (!lo.value() || lo.value() >= hi.value())
        return {Status::invalid_request, {}, {}, 0};
    // The original source validates ordinary (< 0x80000000) pointers against
    // the ARAM arena. Higher source addresses are device/other-memory values
    // and deliberately bypass that check.
    if (lo.value() < 0x80000000U && hi.value() < 0x80000000U &&
        (lo.value() < arena_.lo.value() || hi.value() > arena_.hi.value()))
        return {Status::invalid_request, {}, {}, 0};
    const auto identity = pop_heap_handle();
    if (!identity.value()) return {Status::descriptor_exhausted, {}, {}, 0};
    auto* record = find(identity);
    record->next = {};
    record->lo = lo;
    record->hi = hi;
    record->prev = {};
    return {Status::ok, identity, {}, 0};
}

HandleResult Context::new_current(Address lo, Address hi)
{
    const auto result = new_handle(lo, hi);
    if (result.status == Status::ok) current_ = result.handle;
    return result;
}

HandleResult Context::allocate(Address heap, std::uint64_t requested)
{
    if (!initialized_) return {Status::missing_context, {}, {}, 0};
    auto* owner = active_heap(heap);
    if (!owner) return {Status::unknown_handle, {}, {}, 0};
    if (!requested || requested > std::numeric_limits<std::uint32_t>::max())
        return {Status::invalid_request, {}, {}, 0};
    const auto rounded = (requested + kAlign - 1) & ~std::uint64_t(kAlign - 1);
    if (!rounded || rounded > std::numeric_limits<std::uint32_t>::max())
        return {Status::invalid_request, {}, {}, 0};

    std::uint64_t least_leftover = 0x40000000U;
    Address before = owner->identity;
    Address start = owner->lo;
    Address chosen_before{};
    Address chosen_start{};
    for (Address child = owner->prev; child.value();) {
        const auto* record = find(child);
        if (!record || !record->active) return {Status::invalid_layout, {}, {}, 0};
        const auto end = std::uint64_t(record->lo.value());
        if (end < start.value()) return {Status::invalid_layout, {}, {}, 0};
        const auto available = end - start.value();
        if (available >= rounded) {
            const auto leftover = available - rounded;
            if (leftover <= least_leftover) {
                least_leftover = leftover;
                chosen_before = before;
                chosen_start = start;
            }
        }
        const auto child_end = std::uint64_t(record->lo.value()) + record->hi.value();
        if (child_end > kAddressLimit) return {Status::invalid_layout, {}, {}, 0};
        start = Address(static_cast<std::uint32_t>(child_end));
        before = child;
        child = record->next;
    }
    const auto final_end = std::uint64_t(owner->hi.value());
    if (final_end < start.value()) return {Status::invalid_layout, {}, {}, 0};
    const auto available = final_end - start.value();
    if (available >= rounded) {
        const auto leftover = available - rounded;
        if (leftover <= least_leftover) {
            least_leftover = leftover;
            chosen_before = before;
            chosen_start = start;
        }
    }
    if (!chosen_start.value()) return {Status::exhausted, {}, {}, 0};

    const auto identity = pop_mem_entry();
    if (!identity.value()) return {Status::descriptor_exhausted, {}, {}, 0};
    auto* result = find(identity);
    auto* insertion = find(chosen_before);
    if (!result || !insertion) return {Status::invalid_layout, {}, {}, 0};
    result->lo = chosen_start;
    result->hi = Address(static_cast<std::uint32_t>(rounded));
    result->next = chosen_before == owner->identity ? owner->prev : insertion->next;
    result->prev = {};
    if (chosen_before == owner->identity) owner->prev = identity;
    else insertion->next = identity;
    ++allocations_;
    max_allocations_ = std::max(max_allocations_, allocations_);
    return {Status::ok, identity, chosen_start, static_cast<std::uint32_t>(rounded)};
}

Status Context::free_payload(Address heap, Address payload)
{
    if (!initialized_) return Status::missing_context;
    auto* owner = active_heap(heap);
    if (!owner) return Status::unknown_handle;
    Address before = owner->identity;
    Address child = owner->prev;
    while (child.value()) {
        auto* record = find(child);
        if (!record || !record->active) return Status::invalid_layout;
        if (record->lo == payload) {
            auto* prior = find(before);
            if (!prior) return Status::invalid_layout;
            if (before == owner->identity) owner->prev = record->next;
            else prior->next = record->next;
            push_mem_entry(*record);
            if (!allocations_) return Status::invalid_layout;
            --allocations_;
            return Status::ok;
        }
        before = child;
        child = record->next;
    }
    return Status::unknown_payload;
}

Status Context::destroy(Address heap)
{
    if (!initialized_) return Status::missing_context;
    auto* owner = active_heap(heap);
    if (!owner) return Status::unknown_handle;
    Address child = owner->prev;
    while (child.value()) {
        auto* record = find(child);
        if (!record || !record->active) return Status::invalid_layout;
        const auto next = record->next;
        push_mem_entry(*record);
        if (!allocations_) return Status::invalid_layout;
        --allocations_;
        child = next;
    }
    push_heap_handle(*owner);
    return Status::ok;
}

Status Context::destroy_current()
{
    if (!initialized_) return Status::missing_context;
    if (!current_.value()) return Status::unknown_handle;
    const auto status = destroy(current_);
    if (status == Status::ok) current_ = {};
    return status;
}

Status Context::compact(Address heap)
{
    if (!initialized_) return Status::missing_context;
    const auto* owner = active_heap(heap);
    if (!owner) return Status::unknown_handle;
    Address expected = owner->lo;
    for (Address child = owner->prev; child.value();) {
        const auto* record = find(child);
        if (!record || !record->active) return Status::invalid_layout;
        if (record->lo != expected) return Status::async_move_required;
        const auto end = std::uint64_t(record->lo.value()) + record->hi.value();
        if (end > kAddressLimit) return Status::invalid_layout;
        expected = Address(static_cast<std::uint32_t>(end));
        child = record->next;
    }
    return Status::ok;
}

Status Context::compact_current()
{
    if (!initialized_) return Status::missing_context;
    if (!current_.value()) return Status::unknown_handle;
    return compact(current_);
}

std::vector<Address> Context::follow(Address head, std::size_t bound) const
{
    std::vector<Address> result;
    for (std::size_t i = 0; head.value() && i < bound; ++i) {
        const auto* record = find(head);
        if (!record) break;
        result.push_back(head);
        head = record->next;
    }
    return result;
}

const Snapshot Context::snapshot() const
{
    Snapshot result;
    result.initialized = initialized_;
    result.current = current_;
    result.free_mem = follow(free_mem_, kMemEntries);
    result.free_heap = follow(free_heap_, kHeapHandles);
    result.allocations = allocations_;
    result.max_allocations = max_allocations_;
    for (const auto& record : records_) {
        if (!record.active) continue;
        result.active.push_back({record.identity, record.next, record.lo,
                                 record.hi,
                                 record.heap_descriptor ? record.prev : Address{}});
    }
    std::sort(result.active.begin(), result.active.end(),
        [](const HandleView& a, const HandleView& b) { return a.identity < b.identity; });
    return result;
}

const char* status_name(Status status)
{
    switch (status) {
    case Status::ok: return "ok";
    case Status::missing_context: return "missing_context";
    case Status::invalid_layout: return "invalid_layout";
    case Status::invalid_request: return "invalid_request";
    case Status::descriptor_exhausted: return "descriptor_exhausted";
    case Status::unknown_handle: return "unknown_handle";
    case Status::unknown_payload: return "unknown_payload";
    case Status::exhausted: return "exhausted";
    case Status::async_move_required: return "async_move_required";
    }
    return "unknown_status";
}

} // namespace melee_web::source_handle
