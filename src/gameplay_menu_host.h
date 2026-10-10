#ifndef MELEE_WEB_GAMEPLAY_MENU_HOST_H
#define MELEE_WEB_GAMEPLAY_MENU_HOST_H
#include "gameplay_compat.h"
#include <stddef.h>
#include <stdint.h>
#include <dolphin/pad.h>
#include <melee/gm/types.h>
#include <melee/mn/types.h>
#include "gameplay_audio.h"
#include "gameplay_pad_state.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
enum {
    MELEE_WEB_SAVE_MODE_EVERYTHING = 0,
    MELEE_WEB_SAVE_MODE_PERSONAL = 1,
};
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
    /* Set only for the source Title attract demo. This keeps its authored
     * mode/callback and 99-stock payload separate from ordinary VS rules. */
    uint8_t opening_demo;
    /* The original Sudden Death scene callback adds rules.x6 before entering
     * fn_8016E730. Its active source players may occupy sparse original ports. */
    uint8_t sudden_death;
} MeleeWebMenuMatchSelection;
typedef enum MeleeWebMenuMatchContinuationKind {
    MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS = 1,
    MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH = 2,
} MeleeWebMenuMatchContinuationKind;
typedef struct MeleeWebMenuMatchContinuation {
    MeleeWebMenuMatchContinuationKind kind;
    /* Nonzero only while this host owns the matching original SD continuation. */
    uint64_t owner_id;
    union {
        /* The exact StartMeleeData produced by the original VS Sudden Death
         * on_enter callback. The source may change its participants/rules. */
        StartMeleeData sudden_death_start;
        /* The exact ResultsMatchInfo produced by the original Results
         * on_enter callback after either VS or Sudden Death. */
        struct ResultsMatchInfo results;
    } payload;
} MeleeWebMenuMatchContinuation;
typedef struct MeleeWebMenuSourceObservation {
    int source_scene;
    int menu_kind;
    int previous_menu_kind;
    int hovered_selection;
    int confirmed_selection;
    uint64_t menu_buttons;
    int item_input_locked;
    int rule_mode;
    int stock_count;
    int time_limit;
    int stock_time_limit;
    int handicap;
    int damage_ratio;
    int friendly_fire;
    int pause;
    int item_frequency;
    uint64_t item_mask;
    int css_setup_valid;
    int css_is_teams;
    int css_player_teams[4];
} MeleeWebMenuSourceObservation;
typedef struct MeleeWebOpeningPreview {
    uint32_t characters[4];
    uint32_t costumes[4];
    uint32_t stage_kind;
    uint32_t match_kind;
} MeleeWebOpeningPreview;
enum {
    MELEE_WEB_MENU_HOST_SCENE_CSS = 1,
    MELEE_WEB_MENU_HOST_SCENE_SSS = 2,
    MELEE_WEB_MENU_HOST_SCENE_TITLE = 3,
    MELEE_WEB_MENU_HOST_SCENE_MAIN = 4,
    MELEE_WEB_MENU_HOST_SCENE_OPENING = 5,
    MELEE_WEB_MENU_HOST_SCENE_OPENING_VS = 6,
};
/* Owns source selection across separate CSS, SSS and match SDK worlds.
 * Enter only after GameplayMenuWorld has published its native assets. */
MeleeWebMenuHost* melee_web_menu_host_create(char*,size_t);
/* Browser owner entry. The optional card data is a validated, source-format
 * profile and is applied before original CSS or gameplay can read SaveData. */
MeleeWebMenuHost* melee_web_menu_host_create_with_profile(
    int save_mode, const uint8_t* card_data, size_t card_data_size,
    char*, size_t);
/* Complete the source-backed save baseline after GameplayMenuWorld has
 * started its source-file scope and published the original TyDatai roots. */
int melee_web_menu_host_initialize_profile_baseline(
    MeleeWebMenuHost*, char*, size_t);
/* Copy exact card-manifest bytes at a strict source command boundary. Pass
 * baseline=1 to export the immutable mode baseline captured before CSS. */
int melee_web_menu_host_snapshot_card_data(
    MeleeWebMenuHost*, int baseline, uint8_t* output,
    size_t output_size, char*, size_t);
