#include "source_address_context.hpp"
#include <cstdio>
#include <cstdlib>
#include <type_traits>

using namespace melee_web::source;
static_assert(!std::is_constructible_v<Address, void*>);
static_assert(!std::is_convertible_v<Address, std::uint32_t>);
static_assert(!std::is_copy_assignable_v<Heap>);
static_assert(!std::is_copy_constructible_v<ObjectPool>);
static_assert(!std::is_copy_assignable_v<ObjectPool>);
static_assert(!std::is_move_constructible_v<ObjectPool>);
static_assert(!std::is_move_assignable_v<ObjectPool>);

static void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

static void context_guards()
{
    Heap heap;
    check(heap.allocate(8).status == Status::missing_context, "unknown heap cannot fabricate an arena");
    check(!heap.free_bytes(), "unknown is distinct from empty");
    check(heap.release(Address(32)) == Status::missing_context, "unknown free is explicit");
    check(heap.create_empty(Address(0xfffffff0), Address(0xffffffff)) == Status::invalid_context,
          "inward alignment cannot wrap the source address space");
    check(!heap.initialized(), "failed context remains unavailable");
    check(heap.create_empty(Address(0x1001), Address(0x5001)) == Status::ok,
          "explicit synthetic source bounds accepted");
    check(heap.state().arena_begin == Address(0x1020) && heap.state().arena_end == Address(0x5000),
          "OSCreateHeap rounds bounds inward");
    const auto a = heap.allocate(73), b = heap.allocate(101);
    check(a.status == Status::ok && b.status == Status::ok, "allocate synthetic blocks");
    const auto saved = heap.state();
    const auto generation = heap.generation();
    auto broken = saved;
    broken.allocated.push_back(broken.allocated.front());
    check(heap.restore(broken) == Status::invalid_context, "duplicated cell ownership rejected");
    check(heap.generation() == generation && heap.referent_size(a.address),
          "bad restore preserves existing ownership");
    broken = saved;
    broken.allocated[0].start = broken.free[0].start;
    check(heap.restore(broken) == Status::invalid_context, "overlapping cells rejected");
    broken = saved;
    broken.heap_bytes -= 32;
    check(heap.restore(broken) == Status::invalid_context, "incomplete heap accounting rejected");
    check(heap.allocate(0).status == Status::invalid_request &&
          heap.allocate(0xffffffff).status == Status::invalid_request,
          "invalid request cannot wrap source size arithmetic");
    check(heap.release(Address(a.address.value() + 1)) == Status::unknown_allocation,
          "interior pointer cannot free a cell");
    check(heap.release(a.address) == Status::ok &&
          heap.release(a.address) == Status::unknown_allocation, "double free rejected");
    Heap restored;
    check(restored.restore(heap.state()) == Status::ok, "explicit fragmented state restores");
    check(heap.allocate(20).address == restored.allocate(20).address,
          "restoration retains allocation history rather than assuming fresh heap");
    heap.clear();
    check(!heap.referent_size(b.address), "teardown invalidates source context");
}

static void pool_lifetime()
{
    Heap heap;
    ObjectPool absent(heap);
    check(absent.initialize(12, 4) == Status::missing_context, "pool requires explicit heap context");
    check(heap.create_empty(Address(0x10200), Address(0x20200)) == Status::ok, "pool arena");
    ObjectPool unsupported(heap);
    check(unsupported.initialize(12, 4, true) == Status::unsupported_configuration,
          "dedicated bump heap cannot fall back to OS");
    check(unsupported.initialize(12, 4, false, true) == Status::unsupported_configuration &&
          unsupported.initialize(12, 4, false, false, true) == Status::unsupported_configuration,
          "unmodeled limit flags cannot silently disappear");
    ObjectPool pool(heap);
    check(pool.initialize(21, 4) == Status::ok && pool.state().size == 24, "original HSD size rounding");
    check(pool.add_free(3) == Status::ok, "original multi-object refill");
    const auto a = pool.allocate(), b = pool.allocate(), c = pool.allocate();
    check(a.status == Status::ok && b.address.value() == a.address.value() + 24 &&
          c.address.value() == b.address.value() + 24, "refill order preserves consecutive objects");
    check(pool.release(a.address) == Status::ok && pool.release(c.address) == Status::ok,
          "pool frees retain OS allocation");
    const auto free_bytes = heap.free_bytes();
    check(pool.allocate().address == c.address && pool.allocate().address == a.address,
          "HSD reuse is LIFO");
    check(heap.free_bytes() == free_bytes && pool.state().peak == 3, "reuse does not allocate an OS block");
    check(pool.release(Address(b.address.value()+1)) == Status::unknown_allocation,
          "pool rejects unrelated object identity");
    check(heap.release(a.address) == Status::invalid_request && heap.referent_size(a.address),
          "direct OS free cannot invalidate HSD backing ownership");
    check(pool.release(b.address) == Status::ok && pool.allocate().address == b.address,
          "rejected backing-cell free leaves pool reuse intact");
    check(pool.initialize(21, 4) == Status::invalid_context,
          "unmodeled in-place HSD reset is rejected");
    const auto snapshot = heap.state();
    check(heap.restore(snapshot) == Status::ok, "explicit context replacement");
    check(pool.allocate().status == Status::missing_context &&
          pool.release(b.address) == Status::missing_context,
          "old pool cannot borrow a replacement heap with reused numeric addresses");
    check(pool.initialize(16, 4) == Status::ok && pool.state().used == 0 &&
          pool.state().peak == 0 && pool.state().free.empty() && pool.state().live.empty(),
          "explicit initialization in a new heap generation discards stale pool state");
    check(pool.allocate().status == Status::ok, "reinitialized owner can allocate in new context");
    Address retained;
    {
        ObjectPool temporary(heap);
        check(temporary.initialize(16, 4) == Status::ok, "temporary owner setup");
        retained = temporary.allocate().address;
    }
    check(heap.release(retained) == Status::invalid_request,
          "destroying model owner does not return original HSD backing blocks");
}

static void register_carry()
{
    RegisterWord unavailable;
    const auto intentional_zero = RegisterWord::from_scalar(0);
    check(!resolve_stick_carry(unavailable, intentional_zero), "missing r5 is not neutral");
    check(!resolve_stick_carry(intentional_zero, unavailable), "missing r30 is not neutral");
    const auto zero = resolve_stick_carry(intentional_zero, intentional_zero);
    check(zero && zero->x == 0 && zero->y == 0, "intentional zero remains available");
    for (std::uint32_t low = 0; low < 256; ++low) {
        const Address identity(0x12340000U | low); // synthetic, never a retail identity
        const auto word = RegisterWord::from_source_address(identity);
        const auto result = resolve_stick_carry(word, word);
        const int expected = low < 128 ? int(low) : int(low) - 256;
        check(result && result->x == expected && result->y == expected,
              "source command writer consumes signed low byte for every possible byte");
        auto clobbered = word;
        clobbered.invalidate();
        check(!resolve_stick_carry(clobbered, word), "unknown call clobber cannot retain guessed residue");
    }
}

int main()
{
    context_guards();
    pool_lifetime();
    register_carry();
    std::puts("Source address context and register guards: passed");
}
