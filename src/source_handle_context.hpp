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
    compact_started,
    compact_in_progress,
    compact_complete,
    missing_callback,
    reentrant_compaction,
    invalid_transition,
    invalid_generation,
    cancelled,
    mutation_blocked,
};

enum class TransferKind {
    none,
    ram_alarm,
    devcom_1b,
};

enum class CompactPhase {
    idle,
    awaiting_callback,
    waiting_ram_alarm,
    waiting_devcom,
    awaiting_completion,
    complete,
    rejected,
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

// These are source addresses and source-state identities, never host
// pointers. A move's destination is published at source move-start, while its
// payload becomes usable only after the corresponding asynchronous completion.
struct CompactMove {
    Address handle;
    Address source;
    Address destination;
    Address next;
    std::uint32_t size = 0; // OSRoundUp32B(handle->x8_hi)
    std::uint32_t offset = 0;
    std::uint32_t generation = 0;
    TransferKind transfer = TransferKind::none;
};

struct CompactState {
    CompactPhase phase = CompactPhase::idle;
    Address heap;
    Address callback;
    std::uint32_t callback_arg = 0;
    Address callback_handle;
    std::uint32_t callback_generation = 0;
    Address cursor;
    CompactMove move;
    bool callback_invoked = false;
};

struct CompactBeginResult {
    Status status = Status::missing_context;
    bool started = false;
    CompactState state;
};

struct CompactStepResult {
    Status status = Status::missing_context;
    std::uint32_t copied = 0;
    bool transfer_complete = false;
    bool callback_invoked = false;
    CompactState state;
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

    // lbMemory_8001529C's old fail-closed diagnostic view. It does not mutate
    // source placement or pretend that an asynchronous move completed.
    Status compact(Address heap);
    Status compact_current();

    // Source-shaped asynchronous compaction. The lbHeap_80015D6C heap-index
    // bypass belongs to the game wrapper; this allocator API models direct
    // lbMemory_8001529C and therefore has no heap-index policy.
    CompactBeginResult compact_begin(Address heap, Address callback,
                                     std::uint32_t callback_arg);
    CompactStepResult compact_ram_alarm_chunk(std::uint32_t generation);
    CompactStepResult compact_callback_transition(std::uint32_t generation,
                                                   bool cancelled = false);
    CompactStepResult compact_devcom_complete(std::uint32_t generation,
                                              bool cancelled = false);
    const CompactState& compact_state() const { return compact_; }

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

    static std::uint32_t round32(std::uint32_t size);
    bool issue_generation(std::uint32_t& generation);
    bool validate_chain(const Record& owner) const;
    CompactStepResult invalid_step(Status status) const;
    CompactStepResult transfer_completed(TransferKind transfer,
                                         std::uint32_t generation);
    CompactStepResult start_move(Record& handle);

    Layout layout_{};
    Arena arena_{};
    std::vector<Record> records_;
    Address free_mem_{};
    Address free_heap_{};
    Address current_{};
    std::uint32_t allocations_ = 0;
    std::uint32_t max_allocations_ = 0;
    bool initialized_ = false;
    CompactState compact_{};
    Address compact_cursor_{};
    std::uint32_t next_generation_ = 1;
};

const char* status_name(Status status);

} // namespace melee_web::source_handle

#endif
