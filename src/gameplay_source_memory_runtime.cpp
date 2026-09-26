#include "gameplay_source_memory_runtime.h"

#include "source_address_context.hpp"
#include "source_game_heap_context.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
EM_JS(void, trace_source_fighter_owner,
      (uint32_t host, uint32_t source, uint32_t allocation_host,
       uint32_t allocation_source, uint32_t requested, uint32_t offset), {
    if (typeof window !== 'undefined' &&
        Array.isArray(window.__meleeSourceOwnerTrace)) {
        window.__meleeSourceOwnerTrace.push({
            host: host >>> 0,
            source: source >>> 0,
            allocation_host: allocation_host >>> 0,
            allocation_source: allocation_source >>> 0,
            requested: requested >>> 0,
            offset: offset >>> 0,
        });
    }
});
EM_JS(void, trace_source_main_alloc,
      (int operation, int heap, uint32_t host, uint32_t source,
       uint32_t requested, uint32_t generation), {
    if (typeof window !== 'undefined') {
        const capacity = 16384;
        const sequence = window.__meleeSourceAllocationTraceTotal || 0;
        window.__meleeSourceAllocationTraceTotal = sequence + 1;
        if (Array.isArray(window.__meleeSourceAllocationTrace)) {
            const event = {
                sequence: sequence,
                operation: operation,
                heap: heap,
                host: host >>> 0,
                source: source >>> 0,
                requested: requested >>> 0,
                generation: generation >>> 0,
            };
            if (window.__meleeSourceAllocationTrace.length < capacity)
                window.__meleeSourceAllocationTrace.push(event);
            else
                window.__meleeSourceAllocationTrace[sequence % capacity] = event;
        }
    }
});
#else
static void trace_source_fighter_owner(uint32_t, uint32_t, uint32_t,
                                       uint32_t, uint32_t, uint32_t) {}
static void trace_source_main_alloc(int, int, uint32_t, uint32_t, uint32_t,
                                    uint32_t) {}
#endif

namespace {
using melee_web::source::Address;
using melee_web::source::Heap;
using melee_web::source::Status;

// GALE01r2 startup profile: OS main-memory roots recovered from the pinned
// boot/memory code and the 24 MiB MEM1 layout. The VS preload-3 state retains
// heaps 2 and 3, so lbHeap_80015900 derives the replacement main HSD bounds.
// This is startup configuration; register values from comparison captures are
// never consumed here.
constexpr melee_web::source_game_heap::Bounds kBootBounds{
    melee_web::source_game_heap::Address(0x806dcc40u),
    melee_web::source_game_heap::Address(0x817f8ac0u),
    melee_web::source_game_heap::Address(0x00629ea0u),
    melee_web::source_game_heap::Address(0x01629ea0u),
};
constexpr melee_web::source_game_heap::Descriptor kDescriptors[] = {
    {2, 1, 6, 0x00000800u},
    {3, 1, 2, 0x004f8800u},
    {4, 2, 6, 0x0064b400u},
    {5, 4, 6, 0x0096c800u},
};

struct HostAllocation {
    uintptr_t host = 0;
    uint32_t requested = 0;
    Address source;
    uint64_t generation = 0;
};

struct FighterLease {
    uintptr_t host = 0;
    uint32_t source = 0;
    uint64_t world_generation = 0;
    uint64_t allocation_generation = 0;
    bool live = false;
};

Heap source_address_heap;
int source_heap_handle = -1;
uint64_t world_generation = 0;
uint64_t next_generation = 0;
bool active = false;
bool healthy = true;
std::string failure;
std::vector<HostAllocation> allocations;
std::vector<FighterLease> fighters;

int fail(const char* why)
{
    healthy = false;
    if (failure.empty()) failure = why;
    return 0;
}

uint64_t next_id()
{
    if (++next_generation == 0) ++next_generation;
    return next_generation;
}

bool derive_vs_main_heap(uint32_t* out_begin, uint32_t* out_end)
{
    using namespace melee_web::source_game_heap;
    Context heaps;
    std::vector<Descriptor> descriptors(std::begin(kDescriptors),
                                        std::end(kDescriptors));
    if (heaps.initialize(kBootBounds, descriptors) !=
            melee_web::source_game_heap::Status::ok ||
        heaps.set_transient(2, 0) != melee_web::source_game_heap::Status::ok ||
        heaps.set_transient(3, 0) != melee_web::source_game_heap::Status::ok ||
        heaps.begin_rebuild() != melee_web::source_game_heap::Status::ok)
        return false;
    const auto request = heaps.next_request();
    if (!request || request->kind != RequestKind::replace_hsd_main ||
        request->lo.value() >= request->hi.value())
        return false;
    *out_begin = request->lo.value();
    *out_end = request->hi.value();
    return true;
}

HostAllocation* find_host_allocation(uintptr_t pointer)
{
    for (auto& allocation : allocations) {
        if (pointer >= allocation.host &&
            pointer - allocation.host < allocation.requested)
            return &allocation;
    }
    return nullptr;
}

FighterLease* find_fighter(uintptr_t host)
{
    const auto found = std::find_if(fighters.begin(), fighters.end(),
        [host](const FighterLease& lease) { return lease.host == host; });
    return found == fighters.end() ? nullptr : &*found;
}
}

extern "C" int melee_web_source_memory_begin(uint64_t generation,
                                              int heap_handle)
{
    if (!generation || heap_handle < 0 || active)
        return fail("source address heap cannot begin with this world/heap owner");
    uint32_t begin = 0, end = 0;
    if (!derive_vs_main_heap(&begin, &end))
        return fail("GALE01r2 VS main heap bounds could not be derived");
    if (source_address_heap.create_empty(Address(begin), Address(end)) != Status::ok)
        return fail("GALE01r2 VS main heap rejected its derived bounds");
    source_heap_handle = heap_handle;
    world_generation = generation;
    active = true;
    healthy = true;
    failure.clear();
    allocations.clear();
    fighters.clear();
    return 1;
}

