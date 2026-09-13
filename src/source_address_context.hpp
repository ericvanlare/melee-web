#ifndef MELEE_WEB_SOURCE_ADDRESS_CONTEXT_HPP
#define MELEE_WEB_SOURCE_ADDRESS_CONTEXT_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace melee_web::source {

// Source identities are numbers in the original address space. This type has
// no host-pointer constructor, dereference, or implicit integer conversion.
class Address {
public:
    constexpr Address() = default;
    explicit constexpr Address(std::uint32_t value) : value_(value) {}
    constexpr std::uint32_t value() const { return value_; }
    friend constexpr bool operator==(Address a, Address b) { return a.value_ == b.value_; }
    friend constexpr bool operator!=(Address a, Address b) { return !(a == b); }
private:
    std::uint32_t value_ = 0;
};

enum class Status {
    ok, missing_context, invalid_context, invalid_request, exhausted,
    unknown_allocation, unsupported_configuration
};

struct Cell {
    Address start;                   // Source cell header, not payload.
    std::uint32_t bytes;
};

struct HeapState {
    Address arena_begin, arena_end;
    std::uint32_t heap_bytes = 0;
    std::vector<Cell> free;           // Original address-ordered list.
    std::vector<Cell> allocated;      // Original most-recent-first list.
};

struct Allocation {
    Status status;
    Address address;
};

// Address-only OSAlloc model. No payload bytes, host allocator substitution,
// default arena, implicit fresh history, OSAllocFixed, or OSAddToHeap replay.
// Restore accepts an explicitly supplied current context, including gaps left
// by fixed reservations. It validates cell ownership/order and total size,
// not provenance: the provider must attest the complete original context,
// including every gap. A structurally valid snapshot is not retail evidence.
class Heap {
public:
    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Status create_empty(Address begin, Address end);
    Status restore(const HeapState& state);
    void clear();
    bool initialized() const { return initialized_; }
    std::uint64_t generation() const { return generation_; }
    const HeapState& state() const { return state_; }
    Allocation allocate(std::uint32_t requested);
    Status release(Address payload);
    std::optional<std::uint32_t> free_bytes() const;
    std::optional<std::uint32_t> referent_size(Address payload) const;
private:
    friend class ObjectPool;
    HeapState state_;
    // HSD retains its OS cells, even after every object is freed. Reject a
    // direct OS free of those cells until the entire context is replaced.
    std::vector<Address> pool_backing_;
    bool initialized_ = false;
    std::uint64_t generation_ = 0;
};

struct PoolState {
    std::uint32_t size = 0, align_mask = 0;
    std::uint32_t used = 0, peak = 0;
    std::vector<Address> free;        // HSD link order, head first.
    std::vector<Address> live;
};

// Ordinary HSD_ObjAlloc/Free identity path: obj_heap.top == 0, both limit
// flags disabled. Other modes must be implemented before a caller selects them.
// Pool lifetime must remain inside its Heap context lifetime. Destroying this
// owner does not return HSD blocks to OSAlloc: the original pool retains them.
class ObjectPool {
public:
    explicit ObjectPool(Heap& heap) : heap_(heap) {}
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;
    // May initialize again after a heap-generation change. In-place original
    // HSD_ObjAllocInit resets and imports of existing pools are not supported.
    Status initialize(std::uint32_t size, std::uint32_t alignment,
                      bool dedicated_heap = false, bool number_limit = false,
                      bool heap_limit = false);
    Status add_free(std::uint32_t count);
    Allocation allocate();
    Status release(Address object);
    const PoolState& state() const { return state_; }
private:
    Heap& heap_;
    PoolState state_;
    bool initialized_ = false;
    std::uint64_t heap_generation_ = 0;
    bool context_available() const;
};

// A known source zero and an unavailable register are different states.
// Callers must explicitly supply each original call's reaching definition.
class RegisterWord {
public:
    static RegisterWord from_source_address(Address address);
    static RegisterWord from_scalar(std::uint32_t word);
    void invalidate() { word_.reset(); }
    std::optional<int> signed_low_byte() const;
private:
    std::optional<std::uint32_t> word_;
};

struct StickCarry { int x, y; };
std::optional<StickCarry> resolve_stick_carry(RegisterWord r5, RegisterWord r30);

} // namespace melee_web::source
#endif
