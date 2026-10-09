#ifndef MELEE_WEB_STADIUM_C1_HEAP_OWNER_OBSERVER_H
#define MELEE_WEB_STADIUM_C1_HEAP_OWNER_OBSERVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_CAPACITY = 4096,
};

typedef enum MeleeWebStadiumC1HeapOwnerKind {
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_OBJALLOC_POOL = 1,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_DIRECTORY = 2,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_BUCKET = 3,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_SLAB = 4,
} MeleeWebStadiumC1HeapOwnerKind;

typedef enum MeleeWebStadiumC1HeapOwnerPhase {
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_BEFORE_LIGHT = 1,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_LIGHT = 2,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_ONINIT = 3,
    MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_STAGE_LAST = 4,
} MeleeWebStadiumC1HeapOwnerPhase;

typedef struct MeleeWebStadiumC1HeapOwnerEvent {
    uintptr_t payload;
    uintptr_t owner_identity;
    uint64_t world_generation;
    uint64_t allocation_generation;
    uint64_t hsd_requested_bytes;
    uint32_t lease_requested_bytes;
    uint32_t owner_size;
    uint32_t auxiliary;
    int32_t source_heap_handle;
    uint8_t kind;
    uint8_t lease_status;
    uint8_t live;
    uint8_t reserved;
} MeleeWebStadiumC1HeapOwnerEvent;

typedef struct MeleeWebStadiumC1HeapOwnerMarker {
    uint32_t event_count;
    uint8_t phase;
    uint8_t census_complete;
    uint8_t reserved[2];
} MeleeWebStadiumC1HeapOwnerMarker;

void melee_web_stadium_c1_heap_owner_arm(void);
void melee_web_stadium_c1_heap_owner_disable(void);
uint32_t melee_web_stadium_c1_heap_owner_reserve(
    uint8_t kind, const void* owner_identity, uint32_t owner_size,
    uint32_t auxiliary, void* payload, size_t hsd_requested_bytes);
void melee_web_stadium_c1_heap_owner_complete(uint32_t token);
void melee_web_stadium_c1_heap_owner_record(
    uint8_t kind, const void* owner_identity, uint32_t owner_size,
    uint32_t auxiliary, void* payload, size_t hsd_requested_bytes);
void melee_web_stadium_c1_heap_owner_mark(uint8_t phase,
                                         int census_complete);
size_t melee_web_stadium_c1_heap_owner_count(void);
size_t melee_web_stadium_c1_heap_owner_overflow_count(void);
size_t melee_web_stadium_c1_heap_owner_invalid_count(void);
size_t melee_web_stadium_c1_heap_owner_pending_count(void);
int melee_web_stadium_c1_heap_owner_overflowed(void);
int melee_web_stadium_c1_heap_owner_armed(void);
size_t melee_web_stadium_c1_heap_owner_row_bytes(void);
size_t melee_web_stadium_c1_heap_owner_buffer_bytes(void);
size_t melee_web_stadium_c1_heap_owner_marker_count(void);
int melee_web_stadium_c1_heap_owner_read(
    size_t index, MeleeWebStadiumC1HeapOwnerEvent* out);
int melee_web_stadium_c1_heap_owner_marker_read(
    size_t index, MeleeWebStadiumC1HeapOwnerMarker* out);

#ifdef __cplusplus
}
#endif
#endif