extern "C" int melee_web_source_memory_end(uint64_t generation)
{
    if (!active || !generation || generation != world_generation)
        return fail("source address heap world owner changed during teardown");
    active = false;
    source_heap_handle = -1;
    world_generation = 0;
    allocations.clear();
    fighters.clear();
    source_address_heap.clear();
    return healthy ? 1 : 0;
}

extern "C" int melee_web_source_memory_alloc(int heap, void* host_payload,
                                              size_t requested_bytes)
{
    if (!active) return 1;
    if (heap != source_heap_handle) return 1;
    if (!host_payload || !requested_bytes ||
        requested_bytes > std::numeric_limits<uint32_t>::max())
        return fail("source main heap observed an invalid host allocation");
    const uintptr_t host = reinterpret_cast<uintptr_t>(host_payload);
    for (const auto& existing : allocations) {
        const uintptr_t existing_end = existing.host + existing.requested;
        const uintptr_t requested_end = host + requested_bytes;
        if (host < existing_end && existing.host < requested_end)
            return fail("source main heap reused an overlapping live host allocation");
    }
    const auto result = source_address_heap.allocate(static_cast<uint32_t>(requested_bytes));
    if (result.status != Status::ok)
        return fail("source main heap rejected a live OSAllocFromHeap request");
    allocations.push_back({host, static_cast<uint32_t>(requested_bytes),
                           result.address, next_id()});
    trace_source_main_alloc(1, heap, static_cast<uint32_t>(host),
                            result.address.value(),
                            static_cast<uint32_t>(requested_bytes),
                            static_cast<uint32_t>(world_generation));
    return 1;
}

extern "C" int melee_web_source_memory_free(int heap, void* host_payload)
{
    if (!active) return 1;
    if (heap != source_heap_handle) return 1;
    if (!host_payload) return fail("source main heap observed a null OS free");
    const uintptr_t host = reinterpret_cast<uintptr_t>(host_payload);
    const auto found = std::find_if(allocations.begin(), allocations.end(),
        [host](const HostAllocation& allocation) { return allocation.host == host; });
    if (found == allocations.end())
        return fail("source main heap free has no matching live allocation");
    if (source_address_heap.release(found->source) != Status::ok)
        return fail("source main heap rejected a live OSFreeToHeap request");
    trace_source_main_alloc(0, heap, static_cast<uint32_t>(host),
                            found->source.value(), found->requested,
                            static_cast<uint32_t>(world_generation));
    allocations.erase(found);
    return 1;
}

extern "C" int melee_web_source_memory_healthy(void)
{
    return !active || healthy;
}

extern "C" int melee_web_source_memory_fighter_acquire(
    void* host_fighter, size_t fighter_bytes,
    MeleeWebSourceFighterAddress* out)
{
    if (out) *out = {};
    if (!active || !healthy || !host_fighter || !out || !fighter_bytes ||
        fighter_bytes > std::numeric_limits<uint32_t>::max())
        return fail("source Fighter identity requires a healthy live memory context");
    const uintptr_t host = reinterpret_cast<uintptr_t>(host_fighter);
    HostAllocation* allocation = find_host_allocation(host);
    if (!allocation || host + fighter_bytes < host ||
        host + fighter_bytes > allocation->host + allocation->requested)
        return fail("source Fighter does not belong to a live mirrored main-heap allocation");
    FighterLease* lease = find_fighter(host);
    if (lease && lease->live)
        return fail("source Fighter host owner was acquired twice without release");
    const uint64_t lifetime = next_id();
    if (!lease) {
        fighters.push_back({host, allocation->source.value() +
                            static_cast<uint32_t>(host - allocation->host),
                            world_generation, lifetime, true});
        lease = &fighters.back();
    } else {
        lease->source = allocation->source.value() +
                        static_cast<uint32_t>(host - allocation->host);
        lease->world_generation = world_generation;
        lease->allocation_generation = lifetime;
        lease->live = true;
    }
    out->source_address = lease->source;
    out->world_generation = lease->world_generation;
    out->allocation_generation = lease->allocation_generation;
    out->live = 1;
    trace_source_fighter_owner(
        static_cast<uint32_t>(host), lease->source,
        static_cast<uint32_t>(allocation->host), allocation->source.value(),
        allocation->requested,
        static_cast<uint32_t>(host - allocation->host));
    return 1;
}

extern "C" int melee_web_source_memory_fighter_read(
    void* host_fighter, MeleeWebSourceFighterAddress* out)
{
    if (out) *out = {};
    if (!active || !healthy || !host_fighter || !out)
        return fail("source Fighter lookup requires a healthy live context");
    FighterLease* lease = find_fighter(reinterpret_cast<uintptr_t>(host_fighter));
    if (!lease || !lease->live || lease->world_generation != world_generation)
        return 0;
    out->source_address = lease->source;
    out->world_generation = lease->world_generation;
    out->allocation_generation = lease->allocation_generation;
    out->live = 1;
    return 1;
}

extern "C" int melee_web_source_memory_fighter_release(void* host_fighter)
{
    if (!active || !healthy || !host_fighter)
        return fail("source Fighter release requires a healthy live context");
    FighterLease* lease = find_fighter(reinterpret_cast<uintptr_t>(host_fighter));
    if (!lease || !lease->live || lease->world_generation != world_generation)
        return fail("source Fighter release has no matching live allocation lease");
    lease->live = false;
    return 1;
}
