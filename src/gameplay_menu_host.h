#ifndef MELEE_WEB_GAMEPLAY_MENU_HOST_H
#define MELEE_WEB_GAMEPLAY_MENU_HOST_H
#include "gameplay_compat.h"
#include <stddef.h>
#include <stdint.h>
#include <dolphin/pad.h>
#include <melee/mn/types.h>
#include "gameplay_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
typedef struct MeleeWebMenuMatchSelection {
    /* Complete source payload copied while the menu-owned VsModeData is still
     * live.  Match handoff consumes this typed value synchronously. */
    StartMeleeData start;
    /* Compatibility view used by the existing match session; these values
     * are copied from start.players by the host selection boundary. */
    struct {uint32_t controller,stocks,costume,sub_color;} players[4];
    uint32_t player_count;
    uint32_t random_seed, hud_layout;
} MeleeWebMenuMatchSelection;
/* Owns source selection across separate CSS, SSS and match SDK worlds.
 * Enter only after GameplayMenuWorld has published its native assets. */
MeleeWebMenuHost* melee_web_menu_host_create(char*,size_t);
int melee_web_menu_host_enter(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Original raw PAD processing, scene callback, audio control and scheduler.
 * Returns 1 while active, 3 on an original transition request, 0 on failure. */
int melee_web_menu_host_tick(MeleeWebMenuHost*,const PADStatus[4],char*,size_t);
int melee_web_menu_host_draw(MeleeWebMenuHost*,char*,size_t);
/* Call after the final Aurora frame has submitted, before closing its world. */
int melee_web_menu_host_leave(MeleeWebMenuHost*,int abort_scene,char*,size_t);
/* Reads the separate configuration produced by the original VS-entry rules
 * and player preparation after CSS/SSS OnExit. */
int melee_web_menu_host_selection(const MeleeWebMenuHost*,MeleeWebMenuMatchSelection*,char*,size_t);
/* Reads the raw SSS-owned payload after OnExit and before VS-entry
 * normalization. This is a read-only retail-equivalence observation. */
int melee_web_menu_host_raw_selection(const MeleeWebMenuHost*,StartMeleeData*,char*,size_t);
/* Called after source match teardown restores RNG ownership. */
int melee_web_menu_host_match_finished(MeleeWebMenuHost*,uint32_t random_seed,char*,size_t);
int melee_web_menu_host_destroy(MeleeWebMenuHost*,char*,size_t);
/* Same phase values as the checked source session: CSS=1, SSS-ready=2,
 * SSS=3, CSS-ready=4, match-ready=5, closed=6. */
int melee_web_menu_host_phase(const MeleeWebMenuHost*);
#ifdef __cplusplus
}
#endif
#endif
