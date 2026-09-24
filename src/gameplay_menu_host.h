#ifndef MELEE_WEB_GAMEPLAY_MENU_HOST_H
#define MELEE_WEB_GAMEPLAY_MENU_HOST_H
#include "gameplay_compat.h"
#include <stddef.h>
#include <stdint.h>
#include <dolphin/pad.h>
#include <melee/mn/types.h>
#include "gameplay_audio.h"
#include "gameplay_pad_state.h"
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
    /* Source save inputs must survive the menu world's teardown. Zero present
     * means an older diagnostic did not record a profile; it is not all-unlocked. */
    uint16_t unlocked_characters, unlocked_stages;
    uint8_t save_profile_present;
} MeleeWebMenuMatchSelection;
/* Owns source selection across separate CSS, SSS and match SDK worlds.
 * Enter only after GameplayMenuWorld has published its native assets. */
MeleeWebMenuHost* melee_web_menu_host_create(char*,size_t);
int melee_web_menu_host_enter(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Install the copied first-CSS source context before the initial scene is
 * entered.  Rules/save ranges are observer PowerPC bytes and are translated
 * by the save/profile owner; the PAD state is semantic wire data and is
 * applied without transferring any live queue or rumble pointer. */
int melee_web_menu_host_apply_replay_context(
    MeleeWebMenuHost*, uint32_t random_seed,
    const uint8_t pad_state[MELEE_WEB_PAD_STATE_BYTES],
    const uint8_t css_data[0x148], const uint8_t ko_counts[GM_MAX_PLAYERS],
    const uint8_t game_rules[0x18], const uint8_t save_data[0x55E8],
    char*, size_t);
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
/* Retained source input for the next match; remains owned by the host until
 * match_finished or destruction. Read only after closing the SSS scene. */
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost*);
/* Capture the final match PAD state before closing its source context. After
 * teardown restores external owners, publish that state and the final RNG.
 * Only semantic PAD configuration/history transfers, never queue pointers. */
int melee_web_menu_host_match_finished(MeleeWebMenuHost*,uint32_t random_seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char*,size_t);
/* Enclose the original Results scene with ordinary VS mode callbacks. Begin
 * follows match teardown. Exit follows Results scene OnExit with its assets
 * still resident. End follows Results teardown and transfers final PAD/RNG.
 * Unsupported source routes are reported; they are never forced to CSS. */
int melee_web_menu_host_results_begin(MeleeWebMenuHost*,
    const struct MatchExitInfo*,uint32_t random_seed,
    struct ResultsMatchInfo*,char*,size_t);
int melee_web_menu_host_results_exit(MeleeWebMenuHost*,char*,size_t);
int melee_web_menu_host_results_end(MeleeWebMenuHost*,uint32_t random_seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char*,size_t);
/* Results may request the original Prize state (192). Its mode entry runs
 * only after the new Prize heap and assets exist; the linked payload belongs
 * to that heap until Prize's original scene and mode exits complete. */
int melee_web_menu_host_results_destination(const MeleeWebMenuHost*);
int melee_web_menu_host_prize_enter(MeleeWebMenuHost*,char*,size_t);
int melee_web_menu_host_prize_exit(MeleeWebMenuHost*,char*,size_t);
int melee_web_menu_host_prize_end(MeleeWebMenuHost*,uint32_t random_seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char*,size_t);
int melee_web_menu_host_destroy(MeleeWebMenuHost*,char*,size_t);
/* Same phase values as the checked source session: CSS=1, SSS-ready=2,
 * SSS=3, CSS-ready=4, match-ready=5, closed=6. */
int melee_web_menu_host_phase(const MeleeWebMenuHost*);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance.h"
int melee_web_menu_host_provenance(const MeleeWebMenuHost*, MeleeWebPipelineSourceContext*);
#endif
#ifdef __cplusplus
}
#endif
#endif
