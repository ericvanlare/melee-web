#ifndef MELEE_WEB_GAMEPLAY_MENU_H
#define MELEE_WEB_GAMEPLAY_MENU_H

#include <stddef.h>
#include <stdint.h>

#include <melee/mn/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The menu uses the retail stage identifier (StKind) in SSSData.  Final
 * Destination is the CSS/SSS entry at 25, while the value carried into the
 * match is St_Kind_Last (0x20) and the ground runtime later maps it to
 * Gr_Kind_Last (0x25).  Keep these values named separately at this boundary.
 */
enum {
    MELEE_WEB_MENU_FD_ENTRY = 25,
    MELEE_WEB_MENU_FD_ST_KIND = 0x20,
    MELEE_WEB_MENU_FD_GR_KIND = 0x25,
};

typedef enum MeleeWebMenuScene {
    MELEE_WEB_MENU_SCENE_CSS = 0,
    MELEE_WEB_MENU_SCENE_SSS = 1,
} MeleeWebMenuScene;

typedef enum MeleeWebMenuPhase {
    MELEE_WEB_MENU_CREATED = 0,
    MELEE_WEB_MENU_CSS = 1,
    MELEE_WEB_MENU_SSS_READY = 2,
    MELEE_WEB_MENU_SSS = 3,
    MELEE_WEB_MENU_CSS_READY = 4,
    MELEE_WEB_MENU_READY = 5,
    MELEE_WEB_MENU_CLOSED = 6,
} MeleeWebMenuPhase;

/* Return nonzero only after the host has installed the native HSD object
 * registry, checked archive publication, and confirmed that no match world
 * owns the source scene.  The callback is run before enter and every tick.
 * It must not manufacture successful archive or scheduler behavior.
 */
typedef int (*MeleeWebMenuRuntimeCheck)(void* user, MeleeWebMenuScene scene,
                                        char* error, size_t error_size);

/* Runs one original HSD object scheduler tick.  The menu wrapper invokes the
 * original scene OnFrame first, matching gm_801A4D34, then invokes this
 * callback.  The callback owns render/audio work beyond object scheduling.
 */
typedef int (*MeleeWebMenuScheduler)(void* user, char* error,
                                     size_t error_size);

/* The original scene owns its transition state in private statics.  The host
 * must read and clear the checked gm_801A4D34 request flag after OnFrame and
 * HSD scheduling instead of inferring readiness from CSSData/SSSData.  The
 * callback writes 0 when the source remains in-scene, or the original
 * gm_801A4B60/74 request kind when it has finished its transition request.
 * The wrapper exposes that request as RESULT_TRANSITION_REQUESTED; the host
 * then calls leave_* so the source OnExit owns archive release. */
typedef int (*MeleeWebMenuTransition)(void* user, MeleeWebMenuScene scene,
                                      int* requested, char* error,
                                      size_t error_size);

typedef struct MeleeWebMenuRuntime {
    void* user;
    MeleeWebMenuRuntimeCheck check;
    MeleeWebMenuScheduler scheduler;
    MeleeWebMenuTransition transition;
} MeleeWebMenuRuntime;

typedef struct MeleeWebMenuConfig {
    /* Only the first two local human slots are in scope for this gate. */
    uint8_t stocks; /* must be 4 for the current native launch contract */
    uint8_t player0_color;
    uint8_t player1_color;
} MeleeWebMenuConfig;

typedef struct MeleeWebMenuSession MeleeWebMenuSession;

enum {
    MELEE_WEB_MENU_RESULT_ERROR = 0,
    MELEE_WEB_MENU_RESULT_TICKED = 1,
    MELEE_WEB_MENU_RESULT_SELECTION_REJECTED = 2,
    MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED = 3,
};

/* One owner may hold a native menu session at a time.  Creation only builds
 * source-owned CSS/SSS data; it does not enter a scene or allocate HSD
 * objects. */
MeleeWebMenuSession* melee_web_menu_session_create(
    const MeleeWebMenuRuntime* runtime, const MeleeWebMenuConfig* config,
    char* error, size_t error_size);
int melee_web_menu_session_destroy(MeleeWebMenuSession*, char* error,
                                   size_t error_size);

/* Native scene lifecycle.  enter/leave are explicit so a host cannot create a
 * second SDK scene while a match still owns the source world. */
int melee_web_menu_enter_css(MeleeWebMenuSession*, char* error,
                             size_t error_size);
/* Re-enter CSS after the host has torn down the match world.  This preserves
 * the validated character selection and requires the same runtime check
 * used for the initial native scene. */
int melee_web_menu_return_to_css(MeleeWebMenuSession*, char* error,
                                 size_t error_size);
int melee_web_menu_enter_sss(MeleeWebMenuSession*, char* error,
                             size_t error_size);
int melee_web_menu_tick(MeleeWebMenuSession*, char* error, size_t error_size);
int melee_web_menu_leave_css(MeleeWebMenuSession*, char* error,
                             size_t error_size);
int melee_web_menu_leave_sss(MeleeWebMenuSession*, char* error,
                             size_t error_size);
int melee_web_menu_abort(MeleeWebMenuSession*, char* error, size_t error_size);

MeleeWebMenuPhase melee_web_menu_phase(const MeleeWebMenuSession*);
const CSSData* melee_web_menu_css(const MeleeWebMenuSession*);
const SSSData* melee_web_menu_sss(const MeleeWebMenuSession*);
/* Returns the separate VS-entry payload after the original source adapter has
 * applied persistent rules, stocks, item settings and rumble. */
const VsModeData* melee_web_menu_ready_vs(const MeleeWebMenuSession*);

/* Pure boundary checks used by the lifecycle and by the host before launch.
 * These check source IDs and rule shape; they do not consult browser state or
 * undo a source scene's work.  The native cursor/confirm code must enforce
 * availability before selecting or loading an entry. */
int melee_web_menu_character_available(int ckind);
int melee_web_menu_stage_available(int stkind);
int melee_web_menu_css_selection_valid(const CSSData*);
int melee_web_menu_sss_selection_valid(const SSSData*);

#ifdef __cplusplus
}
#endif

#endif
