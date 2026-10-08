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
typedef struct MeleeWebStadiumDisplayOwner MeleeWebStadiumDisplayOwner;
typedef void (*MeleeWebStadiumBufferFree)(void* image_ptr, void* context);

/* The owner is opaque so the private grpstadium TextWrapper ABI stays in its
 * defining translation unit. Call prepare before E8, bind after E8, and
 * capture immediately after original Ground_801C0800 returns. */
int melee_web_stadium_display_owner_prepare(
    MeleeWebStadiumDisplayOwner** out, char* error, size_t error_size);
int melee_web_stadium_display_owner_bind_source(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_capture(
    MeleeWebStadiumDisplayOwner*, HSD_GObj* display_ground,
    char* error, size_t error_size);
int melee_web_stadium_display_owner_cancel(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_retire(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_display_owner_end(
    MeleeWebStadiumDisplayOwner*, char* error, size_t error_size);
int melee_web_stadium_image_release(
    MeleeWebStadiumImageProvenance*, MeleeWebStadiumBufferFree, void* context);

/* Focused asset-free controls for the shared source buffer branch and exact
 * owned-versus-borrowed release behavior. */
int melee_web_stadium_display_provenance_controls(void);
int melee_web_stadium_display_list_controls(void);
int melee_web_stadium_display_owner_retirement_controls(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