int melee_web_menu_host_enter(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Source title/main route.  These callbacks retain the host's persistent
 * GameSceneInfo and authored mode payloads across the Aurora world boundary;
 * they do not synthesize a browser menu. */
int melee_web_menu_host_enter_title(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
int melee_web_menu_host_enter_main(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Enter the original Training mode's source CSS after its GM_MENU handoff.
 * This is the original training CSS/SSS navigation owner; the training
 * simulation state remains a separate checked runtime boundary. */
int melee_web_menu_host_enter_training_css(
    MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Current original game-mode route selected by the source mode owner. */
int melee_web_menu_host_mode_kind(const MeleeWebMenuHost*);
int melee_web_menu_host_training_start_pending(const MeleeWebMenuHost*);
/* Opening mode uses the original state table. Preview is read-only source
 * selection data used to request the exact assets before preload/OnEnter. */
int melee_web_menu_host_opening_preview(const MeleeWebMenuHost*,
    MeleeWebOpeningPreview*,char*,size_t);
int melee_web_menu_host_enter_opening(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
int melee_web_menu_host_opening_selection(const MeleeWebMenuHost*,
    MeleeWebMenuMatchSelection*,char*,size_t);
int melee_web_menu_host_opening_match_suspend(MeleeWebMenuHost*,char*,size_t);
int melee_web_menu_host_opening_match_finish(MeleeWebMenuHost*,uint32_t,
    const uint8_t[MELEE_WEB_PAD_STATE_BYTES],char*,size_t);
int melee_web_menu_host_opening_match_abort(MeleeWebMenuHost*,char*,size_t);
const MeleeWebPadState* melee_web_menu_host_opening_input(const MeleeWebMenuHost*);
int melee_web_menu_host_opening_target_state(const MeleeWebMenuHost*);
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
typedef struct MeleeWebMenuCssReturnSnapshot {
    /* Typed source fields; css.ko_counts is NULL in this copied snapshot.
     * Pointer-valued fields are not guest/native equality authorities. */
    CSSData css;
    uint8_t ko_counts[GM_MAX_PLAYERS];
    uint8_t pad_state[MELEE_WEB_PAD_STATE_BYTES];
    uint32_t random_seed;
    int source_scene;
    int source_scene_kind;
} MeleeWebMenuCssReturnSnapshot;
/* Arm once after reference context installation, before first host_enter.
 * Read only after successful enter, before any tick/leave; a failed note
 * remains explicit while original enter/phase updates finish for cleanup. */
int melee_web_menu_host_arm_first_css_return(MeleeWebMenuHost*, char*, size_t);
int melee_web_menu_host_first_css_return(MeleeWebMenuHost*,
    MeleeWebMenuCssReturnSnapshot*, char*, size_t);
#endif

/* Install only the agreed networked seed on a fresh unentered host. Requires
 * the canonical Everything mode, no personal profile and default PAD history. */
int melee_web_menu_host_apply_net_context(MeleeWebMenuHost*, uint32_t random_seed,
                                          char*, size_t);
/* Explicit native four-stock canonical setup fixture with the existing supported
 * stock timer. Copies rules on a fresh host, applied before original CSS; ordinary context restoration
 * retires it. This does not represent original Rules-menu input. */
int melee_web_menu_host_apply_initial_native_rules(
    MeleeWebMenuHost*, const GameRules*, char*, size_t);
/* Read-only copy of the session-owned CSS (returns 1) or SSS (returns 2)
 * selection payload and its small scalar header; 0 outside those scenes. */
int melee_web_menu_host_selection_state(const MeleeWebMenuHost*, StartMeleeData*,
                                        uint8_t header[6]);
/* Original raw PAD processing, scene callback, audio control and scheduler.
 * Returns 1 while active, 3 on an original transition request, 0 on failure. */
int melee_web_menu_host_tick(MeleeWebMenuHost*,const PADStatus[4],char*,size_t);
int melee_web_menu_host_draw(MeleeWebMenuHost*,char*,size_t);
/* Call after the final Aurora frame has submitted, before closing its world. */
int melee_web_menu_host_leave(MeleeWebMenuHost*,int abort_scene,char*,size_t);
/* Re-enter the original CSS after a checked CSS LR+Start -> GM_MENU route,
 * preserving the source VS payload and its mode lease. */
int melee_web_menu_host_reenter_css_after_parent(MeleeWebMenuHost*,MeleeWebAudio*,char*,size_t);
/* Current source scene: 0 when the host is between worlds, 1 CSS, 2 SSS,
 * 3 title, 4 main. */
int melee_web_menu_host_source_scene(const MeleeWebMenuHost*);
/* Read the original menu selection and the live VS rule/item values without
 * advancing source code or transferring any source-owned pointers. */
int melee_web_menu_host_source_observe(
    const MeleeWebMenuHost*, MeleeWebMenuSourceObservation*, char*, size_t);
/* Checked retail destination after leaving a title/main/CSS scene. Known
 * menu routes use the compact values GM_TITLE=0, GM_MENU=1, GM_VS=2; other
 * source modes retain their original mode id. */
int melee_web_menu_host_route_target_mode(const MeleeWebMenuHost*);
/* Original opening-mode state selected by the source title callback, or -1
 * unless the completed source route targets GM_OPENING_MV. */
int melee_web_menu_host_route_target_state(const MeleeWebMenuHost*);
/* Reads the separate configuration produced by the original VS-entry rules
 * and player preparation after CSS/SSS OnExit. */
int melee_web_menu_host_selection(const MeleeWebMenuHost*,MeleeWebMenuMatchSelection*,char*,size_t);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
/* Private development checkpoint: arms one fresh VS menu owner and reads its
 * source-selected Stadium payload for manifest preparation only. */
int melee_web_menu_host_enable_stadium_c1a(MeleeWebMenuHost*,char*,size_t);
int melee_web_menu_host_stadium_c1a_raw_selection(
    const MeleeWebMenuHost*,StartMeleeData*,char*,size_t);
int melee_web_menu_host_stadium_c1a_selection(const MeleeWebMenuHost*,
    MeleeWebMenuMatchSelection*,char*,size_t);
#endif
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
/* Follow original VS/Sudden Death callbacks. A Sudden Death continuation
 * retains the VS mode lease until sudden_death_finish and Results teardown. */
int melee_web_menu_host_match_continuation_begin(MeleeWebMenuHost*,
    const struct MatchExitInfo*,uint32_t random_seed,
    MeleeWebMenuMatchContinuation*,char*,size_t);
int melee_web_menu_host_sudden_death_finish(MeleeWebMenuHost*,
    uint64_t owner_id,const struct MatchExitInfo*,uint32_t random_seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],
    MeleeWebMenuMatchContinuation*,char*,size_t);
/* Read or claim the exact source SD continuation while its VS mode lease stays
 * with the host. The match owner must release its claim after world teardown. */
int melee_web_menu_host_sudden_death_selection(
    const MeleeWebMenuHost*,const MeleeWebMenuMatchContinuation*,
    MeleeWebMenuMatchSelection*,char*,size_t);
int melee_web_menu_host_sudden_death_match_claim(
    MeleeWebMenuHost*,const MeleeWebMenuMatchContinuation*,
    MeleeWebMenuMatchSelection*,char*,size_t);
int melee_web_menu_host_sudden_death_match_release(
    MeleeWebMenuHost*,uint64_t owner_id,char*,size_t);
/* Temporarily assigns the original GS_SUDDEN_DEATH scene-info identity while
 * its claimed native match owns the world. The matching end restores the exact
 * prior host-owned CSS/SSS scene-info pointer after teardown. */
int melee_web_menu_host_sudden_death_scene_begin(
    MeleeWebMenuHost*,uint64_t owner_id,char*,size_t);
int melee_web_menu_host_sudden_death_scene_end(
    MeleeWebMenuHost*,uint64_t owner_id,char*,size_t);
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
/* Borrowed exact post-VS mode state, only while the owned menus are closed. */
const VsModeData* melee_web_menu_host_post_vs_mode(const MeleeWebMenuHost*);
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
