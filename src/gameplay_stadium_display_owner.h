#ifndef MELEE_WEB_GAMEPLAY_STADIUM_DISPLAY_OWNER_H
#define MELEE_WEB_GAMEPLAY_STADIUM_DISPLAY_OWNER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Provenance is returned by the actual source buffer-selection branch. */
typedef enum MeleeWebStadiumImageOrigin {
    MELEE_WEB_STADIUM_IMAGE_ORIGIN_UNKNOWN = 0,
    MELEE_WEB_STADIUM_IMAGE_ORIGIN_OWNED_HSD = 1,
    MELEE_WEB_STADIUM_IMAGE_ORIGIN_BORROWED_PRELOAD = 2,
    MELEE_WEB_STADIUM_IMAGE_ORIGIN_RELEASED = 3,
} MeleeWebStadiumImageOrigin;

typedef struct MeleeWebStadiumImageProvenance {
    void* image_ptr;
    MeleeWebStadiumImageOrigin origin;
} MeleeWebStadiumImageProvenance;

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
typedef struct HSD_GObj HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
/* Borrowed identities from exactly one original Ground manager construction.
 * Source heap leases remain with StageLast, separate from logical identity. */
typedef struct MeleeWebStadiumManagerView {
    HSD_GObj* object;
    HSD_GObjProc* proc;
    void (*callback)(HSD_GObj*);
} MeleeWebStadiumManagerView;
typedef struct MeleeWebStadiumDisplayOwner MeleeWebStadiumDisplayOwner;
typedef void (*MeleeWebStadiumBufferFree)(void* image_ptr, void* context);

typedef enum MeleeWebStadiumMap2BufferOrigin {
    MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_UNKNOWN = 0,
    MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_BORROWED_PRELOAD = 1,
    MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK = 2,
    MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_RETIRED = 3,
} MeleeWebStadiumMap2BufferOrigin;

#define MELEE_WEB_STADIUM_SOURCE_EVENT_CAPACITY 8

typedef enum MeleeWebStadiumSourceEventKind {
    MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_E8 = 1,
    MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_24C = 2,
    MELEE_WEB_STADIUM_SOURCE_EVENT_GROUND_0800 = 3,
    MELEE_WEB_STADIUM_SOURCE_EVENT_ON_INIT = 4,
    MELEE_WEB_STADIUM_SOURCE_EVENT_MAP_GOBJ = 5,
} MeleeWebStadiumSourceEventKind;

typedef struct MeleeWebStadiumSourceEvent {
    MeleeWebStadiumSourceEventKind kind;
    int map_id;
    HSD_GObj* gobj;
} MeleeWebStadiumSourceEvent;

typedef struct MeleeWebStadiumSourceJournal {
    MeleeWebStadiumSourceEvent events[MELEE_WEB_STADIUM_SOURCE_EVENT_CAPACITY];
    unsigned count;
    int failed;
    int overflowed;
} MeleeWebStadiumSourceJournal;

/* This source-derived record crosses the private Stadium TU boundary. It
 * keeps the exact branch provenance alive after display/SIS retirement and
 * until the original Ground owner objects have been removed. */
typedef struct MeleeWebStadiumMap2BufferOwner {
    HSD_GObj* map0_ground;
    HSD_GObj* display_ground;
    HSD_GObj* map2_ground;
    HSD_GObj* nested_map5_ground;
    void* buffer;
    MeleeWebStadiumMap2BufferOrigin origin;
    int captured;
} MeleeWebStadiumMap2BufferOwner;

/* The owner is opaque so the private grpstadium TextWrapper ABI stays in its
 * defining translation unit. Call prepare before E8, bind after E8, and
 * capture immediately after original Ground_801C0800 returns. */
int melee_web_stadium_display_owner_prepare(
    MeleeWebStadiumDisplayOwner** out, char* error, size_t error_size);
int melee_web_stadium_display_owner_bind_source(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_arm_source_journal(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
void melee_web_stadium_display_owner_note_source_event(
    MeleeWebStadiumSourceEventKind, int map_id, HSD_GObj*);
int melee_web_stadium_display_owner_source_journal_snapshot(
    const MeleeWebStadiumDisplayOwner*, MeleeWebStadiumSourceJournal* out);
int melee_web_stadium_display_owner_capture(
    MeleeWebStadiumDisplayOwner*, HSD_GObj* display_ground,
    MeleeWebStadiumMap2BufferOwner* map2_buffer_owner,
    char* error, size_t error_size);
void melee_web_stadium_display_owner_note_manager(HSD_GObj*, HSD_GObjProc*);
int melee_web_stadium_display_owner_capture_manager(
    MeleeWebStadiumDisplayOwner*, MeleeWebStadiumManagerView*, char*, size_t);
int melee_web_stadium_manager_view_preflight(
    const MeleeWebStadiumManagerView*, char*, size_t);
int melee_web_stadium_display_owner_preflight(
    MeleeWebStadiumDisplayOwner*, char*, size_t);
int melee_web_stadium_display_owner_cancel(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_retire(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_end(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_map2_buffer_owner_end(
    MeleeWebStadiumMap2BufferOwner*, char* error, size_t error_size);
int melee_web_stadium_image_release(
    MeleeWebStadiumImageProvenance*, MeleeWebStadiumBufferFree, void* context);

/* Focused asset-free controls for the shared source buffer branch and exact
 * owned-versus-borrowed release behavior. */
int melee_web_stadium_display_provenance_controls(void);
int melee_web_stadium_display_list_controls(void);
int melee_web_stadium_display_owner_retirement_controls(void);
int melee_web_stadium_map2_buffer_controls(void);
int melee_web_stadium_source_journal_controls(void);
/* Actual text-only SIS append/remove controls, under a live backing-heap
 * token. Per-text identity is checked SIS suballocation membership. */
struct MeleeWebDiagnosticSisOwner;
int melee_web_stadium_text_topology_controls(
    const struct MeleeWebDiagnosticSisOwner*);
#endif

#ifdef __cplusplus
}
#endif

#endif
