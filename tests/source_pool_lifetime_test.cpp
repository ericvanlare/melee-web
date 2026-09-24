#include "source_address_context.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace melee_web::source;

static void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto base = static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 0));
    Heap primary, selected;
    check(primary.create_empty(Address(base + 0x20), Address(base + 0x5000)) == Status::ok,
          "primary heap setup");
    check(selected.create_empty(Address(base + 0x5000), Address(base + 0x10000)) == Status::ok,
          "selected heap setup");

    ObjectPool pool(primary);
    check(pool.initialize(24, 4) == Status::ok, "ordinary pool initialization");
    check(pool.initialize(24, 4) == Status::invalid_context,
          "ordinary initialize retains the same-generation guard");
    const auto selected_before_adopt = *selected.free_bytes();
    const auto backing = selected.allocate(pool.state().size * 2);
    check(backing.status == Status::ok, "nested OS allocation for adoption");
    check(pool.adopt_backing(selected, backing.address, 2) == Status::ok,
          "adopt an already replayed OS allocation");
    check(*selected.free_bytes() < selected_before_adopt,
          "adoption does not allocate a second OS cell");
    check(pool.adopt_backing(selected, backing.address, 2) == Status::invalid_request,
          "duplicate adoption is rejected");
    const auto old = pool.allocate_existing();
    check(old.status == Status::ok && selected.free_bytes() && primary.free_bytes(),
          "selected refill allocation");
    const auto selected_free = *selected.free_bytes();

    check(pool.reset(24, 4) == Status::ok && pool.state().used == 0 &&
          pool.state().free.empty() && pool.state().live.empty(),
          "reset discards only descriptor chains");
    check(pool.release(old.address) == Status::unknown_allocation,
          "reset rejects a stale live object");
    check(*selected.free_bytes() == selected_free,
          "reset retains the backing OS cell");
    check(pool.allocate(selected).status == Status::ok,
          "reset can refill from the same heap generation");

    selected.clear();
    check(selected.create_empty(Address(base + 0x5000), Address(base + 0x10000)) == Status::ok,
          "recreated selected heap context");
    check(pool.allocate(selected).status == Status::missing_context,
          "unreset descriptor rejects stale pool backing generation");
    check(pool.reset(24, 4) == Status::ok, "reset drops replaced-heap backing metadata");
    check(pool.allocate(selected).status == Status::ok,
          "one reset permits refill after heap recreation");

    check(pool.reset(24, 4) == Status::ok, "clear descriptor before pop-only guard");
    check(pool.allocate_existing().status == Status::exhausted,
          "pop-only allocation never performs an implicit refill");

    Heap no_alloc;
    check(no_alloc.create_empty(Address(base + 0x11000), Address(base + 0x15000)) == Status::ok,
          "no-allocation heap setup");
    ObjectPool no_alloc_pool(no_alloc);
    check(no_alloc_pool.initialize(32, 4) == Status::ok, "no-allocation pool setup");
    const auto snapshot = no_alloc.state();
    check(no_alloc.restore(snapshot) == Status::ok, "replace an untouched heap context");
    check(no_alloc_pool.allocate(no_alloc).status == Status::ok,
          "generation change without backing does not imply stale ownership");

    Registry registry;
    const Address descriptor1(base + 0x12000), descriptor2(base + 0x12040),
        descriptor3(base + 0x12080);
    check(registry.initialize(descriptor1) == Status::ok &&
          registry.head() && *registry.head() == descriptor1 &&
          registry.known(descriptor1) && !registry.next(descriptor1),
          "registry inserts the first descriptor");
    check(registry.initialize(descriptor2) == Status::ok &&
          registry.head() && *registry.head() == descriptor2 &&
          registry.next(descriptor2) && *registry.next(descriptor2) == descriptor1,
          "registry links new descriptors at the head");
    check(registry.initialize(descriptor1) == Status::ok &&
          registry.head() && *registry.head() == descriptor1 &&
          registry.next(descriptor1) && *registry.next(descriptor1) == descriptor2 &&
          !registry.next(descriptor2),
          "reinitialization removes then reinserts one descriptor");
    registry.forget_memory();
    check(!registry.head() && registry.next(descriptor1) &&
          *registry.next(descriptor1) == descriptor2,
          "forget clears only the global head and preserves descriptor next");
    check(registry.initialize(descriptor3) == Status::ok &&
          registry.head() && *registry.head() == descriptor3 &&
          !registry.next(descriptor3) && registry.next(descriptor1) &&
          *registry.next(descriptor1) == descriptor2,
          "post-forget initialization starts a new list while stale links remain");

    std::puts("Source pool lifetime and selected-refill guards: passed");
}
