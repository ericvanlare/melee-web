#include "gameplay_source_memory_runtime.h"

#include "source_address_context.hpp"
#include "source_game_heap_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <vector>


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

std::unique_ptr<Heap> source_address_heap = std::make_unique<Heap>();
uint64_t owner_epoch = 0;
int source_heap_handle = -1;
uint64_t world_generation = 0;
uint64_t next_generation = 0;
// Unlike the mirror checkpoint's deliberately rewindable allocation watermark,
// this revision never rewinds. It lets payload checkpoints reject an intervening
// alloc/free or mirror restore even if mirror metadata later returns to the same
// values.
uint64_t topology_revision = 0;
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

bool topology_revision_available()
{
    return topology_revision != std::numeric_limits<uint64_t>::max();
}

void advance_topology_revision()
{
    ++topology_revision;
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
    if (source_address_heap->create_empty(Address(begin), Address(end)) != Status::ok)
        return fail("GALE01r2 VS main heap rejected its derived bounds");
    if (++owner_epoch == 0) ++owner_epoch;
    source_heap_handle = heap_handle;
    world_generation = generation;
    topology_revision = 0;
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
    source_address_heap->clear();
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
    if (!topology_revision_available())
        return fail("source main heap topology revision exhausted");
    const uintptr_t host = reinterpret_cast<uintptr_t>(host_payload);
    for (const auto& existing : allocations) {
        const uintptr_t existing_end = existing.host + existing.requested;
        const uintptr_t requested_end = host + requested_bytes;
        if (host < existing_end && existing.host < requested_end)
            return fail("source main heap reused an overlapping live host allocation");
    }
    const auto result = source_address_heap->allocate(static_cast<uint32_t>(requested_bytes));
    if (result.status != Status::ok)
        return fail("source main heap rejected a live OSAllocFromHeap request");
    allocations.push_back({host, static_cast<uint32_t>(requested_bytes),
                           result.address, next_id()});
    advance_topology_revision();
    return 1;
}

extern "C" int melee_web_source_memory_free(int heap, void* host_payload)
{
    if (!active) return 1;
    if (heap != source_heap_handle) return 1;
    if (!host_payload) return fail("source main heap observed a null OS free");
    if (!topology_revision_available())
        return fail("source main heap topology revision exhausted");
    const uintptr_t host = reinterpret_cast<uintptr_t>(host_payload);
    const auto found = std::find_if(allocations.begin(), allocations.end(),
        [host](const HostAllocation& allocation) { return allocation.host == host; });
    if (found == allocations.end())
        return fail("source main heap free has no matching live allocation");
    if (source_address_heap->release(found->source) != Status::ok)
        return fail("source main heap rejected a live OSFreeToHeap request");
    allocations.erase(found);
    advance_topology_revision();
    return 1;
}

extern "C" int melee_web_source_memory_healthy(void)
{
    return !active || healthy;
}

extern "C" MeleeWebSourceMemoryReadStatus
melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* out)
{
    if (out) *out = {};
    if (!out) return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    if (!active) return MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE;
    if (!healthy) return MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY;
    out->source_heap_handle = source_heap_handle;
    out->world_generation = world_generation;
    out->allocation_generation_watermark = next_generation;
    return MELEE_WEB_SOURCE_MEMORY_READ_OK;
}

