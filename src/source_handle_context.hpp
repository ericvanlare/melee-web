#ifndef MELEE_WEB_SOURCE_HANDLE_CONTEXT_HPP
#define MELEE_WEB_SOURCE_HANDLE_CONTEXT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace melee_web::source_handle {

// A source address is an identity in the original 32-bit address space. It is
// never converted to, or dereferenced as, a host pointer.
class Address {
public:
    constexpr Address() = default;
    explicit constexpr Address(std::uint32_t value) : value_(value) {}
    constexpr std::uint32_t value() const { return value_; }
    friend constexpr bool operator==(Address a, Address b) { return a.value_ == b.value_; }
    friend constexpr bool operator!=(Address a, Address b) { return !(a == b); }
    friend constexpr bool operator<(Address a, Address b) { return a.value_ < b.value_; }

private:
    std::uint32_t value_ = 0;
};

struct Layout {
    // Every root is supplied by the source-context provider. The model does
    // not infer these from a host object or a captured allocation result.
    Address allocator;
    Address mem_entries;
    Address heap_handles;
    Address current_handle_slot;
};

struct Arena {
    Address lo;
    Address hi;
};

enum class Status {
    ok,
    missing_context,
    invalid_layout,
    invalid_request,
    descriptor_exhausted,
    unknown_handle,
    unknown_payload,
    exhausted,
    async_move_required,
};

struct HandleResult {
    Status status = Status::missing_context;
    Address handle;
    Address payload;
    std::uint32_t size = 0;
};

struct HandleView {
    Address identity;
    Address next;
    Address lo;
    Address hi;
    // Meaningful for heap descriptors. Allocation descriptors are the
    // original 12-byte MemEntry and have no xC_prev word.
    Address prev;
};

struct Snapshot {
    bool initialized = false;
    Address current;
    std::vector<Address> free_mem;
    std::vector<Address> free_heap;
    std::vector<HandleView> active;
    std::uint32_t allocations = 0;
    std::uint32_t max_allocations = 0;
};

class Context {
public:
    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // This is the address-only equivalent of lbMemory_8001564C. It creates
    // the fixed 131-entry MemEntry and six-entry heap-handle free chains, then
    // consumes the first heap handle for the initial current handle.
    Status initialize(Layout layout, Arena arena);
    void clear();

    bool initialized() const { return initialized_; }
    const Layout& layout() const { return layout_; }
    const Arena& arena() const { return arena_; }
    const Snapshot snapshot() const;

    // lbMemory_80014E24 and lbMemory_800154D4. The latter also publishes the
    // returned handle through the explicitly supplied current-handle root.
    HandleResult new_handle(Address lo, Address hi);
    HandleResult new_current(Address lo, Address hi);

    // lbMemory_80014FC8: 32-byte rounding, best fit, and the original <= tie
    // rule, which chooses the last equally good gap. A zero-size request is
    // rejected as an invalid model request; no such source call is observed.
    HandleResult allocate(Address heap, std::uint64_t requested);

    // lbMemFreeToHeap_800150F0 and lbMemory_80014EEC/800155A4.
    Status free_payload(Address heap, Address payload);
    Status destroy(Address heap);
    Status destroy_current();

    // lbMemory_8001529C. Contiguous children are a complete no-op. A gap is
    // reported explicitly until the source async move path is observed and
    // implemented.
    Status compact(Address heap);
    Status compact_current();

private:
    struct Record {
        Address identity;
        Address next;
        Address lo;
        Address hi;
        Address prev;
        bool heap_descriptor = false;
        bool active = false;
    };

    static constexpr std::size_t kMemEntries = 0x83;
    static constexpr std::size_t kHeapHandles = 6;
    static constexpr std::uint32_t kMemEntryStride = 0xC;
    static constexpr std::uint32_t kHeapHandleStride = 0x10;

    bool valid_address_span(Address start, std::uint64_t bytes) const;
    Record* find(Address identity);
    const Record* find(Address identity) const;
    Record* active_heap(Address identity);
    Address pop_mem_entry();
    Address pop_heap_handle();
    void push_mem_entry(Record& record);
    void push_heap_handle(Record& record);
    std::vector<Address> follow(Address head, std::size_t bound) const;

    Layout layout_{};
    Arena arena_{};
    std::vector<Record> records_;
    Address free_mem_{};
    Address free_heap_{};
    Address current_{};
    std::uint32_t allocations_ = 0;
    std::uint32_t max_allocations_ = 0;
    bool initialized_ = false;
};

const char* status_name(Status status);

} // namespace melee_web::source_handle

#endif
