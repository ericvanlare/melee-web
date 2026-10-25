#include "gameplay_source_memory_runtime.h"

#include "source_address_context.hpp"
#include "source_game_heap_context.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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

#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
struct PoolAssociation {
    uint32_t state=0; // 0 unknown, 1 observed, 2 poisoned
    uintptr_t descriptor=0;
    uint32_t stride=0,count=0,alignment_mask=0,pool_bytes=0;
    uint64_t generation=0,epoch=0,world=0;
};
// Diagnostic lifetime only: never copied/restored by either snapshot kind.
// reason: 1 hook inactive, 2 missing event, 3 target, 4 mask, 5 extent,
// 6 duplicate/conflict, 7 internal heap unsupported, 8 stale metadata.
uint32_t pool_refusal=0;
struct AllocationOrigin { const char* file=nullptr; uint32_t line=0,kind=0;
    uint64_t generation=0,epoch=0,world=0; };
uint32_t origin_refusal=0;
#endif
struct HostAllocation {
    uintptr_t host = 0;
    uint32_t requested = 0;
    Address source;
    uint64_t generation = 0;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    PoolAssociation pool{};
    AllocationOrigin origin{};
#endif
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

#include "gameplay_class_piece_diagnostic.inc"

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
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    pool_refusal = 0;
    origin_refusal = 0;
#endif
    active = true;
    healthy = true;
    failure.clear();
    allocations.clear();
    fighters.clear();
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    piece_init(begin,end);
#endif
    return 1;
}

extern "C" int melee_web_source_memory_end(uint64_t generation)
{
    if (!active || !generation || generation != world_generation)
        return fail("source address heap world owner changed during teardown");
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    piece_end();
#endif
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
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    piece_clear_row(allocations.back());
#endif
    advance_topology_revision();
    return 1;
}

extern "C" void melee_web_source_memory_objalloc_event(
    const void* pool,const void* descriptor,uint32_t stride,uint32_t count,
    uint32_t alignment_mask,uint32_t pool_bytes,uint32_t internal_heap)
{
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(!active)return; // original bootstrap outside the tracked owner
    HostAllocation* row=nullptr;
    const auto host=reinterpret_cast<uintptr_t>(pool);
    for(auto& candidate:allocations)if(candidate.host==host){row=&candidate;break;}
    auto refuse=[&](uint32_t reason){
        if(!pool_refusal)pool_refusal=reason;
        if(row){row->pool={};row->pool.state=2;}
    };
    if(internal_heap){refuse(7);return;}
    if(!healthy||!row||!descriptor){refuse(3);return;}
    if(alignment_mask==UINT32_MAX||(alignment_mask&(alignment_mask+1u))||
       !stride||(stride&alignment_mask)||(host&alignment_mask)){refuse(4);return;}
    const uint64_t bytes=static_cast<uint64_t>(stride)*count;
    if(!count||bytes>UINT32_MAX||bytes!=pool_bytes||row->requested!=pool_bytes){refuse(5);return;}
    // One real allocation event per generation. Even an identical duplicate
    // is refused; no copied metadata is permitted to simulate a new event.
    if(row->pool.state){refuse(6);return;}
    if(pool_refusal)return;
    row->pool={1,reinterpret_cast<uintptr_t>(descriptor),stride,count,alignment_mask,
               pool_bytes,row->generation,owner_epoch,world_generation};
#else
    (void)pool;(void)descriptor;(void)stride;(void)count;(void)alignment_mask;
    (void)pool_bytes;(void)internal_heap;
#endif
}