extern "C" MeleeWebSourceMemoryReadStatus
melee_web_source_memory_allocation_read(
    const void* exact_host_payload, MeleeWebSourceMemoryAllocation* out)
{
    if (out) *out = {};
    if (!out || !exact_host_payload)
        return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    if (!active) return MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE;
    if (!healthy) return MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY;

    out->source_heap_handle = source_heap_handle;
    out->world_generation = world_generation;
    const uintptr_t host = reinterpret_cast<uintptr_t>(exact_host_payload);
    const auto found = std::find_if(allocations.begin(), allocations.end(),
        [host](const HostAllocation& allocation) {
            return allocation.host == host;
        });
    if (found == allocations.end()) return MELEE_WEB_SOURCE_MEMORY_READ_OK;
    out->requested_bytes = found->requested;
    out->allocation_generation = found->generation;
    out->live = 1;
    return MELEE_WEB_SOURCE_MEMORY_READ_OK;
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


/* This owns mirror values only. No host allocation payload, SDK arena, source
 * global, or renderer object is copied or authorized by this interface. */
struct MeleeWebSourceMemorySnapshot {
    uint64_t epoch;
    int heap_handle;
    uint64_t world, watermark;
    melee_web::source::HeapState heap;
    std::vector<HostAllocation> allocations;
    std::vector<FighterLease> fighters;
};

struct MeleeWebSourceMemoryPayloadSnapshot {
    uint64_t magic;
    uint64_t epoch;
    uint64_t world;
    uint64_t watermark;
    uint64_t revision;
    int heap_handle;
    uint32_t arena_begin;
    uint32_t arena_end;
    uint32_t heap_bytes;
    size_t block_bytes;
    size_t row_count;
    size_t free_count;
    size_t allocated_count;
    size_t payload_bytes;
    size_t rows_offset;
    size_t free_offset;
    size_t allocated_offset;
    size_t payload_offset;
};

namespace {
constexpr uint64_t kPayloadSnapshotMagic = UINT64_C(0x4d57535041594c44);

struct PayloadSnapshotLayout {
    size_t rows_offset;
    size_t free_offset;
    size_t allocated_offset;
    size_t payload_offset;
    size_t block_bytes;
};

bool add_size(size_t a, size_t b, size_t* out)
{
    if (!out || b > std::numeric_limits<size_t>::max() - a) return false;
    *out = a + b;
    return true;
}

bool append_aligned(size_t count, size_t element_size, size_t alignment,
                    size_t* cursor, size_t* offset)
{
    if (!cursor || !offset || !alignment) return false;
    const size_t remainder = *cursor % alignment;
    size_t aligned = *cursor;
    if (remainder && !add_size(*cursor, alignment - remainder, &aligned))
        return false;
    if (element_size && count > (std::numeric_limits<size_t>::max() - aligned) /
                                   element_size)
        return false;
    *offset = aligned;
    *cursor = aligned + count * element_size;
    return true;
}

bool payload_layout(size_t rows, size_t free_cells, size_t allocated_cells,
                    size_t payload_bytes, PayloadSnapshotLayout* out)
{
    if (!out) return false;
    size_t cursor = sizeof(MeleeWebSourceMemoryPayloadSnapshot);
    PayloadSnapshotLayout value{};
    if (!append_aligned(rows, sizeof(HostAllocation), alignof(HostAllocation),
                        &cursor, &value.rows_offset) ||
        !append_aligned(free_cells, sizeof(melee_web::source::Cell),
                        alignof(melee_web::source::Cell), &cursor,
                        &value.free_offset) ||
        !append_aligned(allocated_cells, sizeof(melee_web::source::Cell),
                        alignof(melee_web::source::Cell), &cursor,
                        &value.allocated_offset))
        return false;
    value.payload_offset = cursor;
    if (!add_size(cursor, payload_bytes, &value.block_bytes)) return false;
    *out = value;
    return true;
}

bool host_interval(uintptr_t base, size_t bytes, uintptr_t* end)
{
    if (!base || !bytes || !end ||
        bytes > std::numeric_limits<uintptr_t>::max() - base)
        return false;
    *end = base + bytes;
    return true;
}

bool source_interval(uint32_t base, uint32_t bytes, uint64_t* end)
{
    if (!base || !bytes || !end) return false;
    const uint64_t limit = UINT64_C(1) << 32;
    *end = static_cast<uint64_t>(base) + bytes;
    return *end <= limit;
}

bool overlaps(uintptr_t a_begin, uintptr_t a_end,
              uintptr_t b_begin, uintptr_t b_end)
{
    return a_begin < b_end && b_begin < a_end;
}

bool validate_live_allocations(const std::vector<HostAllocation>& rows)
{
    for (size_t i = 0; i < rows.size(); ++i) {
        uintptr_t host_end = 0;
        uint64_t source_end = 0;
        if (!host_interval(rows[i].host, rows[i].requested, &host_end) ||
            !source_interval(rows[i].source.value(), rows[i].requested,
                             &source_end) ||
            !rows[i].generation)
            return false;
        for (size_t j = 0; j < i; ++j) {
            uintptr_t other_host_end = 0;
            uint64_t other_source_end = 0;
            if (!host_interval(rows[j].host, rows[j].requested,
                               &other_host_end) ||
                !source_interval(rows[j].source.value(), rows[j].requested,
                                 &other_source_end))
                return false;
            if (overlaps(rows[i].host, host_end, rows[j].host,
                         other_host_end) ||
                (rows[i].source.value() < other_source_end &&
                 rows[j].source.value() < source_end))
                return false;
        }
    }
    return true;
}

bool host_ranges_overlap_allocations(uintptr_t begin, uintptr_t end,
                                     const std::vector<HostAllocation>& rows)
{
    for (const auto& row : rows) {
        uintptr_t row_end = 0;
        if (!host_interval(row.host, row.requested, &row_end) ||
            overlaps(begin, end, row.host, row_end))
            return true;
    }
    return false;
}

const HostAllocation* payload_rows(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return reinterpret_cast<const HostAllocation*>(
        reinterpret_cast<const unsigned char*>(snapshot) + snapshot->rows_offset);
}

const melee_web::source::Cell* payload_free_cells(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return reinterpret_cast<const melee_web::source::Cell*>(
        reinterpret_cast<const unsigned char*>(snapshot) + snapshot->free_offset);
}

const melee_web::source::Cell* payload_allocated_cells(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return reinterpret_cast<const melee_web::source::Cell*>(
        reinterpret_cast<const unsigned char*>(snapshot) +
        snapshot->allocated_offset);
}

const unsigned char* payload_bytes(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return reinterpret_cast<const unsigned char*>(snapshot) +
           snapshot->payload_offset;
}

bool heap_state_matches(const melee_web::source::HeapState& current,
                        const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    if (current.arena_begin.value() != snapshot->arena_begin ||
        current.arena_end.value() != snapshot->arena_end ||
        current.heap_bytes != snapshot->heap_bytes ||
        current.free.size() != snapshot->free_count ||
        current.allocated.size() != snapshot->allocated_count)
        return false;
    const auto* saved_free = payload_free_cells(snapshot);
    const auto* saved_allocated = payload_allocated_cells(snapshot);
    for (size_t i = 0; i < current.free.size(); ++i) {
        if (current.free[i].start != saved_free[i].start ||
            current.free[i].bytes != saved_free[i].bytes)
            return false;
    }
    for (size_t i = 0; i < current.allocated.size(); ++i) {
        if (current.allocated[i].start != saved_allocated[i].start ||
            current.allocated[i].bytes != saved_allocated[i].bytes)
            return false;
    }
    return true;
}

bool snapshot_layout_valid(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    if (!snapshot || snapshot->magic != kPayloadSnapshotMagic ||
        snapshot->block_bytes < sizeof(*snapshot))
        return false;
    PayloadSnapshotLayout expected{};
    if (!payload_layout(snapshot->row_count, snapshot->free_count,
                        snapshot->allocated_count, snapshot->payload_bytes,
                        &expected))
        return false;
    if (snapshot->rows_offset != expected.rows_offset ||
        snapshot->free_offset != expected.free_offset ||
        snapshot->allocated_offset != expected.allocated_offset ||
        snapshot->payload_offset != expected.payload_offset ||
        snapshot->block_bytes != expected.block_bytes)
        return false;
    uintptr_t block_end = 0;
    return host_interval(reinterpret_cast<uintptr_t>(snapshot),
                         snapshot->block_bytes, &block_end);
}

MeleeWebSourceMemoryPayloadSnapshotStatus payload_snapshot_check(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    if (!snapshot)
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_ARGUMENT;
    if (!active)
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE;
    if (!healthy)
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY;
    if (snapshot->epoch != owner_epoch ||
        snapshot->heap_handle != source_heap_handle ||
        snapshot->world != world_generation)
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_STALE_OWNER;
    if (!snapshot_layout_valid(snapshot))
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE;
    uintptr_t block_end = 0;
    const uintptr_t block_begin = reinterpret_cast<uintptr_t>(snapshot);
    if (!host_interval(block_begin, snapshot->block_bytes, &block_end))
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE;
    if (!validate_live_allocations(allocations))
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE;
    if (host_ranges_overlap_allocations(block_begin, block_end, allocations))
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OVERLAPPING_STORAGE;
    if (snapshot->revision != topology_revision ||
        snapshot->watermark != next_generation ||
        snapshot->row_count != allocations.size())
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;
    size_t total = 0;
    const auto* saved_rows = payload_rows(snapshot);
    for (size_t i = 0; i < allocations.size(); ++i) {
        const auto& current = allocations[i];
        const auto& saved = saved_rows[i];
        if (current.host != saved.host || current.requested != saved.requested ||
            current.source != saved.source || current.generation != saved.generation)
            return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;
        if (current.requested > std::numeric_limits<size_t>::max() - total)
            return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE;
        total += current.requested;
    }
    if (total != snapshot->payload_bytes ||
        !heap_state_matches(source_address_heap->state(), snapshot))
        return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;
    return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;
}
}

extern "C" MeleeWebSourceMemorySnapshotStatus
melee_web_source_memory_snapshot_capture(MeleeWebSourceMemorySnapshot** out)
{
    if (!out) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_ARGUMENT;
    *out = nullptr;
    if (!active) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INACTIVE;
    if (!healthy) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_UNHEALTHY;
    try {
        auto snapshot = std::make_unique<MeleeWebSourceMemorySnapshot>(
            MeleeWebSourceMemorySnapshot{owner_epoch, source_heap_handle,
                world_generation, next_generation, source_address_heap->state(),
                allocations, fighters});
        *out = snapshot.release();
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK;
    } catch (const std::bad_alloc&) {
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_ALLOCATION_FAILED;
    }
}

extern "C" MeleeWebSourceMemorySnapshotStatus
melee_web_source_memory_snapshot_admit(const MeleeWebSourceMemorySnapshot* snapshot)
{
    if (!snapshot) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_ARGUMENT;
    if (!active) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INACTIVE;
    if (!healthy) return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_UNHEALTHY;
    if (snapshot->epoch != owner_epoch ||
        snapshot->heap_handle != source_heap_handle ||
        snapshot->world != world_generation)
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_STALE_OWNER;
    return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK;
}

extern "C" MeleeWebSourceMemorySnapshotStatus
melee_web_source_memory_snapshot_restore(const MeleeWebSourceMemorySnapshot* snapshot)
{
    const auto admission = melee_web_source_memory_snapshot_admit(snapshot);
    if (admission != MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK) return admission;
    if (!topology_revision_available())
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_STATE;
    try {
        // All validation and allocation precedes the first live-owner write.
        // This private Heap has no ObjectPool users: its internal generation is
        // not an externally retained identity. World/allocation IDs are restored
        // explicitly below, with the lifetime epoch deliberately unchanged.
        auto heap = std::make_unique<Heap>();
        if (heap->restore(snapshot->heap) != Status::ok)
            return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_INVALID_STATE;
        auto candidate_allocations = snapshot->allocations;
        auto candidate_fighters = snapshot->fighters;
        source_address_heap.swap(heap);
        allocations.swap(candidate_allocations);
        fighters.swap(candidate_fighters);
        next_generation = snapshot->watermark;
        advance_topology_revision();
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_OK;
    } catch (const std::bad_alloc&) {
        return MELEE_WEB_SOURCE_MEMORY_SNAPSHOT_ALLOCATION_FAILED;
    }
}

extern "C" MeleeWebSourceMemoryPayloadSnapshotCaptureResult
melee_web_source_memory_payload_snapshot_capture(void)
{
    if (!active)
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INACTIVE, nullptr};
    if (!healthy)
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_UNHEALTHY, nullptr};
    if (!validate_live_allocations(allocations))
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE, nullptr};

    const auto& heap = source_address_heap->state();
    size_t payload_size = 0;
    for (const auto& allocation : allocations) {
        if (allocation.requested >
            std::numeric_limits<size_t>::max() - payload_size)
            return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE,
                    nullptr};
        payload_size += allocation.requested;
    }
    PayloadSnapshotLayout layout{};
    if (!payload_layout(allocations.size(), heap.free.size(),
                        heap.allocated.size(), payload_size, &layout))
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE, nullptr};

    void* storage = nullptr;
    try {
        storage = ::operator new(layout.block_bytes);
    } catch (const std::bad_alloc&) {
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_ALLOCATION_FAILED,
                nullptr};
    }
    if (!storage)
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_ALLOCATION_FAILED,
                nullptr};
    uintptr_t storage_end = 0;
    const uintptr_t storage_begin = reinterpret_cast<uintptr_t>(storage);
    if (!host_interval(storage_begin, layout.block_bytes, &storage_end)) {
        ::operator delete(storage);
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE, nullptr};
    }
    if (host_ranges_overlap_allocations(storage_begin, storage_end, allocations)) {
        // A conforming operator new cannot overlap a live allocation. If an
        // allocator violates that contract, do not delete this possibly aliased
        // address: deletion could free or corrupt the live owner.
        return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OVERLAPPING_STORAGE,
                nullptr};
    }

    auto* snapshot = new (storage) MeleeWebSourceMemoryPayloadSnapshot{};
    snapshot->magic = kPayloadSnapshotMagic;
    snapshot->epoch = owner_epoch;
    snapshot->world = world_generation;
    snapshot->watermark = next_generation;
    snapshot->revision = topology_revision;
    snapshot->heap_handle = source_heap_handle;
    snapshot->arena_begin = heap.arena_begin.value();
    snapshot->arena_end = heap.arena_end.value();
    snapshot->heap_bytes = heap.heap_bytes;
    snapshot->block_bytes = layout.block_bytes;
    snapshot->row_count = allocations.size();
    snapshot->free_count = heap.free.size();
    snapshot->allocated_count = heap.allocated.size();
    snapshot->payload_bytes = payload_size;
    snapshot->rows_offset = layout.rows_offset;
    snapshot->free_offset = layout.free_offset;
    snapshot->allocated_offset = layout.allocated_offset;
    snapshot->payload_offset = layout.payload_offset;

    auto* bytes = static_cast<unsigned char*>(storage);
    auto* rows = reinterpret_cast<HostAllocation*>(bytes + layout.rows_offset);
    for (size_t i = 0; i < allocations.size(); ++i)
        new (&rows[i]) HostAllocation(allocations[i]);
    auto* free_cells = reinterpret_cast<melee_web::source::Cell*>(
        bytes + layout.free_offset);
    for (size_t i = 0; i < heap.free.size(); ++i)
        new (&free_cells[i]) melee_web::source::Cell(heap.free[i]);
    auto* allocated_cells = reinterpret_cast<melee_web::source::Cell*>(
        bytes + layout.allocated_offset);
    for (size_t i = 0; i < heap.allocated.size(); ++i)
        new (&allocated_cells[i]) melee_web::source::Cell(heap.allocated[i]);

    size_t payload_offset = 0;
    for (const auto& allocation : allocations) {
        std::memcpy(bytes + layout.payload_offset + payload_offset,
                    reinterpret_cast<const void*>(allocation.host),
                    allocation.requested);
        payload_offset += allocation.requested;
    }
    return {MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK, snapshot};
}

extern "C" MeleeWebSourceMemoryPayloadSnapshotStatus
melee_web_source_memory_payload_snapshot_admit(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return payload_snapshot_check(snapshot);
}

extern "C" MeleeWebSourceMemoryPayloadSnapshotStatus
melee_web_source_memory_payload_snapshot_restore(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    const auto status = payload_snapshot_check(snapshot);
    if (status != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) return status;

    const auto* saved_rows = payload_rows(snapshot);
    const auto* saved_bytes = payload_bytes(snapshot);
    size_t payload_offset = 0;
    for (size_t i = 0; i < allocations.size(); ++i) {
        const auto& allocation = allocations[i];
        std::memcpy(reinterpret_cast<void*>(allocation.host),
                    saved_bytes + payload_offset, allocation.requested);
        payload_offset += saved_rows[i].requested;
    }
    return MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK;
}

extern "C" void melee_web_source_memory_payload_snapshot_destroy(
    MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    if (!snapshot) return;
    snapshot->~MeleeWebSourceMemoryPayloadSnapshot();
    ::operator delete(snapshot);
}

extern "C" void melee_web_source_memory_snapshot_destroy(
    MeleeWebSourceMemorySnapshot* snapshot)
{
    delete snapshot;
}
