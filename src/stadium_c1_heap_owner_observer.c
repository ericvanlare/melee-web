#include "stadium_c1_heap_owner_observer.h"

#include <limits.h>
#include <string.h>

#include "gameplay_source_memory_runtime.h"

_Static_assert(sizeof(MeleeWebStadiumC1HeapOwnerEvent) <= 64,
               "C1 heap-owner rows must stay within the fixed row budget");

static MeleeWebStadiumC1HeapOwnerEvent events[
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_CAPACITY];
static MeleeWebStadiumC1HeapOwnerMarker markers[4];
static size_t event_count;
static size_t overflow_count;
static size_t invalid_count;
static size_t marker_count;
static int armed;
static int enabled;
static int marker_overflow;

_Static_assert(sizeof(size_t) <= sizeof(uint64_t),
               "C1 owner request field must preserve size_t exactly");

static void note_invalid(void)
{
    if (invalid_count != SIZE_MAX) ++invalid_count;
}

void melee_web_stadium_c1_heap_owner_arm(void)
{
    if (armed) {
        enabled = 0;
        marker_overflow = 1;
        return;
    }
    memset(events, 0, sizeof(events));
    memset(markers, 0, sizeof(markers));
    event_count = 0;
    overflow_count = 0;
    invalid_count = 0;
    marker_count = 0;
    marker_overflow = 0;
    armed = 1;
    enabled = 1;
}

void melee_web_stadium_c1_heap_owner_disable(void)
{
    enabled = 0;
}

uint32_t melee_web_stadium_c1_heap_owner_reserve(
    uint8_t kind, const void* owner_identity, uint32_t owner_size,
    uint32_t auxiliary, void* payload, size_t hsd_requested_bytes)
{
    if (!enabled || !payload) return UINT32_MAX;
    if (event_count == MELEE_WEB_STADIUM_C1_HEAP_OWNER_CAPACITY) {
        if (overflow_count != SIZE_MAX) ++overflow_count;
        return UINT32_MAX;
    }

    MeleeWebStadiumC1HeapOwnerEvent event = {0};
    event.payload = (uintptr_t) payload;
    event.owner_identity = (uintptr_t) owner_identity;
    event.hsd_requested_bytes = (uint64_t) hsd_requested_bytes;
    event.owner_size = owner_size;
    event.auxiliary = auxiliary;
    event.kind = kind;
    event.lease_status = UINT8_MAX;
    const uint32_t token = (uint32_t) event_count;
    events[event_count++] = event;
    return token;
}

void melee_web_stadium_c1_heap_owner_complete(uint32_t token)
{
    if (!enabled) return;
    if (token == UINT32_MAX || token >= event_count) {
        note_invalid();
        return;
    }
    if (events[token].lease_status != UINT8_MAX) {
        note_invalid();
        return;
    }
    MeleeWebSourceMemoryAllocation lease = {0};
    events[token].lease_status = (uint8_t)
        melee_web_source_memory_allocation_read(
            (const void*) events[token].payload, &lease);
    events[token].live = lease.live;
    events[token].lease_requested_bytes = lease.requested_bytes;
    events[token].source_heap_handle = lease.source_heap_handle;
    events[token].world_generation = lease.world_generation;
    events[token].allocation_generation = lease.allocation_generation;
}

void melee_web_stadium_c1_heap_owner_record(
    uint8_t kind, const void* owner_identity, uint32_t owner_size,
    uint32_t auxiliary, void* payload, size_t hsd_requested_bytes)
{
    const uint32_t token = melee_web_stadium_c1_heap_owner_reserve(
        kind, owner_identity, owner_size, auxiliary, payload,
        hsd_requested_bytes);
    melee_web_stadium_c1_heap_owner_complete(token);
}

void melee_web_stadium_c1_heap_owner_mark(uint8_t phase,
                                         int census_complete)
{
    if (!armed || marker_count == sizeof(markers) / sizeof(markers[0])) {
        marker_overflow = 1;
        return;
    }
    markers[marker_count++] = (MeleeWebStadiumC1HeapOwnerMarker) {
        (uint32_t) event_count, phase, (uint8_t) (census_complete != 0), {0, 0}
    };
}

size_t melee_web_stadium_c1_heap_owner_count(void)
{
    return event_count;
}

size_t melee_web_stadium_c1_heap_owner_overflow_count(void)
{
    return overflow_count;
}

size_t melee_web_stadium_c1_heap_owner_invalid_count(void)
{
    return invalid_count;
}

size_t melee_web_stadium_c1_heap_owner_pending_count(void)
{
    size_t pending = 0;
    for (size_t i = 0; i < event_count; ++i) {
        if (events[i].lease_status == UINT8_MAX) ++pending;
    }
    return pending;
}

int melee_web_stadium_c1_heap_owner_overflowed(void)
{
    return overflow_count != 0 || marker_overflow;
}

int melee_web_stadium_c1_heap_owner_armed(void)
{
    return armed;
}

size_t melee_web_stadium_c1_heap_owner_row_bytes(void)
{
    return sizeof(events[0]);
}

size_t melee_web_stadium_c1_heap_owner_buffer_bytes(void)
{
    return sizeof(events);
}

size_t melee_web_stadium_c1_heap_owner_marker_count(void)
{
    return marker_count;
}

int melee_web_stadium_c1_heap_owner_read(
    size_t index, MeleeWebStadiumC1HeapOwnerEvent* out)
{
    if (!out || index >= event_count) return 0;
    *out = events[index];
    return 1;
}

int melee_web_stadium_c1_heap_owner_marker_read(
    size_t index, MeleeWebStadiumC1HeapOwnerMarker* out)
{
    if (!out || index >= marker_count) return 0;
    *out = markers[index];
    return 1;
}