extern "C" void melee_web_source_memory_origin_event(const void* payload,uint32_t requested,
    const char* file,uint32_t line,uint32_t kind)
{
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(!active||!payload)return;
    HostAllocation* row=nullptr;
    for(auto& a:allocations)if(a.host==reinterpret_cast<uintptr_t>(payload)){row=&a;break;}
    auto refuse=[&](uint32_t reason){if(!origin_refusal)origin_refusal=reason;if(row)row->origin={};};
    if(!healthy||!row||row->requested!=requested){refuse(3);return;}
    size_t length=0;if(file)while(length<232&&file[length])++length;
    if(!file||!length||length>=232||!line||(kind!=1&&kind!=2)){refuse(4);return;}
    if(row->origin.file){refuse(5);return;}
    if(origin_refusal)return;
    row->origin={file,line,kind,row->generation,owner_epoch,world_generation};
#else
    (void)payload;(void)requested;(void)file;(void)line;(void)kind;
#endif
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
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    piece_clear_row(*found);
#endif
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
        // A mirror copy is not an allocation event. Never revive provenance
        // from copied rows; aggregate poison deliberately remains sticky.
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
        for(auto& row:candidate_allocations){row.pool={};row.origin={};}
#endif
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
        if(piece_topology(10))piece_refuse(8,10);
#endif
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

extern "C" MeleeWebSourcePayloadComparison
melee_web_source_memory_payload_snapshot_compare_live(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    const auto status = payload_snapshot_check(snapshot);
    if (status != MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
        return {1,static_cast<uint32_t>(status),SIZE_MAX,SIZE_MAX,0,0,0,0,0};
    const auto* saved_rows = payload_rows(snapshot);
    const auto* saved_bytes = payload_bytes(snapshot);
    size_t offset = 0;
    for (size_t i = 0; i < allocations.size(); ++i) {
        const auto& allocation = allocations[i];
        const auto* actual = reinterpret_cast<const unsigned char*>(allocation.host);
        for (size_t j = 0; j < saved_rows[i].requested; ++j) {
            const auto expected = static_cast<unsigned char>(saved_bytes[offset+j]);
            if (actual[j] != expected)
                return {2,0,i,j,allocation.host,allocation.source.value(),
                        allocation.requested,expected,actual[j]};
        }
        offset += saved_rows[i].requested;
    }
    return {0,0,SIZE_MAX,SIZE_MAX,0,0,0,0,0};
}
static_assert(sizeof(MeleeWebSourcePayloadWitness)==168);
extern "C" MeleeWebSourcePayloadWitness
melee_web_source_memory_payload_snapshot_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    MeleeWebSourcePayloadWitness result{};
    result.schema=1;result.bytes=sizeof(result);
    // Reuse the actual strict comparator, including all admission checks. No
    // live pointer is dereferenced or saved byte read before it succeeds.
    const auto difference=melee_web_source_memory_payload_snapshot_compare_live(snapshot);
    result.status=difference.status;result.admission_status=difference.admission_status;
    if(difference.status!=2)return result;
    const auto refuse=[&](){
        MeleeWebSourcePayloadWitness value{};value.schema=1;value.bytes=sizeof(value);
        value.status=1;value.admission_status=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_INVALID_STATE;
        return value;
    };
    const size_t row=difference.allocation_index,offset=difference.byte_offset;
    if(row>=allocations.size()||row>UINT32_MAX||offset>UINT32_MAX)return refuse();
    const auto& allocation=allocations[row];
    if(offset>=allocation.requested)return refuse();
    size_t saved_offset=0;
    const auto* saved_rows=payload_rows(snapshot);
    for(size_t i=0;i<row;++i)
        if(!add_size(saved_offset,saved_rows[i].requested,&saved_offset))return refuse();
    // Start up to eight bytes before the exact first difference. Subtraction
    // and clamped extent avoid wrapping at either end of a tiny allocation.
    const size_t start=offset>8?offset-8:0;
    const size_t count=std::min<size_t>(32,allocation.requested-start);
    size_t begin=0,end=0;
    if(!add_size(saved_offset,start,&begin)||!add_size(begin,count,&end)||
       end>snapshot->payload_bytes)return refuse();
    result.row=static_cast<uint32_t>(row);result.offset=static_cast<uint32_t>(offset);
    result.host_address=allocation.host;result.source_address=allocation.source.value();
    result.requested_bytes=allocation.requested;result.allocation_generation=allocation.generation;
    result.owner_epoch=owner_epoch;result.world_generation=world_generation;
    result.topology_revision=topology_revision;
    result.window_offset=static_cast<uint32_t>(start);result.window_count=static_cast<uint32_t>(count);
    // No typed attribution: FighterLease lifetime is independent of this
    // allocation generation and can remain live after host allocation reuse.
    std::memcpy(result.expected,payload_bytes(snapshot)+begin,count);
    std::memcpy(result.actual,reinterpret_cast<const unsigned char*>(allocation.host)+start,count);
    return result;
}
static_assert(sizeof(MeleeWebSourcePayloadFailureWitness)==256);
extern "C" MeleeWebSourcePayloadFailureWitness
melee_web_source_memory_payload_failure_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot,MeleeWebSourcePayloadComparison original)
{
    MeleeWebSourcePayloadFailureWitness result{};result.schema=1;result.bytes=sizeof(result);
    result.original_status=original.status;
    result.window=melee_web_source_memory_payload_snapshot_witness(snapshot);
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    result.hook_active=1;
#endif
    const auto& w=result.window;
    // This must describe the ORIGINAL synchronous first difference. A later
    // result or malformed caller comparison cannot grant pointer authority.
    if(original.status!=2||w.status!=2||original.allocation_index!=w.row||
       original.byte_offset!=w.offset||original.host_address!=w.host_address||
       original.source_address!=w.source_address||original.requested_bytes!=w.requested_bytes||
       w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
       original.expected_byte!=w.expected[w.offset-w.window_offset]||
       original.actual_byte!=w.actual[w.offset-w.window_offset]){result.reason=9;return result;}
    result.first_difference_matches=1;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(pool_refusal){result.attribution=2;result.reason=pool_refusal;return result;}
    const auto& live=allocations[w.row].pool;
    const auto& saved=payload_rows(snapshot)[w.row].pool;
    result.association_state=live.state;
    if(!live.state){result.reason=2;return result;}
    if(live.state!=1||live.generation!=w.allocation_generation||live.epoch!=owner_epoch||
       live.world!=world_generation||live.state!=saved.state||live.descriptor!=saved.descriptor||
       live.stride!=saved.stride||live.count!=saved.count||live.alignment_mask!=saved.alignment_mask||
       live.pool_bytes!=saved.pool_bytes||live.generation!=saved.generation||
       live.epoch!=saved.epoch||live.world!=saved.world||!live.stride){
        result.attribution=2;result.reason=8;return result;
    }
    result.attribution=1;result.descriptor=live.descriptor;
    result.allocation_generation=live.generation;result.owner_epoch=live.epoch;result.world_generation=live.world;
    result.stride=live.stride;result.count=live.count;result.alignment_mask=live.alignment_mask;
    result.pool_bytes=live.pool_bytes;result.cell_index=w.offset/live.stride;result.cell_offset=w.offset%live.stride;
#else
    result.reason=1;
#endif
    return result;
}
static_assert(sizeof(MeleeWebSourceAllocationOriginWitness)==320);
extern "C" MeleeWebSourceAllocationOriginWitness melee_web_source_memory_allocation_origin_witness(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot,MeleeWebSourcePayloadComparison original)
{
    MeleeWebSourceAllocationOriginWitness r{};r.schema=1;r.bytes=sizeof(r);
    const auto w=melee_web_source_memory_payload_snapshot_witness(snapshot);
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    r.hook_active=1;
#endif
    if(original.status!=2||w.status!=2||original.allocation_index!=w.row||original.byte_offset!=w.offset||
       original.host_address!=w.host_address||original.source_address!=w.source_address||
       original.requested_bytes!=w.requested_bytes||w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
       original.expected_byte!=w.expected[w.offset-w.window_offset]||original.actual_byte!=w.actual[w.offset-w.window_offset]){
        r.reason=7;return r;}
    r.first_difference_matches=1;r.row=w.row;r.offset=w.offset;r.host=w.host_address;
    r.generation=w.allocation_generation;r.epoch=w.owner_epoch;r.world=w.world_generation;
    r.source=w.source_address;r.requested=w.requested_bytes;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
    if(origin_refusal){r.status=2;r.reason=origin_refusal;return r;}
    const auto& a=allocations[w.row].origin;const auto& b=payload_rows(snapshot)[w.row].origin;
    if(!a.file){r.reason=2;return r;}
    if(a.file!=b.file||a.line!=b.line||a.kind!=b.kind||a.generation!=r.generation||a.epoch!=r.epoch||a.world!=r.world||
       a.generation!=b.generation||a.epoch!=b.epoch||a.world!=b.world){r.status=2;r.reason=6;return r;}
    size_t length=0;while(length<sizeof(r.path)&&a.file[length])++length;
    if(!length||length>=sizeof(r.path)){r.status=2;r.reason=4;return r;}
    r.status=1;r.site_kind=a.kind;r.line=a.line;r.path_bytes=length;
    std::memcpy(r.path,a.file,length);
#else
    r.reason=1;
#endif
    return r;
}
extern "C" int melee_web_source_memory_payload_snapshot_matches_live(
    const MeleeWebSourceMemoryPayloadSnapshot* snapshot)
{
    return melee_web_source_memory_payload_snapshot_compare_live(snapshot).status==0;
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

extern "C" MeleeWebClassPieceWitness melee_web_source_memory_class_piece_witness(
 const MeleeWebSourceMemoryPayloadSnapshot* snapshot,MeleeWebSourcePayloadComparison original,MeleeWebClassPieceAnchor anchor) {
 MeleeWebClassPieceWitness r{};r.schema=1;r.bytes=sizeof(r);
 const auto w=melee_web_source_memory_payload_snapshot_witness(snapshot);
 if(original.status!=2||w.status!=2||original.allocation_index!=w.row||original.byte_offset!=w.offset||
    original.host_address!=w.host_address||original.source_address!=w.source_address||
    original.requested_bytes!=w.requested_bytes||w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
    original.expected_byte!=w.expected[w.offset-w.window_offset]||original.actual_byte!=w.actual[w.offset-w.window_offset]){
  r.reason=9;return r;}
 r.first_difference_matches=1;r.row=w.row;r.offset=w.offset;r.host=w.host_address;r.generation=w.allocation_generation;
 r.epoch=w.owner_epoch;r.world=w.world_generation;r.source=w.source_address;r.requested=w.requested_bytes;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 r.hook_active=1;r.instance=piece_owner.instance;r.capture_serial=anchor.serial;r.event_serial=piece_owner.serial;
 r.coverage_state=piece_owner.state;r.first_event=piece_owner.first;
 if(piece_owner.reason){r.status=2;r.reason=piece_owner.reason;return r;}
 if(!anchor.ready){r.reason=anchor.reason?anchor.reason:1;return r;}
 if(!active||!piece_owner.cells||piece_owner.state!=3||anchor.epoch!=owner_epoch||anchor.world!=world_generation||
    anchor.instance!=piece_owner.instance||anchor.serial!=piece_owner.capture||anchor.serial!=piece_owner.serial){r.status=2;r.reason=9;return r;}
 const auto& row=allocations[w.row];
 if(row.source.value()<piece_owner.begin||uint64_t(row.source.value())+row.requested>piece_owner.end){r.status=2;r.reason=3;return r;}
 const uint32_t first=(row.source.value()-piece_owner.begin)/32;
 const uint32_t last=(uint64_t(row.source.value())+row.requested-piece_owner.begin+31)/32;
 const uint64_t target=uint64_t(row.source.value())+w.offset;
 uint32_t selected=UINT32_MAX;
 for(uint32_t i=first;i<last;++i){const auto& c=piece_owner.cells[i];if(!(c.geometry&piece_live))continue;
  const uint64_t start=uint64_t(piece_owner.begin)+uint64_t(i)*32,extent=piece_extent(c);
  if(!extent||start<row.source.value()||extent>uint64_t(row.source.value())+row.requested-start){r.status=2;r.reason=3;return r;}
  if(target>=start&&target-start<extent){if(selected!=UINT32_MAX){r.status=2;r.reason=5;return r;}selected=i;}}
 if(selected==UINT32_MAX){r.reason=4;return r;}
 const auto& c=piece_owner.cells[selected];r.piece_source=piece_owner.begin+selected*32;
 r.piece_offset=target-r.piece_source;r.physical=piece_extent(c);r.original_requested=piece_requested(c);r.piece_generation=c.generation;
 if(!c.generation){r.status=2;r.reason=6;return r;}
 r.record_state=c.tag?((c.tag&3)==2?3:2):1;
 if(!c.tag){r.status=1;return r;} // Observed raw piece; type remains UNKNOWN.
 if((c.tag&3)!=2){r.reason=4;return r;}
 r.class_info=c.tag&~3u;r.class_obj_size=r.original_requested;r.confirmed=1;
 const size_t in_row=r.piece_source-row.source.value();
 if(r.original_requested<4||in_row>row.requested-4){r.status=2;r.reason=3;return r;}
 size_t saved=0;const auto* rows=payload_rows(snapshot);
 for(size_t i=0;i<w.row;++i)if(!add_size(saved,rows[i].requested,&saved)){r.status=2;r.reason=3;return r;}
 if(!add_size(saved,in_row,&saved)||saved>snapshot->payload_bytes-4){r.status=2;r.reason=3;return r;}
 std::memcpy(&r.saved_class_info,payload_bytes(snapshot)+saved,4);
 std::memcpy(&r.live_class_info,reinterpret_cast<const void*>(row.host+in_row),4);
 if(r.saved_class_info!=r.class_info||r.live_class_info!=r.class_info){r.status=2;r.reason=10;return r;}
 r.status=1;
#else
 (void)anchor;r.reason=1;
#endif
 return r;
}

// Historical diagnostic only: the source table froze at arm, while the two
// snapshots preserve distinct PRE and POST bytes. Never read a live header.
static_assert(sizeof(MeleeWebClassPieceCaptureWitness)==160);
static_assert(offsetof(MeleeWebClassPieceCaptureWitness,epoch)==64);
static_assert(offsetof(MeleeWebClassPieceCaptureWitness,piece_source)==120);
static_assert(offsetof(MeleeWebClassPieceCaptureWitness,reserved)==152);
extern "C" MeleeWebClassPieceCaptureWitness melee_web_source_memory_class_piece_capture_witness(
 const MeleeWebSourceMemoryPayloadSnapshot* pre,const MeleeWebSourceMemoryPayloadSnapshot* post,
 MeleeWebSourcePayloadComparison original,MeleeWebClassPieceAnchor anchor) {
 MeleeWebClassPieceCaptureWitness r{};r.schema=1;r.bytes=sizeof(r);r.scope=1;
 const auto w=melee_web_source_memory_payload_snapshot_witness(post);
 if(original.status!=2||w.status!=2||original.allocation_index!=w.row||original.byte_offset!=w.offset||
    original.host_address!=w.host_address||original.source_address!=w.source_address||
    original.requested_bytes!=w.requested_bytes||w.offset<w.window_offset||w.offset-w.window_offset>=w.window_count||
    original.expected_byte!=w.expected[w.offset-w.window_offset]||original.actual_byte!=w.actual[w.offset-w.window_offset]){
  r.reason=9;return r;}
 r.first_difference_matches=1;r.post_row=w.row;r.offset=w.offset;r.source=w.source_address;
 r.requested=w.requested_bytes;r.host=w.host_address;r.generation=w.allocation_generation;
 r.epoch=w.owner_epoch;r.world=w.world_generation;
#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED
 r.hook_active=1;r.live_reason=piece_owner.reason;r.coverage_state=piece_owner.state;
 r.instance=piece_owner.instance;r.capture_serial=anchor.serial;r.event_serial=piece_owner.serial;
 // Admission validates lifetime/topology before any saved row/table access.
 if(payload_snapshot_check(pre)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||
    payload_snapshot_check(post)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK||
    !anchor.ready||!active||!healthy||!piece_owner.cells||
    anchor.epoch!=owner_epoch||anchor.world!=world_generation||
    anchor.epoch!=piece_owner.epoch||anchor.world!=piece_owner.world||
    anchor.instance!=piece_owner.instance||anchor.serial!=piece_owner.capture){r.status=2;r.reason=9;return r;}
 const bool armed=piece_owner.state==3&&!piece_owner.reason&&piece_owner.serial==anchor.serial;
 const bool frozen=piece_owner.state==4&&piece_owner.reason==7&&anchor.serial!=UINT64_MAX&&
   piece_owner.first.serial==anchor.serial+1&&piece_owner.serial==piece_owner.first.serial;
 if(!armed&&!frozen){r.status=2;r.reason=piece_owner.reason?piece_owner.reason:9;return r;}
 const auto& postrow=payload_rows(post)[w.row];const auto* rows=payload_rows(pre);
 size_t selected_row=SIZE_MAX,saved=0;
 for(size_t i=0;i<pre->row_count;++i){const auto& row=rows[i];
  if(row.host==postrow.host&&row.source==postrow.source&&row.requested==postrow.requested&&row.generation==postrow.generation){
   if(selected_row!=SIZE_MAX){r.status=2;r.reason=5;return r;}selected_row=i;r.pre_row=static_cast<uint32_t>(i);
  }
 }
 if(selected_row==SIZE_MAX){r.status=2;r.reason=9;return r;}
 const auto& row=rows[selected_row];const uint64_t begin=row.source.value(),end=begin+row.requested;
 if(piece_owner.begin>=piece_owner.end||(piece_owner.begin&31)||(piece_owner.end&31)||
    piece_owner.count!=(piece_owner.end-piece_owner.begin)/32||begin<piece_owner.begin||end>piece_owner.end||w.offset>=row.requested){r.status=2;r.reason=3;return r;}
 const uint32_t first=(begin-piece_owner.begin)/32,last=(end-piece_owner.begin+31)/32;
 if(last>piece_owner.count){r.status=2;r.reason=3;return r;}
 const uint64_t target=begin+w.offset;uint32_t cell=UINT32_MAX;
 for(uint32_t i=first;i<last;++i){const auto& c=piece_owner.cells[i];if(!(c.geometry&piece_live))continue;
  const uint64_t start=uint64_t(piece_owner.begin)+uint64_t(i)*32,extent=piece_extent(c);
  if(!extent||start<begin||start>end||extent>end-start){r.status=2;r.reason=3;return r;}
  if(target>=start&&target-start<extent){if(cell!=UINT32_MAX){r.status=2;r.reason=5;return r;}cell=i;}
 }
 if(cell==UINT32_MAX){r.reason=4;return r;}
 const auto& c=piece_owner.cells[cell];r.piece_source=piece_owner.begin+cell*32;
 r.piece_offset=target-r.piece_source;r.physical=piece_extent(c);r.original_requested=piece_requested(c);
 r.piece_generation=c.generation;r.flags=r.piece_offset>=r.original_requested?1:0;
 if(!c.generation){r.status=2;r.reason=6;return r;}
 r.record_state=c.tag?((c.tag&3)==2?3:2):1;
 if(!c.tag){r.status=1;return r;} // Raw at capture: no class identity.
 if((c.tag&3)!=2){r.reason=4;return r;}
 const size_t in_row=r.piece_source-begin;
 if(r.original_requested<4||row.requested<4||in_row>row.requested-4){r.status=2;r.reason=3;return r;}
 for(size_t i=0;i<selected_row;++i)if(!add_size(saved,rows[i].requested,&saved)){r.status=2;r.reason=3;return r;}
 if(!add_size(saved,in_row,&saved)||pre->payload_bytes<4||saved>pre->payload_bytes-4){r.status=2;r.reason=3;return r;}
 r.class_info=c.tag&~3u;
 if(!r.class_info){r.status=2;r.reason=10;return r;}
 std::memcpy(&r.saved_class_info,payload_bytes(pre)+saved,4);
 if(r.saved_class_info!=r.class_info){r.status=2;r.reason=10;return r;}
 r.status=1;
#else
 (void)pre;(void)anchor;r.reason=1;
#endif
 return r;
}

#include "gameplay_lobj_writer_diagnostic.inc"
