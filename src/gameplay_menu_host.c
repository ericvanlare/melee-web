#include "gameplay_menu_host.h"
#include "gameplay_menu.h"
#include "gameplay_match_clock.h"
#include "gameplay_match_rules.h"
#include "gameplay_save_profile.h"
#include "gameplay_content.h"
#include "gameplay_bootstrap.h"
#include "gameplay_results_context.h"
#include "gameplay_prize_context.h"
#include "gameplay_audio_bank_transport.h"
#include "gameplay_source_files.h"
#include "hsd_native_joint.h"
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/forward.h>
#include <melee/gm/gmevent.h>
#include <melee/gm/gmmenu.h>
#include <melee/gm/gmscdata.h>
#include <melee/gm/gmmenumode.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmtitle.h>
#include <melee/gm/gmtitlemode.h>
#include <melee/gm/gmtrainingmode.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbsnap.h>
#include <melee/ty/toy.h>
#include <melee/ty/tydisplay.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/video.h>
#include <sysdolphin/baselib/random.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnitemsw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern GameRules gmMainLib_803D4A48;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
extern int melee_web_audio_is_active(MeleeWebAudio*);
extern int melee_web_menu_clock_begin(void);
extern int melee_web_menu_clock_request(int*);
extern int melee_web_menu_clock_tick(void);
extern int melee_web_menu_clock_present(void);
extern int melee_web_menu_clock_end(void);
extern int melee_web_vs_mode_begin(void);
extern int melee_web_vs_mode_end(void);
extern int melee_web_vs_mode_select_state(int);
extern int melee_web_vs_mode_next_state(void);
extern int melee_web_vs_mode_resolve_next_state(GameModeState*);
extern int melee_web_vs_mode_pending_mode(void);
extern int melee_web_vs_mode_set_route(int current_mode, int previous_mode);
extern int melee_web_menu_parent_route_pending(const MeleeWebMenuSession*);
extern GameModeState* melee_web_opening_mode_state(int id);
extern int melee_web_opening_mode_preload(int id);
extern int melee_web_opening_mode_next_state(void);
extern int melee_web_opening_mode_advance_state(void);
extern int melee_web_opening_preview(int characters[4], unsigned char costumes[4],
                                     int* stage_kind, int* match_kind);
extern struct GameSceneInfo* melee_web_current_scene_info(void);
enum {
    MELEE_WEB_HOST_SCENE_NONE = 0,
    MELEE_WEB_HOST_SCENE_CSS = 1,
    MELEE_WEB_HOST_SCENE_SSS = 2,
    MELEE_WEB_HOST_SCENE_TITLE = 3,
    MELEE_WEB_HOST_SCENE_MAIN = 4,
    MELEE_WEB_HOST_SCENE_OPENING = 5,
    MELEE_WEB_HOST_SCENE_OPENING_VS = 6,
};
enum { HOST_SSS_CONTINUATION_EMPTY = 0, HOST_SSS_CONTINUATION_AVAILABLE = 1, HOST_SSS_CONTINUATION_USED = 2 };
struct MeleeWebMenuHost {
    MeleeWebMenuSession* session;
    MeleeWebSaveProfileOwner* profile;
    int save_mode;
    int baseline_profile_ready;
    size_t configured_profile_size;
    uint8_t configured_profile[MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    uint8_t baseline_profile[MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    MeleeWebAudio* audio;
    uint64_t generation,audio_generation;
    u32 seed,*saved_seed;
    HSD_PadData queue;
    PadLibData saved_library;
    HSD_PadStatus saved_game[4],saved_master[4],saved_copy[4];
    MeleeWebPadState* input;
    GameRules saved_rules;
    GameRules selected_rules, route_saved_rules;
    MeleeWebSaveProfilePreferences persisted_preferences;
    MeleeWebSaveProfilePreferences runtime_preferences;
    struct gmm_x1CB0 saved_preferences;
    struct gmm_x1CB0 selected_preferences, route_saved_preferences;
    int saved_language,saved_saved_language;
    u16 saved_characters,saved_stages;
    u16 selected_characters,selected_stages;
    u16 route_saved_characters,route_saved_stages;
    uint8_t initial_game_rules[0x18];
    uint8_t initial_save_data[0x55E8];
    int initial_replay_context;
    int net_start_context;
    int initial_native_rules;
    GameRules native_rules;
    VsModeData route_saved_vs;
    MatchExitInfo route_saved_exit;
    ResultsMatchInfo route_saved_result;
    ChallengerData route_saved_challenger;
    u8 route_saved_ko[GM_MAX_PLAYERS];
    /* Persistent source mode/scene payloads.  The source callbacks retain
     * GameSceneInfo through every tick, so gm_GetCurrentSceneExitData never
     * observes a stack object or a browser-owned substitute. */
    struct GameSceneInfo* saved_scene_info;
    GameModeState source_state;
    GameScene* opening_scene_handler;
    GameSceneInfo source_scene_info;
    GameModeState vs_css_state;
    GameModeState vs_sss_state;
    GameModeState training_css_state;
    GameModeState training_sss_state;
    MenuEnterData main_enter;
    MenuExitData main_exit;
    int title_exit_payload;
    int source_scene;
    int source_target_mode;
    int source_previous_mode;
    int source_mode_kind;
    int vs_mode_owned;
    int training_mode_initialized;
    int opening_active;
    int opening_scene_entered;
    int opening_match_suspended;
    int opening_state_id;
    int opening_state_exit_called;
    int aborted_source_scene;
    int css_parent_route_requested;
    int training_start_pending;
    int sss_continuation_state;
    MeleeWebMenuClockCounters sss_continuation_counters;
    MeleeWebMenuSession* sss_continuation_session;
    MeleeWebAudio* sss_continuation_audio;
    uint64_t sss_continuation_audio_generation;
    uint64_t sss_continuation_departed_world_generation;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    int first_css_return_state; /* 0 unarmed, 1 armed, 2 captured, 3 invalid */
    uint64_t first_css_return_generation;
    MeleeWebMenuCssReturnSnapshot first_css_return;
    char first_css_return_error[160];
    /* Result-3 CSS handoff witness, followed by a single-use terminal-draw
     * authorization. The witness records only native owner identities and
     * the exact raw PAD values consumed by the immediately preceding tick. */
    int final_pending_css_draw_witness;
    int final_pending_css_draw_state; /* 0 unused, 1 armed, 2 consumed/invalid */
    int final_pending_css_draw_returned;
    int final_pending_css_draw_tick_result;
    int final_pending_css_draw_request;
    int final_pending_css_draw_pending_scene_change;
    unsigned final_pending_css_draw_input_ordinal;
    unsigned final_pending_css_draw_pad_sequence;
    PADStatus final_pending_css_draw_raw[4];
    uint64_t final_pending_css_draw_generation;
    MeleeWebMenuSession* final_pending_css_draw_session;
    CSSData* final_pending_css_draw_css;
    struct GameSceneInfo* final_pending_css_draw_scene_info;
    uint32_t* final_pending_css_draw_seed_owner;
    uint32_t final_pending_css_draw_seed;
    int first_sss_pair_state; /* 0 idle, 1 CSS armed, 2 SSS eligible, 3 entry, 4 captured, 5 failed, 6 host-entered */
    MeleeWebAudio* first_sss_pair_audio_owner;
    uint64_t first_sss_pair_audio_generation;
    uint64_t first_sss_pair_world_generation;
    unsigned first_sss_pair_host_tick_calls;
    unsigned first_sss_pair_host_draw_calls;
    MeleeWebMenuFirstSssPairNoteSnapshot first_sss_pair_entry;
    MeleeWebMenuFirstSssPairNoteSnapshot first_sss_pair_returned;
    char first_sss_pair_error[160];
    int stadium_c1a_enabled;
#endif
    int results_active,results_exited,results_committed,prize_active;
    int sudden_death_active;
    int sudden_death_claimed;
    int sudden_death_claim_consumed;
    uint64_t sudden_death_owner_id;
    StartMeleeData sudden_death_start;
    GameSceneInfo sudden_death_scene_info;
    GameSceneInfo* sudden_death_saved_scene_info;
    int sudden_death_scene_active;
    StartMeleeData route_saved_start;
    MatchExitInfo route_saved_sudden_death_exit;
    int entered,drawing,transition;
};
static MeleeWebMenuHost* owner;
static uint64_t next_sudden_death_owner_id=1;
static int fail(char* e,size_t n,const char* text){if(e&&n)snprintf(e,n,"%s",text);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static GameModeState* training_state_for_scene(MeleeWebMenuScene scene)
{
    const int state_id = scene == MELEE_WEB_MENU_SCENE_CSS ? 0 :
                         scene == MELEE_WEB_MENU_SCENE_SSS ? 1 : -1;
    GameModeState* state;

    if (state_id < 0) return NULL;
    for (state = gm_Mode_Training_States; state->id != (u8) -1; ++state) {
        if (state->id == (u8) state_id) return state;
    }
    return NULL;
}
static void host_reset_preload_scene_aliases(void)
{
    /* These are the non-heap side effects of gm_1A3F's preloadState. The
     * narrow menu owner has no source DVD heap/cache, but its scene aliases
     * must still start clean before an authored Opening OnEnter callback. */
    lb_8001C5A4();
    lb_8001D1F4();
    lbSnap_8001E27C();
    Toy_803127D4();
    tyDisplay_8031C8B8();
}
static int live(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->audio||!h->generation||
       h->generation!=melee_web_gameplay_stats().generation||
       !melee_web_audio_is_active(h->audio)||!melee_web_audio_bank_transport_active())
        return fail(e,n,"Native menu world/audio ownership changed");
    return melee_web_save_profile_owner_live(h->profile,e,n);
}
static void host_sss_continuation_invalidate(MeleeWebMenuHost* h)
{
    if (h != NULL && h == owner &&
        h->sss_continuation_state == HOST_SSS_CONTINUATION_AVAILABLE)
        h->sss_continuation_state = HOST_SSS_CONTINUATION_USED;
}

static int host_sss_continuation_owner(MeleeWebMenuHost* h, char* e, size_t n)
{
    if (h == NULL || h != owner) {
        host_sss_continuation_invalidate(owner);
        return fail(e, n, "Native menu host is not the owned continuation host");
    }
    return 1;
}

static int host_sss_continuation_alternative_entry(MeleeWebMenuHost* h,
                                                    char* e, size_t n)
{
    if (!host_sss_continuation_owner(h, e, n)) return 0;
    host_sss_continuation_invalidate(h);
    return 1;
}

static int host_sss_continuation_arm(MeleeWebMenuHost* h, char* e, size_t n)
{
    MeleeWebMenuClockCounters counters;
    CSSData* css;
    GameModeState* authored_sss;

    if (!host_sss_continuation_owner(h, e, n)) return 0;
    /* A new eligible CSS leave is the only operation that can rearm a used
     * token. Invalidate first so any later read/capture failure stays closed. */
    h->sss_continuation_state = HOST_SSS_CONTINUATION_USED;
    if (!h->session)
        return fail(e,n,"CSS-to-SSS counter continuation lost its menu session");
    css = (CSSData*) melee_web_menu_css(h->session);
    authored_sss = &gm_Mode_Vs_States[gmVsMode_State_Sss];
    if (!h->audio || !h->generation || !h->audio_generation ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned || h->entered ||
        h->source_scene != MELEE_WEB_HOST_SCENE_NONE || h->transition ||
        h->source_target_mode != -1 || css == NULL ||
        css->pending_scene_change != 1 ||
        h->source_scene_info.scene_kind != GS_CSS ||
        h->source_scene_info.enter_data != css ||
        h->source_scene_info.exit_data != css ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_SSS_READY ||
        gm_GetCurrentGameMode() != GM_VS ||
        gm_GetCurrentSceneIndex() != gmVsMode_State_Sss ||
        authored_sss->id != gmVsMode_State_Sss ||
        authored_sss->info.scene_kind != GS_SSS ||
        melee_web_vs_mode_pending_mode() != -1 ||
        h->generation != melee_web_gameplay_stats().generation ||
        !melee_web_audio_is_active(h->audio) ||
        !melee_web_audio_bank_transport_active() ||
        melee_web_audio_generation(h->audio) != h->audio_generation ||
        !melee_web_menu_clock_capture_counters(&counters))
        return fail(e, n, "CSS-to-SSS counter continuation could not be captured");

    h->sss_continuation_counters = counters;
    h->sss_continuation_session = h->session;
    h->sss_continuation_audio = h->audio;
    h->sss_continuation_audio_generation = h->audio_generation;
    h->sss_continuation_departed_world_generation = h->generation;
    h->sss_continuation_state = HOST_SSS_CONTINUATION_AVAILABLE;
    return ok(e, n);
}

static int host_sss_continuation_claim(MeleeWebMenuHost* h,
    MeleeWebAudio* audio, MeleeWebMenuPhase phase, uint64_t world_generation,
    MeleeWebMenuClockCounters* out, char* e, size_t n)
{
    MeleeWebMenuClockCounters captured_counters;
    MeleeWebMenuSession* expected_session;
    MeleeWebAudio* expected_audio;
    CSSData* css;
    GameModeState* authored_sss;
    uint64_t expected_audio_generation;
    uint64_t departed_world_generation;

    if (!host_sss_continuation_owner(h, e, n)) return 0;
    if (h->sss_continuation_state != HOST_SSS_CONTINUATION_AVAILABLE)
        return fail(e, n, "Ordinary VS SSS entry has no available CSS continuation");

    /* Copy the real scalar boundary and permanently consume before checking
     * the output pointer or any destination owner, so every attempt is one-use. */
    captured_counters = h->sss_continuation_counters;
    h->sss_continuation_state = HOST_SSS_CONTINUATION_USED;
    expected_session = h->sss_continuation_session;
    expected_audio = h->sss_continuation_audio;
    expected_audio_generation = h->sss_continuation_audio_generation;
    departed_world_generation = h->sss_continuation_departed_world_generation;
    css = h->session != NULL ? (CSSData*) melee_web_menu_css(h->session) : NULL;
    authored_sss = &gm_Mode_Vs_States[gmVsMode_State_Sss];

    if (phase != MELEE_WEB_MENU_SSS_READY || h->session != expected_session ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        h->source_scene != MELEE_WEB_HOST_SCENE_NONE || h->entered ||
        h->transition || h->source_target_mode != -1 || h->audio != NULL ||
        h->generation != 0 || audio == NULL || audio != expected_audio ||
        !h->audio_generation || h->audio_generation != expected_audio_generation ||
        !world_generation || world_generation == departed_world_generation ||
        !melee_web_audio_is_active(audio) ||
        melee_web_audio_generation(audio) != expected_audio_generation ||
        !melee_web_audio_bank_transport_active() || seed_ptr != &h->seed ||
        gm_GetCurrentGameMode() != GM_VS ||
        gm_GetCurrentSceneIndex() != gmVsMode_State_Sss ||
        authored_sss->id != gmVsMode_State_Sss ||
        authored_sss->info.scene_kind != GS_SSS || css == NULL ||
        css->pending_scene_change != 1 ||
        h->source_scene_info.scene_kind != GS_CSS ||
        h->source_scene_info.enter_data != css ||
        h->source_scene_info.exit_data != css ||
        melee_web_vs_mode_pending_mode() != -1 || out == NULL)
        return fail(e, n, "CSS-to-SSS continuation owner or fresh world changed");
    *out = captured_counters;
    return ok(e, n);
}

static int host_commit_vs_css_sss_route(MeleeWebMenuHost* h, CSSData* css,
                                         char* e, size_t n)
{
    GameModeState* authored_css;
    GameModeState* authored_sss;
    int next;

    if (h == NULL || h != owner || css == NULL || !h->session ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        h->source_scene != MELEE_WEB_HOST_SCENE_CSS ||
        melee_web_menu_css(h->session) != css ||
        h->source_scene_info.scene_kind != GS_CSS ||
        h->source_scene_info.enter_data != css ||
        h->source_scene_info.exit_data != css ||
        gm_GetCurrentGameMode() != GM_VS ||
        gm_GetCurrentSceneIndex() != gmVsMode_State_Css ||
        css->pending_scene_change != 1 ||
        melee_web_vs_mode_pending_mode() != -1)
        return fail(e, n, "CSS SSS route has no exact live VS CSS owner");

    authored_css = &gm_Mode_Vs_States[gmVsMode_State_Css];
    if (authored_css->id != gmVsMode_State_Css ||
        authored_css->info.scene_kind != GS_CSS)
        return fail(e, n, "CSS SSS route has no authored CSS table row");
    next = melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
    authored_sss = &gm_Mode_Vs_States[gmVsMode_State_Sss];
    if (next != gmVsMode_State_Sss || authored_sss->id != next ||
        authored_sss->info.scene_kind != GS_SSS)
        return fail(e, n, "CSS requested a non-authored SSS state");
    if (!melee_web_vs_mode_select_state(next))
        return fail(e, n, "CSS SSS state could not be committed to VS routing");
    h->source_target_mode = -1;
    return ok(e, n);
}
static int host_prepare_vs_sss_cancel_css_route(MeleeWebMenuHost* h,
                                                 SSSData* sss,
                                                 char* e, size_t n)
{
    GameModeState* authored_css;
    GameModeState* authored_sss;

    if (h == NULL || h != owner || h->session == NULL || sss == NULL ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        h->source_scene != MELEE_WEB_HOST_SCENE_SSS ||
        h->source_target_mode != -1 ||
        melee_web_menu_sss(h->session) != sss || sss->start_game ||
        h->source_scene_info.scene_kind != GS_SSS ||
        h->source_scene_info.enter_data != sss ||
        h->source_scene_info.exit_data != sss ||
        gm_GetCurrentGameMode() != GM_VS ||
        gm_GetCurrentSceneIndex() != gmVsMode_State_Sss ||
        melee_web_vs_mode_pending_mode() != -1 ||
        melee_web_vs_mode_next_state() != -1)
        return fail(e, n, "Original VS SSS cancel has no exact live route owner");

    authored_css = &gm_Mode_Vs_States[gmVsMode_State_Css];
    authored_sss = &gm_Mode_Vs_States[gmVsMode_State_Sss];
    if (authored_css->id != gmVsMode_State_Css ||
        authored_css->info.scene_kind != GS_CSS ||
        authored_sss->id != gmVsMode_State_Sss ||
        authored_sss->info.scene_kind != GS_SSS ||
        h->vs_sss_state.id != gmVsMode_State_Sss ||
        h->vs_sss_state.info.scene_kind != GS_SSS ||
        h->vs_sss_state.info.exit_data != sss)
        return fail(e, n, "Original VS SSS cancel has no authored CSS/SSS rows");
    return ok(e, n);
}

static int host_commit_vs_sss_cancel_css_route(MeleeWebMenuHost* h,
                                                SSSData* sss,
                                                char* e, size_t n)
{
    GameModeState* authored_css;
    GameModeState* authored_sss;
    int next;

    if (h == NULL || h != owner || h->session == NULL || sss == NULL ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        h->source_scene != MELEE_WEB_HOST_SCENE_SSS ||
        h->source_target_mode != -1 || sss->start_game ||
        melee_web_menu_sss(h->session) != sss ||
        h->source_scene_info.scene_kind != GS_SSS ||
        h->source_scene_info.enter_data != sss ||
        h->source_scene_info.exit_data != sss ||
        gm_GetCurrentGameMode() != GM_VS ||
        gm_GetCurrentSceneIndex() != gmVsMode_State_Sss ||
        melee_web_vs_mode_pending_mode() != -1 ||
        melee_web_vs_mode_next_state() != gmVsMode_State_Css)
        return fail(e, n, "Original VS SSS cancel did not queue its authored CSS route");

    authored_css = &gm_Mode_Vs_States[gmVsMode_State_Css];
    authored_sss = &gm_Mode_Vs_States[gmVsMode_State_Sss];
    next = melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
    if (authored_css->id != gmVsMode_State_Css ||
        authored_css->info.scene_kind != GS_CSS ||
        authored_sss->id != gmVsMode_State_Sss ||
        authored_sss->info.scene_kind != GS_SSS ||
        next != gmVsMode_State_Css)
        return fail(e, n, "Original VS SSS cancel resolved outside authored CSS");
    if (!melee_web_vs_mode_select_state(next))
        return fail(e, n, "Original VS SSS cancel could not commit authored CSS");
    h->source_target_mode = -1;
    return ok(e, n);
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static void first_sss_pair_fail(MeleeWebMenuHost* h, const char* text)
{
    if (h == NULL || h != owner) return;
    h->first_sss_pair_state = 5;
    if (h->first_sss_pair_error[0] == 0 && text != NULL)
        snprintf(h->first_sss_pair_error, sizeof(h->first_sss_pair_error), "%s", text);
}

static void final_pending_css_draw_invalidate(MeleeWebMenuHost* h)
{
    if (h != NULL && h == owner) {
        h->final_pending_css_draw_witness = 0;
        h->final_pending_css_draw_returned = 0;
        if (h->final_pending_css_draw_state == 1)
            h->final_pending_css_draw_state = 2;
    }
}

static int final_pending_css_pad_equal(const PADStatus* a,
                                       const PADStatus* b)
{
    return a != NULL && b != NULL &&
        a->button == b->button && a->stickX == b->stickX &&
        a->stickY == b->stickY && a->substickX == b->substickX &&
        a->substickY == b->substickY && a->triggerLeft == b->triggerLeft &&
        a->triggerRight == b->triggerRight && a->analogA == b->analogA &&
        a->analogB == b->analogB && a->err == b->err
#ifdef TARGET_PC
        && a->extButton == b->extButton
#endif
        ;
}

static void final_pending_css_pad_copy(PADStatus* destination,
                                       const PADStatus* source)
{
    memset(destination, 0, sizeof(*destination));
    destination->button = source->button;
    destination->stickX = source->stickX;
    destination->stickY = source->stickY;
    destination->substickX = source->substickX;
    destination->substickY = source->substickY;
    destination->triggerLeft = source->triggerLeft;
    destination->triggerRight = source->triggerRight;
    destination->analogA = source->analogA;
    destination->analogB = source->analogB;
    destination->err = source->err;
#ifdef TARGET_PC
    destination->extButton = source->extButton;
#endif
}

static int final_pending_css_draw_owner_live(MeleeWebMenuHost* h,
                                             char* e, size_t n)
{
    const CSSData* css;
    unsigned port;
    if (!live(h, e, n) || !h->entered || h->drawing ||
        h->source_scene != MELEE_WEB_HOST_SCENE_CSS ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        !h->session || gm_GetCurrentGameMode() != GM_VS ||
        HSD_GObj_804D781C || HSD_GObj_804D7838 || HSD_GObj_804D7830 ||
        HSD_GObj_804D7814 || HSD_GObj_804D7818 ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_CSS ||
        h->transition == 0 || h->transition != h->final_pending_css_draw_request ||
        h->generation != h->final_pending_css_draw_generation ||
        h->session != h->final_pending_css_draw_session ||
        seed_ptr == NULL || seed_ptr != h->final_pending_css_draw_seed_owner ||
        seed_ptr != &h->seed || h->seed != h->final_pending_css_draw_seed ||
        h->final_pending_css_draw_scene_info != &h->source_scene_info ||
        melee_web_current_scene_info() != &h->source_scene_info ||
        h->source_scene_info.scene_kind != GS_CSS) {
        return fail(e, n,
                    "Final CSS draw lost its retained live transition owner");
    }
    css = melee_web_menu_css(h->session);
    if (css == NULL || css != h->final_pending_css_draw_css ||
        h->source_scene_info.enter_data != css ||
        h->source_scene_info.exit_data != css ||
        css->pending_scene_change != h->final_pending_css_draw_pending_scene_change ||
        h->final_pending_css_draw_tick_result !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
        HSD_PadLibData.queue != &h->queue || HSD_PadLibData.qcount != 0) {
        return fail(e, n,
                    "Final CSS draw lost the exact retained result-3 input");
    }
    for (port = 0; port < 4; ++port) {
        if (!final_pending_css_pad_equal(&h->final_pending_css_draw_raw[port],
                                         &h->queue.stat[port])) {
            return fail(e, n,
                        "Final CSS draw PAD no longer matches its consumed input");
        }
    }
    return ok(e, n);
}

static void first_css_return_note(void* data, MeleeWebMenuSession* session,
    const CSSData* css, const uint8_t* ko_counts)
{
    MeleeWebMenuHost* h = data;
    /* Refuse foreign pointers before dereferencing; failure never interrupts
     * the original callback return or the session's subsequent phase updates. */
    if (!h || h != owner) return;
    if (h->first_css_return_state != 1 || !session || !css || session != h->session ||
        css != melee_web_menu_css(h->session) || !ko_counts ||
        css->ko_counts != ko_counts ||
        ko_counts != gmVsMelee_GetKOCounts() || h->entered || h->drawing ||
        !h->initial_replay_context || seed_ptr != &h->seed ||
        melee_web_gameplay_stats().ticks != 0 ||
        HSD_GObj_804D781C || HSD_GObj_804D7838 || HSD_GObj_804D7830 ||
        HSD_GObj_804D7814 || HSD_GObj_804D7818 ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_CREATED ||
        h->source_scene != MELEE_WEB_HOST_SCENE_CSS ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        gm_GetCurrentGameMode() != GM_VS ||
        melee_web_current_scene_info() != &h->source_scene_info ||
        h->source_scene_info.scene_kind != GS_CSS ||
        h->source_scene_info.enter_data != css ||
        h->source_scene_info.exit_data != css) {
        h->first_css_return_state = 3;
        fail(h->first_css_return_error, sizeof(h->first_css_return_error),
             "First CSS return lost its exact session/scene/KO/RNG owner");
        return;
    }
    if (!live(h, h->first_css_return_error, sizeof(h->first_css_return_error))) {
        h->first_css_return_state = 3;
        return;
    }
    h->first_css_return.css = *css;
    h->first_css_return.css.ko_counts = NULL;
    memcpy(h->first_css_return.ko_counts, ko_counts, GM_MAX_PLAYERS);
    melee_web_pad_state_capture(h->first_css_return.pad_state);
    h->first_css_return.random_seed = h->seed;
    h->first_css_return.source_scene = h->source_scene;
    h->first_css_return.source_scene_kind = h->source_scene_info.scene_kind;
    h->first_css_return_generation = h->generation;
    h->first_css_return_state = 2;
}

int melee_web_menu_host_arm_first_css_return(MeleeWebMenuHost* h,
    char* e, size_t n)
{
    if (!h || h != owner || !h->session || h->entered || h->audio ||
        h->drawing || h->source_scene != MELEE_WEB_HOST_SCENE_NONE ||
        !h->initial_replay_context || seed_ptr != &h->seed ||
        h->first_css_return_state ||
        HSD_GObj_804D781C || HSD_GObj_804D7838 || HSD_GObj_804D7830 ||
        HSD_GObj_804D7814 || HSD_GObj_804D7818 ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_CREATED)
        return fail(e,n,"First CSS return arm requires its unentered replay-context host");
    if (!melee_web_save_profile_owner_live(h->profile,e,n) ||
        !melee_web_menu_arm_first_css_return(h->session,first_css_return_note,e,n))
        return 0;
    h->first_css_return_state = 1;
    return ok(e,n);
}

int melee_web_menu_host_first_css_return(MeleeWebMenuHost* h,
    MeleeWebMenuCssReturnSnapshot* out, char* e, size_t n)
{
    if (!h || h != owner || !out)
        return fail(e,n,"First CSS return read requires its exact host and output");
    if (h->first_css_return_state == 3 && h->first_css_return_error[0])
        return fail(e,n,h->first_css_return_error);
    if (!live(h,e,n)) return 0;
    if (h->first_css_return_state != 2 || !h->entered || h->drawing ||
        h->generation != h->first_css_return_generation ||
        melee_web_gameplay_stats().ticks != 0 ||
        HSD_GObj_804D781C || HSD_GObj_804D7838 || HSD_GObj_804D7830 ||
        HSD_GObj_804D7814 || HSD_GObj_804D7818 ||
        seed_ptr != &h->seed ||
        h->source_scene != MELEE_WEB_HOST_SCENE_CSS ||
        !melee_web_menu_first_css_return_live(h->session) ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        gm_GetCurrentGameMode() != GM_VS ||
        melee_web_menu_css(h->session)->ko_counts != gmVsMelee_GetKOCounts() ||
        melee_web_current_scene_info() != &h->source_scene_info ||
        h->source_scene_info.scene_kind != GS_CSS)
        return fail(e,n,"First CSS return snapshot is absent or no longer before the first tick");
    *out = h->first_css_return;
    return ok(e,n);
}

static void first_sss_pair_note(void* data, MeleeWebMenuSession* session,
    const SSSData* sss, uint64_t session_ticks, int boundary)
{
    MeleeWebMenuHost* h = data;
    MeleeWebMenuFirstSssPairNoteSnapshot snapshot;
    MeleeWebMenuFirstSssPairNoteSnapshot* destination;
    const int expected_state = boundary == MELEE_WEB_MENU_SSS_PAIR_ENTRY ? 2 : 3;
    const int note_phase = boundary == MELEE_WEB_MENU_SSS_PAIR_ENTRY ? 1 : 2;
    const struct GameSceneInfo* current_info;
    if (h == NULL || h != owner) return;
    if (boundary != MELEE_WEB_MENU_SSS_PAIR_ENTRY &&
        boundary != MELEE_WEB_MENU_SSS_PAIR_RETURN) {
        first_sss_pair_fail(h, "SSS constructor note has an unknown boundary");
        return;
    }
    destination = boundary == MELEE_WEB_MENU_SSS_PAIR_ENTRY ?
        &h->first_sss_pair_entry : &h->first_sss_pair_returned;
    if (destination->captured) {
        first_sss_pair_fail(h, "Duplicate SSS constructor note refused; prior note retained");
        return;
    }
    if (h->first_sss_pair_state != expected_state || h->entered || h->drawing) {
        first_sss_pair_fail(h,
            "SSS constructor note arrived outside its exact one-use boundary");
        return;
    }
    if (!live(h, h->first_sss_pair_error, sizeof(h->first_sss_pair_error))) {
        first_sss_pair_fail(h, "SSS constructor note lost its live world/audio owner");
        return;
    }
    const int session_matches = session != NULL && session == h->session;
    const int payload_matches = session_matches && sss != NULL &&
        sss == melee_web_menu_sss(session);
    const int info_matches = melee_web_current_scene_info() == &h->source_scene_info;
    current_info = info_matches ? &h->source_scene_info : NULL;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.phase = note_phase;
    snapshot.host_entered = h->entered;
    snapshot.session_phase = session_matches ? melee_web_menu_phase(session) :
        MELEE_WEB_MENU_CLOSED;
    snapshot.source_scene = h->source_scene;
    snapshot.scene_kind = h->source_scene_info.scene_kind;
    snapshot.world_generation = melee_web_gameplay_stats().generation;
    snapshot.audio_generation = h->audio_generation;
    snapshot.session_ticks = session_ticks;
    snapshot.owners[0] = 1;
    snapshot.owners[1] = session_matches;
    snapshot.owners[2] = h->generation != 0 &&
        h->generation == snapshot.world_generation;
    snapshot.owners[3] = h->audio != NULL && h->audio == h->first_sss_pair_audio_owner &&
        h->audio_generation == h->first_sss_pair_audio_generation &&
        melee_web_audio_generation(h->audio) == h->first_sss_pair_audio_generation &&
        melee_web_audio_is_active(h->audio) && melee_web_audio_bank_transport_active();
    snapshot.owners[4] = h->vs_mode_owned && h->source_mode_kind == GM_VS &&
        gm_GetCurrentGameMode() == GM_VS;
    snapshot.owners[5] = info_matches && h->source_scene_info.scene_kind == GS_SSS;
    snapshot.owners[6] = payload_matches &&
        h->source_scene_info.enter_data == sss &&
        h->source_scene_info.exit_data == sss;
    snapshot.owners[7] = seed_ptr != NULL && seed_ptr == &h->seed;
    if (h->first_sss_pair_state != expected_state || boundary < 1 || boundary > 2 ||
        !session_matches || !payload_matches ||
        snapshot.session_phase != MELEE_WEB_MENU_SSS_READY ||
        h->source_scene != MELEE_WEB_HOST_SCENE_SSS ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        gm_GetCurrentGameMode() != GM_VS || current_info == NULL ||
        current_info->enter_data != sss || current_info->exit_data != sss ||
        !snapshot.owners[2] || !snapshot.owners[3] || !snapshot.owners[4] ||
        !snapshot.owners[5] || !snapshot.owners[6] || !snapshot.owners[7] ||
        HSD_GObj_804D781C || HSD_GObj_804D7838 || HSD_GObj_804D7830 ||
        HSD_GObj_804D7814 || HSD_GObj_804D7818 ||
        HSD_PadLibData.queue != &h->queue || HSD_PadLibData.qcount != 0) {
        first_sss_pair_fail(h,
            "SSS constructor note lost its exact idle session/world/audio/scene owner");
        return;
    }
    if (boundary == MELEE_WEB_MENU_SSS_PAIR_ENTRY &&
        snapshot.world_generation == h->final_pending_css_draw_generation) {
        first_sss_pair_fail(h,
            "SSS constructor notes retained the retired CSS world generation");
        return;
    }
    if (h->first_sss_pair_world_generation == 0)
        h->first_sss_pair_world_generation = snapshot.world_generation;
    if (snapshot.world_generation != h->first_sss_pair_world_generation) {
        first_sss_pair_fail(h, "SSS constructor world generation changed between notes");
        return;
    }
    snapshot.scene_frame = gm_801A4BA8();
    snapshot.random_seed = h->seed;
    melee_web_pad_state_capture(snapshot.pad_state);
    snapshot.scene_routing_getters[0] = gm_GetCurrentGameMode();
    snapshot.scene_routing_getters[1] = gm_GetPreviousGameMode();
    snapshot.scene_routing_getters[2] = gm_GetCurrentSceneIndex();
    snapshot.scene_routing_getters[3] = gm_GetPreviousSceneIndex();
    snapshot.sss = *sss;
    snapshot.captured = 1;
    *destination = snapshot;
    h->first_sss_pair_state = boundary == MELEE_WEB_MENU_SSS_PAIR_ENTRY ? 3 : 4;
}

static int first_sss_pair_css_owner_live(MeleeWebMenuHost* h,
                                         char* e, size_t n)
{
    if (!live(h, e, n) || !h->session || !h->entered || h->drawing ||
        h->source_scene != MELEE_WEB_HOST_SCENE_CSS ||
        h->source_mode_kind != GM_VS || !h->vs_mode_owned ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_CSS ||
        h->final_pending_css_draw_state != 2 ||
        h->final_pending_css_draw_returned != 1 ||
        !final_pending_css_draw_owner_live(h, e, n)) {
        return fail(e, n,
                    "First SSS pair requires the completed retained final CSS draw");
    }
    return ok(e, n);
}

int melee_web_menu_host_arm_first_sss_pair(MeleeWebMenuHost* h,
                                            char* e, size_t n)
{
    if (!h || h != owner || h->first_sss_pair_state != 0 ||
        h->first_sss_pair_audio_owner != NULL ||
        !first_sss_pair_css_owner_live(h, e, n)) {
        return fail(e, n,
                    "First SSS pair arm requires its completed final CSS draw");
    }
    if (!melee_web_menu_arm_first_sss_pair(h->session, first_sss_pair_note,
                                            e, n)) {
        return 0;
    }
    h->first_sss_pair_audio_owner = h->audio;
    h->first_sss_pair_audio_generation = h->audio_generation;
    h->first_sss_pair_world_generation = 0;
    h->first_sss_pair_host_tick_calls = 0;
    h->first_sss_pair_host_draw_calls = 0;
    memset(&h->first_sss_pair_entry, 0, sizeof(h->first_sss_pair_entry));
    memset(&h->first_sss_pair_returned, 0, sizeof(h->first_sss_pair_returned));
    h->first_sss_pair_error[0] = 0;
    h->first_sss_pair_state = 1;
    return ok(e, n);
}

int melee_web_menu_host_first_sss_pair(
    const MeleeWebMenuHost* h, MeleeWebMenuFirstSssPairObservation* out,
    char* e, size_t n)
{
    if (!h || h != owner || !out) {
        return fail(e, n,
                    "First SSS pair observation requires its exact host and output");
    }
    memset(out, 0, sizeof(*out));
    out->state = h->first_sss_pair_state == 0 ? 0 :
        h->first_sss_pair_state == 5 ? 3 :
        h->first_sss_pair_state == 6 ? 2 : 1;
    out->host_tick_calls = h->first_sss_pair_host_tick_calls;
    out->host_draw_calls = h->first_sss_pair_host_draw_calls;
    snprintf(out->error, sizeof(out->error), "%s", h->first_sss_pair_error);
    out->entry = h->first_sss_pair_entry;
    out->returned = h->first_sss_pair_returned;
    return ok(e, n);
}
#endif

static int runtime_check(void* data,MeleeWebMenuScene scene,char* e,size_t n){
    (void)scene;return live(data,e,n);
}
static int runtime_scheduler(void* data,char* e,size_t n){
    if(!live(data,e,n))return 0;
    if(!melee_web_source_files_pump(e,n))return 0;
    lbAudioAx_80027DF8();
    if(!melee_web_gameplay_step(e,n))return 0;
    if(!melee_web_menu_clock_tick())return fail(e,n,"Unsupported native menu pause/control state");
    return 1;
}
static int runtime_transition(void* data,MeleeWebMenuScene scene,int* request,char* e,size_t n){
    MeleeWebMenuHost* h=data;
    if(!live(h,e,n)||!melee_web_menu_clock_request(request))return fail(e,n,"Invalid original menu transition state");
    h->css_parent_route_requested = 0;
    if (scene == MELEE_WEB_MENU_SCENE_CSS && *request != 0 &&
        (gm_GetCurrentGameMode() == GM_VS || h->source_mode_kind == GM_TRAINING) &&
        melee_web_vs_mode_pending_mode() == GM_MENU) {
        if (!melee_web_menu_mark_css_parent_route(h->session, e, n)) {
            return 0;
        }
        h->css_parent_route_requested = 1;
    }
    h->transition=*request;return 1;
}

/* A few preference fields are normalized while a browser-owned source world
 * starts. Keep those runtime values as the comparison baseline, then accept
 * later source menu edits into the persistent preference overlay. */
static int remember_runtime_preferences(MeleeWebMenuHost* h, char* e, size_t n)
{
    return melee_web_save_profile_owner_capture_preferences(
        h->profile, &h->runtime_preferences, e, n);
}

static int sync_source_preference_changes(MeleeWebMenuHost* h, char* e, size_t n)
{
    MeleeWebSaveProfilePreferences current;
    if (!melee_web_save_profile_owner_capture_preferences(
            h->profile, &current, e, n)) {
        return 0;
    }
    if (current.item_frequency != h->runtime_preferences.item_frequency) {
        h->persisted_preferences.item_frequency = current.item_frequency;
        h->runtime_preferences.item_frequency = current.item_frequency;
    }
    if (current.item_mask != h->runtime_preferences.item_mask) {
        h->persisted_preferences.item_mask = current.item_mask;
        h->runtime_preferences.item_mask = current.item_mask;
    }
    if (memcmp(current.rumble_enabled, h->runtime_preferences.rumble_enabled,
               sizeof(current.rumble_enabled)) != 0) {
        memcpy(h->persisted_preferences.rumble_enabled, current.rumble_enabled,
               sizeof(current.rumble_enabled));
        memcpy(h->runtime_preferences.rumble_enabled, current.rumble_enabled,
               sizeof(current.rumble_enabled));
    }
    if (current.deflicker != h->runtime_preferences.deflicker) {
        h->persisted_preferences.deflicker = current.deflicker;
        h->runtime_preferences.deflicker = current.deflicker;
    }
    if (current.saved_language != h->runtime_preferences.saved_language) {
        h->persisted_preferences.saved_language = current.saved_language;
        h->runtime_preferences.saved_language = current.saved_language;
    }
    if (current.sound_balance != h->runtime_preferences.sound_balance) {
        h->persisted_preferences.sound_balance = current.sound_balance;
        h->runtime_preferences.sound_balance = current.sound_balance;
    }
    return 1;
}


static int source_scene_enter(void* data, MeleeWebMenuScene scene,
                              char* e, size_t n)
{
    MeleeWebMenuHost* h = data;
    GameModeState* state;
    CSSData* css;
    SSSData* sss;

    if (!live(h, e, n)) {
        return 0;
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (h->first_sss_pair_state == 2 &&
        (scene != MELEE_WEB_MENU_SCENE_SSS ||
         melee_web_menu_phase(h->session) != MELEE_WEB_MENU_SSS_READY ||
         h->audio != h->first_sss_pair_audio_owner ||
         h->audio_generation != h->first_sss_pair_audio_generation)) {
        first_sss_pair_fail(h,
            "First SSS pair lost the exact ordinary SSS entry route");
        return fail(e, n, h->first_sss_pair_error);
    }
#endif
    memset(&h->source_scene_info, 0, sizeof(h->source_scene_info));
    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        css = (CSSData*) melee_web_menu_css(h->session);
        if (css == NULL) {
            return fail(e, n, "Original CSS payload is unavailable at mode entry");
        }
        if (h->source_mode_kind == GM_TRAINING) {
            GameModeState* authored = training_state_for_scene(scene);
            if (authored == NULL) {
                return fail(e, n,
                            "Original Training mode has no authored CSS state");
            }
            /* Main owns a separate menu selection payload. The Training mode
             * state owns the saved VS-shaped setup that its original CSS
             * callback consumes, so don't overwrite that state with the
             * previous VS CSS payload when entering through GM_MENU. */
            css->match_type = TRAINING_MODE;
            css->vs = *gmVsMelee_GetVsData();
            h->training_css_state = *authored;
            h->training_css_state.info.enter_data = css;
            h->training_css_state.info.exit_data = css;
            state = &h->training_css_state;
        } else if (h->source_mode_kind == GM_VS) {
            *gmVsMelee_GetVsData() = css->vs;
            h->vs_css_state = gm_Mode_Vs_States[gmVsMode_State_Css];
            h->vs_css_state.info.enter_data = css;
            h->vs_css_state.info.exit_data = css;
            state = &h->vs_css_state;
        } else {
            return fail(e, n,
                        "Original CSS route has no checked VS or Training owner");
        }
        h->source_scene_info.scene_kind = GS_CSS;
        h->source_scene_info.enter_data = css;
        h->source_scene_info.exit_data = css;
        state->on_enter(state);
        h->source_scene = MELEE_WEB_HOST_SCENE_CSS;
    } else if (scene == MELEE_WEB_MENU_SCENE_SSS) {
        sss = (SSSData*) melee_web_menu_sss(h->session);
        if (sss == NULL) {
            return fail(e, n, "Original SSS payload is unavailable at mode entry");
        }
        *gmVsMelee_GetVsData() = sss->vs;
        if (h->source_mode_kind == GM_TRAINING) {
            GameModeState* authored = training_state_for_scene(scene);
            if (authored == NULL) {
                return fail(e, n,
                            "Original Training mode has no authored SSS state");
            }
            h->training_sss_state = *authored;
            h->training_sss_state.info.enter_data = sss;
            h->training_sss_state.info.exit_data = sss;
            state = &h->training_sss_state;
        } else if (h->source_mode_kind == GM_VS) {
            h->vs_sss_state = gm_Mode_Vs_States[gmVsMode_State_Sss];
            h->vs_sss_state.info.enter_data = sss;
            h->vs_sss_state.info.exit_data = sss;
            state = &h->vs_sss_state;
        } else {
            return fail(e, n,
                        "Original SSS route has no checked VS or Training owner");
        }
        h->source_scene_info.scene_kind = GS_SSS;
        h->source_scene_info.enter_data = sss;
        h->source_scene_info.exit_data = sss;
        state->on_enter(state);
        h->source_scene = MELEE_WEB_HOST_SCENE_SSS;
    } else {
        return fail(e, n, "Unsupported menu scene lifecycle callback");
    }
    gm_801A4B88(&h->source_scene_info);
    return ok(e, n);
}

static int source_scene_exit(void* data, MeleeWebMenuScene scene,
                             char* e, size_t n)
{
    MeleeWebMenuHost* h = data;
    CSSData* css;
    SSSData* sss;

    if (!live(h, e, n)) {
        return 0;
    }
    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        const int parent_route = h->css_parent_route_requested;
        css = (CSSData*) melee_web_menu_css(h->session);
        if (css == NULL || h->source_scene != MELEE_WEB_HOST_SCENE_CSS) {
            return fail(e, n, "Original CSS payload is unavailable at mode exit");
        }
        if (h->source_mode_kind == GM_TRAINING) {
            const int pending_mode_before = melee_web_vs_mode_pending_mode();
            if (pending_mode_before >= 0 && pending_mode_before != GM_MENU) {
                return fail(e, n,
                            "Original Training CSS requested an unsupported mode route");
            }
            h->training_css_state.info.exit_data = css;
            h->training_css_state.on_exit(&h->training_css_state);
            if (parent_route ||
                css->pending_scene_change == CSSPendingSceneChange_2) {
                if (melee_web_vs_mode_pending_mode() != GM_MENU ||
                    !h->vs_mode_owned ||
                    !melee_web_vs_mode_set_route(GM_MENU, GM_TRAINING)) {
                    return fail(e, n,
                                "Original Training CSS could not commit its GM_MENU return");
                }
                h->source_target_mode = GM_MENU;
                h->source_mode_kind = GM_MENU;
            } else if (css->pending_scene_change == 1) {
                if (melee_web_vs_mode_pending_mode() >= 0) {
                    return fail(e, n,
                                "Original Training CSS changed mode during its SSS handoff");
                }
                h->source_target_mode = -1;
                h->training_start_pending = 0;
            } else {
                return fail(e, n,
                            "Original Training CSS exited without a supported source route");
            }
        } else {
            h->vs_css_state.info.exit_data = css;
            if (parent_route &&
                (gm_GetCurrentGameMode() != GM_VS ||
                 melee_web_vs_mode_pending_mode() != GM_MENU)) {
                return fail(e, n,
                            "Original CSS parent route was no longer pending before OnExit");
            }
            gm_Mode_Vs_States[gmVsMode_State_Css].on_exit(&h->vs_css_state);
            if (parent_route) {
                if (gm_GetCurrentGameMode() != GM_VS ||
                    melee_web_vs_mode_pending_mode() != GM_MENU) {
                    return fail(e, n,
                                "Original CSS parent route changed before its OnExit completed");
                }
                if (!h->vs_mode_owned) {
                    if (!melee_web_vs_mode_begin()) {
                        return fail(e, n,
                                    "Original VS mode lease unavailable for CSS parent return");
                    }
                    h->vs_mode_owned = 1;
                }
                h->source_target_mode = GM_MENU;
                if (!melee_web_vs_mode_set_route(GM_MENU, GM_VS)) {
                    return fail(e, n,
                                "Original GM_MENU route owner could not commit the CSS parent return");
                }
            } else if (css->pending_scene_change == CSSPendingSceneChange_2) {
                /* CSS parent return keeps a checked VS lease through GM_MENU. */
                if (!h->vs_mode_owned) {
                    if (!melee_web_vs_mode_begin()) {
                        return fail(e, n,
                                    "Original VS mode lease unavailable for CSS parent return");
                    }
                    h->vs_mode_owned = 1;
                }
                if (!melee_web_vs_mode_set_route(GM_MENU, GM_VS)) {
                    return fail(e, n,
                                "Original GM_MENU route owner could not set GM_VS provenance");
                }
            } else if (css->pending_scene_change == 1) {
                /* Resolve authored nextState once after CSS OnExit and commit
                 * the same route once before the browser rebuilds SSS. */
                if (!host_commit_vs_css_sss_route(h,css,e,n)) return 0;
            } else if (h->vs_mode_owned) {
                if (!melee_web_vs_mode_end()) {
                    return fail(e, n, "Original VS mode lease did not release after CSS");
                }
                h->vs_mode_owned = 0;
                h->source_target_mode = -1;
            } else {
                h->source_target_mode = -1;
            }
        }
    } else if (scene == MELEE_WEB_MENU_SCENE_SSS) {
        sss = (SSSData*) melee_web_menu_sss(h->session);
        if (sss == NULL || h->source_scene != MELEE_WEB_HOST_SCENE_SSS) {
            return fail(e, n, "Original SSS payload is unavailable at mode exit");
        }
        if (h->source_mode_kind == GM_TRAINING) {
            const int pending_mode_before = melee_web_vs_mode_pending_mode();
            if (pending_mode_before >= 0 && pending_mode_before != GM_MENU) {
                return fail(e, n,
                            "Original Training SSS requested an unsupported mode route");
            }
            h->training_sss_state.info.exit_data = sss;
            h->training_sss_state.on_exit(&h->training_sss_state);
            if (melee_web_vs_mode_pending_mode() == GM_MENU) {
                if (!h->vs_mode_owned ||
                    !melee_web_vs_mode_set_route(GM_MENU, GM_TRAINING)) {
                    return fail(e, n,
                                "Original Training SSS could not commit its GM_MENU return");
                }
                h->source_target_mode = GM_MENU;
                h->source_mode_kind = GM_MENU;
                h->training_start_pending = 0;
            } else if (melee_web_vs_mode_pending_mode() >= 0) {
                return fail(e, n,
                            "Original Training SSS changed to an unsupported mode route");
            } else if (sss->start_game) {
                h->training_start_pending = 1;
                h->source_target_mode = -1;
                if (h->vs_mode_owned) {
                    if (!melee_web_vs_mode_end()) {
                        return fail(e, n,
                                    "Original Training mode lease did not release at simulation handoff");
                    }
                    h->vs_mode_owned = 0;
                }
            } else {
                h->source_target_mode = -1;
                h->training_start_pending = 0;
            }
        } else {
            h->vs_sss_state.info.exit_data = sss;
            if (!sss->start_game &&
                !host_prepare_vs_sss_cancel_css_route(h, sss, e, n)) return 0;
            gm_Mode_Vs_States[gmVsMode_State_Sss].on_exit(&h->vs_sss_state);
            if (sss->start_game && h->vs_mode_owned) {
                if (!melee_web_vs_mode_end()) {
                    return fail(e, n, "Original VS mode lease did not release before match");
                }
                h->vs_mode_owned = 0;
            } else if (!sss->start_game &&
                       !host_commit_vs_sss_cancel_css_route(h, sss, e, n)) {
                return 0;
            }
        }
    } else {
        return fail(e, n, "Unsupported menu scene lifecycle callback");
    }
    return ok(e, n);
}

static int source_scene_tick(MeleeWebMenuHost* h, char* e, size_t n)
{
    int request = 0;

    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE) {
        gm_Scene_Title_OnFrame();
    } else if (h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        mnMain_Scene_OnFrame();
    } else if (h->source_scene == MELEE_WEB_HOST_SCENE_OPENING &&
               h->opening_scene_handler != NULL) {
        if (h->opening_scene_handler->on_frame != NULL) {
            h->opening_scene_handler->on_frame();
        }
    } else {
        return fail(e, n, "Original title/main tick has no live source scene");
    }
    if (!runtime_scheduler(h, e, n)) {
        return 0;
    }
    if (!melee_web_menu_clock_request(&request)) {
        return fail(e, n, "Invalid original title/main transition state");
    }
    h->transition = request;
    return request == 0 ? MELEE_WEB_MENU_RESULT_TICKED
                        : MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED;
}

MeleeWebMenuHost* melee_web_menu_host_create_with_profile(
    int save_mode,const uint8_t* card_data,size_t card_data_size,char* e,size_t n){

    if(owner||!seed_ptr){fail(e,n,"A menu host already exists or source RNG is unavailable");return NULL;}
    if((save_mode!=MELEE_WEB_SAVE_MODE_EVERYTHING&&
        save_mode!=MELEE_WEB_SAVE_MODE_PERSONAL)||
       (card_data_size!=0&&(!card_data||card_data_size!=MELEE_WEB_SAVE_PROFILE_CARD_BYTES))||
       (save_mode==MELEE_WEB_SAVE_MODE_EVERYTHING&&card_data_size!=0)){
        fail(e,n,"Save mode/profile payload is not a supported source profile");return NULL;
    }
    MeleeWebMenuHost* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate native menu host");return NULL;}
    if(card_data_size){
        memcpy(h->configured_profile,card_data,card_data_size);
        h->configured_profile_size=card_data_size;
    }
    h->source_target_mode = -1;
    h->source_mode_kind = GM_VS;
    h->saved_scene_info = melee_web_current_scene_info();
    MeleeWebMenuRuntime runtime={h,runtime_check,runtime_scheduler,runtime_transition,
                                 source_scene_enter,source_scene_exit};
    MeleeWebMenuConfig config={4,0,0};
    h->profile=melee_web_save_profile_owner_create(e,n);
    if(!h->profile){free(h);return NULL;}
    if(!melee_web_save_profile_owner_activate(h->profile,e,n)){
        if(!melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    if(!melee_web_save_profile_owner_initialize_default(h->profile,e,n)){
        if(!melee_web_save_profile_owner_deactivate(h->profile,NULL,0)||
           !melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    h->save_mode=save_mode;
    h->session=melee_web_menu_session_create(&runtime,&config,e,n);
    if(!h->session){
        if(!melee_web_save_profile_owner_deactivate(h->profile,NULL,0)||
           !melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    h->saved_seed=seed_ptr;h->seed=*seed_ptr;seed_ptr=&h->seed;
    owner=h;ok(e,n);return h;
}

int melee_web_menu_host_initialize_profile_baseline(
    MeleeWebMenuHost* h,char* e,size_t n)
{
    size_t archive_size=0;
    const char* archive_name;

    if(!h||h!=owner||!h->profile||!h->session||!seed_ptr||
       seed_ptr!=&h->seed||h->entered)
        return fail(e,n,"Save baseline requires the current unentered menu host");
    if(h->baseline_profile_ready)return ok(e,n);
    if(!melee_web_gameplay_stats().generation||
       !melee_web_source_files_active())
        return fail(e,n,
            "Save baseline requires a live menu world and original source-file owner");

    /* Toy_803124BC loads the exact locale selected by SaveData. The default
     * profile has already selected US here; checking the file before calling
     * the fatal original archive loader preserves a useful boundary error. */
    archive_name=lbLang_IsSavedLanguageJP()?"TyDatai.dat":"TyDatai.usd";
    if(!melee_web_source_file_size(archive_name,&archive_size)||!archive_size)
        return fail(e,n,"Original TyDatai trophy archive is absent from menu RuntimeFiles");
    Toy_803124BC();

    if(!melee_web_save_profile_owner_initialize_everything(h->profile,e,n)||
       !melee_web_save_profile_owner_snapshot_card_data(
           h->profile,h->baseline_profile,sizeof(h->baseline_profile),e,n)||
       (h->save_mode==MELEE_WEB_SAVE_MODE_PERSONAL&&
        (!melee_web_save_profile_owner_restore_default(h->profile,e,n)||
         (h->configured_profile_size&&
          !melee_web_save_profile_owner_apply_card_data(
              h->profile,h->configured_profile,h->configured_profile_size,e,n))))||
       !melee_web_save_profile_owner_capture_preferences(
           h->profile,&h->persisted_preferences,e,n))
        return 0;

    h->baseline_profile_ready=1;
    h->selected_characters=gmMainLib_GetSaveData()->unlocked_characers_bitmask;
    h->selected_stages=gmMainLib_GetSaveData()->x186A;
    return ok(e,n);
}

MeleeWebMenuHost* melee_web_menu_host_create(char* e,size_t n){
    return melee_web_menu_host_create_with_profile(
        MELEE_WEB_SAVE_MODE_EVERYTHING,NULL,0,e,n);
}

int melee_web_menu_host_snapshot_card_data(
    MeleeWebMenuHost* h,int baseline,uint8_t* output,
    size_t output_size,char* e,size_t n){
    if(!h||h!=owner||!output||output_size!=MELEE_WEB_SAVE_PROFILE_CARD_BYTES||
       (baseline!=0&&baseline!=1))
        return fail(e,n,"Save snapshot requires the current source menu owner and exact output size");
    if(baseline){
        if(!h->baseline_profile_ready)
            return fail(e,n,"Original mode baseline is not ready");
        memcpy(output,h->baseline_profile,sizeof(h->baseline_profile));
        return ok(e,n);
    }
    if(h->initial_replay_context)
        return melee_web_save_profile_owner_snapshot_card_data(
            h->profile,output,output_size,e,n);
    if (h->entered &&
        !sync_source_preference_changes(h, e, n)) {
        return 0;
    }
    return melee_web_save_profile_owner_snapshot_card_data_with_preferences(
        h->profile,&h->persisted_preferences,output,output_size,e,n);
}

int melee_web_menu_host_apply_replay_context(
    MeleeWebMenuHost* h, uint32_t random_seed,
    const uint8_t pad_state[MELEE_WEB_PAD_STATE_BYTES],
    const uint8_t css_data[0x148], const uint8_t ko_counts[GM_MAX_PLAYERS],
    const uint8_t game_rules[0x18], const uint8_t save_data[0x55E8],
    char* e, size_t n)
{
    MeleeWebPadState* input;
    if(!h||h!=owner||h->entered||h->audio||h->initial_replay_context||h->initial_native_rules||
       !pad_state||!css_data||!ko_counts||!game_rules||!save_data||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_CREATED||
       seed_ptr!=&h->seed)
        return fail(e,n,"Whole-session first-CSS context requires an unentered menu host");
    /* Profile application is the other owner mutation in this operation.
     * Check its aliases before decoding or installing CSS so a stale profile
     * cannot leave an unentered session holding a committed CSS context. */
    if(!melee_web_save_profile_owner_live(h->profile,e,n))return 0;
    input=melee_web_pad_state_decode(pad_state,MELEE_WEB_PAD_STATE_BYTES,e,n);
    if(!input)return 0;
    if(!melee_web_menu_apply_reference_css_context(
           h->session,css_data,ko_counts,e,n)){
        melee_web_pad_state_free(input);
        return 0;
    }
    if(!melee_web_save_profile_owner_apply_reference_context(
           h->profile,game_rules,save_data,e,n)){
        melee_web_pad_state_free(input);
        return 0;
    }
    memcpy(h->initial_game_rules,game_rules,sizeof(h->initial_game_rules));
    memcpy(h->initial_save_data,save_data,sizeof(h->initial_save_data));
    melee_web_pad_state_free(h->input);h->input=input;
    h->seed=random_seed;h->initial_replay_context=1;
    return ok(e,n);
}
/* Explicit native preparation fixture; not a Rules-menu input path. */
int melee_web_menu_host_apply_initial_native_rules(
    MeleeWebMenuHost* h, const GameRules* rules, char* e, size_t n)
{
    GameRules expected = gmMainLib_803D4A48;
    expected.mode = 1; expected.stock_count = 4;
    if (rules) expected.stock_time_limit = rules->stock_time_limit;
    StartMeleeRules timer_rules = {0};
    timer_rules.timer_enabled = rules && rules->stock_time_limit != 0;
    timer_rules.time_limit = rules ? rules->stock_time_limit * 60 : 0;
    if (!h || h != owner || !rules || h->entered || h->audio || h->input ||
        h->initial_replay_context || h->net_start_context || h->initial_native_rules ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_CREATED ||
        seed_ptr != &h->seed || h->save_mode != MELEE_WEB_SAVE_MODE_EVERYTHING ||
        h->configured_profile_size || !melee_web_match_timer_supported(&timer_rules) ||
        memcmp(rules, &expected, sizeof(expected)))
        return fail(e,n,"Native initial rules require supported canonical four-stock rules and a fresh host");
    h->native_rules = *rules; h->initial_native_rules = 1;
    return ok(e,n);
}
/* Networked sessions use the canonical Everything mode and the fresh host's
 * default rules, preferences and PAD history; only the agreed seed is
 * installed. Personal save preferences change gameplay and are rejected. */
int melee_web_menu_host_apply_net_context(
    MeleeWebMenuHost* h, uint32_t random_seed, char* e, size_t n)
{
    if(!h||h!=owner||h->entered||h->audio||h->initial_replay_context||h->initial_native_rules||
       h->net_start_context||h->input||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_CREATED||
       seed_ptr!=&h->seed)
        return fail(e,n,"Networked start context requires a fresh unentered menu host");
    if(h->save_mode!=MELEE_WEB_SAVE_MODE_EVERYTHING||h->configured_profile_size!=0)
        return fail(e,n,"Networked start context requires the canonical Everything save mode without a personal profile");
    h->seed=random_seed;h->net_start_context=1;
    return ok(e,n);
}
int melee_web_menu_host_selection_state(const MeleeWebMenuHost* h,
                                        StartMeleeData* start,uint8_t header[6]){
    MeleeWebMenuPhase phase;
    if(!h||h!=owner||!h->entered||!start||!header)return 0;
    phase=melee_web_menu_phase(h->session);
    memset(header,0,6);
    if(phase==MELEE_WEB_MENU_CSS||phase==MELEE_WEB_MENU_CSS_READY){
        const CSSData* css=melee_web_menu_css(h->session);
        if(!css)return 0;
        header[0]=(uint8_t)(css->unk_0x0>>8);header[1]=(uint8_t)css->unk_0x0;
        header[2]=css->match_type;header[3]=css->pending_scene_change;
        *start=css->vs.start;
        return 1;
    }
    if(phase==MELEE_WEB_MENU_SSS||phase==MELEE_WEB_MENU_SSS_READY){
        const SSSData* sss=melee_web_menu_sss(h->session);
        if(!sss)return 0;
        header[0]=sss->unk_stage;header[1]=sss->x1;header[2]=sss->no_lras;
        header[3]=(uint8_t)sss->force_stage_id;header[4]=sss->start_game;
        *start=sss->vs.start;
        return 2;
    }
    return 0;
}
static int restore_context_checked(MeleeWebMenuHost* h, char* e, size_t n){
    HSD_PadLibData=h->saved_library;
    memcpy(HSD_PadGameStatus,h->saved_game,sizeof(h->saved_game));
    memcpy(HSD_PadMasterStatus,h->saved_master,sizeof(h->saved_master));
    memcpy(HSD_PadCopyStatus,h->saved_copy,sizeof(h->saved_copy));
    *gmMainLib_GetGameRules()=h->saved_rules;
    *gmMainLib_8015CC58()=h->saved_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->saved_characters;
    *gmMainLib_8015EDA4()=h->saved_stages;
    lbLang_SetLanguageSetting(h->saved_language);lbLang_SetSavedLanguage(h->saved_saved_language);
    if(!melee_web_menu_clock_end())
        return fail(e,n,"Original scene clock refused checked caller restoration");
    h->audio=NULL;h->generation=0;
    return ok(e,n);
}
static void restore_context(MeleeWebMenuHost* h){
    if(!restore_context_checked(h,NULL,0))abort();
}
static int host_cleanup_failed_sss_entry(MeleeWebMenuHost* h,
                                          const char* reason,
                                          char* e, size_t n)
{
    char reason_copy[160] = {0};
    char abort_error[160] = {0};
    char cleanup_error[160] = {0};
    snprintf(reason_copy,sizeof(reason_copy),"%s",
        reason ? reason : "SSS constructor continuation failed");

    if (!h || h != owner || !h->entered ||
        h->source_scene != MELEE_WEB_HOST_SCENE_SSS ||
        melee_web_menu_phase(h->session) != MELEE_WEB_MENU_SSS)
        return fail(e,n,"Cannot clean up an unowned or inactive SSS entry");
    if (!melee_web_menu_abort(h->session,abort_error,sizeof(abort_error))) {
        if (e && n) snprintf(e,n,"%s; SSS abort refused: %s",
            reason_copy, abort_error[0] ? abort_error : "no diagnostic");
        return 0;
    }
    HSD_SisLib_803A5FBC();
    h->entered=0;
    h->source_scene=MELEE_WEB_HOST_SCENE_NONE;
    h->transition=0;
    if (!restore_context_checked(h,cleanup_error,sizeof(cleanup_error)))
        return fail(e,n,cleanup_error);
    return fail(e,n,reason_copy);
}
static int host_restore_failed_continuation(MeleeWebMenuHost* h,
                                             const char* reason,
                                             char* e, size_t n)
{
    char reason_copy[160] = {0};
    char cleanup_error[160] = {0};
    snprintf(reason_copy,sizeof(reason_copy),"%s",
        reason ? reason : "CSS-to-SSS continuation failed");
    if (!restore_context_checked(h,cleanup_error,sizeof(cleanup_error)))
        return fail(e,n,cleanup_error);
    return fail(e,n,reason_copy);
}

static int host_prepare_world(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                              MeleeWebMenuPhase phase, int source_scene,
                              const MeleeWebMenuClockCounters* continuation,
                              char* e, size_t n)
{
    uint64_t audio_generation;
    if (!h || h != owner)
        return fail(e,n,"Native menu host is not the owner");
    audio_generation=melee_web_audio_generation(audio);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (h && h == owner && h->first_sss_pair_state == 2 &&
        (phase != MELEE_WEB_MENU_SSS_READY || source_scene ||
         audio != h->first_sss_pair_audio_owner ||
         audio_generation != h->first_sss_pair_audio_generation)) {
        first_sss_pair_fail(h,
            "SSS rebuild changed the authored phase or retained audio owner");
        return fail(e,n,h->first_sss_pair_error);
    }
#endif
    if(!h||h!=owner||h->entered||h->audio||h->results_active||seed_ptr!=&h->seed||!melee_web_audio_is_active(audio)||
       !audio_generation||!melee_web_audio_bank_transport_active()||!melee_web_gameplay_stats().generation)
        return fail(e,n,"Native menu enter requires a fresh owned world and source audio");
    if(!source_scene&&(phase!=MELEE_WEB_MENU_CREATED&&phase!=MELEE_WEB_MENU_CSS_READY&&phase!=MELEE_WEB_MENU_SSS_READY&&phase!=MELEE_WEB_MENU_READY))
        return fail(e,n,"Native menu session cannot enter from this phase");
    if (phase == MELEE_WEB_MENU_SSS_READY && h->source_mode_kind == GM_VS &&
        (source_scene || continuation == NULL))
        return fail(e,n,"Ordinary VS SSS entry requires its captured CSS clock continuation");
    if (continuation != NULL &&
        (source_scene || phase != MELEE_WEB_MENU_SSS_READY ||
         h->source_mode_kind != GM_VS))
        return fail(e,n,"CSS clock continuation is valid only for ordinary VS SSS entry");
    if(!melee_web_menu_host_initialize_profile_baseline(h,e,n))return 0;
    if(!source_scene&&phase!=MELEE_WEB_MENU_CREATED&&!h->input)
        return fail(e,n,"Returning menu scene requires retained source PAD history");
    if(!melee_web_native_world_enable(e,n))return 0;
    if (continuation != NULL
            ? !melee_web_menu_clock_begin_with_counters(continuation)
            : !melee_web_menu_clock_begin())
        return fail(e,n,"Original scene clock is already owned");
    h->audio=audio;h->generation=melee_web_gameplay_stats().generation;h->transition=0;
    h->saved_rules=*gmMainLib_GetGameRules();
    h->saved_preferences=*gmMainLib_8015CC58();
    h->saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();h->saved_stages=*gmMainLib_8015EDA4();
    h->saved_language=lbLang_GetLanguageSetting();h->saved_saved_language=lbLang_GetSavedLanguage();
    h->saved_library=HSD_PadLibData;
    memcpy(h->saved_game,HSD_PadGameStatus,sizeof(h->saved_game));
    memcpy(h->saved_master,HSD_PadMasterStatus,sizeof(h->saved_master));
    memcpy(h->saved_copy,HSD_PadCopyStatus,sizeof(h->saved_copy));
    lbLang_SetLanguageSetting(LANG_US);lbLang_SetSavedLanguage(LANG_US);
    if(phase==MELEE_WEB_MENU_CREATED){
        if(h->initial_replay_context){
            /* Re-apply the copied source ranges immediately before OnEnter so
             * construction cannot accidentally replace the declared context.
             * The owner performs endian-aware layout translation. */
            if(!melee_web_save_profile_owner_apply_reference_context(
                   h->profile,h->initial_game_rules,h->initial_save_data,e,n)){
                restore_context(h);return 0;
            }
        }else{
            *gmMainLib_GetGameRules()=h->initial_native_rules?
                h->native_rules:gmMainLib_803D4A48;
            gmMainLib_GetGameRules()->mode=1;gmMainLib_GetGameRules()->stock_count=4;
        }
        if(!h->initial_replay_context){
            gmMainLib_8015CC58()->item_freq=(u8)-1;
            gmMainLib_8015CC58()->item_mask=UINT64_MAX;
            gmMainLib_8015CC58()->rumble_enabled[0]=true;
            gmMainLib_8015CC58()->rumble_enabled[1]=true;
        }
        /* Exact authored profile masks were installed once at host creation.
         * Scene entry must not reset source unlock or Prize progress. */
    }else{
        *gmMainLib_GetGameRules()=h->selected_rules;
        *gmMainLib_8015CC58()=h->selected_preferences;
        *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->selected_characters;
        *gmMainLib_8015EDA4()=h->selected_stages;
    }
    HSD_PadLibData=default_libinfo_data;
    HSD_PadLibData.rumble_info=h->saved_library.rumble_info;
    HSD_PadLibData.qnum=1;HSD_PadLibData.queue=&h->queue;
    HSD_PadLibData.clamp_stickType=0;HSD_PadLibData.clamp_stickShift=1;
    HSD_PadLibData.clamp_stickMax=80;HSD_PadLibData.clamp_stickMin=0;HSD_PadLibData.scale_stick=80;
    HSD_PadLibData.clamp_analogLRShift=1;HSD_PadLibData.clamp_analogLRMax=140;
    HSD_PadLibData.clamp_analogLRMin=0;HSD_PadLibData.scale_analogLR=140;
    for(unsigned i=0;i<4;i++)HSD_PadGameStatus[i]=HSD_PadMasterStatus[i]=HSD_PadCopyStatus[i]=default_status_data;
    /* Retail flushes the sample queue at a scene boundary, but retains HSD's
     * edge/repeat history. Restoring only the external owner here fabricates
     * a new LRAS press when the quitting chord is still held on CSS entry. */
    if(h->input)melee_web_pad_state_apply(h->input);
    /* The retail bootstrap initializes the AX driver and language banks once,
     * outside ordinary CSS/SSS scene changes. The per-world audio GObj
     * allocator must still be rebound because its storage uses the fresh HSD
     * heap. A newly owned provider gets the full initialization; a retained
     * menu provider keeps its HPS voice and source stream position. */
    lbAudioAx_8002835C();
    if(h->audio_generation!=audio_generation){
        lbAudioAx_8002838C();lbAudioAx_80028690();
        h->audio_generation=audio_generation;
    }
    HSD_SisLib_803A6048(source_scene||phase==MELEE_WEB_MENU_SSS_READY?0x4800:0x2400);
    if (!remember_runtime_preferences(h,e,n)) {
        if (continuation != NULL) {
            char original_error[160] = {0};
            if (e && n) snprintf(original_error,sizeof(original_error),"%s",e);
            HSD_SisLib_803A5FBC();
            return host_restore_failed_continuation(h,
                original_error[0] ? original_error :
                    "Cannot retain host preferences before SSS entry",e,n);
        }
        return 0;
    }
    return ok(e,n);
}

int melee_web_menu_host_enter(MeleeWebMenuHost* h,MeleeWebAudio* audio,char* e,size_t n){
    MeleeWebMenuPhase phase;
    MeleeWebMenuClockCounters continuation_counters;
    const MeleeWebMenuClockCounters* continuation = NULL;
    int acquired_vs = 0;

    if (!host_sss_continuation_owner(h,e,n)) return 0;
    phase=melee_web_menu_phase(h->session);
    if (phase == MELEE_WEB_MENU_SSS_READY && h->source_mode_kind == GM_VS) {
        if (!host_sss_continuation_claim(h,audio,phase,
                melee_web_gameplay_stats().generation,
                &continuation_counters,e,n)) return 0;
        continuation = &continuation_counters;
    } else {
        host_sss_continuation_invalidate(h);
    }
    if(!host_prepare_world(h,audio,phase,0,continuation,e,n)){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if (h->first_sss_pair_state == 2)
            first_sss_pair_fail(h, e && *e ? e :
                "First SSS pair failed while preparing its retained world");
#endif
        return 0;
    }
    if ((phase == MELEE_WEB_MENU_CREATED || phase == MELEE_WEB_MENU_READY) &&
        !h->vs_mode_owned) {
        const int began_vs = melee_web_vs_mode_begin();
        if (!began_vs || !melee_web_vs_mode_set_route(GM_VS, GM_MENU)) {
            if (began_vs && !melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
            HSD_SisLib_803A5FBC();
            restore_context(h);
            return fail(e, n, "Original VS mode lease unavailable for CSS entry");
        }
        h->vs_mode_owned = 1;
        acquired_vs = 1;
    }
    int accepted=phase==MELEE_WEB_MENU_SSS_READY?melee_web_menu_enter_sss(h->session,e,n):
        phase==MELEE_WEB_MENU_READY?melee_web_menu_return_to_css(h->session,e,n):melee_web_menu_enter_css(h->session,e,n);
    if(!accepted){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if (h->first_sss_pair_state == 2 || h->first_sss_pair_state == 3)
            first_sss_pair_fail(h, e && *e ? e :
                "First SSS pair failed during authored scene entry");
#endif
        if (acquired_vs) {
            if (!melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
        }
        HSD_SisLib_803A5FBC();
        if (continuation != NULL)
            return host_restore_failed_continuation(h,
                e && *e ? e : "Authored SSS entry failed",e,n);
        restore_context(h);
        return 0;
    }
    if (phase == MELEE_WEB_MENU_SSS_READY && continuation != NULL) {
        /* source_scene_enter has installed the persistent authored SSS
         * payload. Mark local ownership before constructor finish or cleanup. */
        h->source_scene=MELEE_WEB_HOST_SCENE_SSS;
        h->entered=1;
        if (!melee_web_menu_clock_finish_constructor())
            return host_cleanup_failed_sss_entry(h,
                "Original SSS constructor counter reset was refused",e,n);
    }
    if(!remember_runtime_preferences(h,e,n)){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if (phase == MELEE_WEB_MENU_SSS_READY && h->first_sss_pair_state != 0)
            first_sss_pair_fail(h, e && *e ? e :
                "First SSS pair failed while retaining host entry preferences");
#endif
        if (continuation != NULL)
            return host_cleanup_failed_sss_entry(h,
                e && *e ? e : "Cannot retain host preferences after SSS entry",e,n);
        return 0;
    }
    h->source_scene=phase==MELEE_WEB_MENU_SSS_READY?MELEE_WEB_HOST_SCENE_SSS:MELEE_WEB_HOST_SCENE_CSS;
    h->entered=1;lb_8001CF18();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (phase == MELEE_WEB_MENU_SSS_READY && h->first_sss_pair_state != 0) {
        if (h->first_sss_pair_state == 4 &&
            h->first_sss_pair_entry.captured &&
            h->first_sss_pair_returned.captured) {
            h->first_sss_pair_state = 6;
        } else {
            first_sss_pair_fail(h,
                "First SSS pair did not retain both constructor notes before host entry");
        }
    }
#endif
    return ok(e,n);
}

static int host_enter_title_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    int previous;
    int acquired = 0;

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE) {
        return fail(e, n, "A source menu scene is already active");
    }
    previous = h->source_target_mode == GM_TITLE
        ? (h->source_previous_mode >= 0 ? h->source_previous_mode : GM_MENU)
        : GM_TITLE;
    if (!h->vs_mode_owned) {
        if (!melee_web_vs_mode_begin()) {
            return fail(e, n, "Original VS mode is already owned by another route");
        }
        h->vs_mode_owned = 1;
        acquired = 1;
    }
    if (!melee_web_vs_mode_set_route(GM_TITLE, previous)) {
        if (acquired) {
            if (!melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
        }
        return fail(e, n, "Original title route could not set mode provenance");
    }
    h->source_mode_kind = GM_TITLE;
    h->source_state = gm_Mode_Title_States[0];
    h->source_state.info.scene_kind = GS_TITLE;
    h->source_state.info.enter_data = NULL;
    h->source_state.info.exit_data = &h->title_exit_payload;
    h->title_exit_payload = 0;
    h->source_target_mode = -1;
    h->source_state.on_enter(&h->source_state);
    /* The GameModeState is persistent in the host, and its embedded
     * GameSceneInfo owns the exact payload written by gm_Scene_Title_OnFrame. */
    gm_801A4B88(&h->source_state.info);
    gm_Scene_Title_OnEnter(NULL);
    h->source_scene = MELEE_WEB_HOST_SCENE_TITLE;
    return ok(e, n);
}

static int host_enter_main_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    const int previous = gm_GetPreviousGameMode();

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE) {
        return fail(e, n, "A source menu scene is already active");
    }
    /* The mode callback owns menu_kind/hovered_selection.  Check the route
     * provenance before it runs so a stale game-mode lease cannot silently
     * open the wrong parent menu. */
    if (!h->vs_mode_owned) {
        return fail(e, n,
                    "Original GM_MENU entry has no retained source mode lease");
    }
    if (h->vs_mode_owned && previous != GM_VS && previous != GM_TITLE &&
        previous != GM_MENU && previous != GM_OPENING_MV &&
        previous != GM_TRAINING) {
        return fail(e, n,
                    "Original GM_MENU entry lacks a supported previous-mode route");
    }
    if (!melee_web_vs_mode_set_route(GM_MENU, previous)) {
        return fail(e, n, "Original GM_MENU route could not set mode provenance");
    }
    h->source_mode_kind = GM_MENU;
    h->source_state = gm_Mode_Menu_States[0];
    h->source_state.info.scene_kind = GS_MENU;
    h->source_state.info.enter_data = &h->main_enter;
    h->source_state.info.exit_data = &h->main_exit;
    memset(&h->main_enter, 0, sizeof(h->main_enter));
    memset(&h->main_exit, 0, sizeof(h->main_exit));
    h->source_target_mode = -1;
    h->source_previous_mode = previous;
    h->source_state.on_enter(&h->source_state);
    h->source_scene_info.scene_kind = GS_MENU;
    h->source_scene_info.enter_data = &h->main_enter;
    h->source_scene_info.exit_data = &h->main_exit;
    gm_801A4B88(&h->source_scene_info);
    mnMain_Scene_OnEnter(&h->main_enter);
    h->source_scene = MELEE_WEB_HOST_SCENE_MAIN;
    return ok(e, n);
}

int melee_web_menu_host_enter_title(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                                    char* e, size_t n)
{
    MeleeWebMenuPhase phase;
    if (!host_sss_continuation_alternative_entry(h,e,n)) return 0;
    phase = melee_web_menu_phase(h->session);

    if (!host_prepare_world(h, audio, phase, 1, NULL, e, n)) {
        return 0;
    }
    if (!host_enter_title_scene(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    if (!remember_runtime_preferences(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}

int melee_web_menu_host_enter_main(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                                   char* e, size_t n)
{
    MeleeWebMenuPhase phase;
    if (!host_sss_continuation_alternative_entry(h,e,n)) return 0;
    phase = melee_web_menu_phase(h->session);

    if (!host_prepare_world(h, audio, phase, 1, NULL, e, n)) {
        return 0;
    }
    if (!host_enter_main_scene(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    if (!remember_runtime_preferences(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}

int melee_web_menu_host_enter_training_css(MeleeWebMenuHost* h,
                                           MeleeWebAudio* audio,
                                           char* e, size_t n)
{
    MeleeWebMenuPhase phase;
    if (!host_sss_continuation_alternative_entry(h,e,n)) return 0;
    phase = melee_web_menu_phase(h->session);

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE ||
        h->entered || h->audio || !h->vs_mode_owned ||
        h->source_target_mode != GM_TRAINING ||
        h->source_mode_kind != GM_TRAINING) {
        return fail(e, n,
                    "Original Training CSS requires the checked Main -> GM_TRAINING route");
    }
    h->training_start_pending = 0;
    if (phase == MELEE_WEB_MENU_CLOSED) {
        if (!melee_web_menu_parent_route_pending(h->session)) {
            return fail(e, n,
                        "Original Training CSS cannot reopen without a completed GM_MENU parent route");
        }
        if (!host_prepare_world(h, audio, MELEE_WEB_MENU_CSS_READY, 0, NULL, e, n)) {
            return 0;
        }
        if (!melee_web_menu_reopen_css_after_parent(h->session, e, n)) {
            HSD_SisLib_803A5FBC();
            restore_context(h);
            return 0;
        }
        h->source_target_mode = -1;
        h->source_scene = MELEE_WEB_HOST_SCENE_CSS;
        h->entered = 1;
        lb_8001CF18();
        return ok(e, n);
    }
    if (phase != MELEE_WEB_MENU_CREATED && phase != MELEE_WEB_MENU_CSS_READY) {
        return fail(e, n,
                    "Original Training CSS route requires a new or CSS-ready menu session");
    }
    if (!melee_web_menu_host_enter(h, audio, e, n)) {
        return 0;
    }
    h->source_target_mode = -1;
    return ok(e, n);
}

int melee_web_menu_host_mode_kind(const MeleeWebMenuHost* h)
{
    return h && h == owner ? h->source_mode_kind : -1;
}

int melee_web_menu_host_training_start_pending(const MeleeWebMenuHost* h)
{
    return h && h == owner ? h->training_start_pending : 0;
}

int melee_web_menu_host_opening_preview(const MeleeWebMenuHost* h,
                                        MeleeWebOpeningPreview* out,
                                        char* e, size_t n)
{
    int characters[4];
    unsigned char costumes[4];
    int stage_kind, match_kind;
    if (!h || h != owner || !out || h->entered || h->audio ||
        !h->vs_mode_owned ||
        melee_web_vs_mode_pending_mode() != -1 ||
        gm_GetCurrentGameMode() != GM_OPENING_MV ||
        (gm_GetCurrentSceneIndex() != 1 && gm_GetCurrentSceneIndex() != 3) ||
        h->source_target_mode != GM_OPENING_MV || seed_ptr != &h->seed ||
        !melee_web_opening_preview(characters, costumes, &stage_kind,
                                   &match_kind)) {
        return fail(e, n, "Opening preview requires the source-selected demo route");
    }
    for (unsigned i = 0; i < 4; ++i) {
        out->characters[i] = (uint32_t) characters[i];
        out->costumes[i] = costumes[i];
    }
    out->stage_kind = (uint32_t) stage_kind;
    out->match_kind = (uint32_t) match_kind;
    return ok(e, n);
}

int melee_web_menu_host_enter_opening(MeleeWebMenuHost* h,
                                      MeleeWebAudio* audio,
                                      char* e, size_t n)
{
    MeleeWebMenuPhase phase;
    int id;
    GameModeState* source;
    GameScene* scene;
    if (!host_sss_continuation_alternative_entry(h,e,n)) return 0;
    phase = melee_web_menu_phase(h->session);
    id = gm_GetCurrentSceneIndex();
    if (!h->vs_mode_owned ||
        gm_GetCurrentGameMode() != GM_OPENING_MV ||
        h->source_target_mode != GM_OPENING_MV || h->opening_active ||
        melee_web_gameplay_vs_startup_active() ||
        melee_web_gameplay_vs_manager_preparing() ||
        (source = melee_web_opening_mode_state(id)) == NULL ||
        !host_prepare_world(h, audio, phase, 1, NULL, e, n)) {
        return fail(e, n,
                    "Opening mode entry requires its selected source state, a fresh narrow world, and no live source VS preload owner");
    }
    h->source_state = *source;
    h->opening_state_id = id;
    h->opening_scene_handler = gm_FindGameSceneHandler(source->info.scene_kind);
    if (h->opening_scene_handler == NULL ||
        h->opening_scene_handler->kind != source->info.scene_kind) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return fail(e, n, "Opening mode state has no authored scene handler");
    }
    /* Keep the exit data inside this persistent GameModeState owner. Source
     * Title callbacks write it through gm_GetCurrentSceneExitData(). */
    if (source->info.scene_kind == GS_TITLE) {
        h->source_state.info.exit_data = &h->title_exit_payload;
        h->title_exit_payload = 0;
    }
    /* host_prepare_world already performed preloadState's 0x4800 SIS setup
     * for every source-scene entry. Preserve its other scene-global resets
     * below, but do not run its DVD heap/cache half for the supported VS and
     * Title handoffs: the browser has scoped those assets before publication,
     * and this narrow world deliberately does not run gmMain's
     * lbMemory_8001564C/lbHeap_80015F3C bootstrap. The guard above verifies
     * that no full source VS owner is active, so lbDvd_80018CF4's persistent
     * heap flags and lbDvd_80018254's cache transition have no owner here.
     * GS_VS still runs its authored OnEnter to build StartMeleeData, and
     * GS_TITLE still runs gmTitleMode_OnEnter. Movie/how-to/omake states
     * remain explicitly unsupported until this host has a source heap lease. */
    if (source->info.scene_kind == GS_VS || source->info.scene_kind == GS_TITLE) {
        host_reset_preload_scene_aliases();
    } else {
        /* The narrow browser host does not own gmMain's lbMemory/lbHeap
         * bootstrap. Movie/how-to/omake preloadState and OnEnter callbacks
         * require that source owner before touching their DVD heaps, so keep
         * this boundary explicit until a checked source heap lease exists. */
        HSD_SisLib_803A5FBC();
        restore_context(h);
        h->opening_scene_handler = NULL;
        return fail(e, n,
                    "Opening movie preload requires an active source lbMemory/lbHeap owner");
    }
    if (h->source_state.on_enter != NULL) {
        h->source_state.on_enter(&h->source_state);
    }
    gm_801A4B88(&h->source_state.info);
    h->opening_active = 1;
    h->opening_scene_entered = 0;
    h->opening_match_suspended = 0;
    h->opening_state_exit_called = 0;
    if (source->info.scene_kind == GS_VS) {
        if (id != 1 && id != 3) {
            HSD_SisLib_803A5FBC();
            restore_context(h);
            h->opening_active = 0;
            h->opening_scene_handler = NULL;
            return fail(e, n, "Unsupported Opening VS state");
        }
        /* GameplayMatchSession owns the source VS scene's supported runtime
         * services. The original mode OnEnter above produced its exact start
         * payload; its source on_match_start callback is dispatched there. */
        h->source_scene = MELEE_WEB_HOST_SCENE_OPENING_VS;
    } else {
        if (source->info.scene_kind == GS_TITLE) {
            h->source_scene = MELEE_WEB_HOST_SCENE_TITLE;
        } else {
            h->source_scene = MELEE_WEB_HOST_SCENE_OPENING;
        }
        if (h->opening_scene_handler->on_enter != NULL) {
            h->opening_scene_handler->on_enter(h->source_state.info.enter_data);
        }
        h->opening_scene_entered = 1;
    }
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}

int melee_web_menu_host_opening_selection(const MeleeWebMenuHost* h,
    MeleeWebMenuMatchSelection* out, char* e, size_t n)
{
    StartMeleeData* start;
    int characters[4];
    unsigned char costumes[4];
    int stage_kind, match_kind;
    if (!h || h != owner || !out || !h->entered || !h->audio ||
        !h->opening_active || h->opening_state_id != gm_GetCurrentSceneIndex() ||
        (h->opening_state_id != 1 && h->opening_state_id != 3) ||
        h->source_scene != MELEE_WEB_HOST_SCENE_OPENING_VS ||
        h->opening_scene_entered ||
        (start = (StartMeleeData*) h->source_state.info.enter_data) == NULL ||
        !melee_web_opening_preview(characters, costumes, &stage_kind,
                                   &match_kind)) {
        return fail(e, n, "Opening match selection requires the live source VS mode payload");
    }
    if (start->rules.stkind != stage_kind || start->rules.match_kind != match_kind) {
        return fail(e, n, "Opening VS payload differs from its Title-selected source preview");
    }
    memset(out, 0, sizeof(*out));
    out->start = *start;
    out->player_count = 4;
    out->hud_layout = start->rules.x0_3;
    out->random_seed = h->seed;
    out->opening_demo = 1;
    for (unsigned i = 0; i < 4; ++i) {
        const PlayerInitData* player = &start->players[i];
        const unsigned port = player->slot ? player->slot - 1u : i;
        if (player->slot_type == Gm_PKind_NA || port != i ||
            player->ckind != characters[i] || player->color != costumes[i]) {
            return fail(e, n, "Opening VS player data differs from the source-selected demo");
        }
        out->players[i].controller = port;
        out->players[i].stocks = player->stocks;
        out->players[i].costume = player->color;
        out->players[i].sub_color = player->sub_color;
    }
    if (start->players[4].slot_type != Gm_PKind_NA) {
        return fail(e, n, "Opening VS source payload exceeds its authored four CPU slots");
    }
    return ok(e, n);
}

int melee_web_menu_host_opening_match_suspend(MeleeWebMenuHost* h,
                                               char* e, size_t n)
{
    if (!live(h, e, n) || !h->entered || !h->opening_active ||
        h->source_scene != MELEE_WEB_HOST_SCENE_OPENING_VS ||
        h->opening_scene_entered || h->opening_match_suspended ||
        gm_GetCurrentGameMode() != GM_OPENING_MV ||
        gm_GetCurrentSceneIndex() != h->opening_state_id) {
        return fail(e, n, "Opening VS handoff requires its prepared source mode payload");
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
    h->entered = 0;
    h->transition = 0;
    h->opening_match_suspended = 1;
    restore_context(h);
    return ok(e, n);
}

const MeleeWebPadState* melee_web_menu_host_opening_input(
    const MeleeWebMenuHost* h)
{
    return h && h == owner && h->opening_active &&
        h->opening_match_suspended ? h->input : NULL;
}

int melee_web_menu_host_opening_match_finish(MeleeWebMenuHost* h,
    uint32_t seed, const uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES],
    char* e, size_t n)
{
    MeleeWebPadState* next_input;
    int pending_mode;
    int next;
    if (!h || h != owner || h->entered || h->source_scene != MELEE_WEB_HOST_SCENE_NONE ||
        !h->opening_active || !h->opening_match_suspended || h->audio ||
        (h->opening_state_id != 1 && h->opening_state_id != 3) ||
        gm_GetCurrentGameMode() != GM_OPENING_MV ||
        gm_GetCurrentSceneIndex() != h->opening_state_id ||
        melee_web_gameplay_generation() != 0 || seed_ptr != &h->seed) {
        return fail(e, n,
            "Opening demo finish requires a closed match and its retained mode owner");
    }
    next_input = melee_web_pad_state_decode(bytes, MELEE_WEB_PAD_STATE_BYTES, e, n);
    if (next_input == NULL) return 0;
    pending_mode = melee_web_vs_mode_pending_mode();
    if (pending_mode == GM_TITLE) {
        h->source_target_mode = GM_TITLE;
        h->source_previous_mode = GM_OPENING_MV;
        h->opening_active = 0;
        h->opening_state_id = -1;
        if (!melee_web_vs_mode_set_route(GM_TITLE, GM_OPENING_MV)) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                "Opening demo interruption could not transfer its source GM_TITLE route");
        }
    } else if (pending_mode < 0) {
        next = melee_web_opening_mode_advance_state();
        if (next < 0) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                "Opening demo has no authored state after its source exit");
        }
        h->source_target_mode = GM_OPENING_MV;
        h->opening_state_id = next;
    } else {
        melee_web_pad_state_free(next_input);
        if (e && n) snprintf(e, n,
            "Opening demo requested unsupported mode %d", pending_mode);
        return 0;
    }
    /* The completed VS world no longer owns an entered Opening scene. Keep
     * the authored next mode/state route, but release the current state's
     * active flag so the next world can enter it through the source table. */
    h->opening_active = 0;
    h->opening_scene_entered = 0;
    h->opening_state_exit_called = 0;
    h->opening_scene_handler = NULL;
    melee_web_pad_state_free(h->input);
    h->input = next_input;
    h->seed = seed;
    h->opening_match_suspended = 0;
    return ok(e, n);
}

int melee_web_menu_host_opening_match_abort(MeleeWebMenuHost* h,
                                             char* e, size_t n)
{
    if (!h || h != owner || h->entered ||
        h->source_scene != MELEE_WEB_HOST_SCENE_NONE || h->audio ||
        (!h->opening_active && h->source_target_mode != GM_OPENING_MV) ||
        melee_web_gameplay_generation() != 0) {
        return fail(e, n,
            "Opening route abort requires an inactive scene and closed match owner");
    }
    h->opening_active = 0;
    h->opening_match_suspended = 0;
    h->opening_scene_entered = 0;
    h->opening_state_exit_called = 0;
    h->opening_state_id = -1;
    h->opening_scene_handler = NULL;
    h->source_target_mode = -1;
    h->aborted_source_scene = 1;
    return ok(e, n);
}

int melee_web_menu_host_tick(MeleeWebMenuHost* h,const PADStatus raw[4],char* e,size_t n){
    int result;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (h && h == owner && h->first_sss_pair_state != 0) {
        if (h->source_scene == MELEE_WEB_HOST_SCENE_SSS)
            ++h->first_sss_pair_host_tick_calls;
        first_sss_pair_fail(h,
            "First SSS constructor pair refuses source ticks until checked cleanup");
        return fail(e,n,h->first_sss_pair_error);
    }
    if (h && h == owner && h->first_css_return_state)
        h->first_css_return_state = 3;
    final_pending_css_draw_invalidate(h);
#endif
    if(!live(h,e,n)||!h->entered||h->drawing||!raw)return fail(e,n,"Native menu tick requires an idle live scene and four raw ports");
    if(HSD_PadLibData.queue!=&h->queue||HSD_PadLibData.qcount)return fail(e,n,"Native menu raw PAD queue is not idle");
    memset(&h->queue,0,sizeof(h->queue));memcpy(h->queue.stat,raw,sizeof(h->queue.stat));
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    /* Supplied raw samples replace PADRead, not the rumble interpreter that
     * precedes it in HSD_PadRenewRawStatus. Match stepping uses this same
     * source boundary; menu confirmations must advance and release requests. */
    HSD_PadRumbleInterpret();
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    if(HSD_PadLibData.qcount)return fail(e,n,"Source PAD processing did not consume its sample");
    gm_EvaluateAllControllerInputs();
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE ||
        h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        result = source_scene_tick(h, e, n);
    } else {
        result = melee_web_menu_tick(h->session,e,n);
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (result == MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED &&
        h->source_scene == MELEE_WEB_HOST_SCENE_CSS && h->transition != 0 &&
        melee_web_menu_phase(h->session) == MELEE_WEB_MENU_CSS) {
        const CSSData* css = melee_web_menu_css(h->session);
        if (css != NULL) {
            h->final_pending_css_draw_witness = 1;
            h->final_pending_css_draw_tick_result = result;
            h->final_pending_css_draw_request = h->transition;
            /* Authored CSSData pending_scene_change is committed by OnExit,
             * which has not run at this retained result-3 draw boundary. */
            h->final_pending_css_draw_pending_scene_change = css->pending_scene_change;
            h->final_pending_css_draw_generation = h->generation;
            h->final_pending_css_draw_session = h->session;
            h->final_pending_css_draw_css = (CSSData*) css;
            h->final_pending_css_draw_scene_info = &h->source_scene_info;
            h->final_pending_css_draw_seed_owner = seed_ptr;
            h->final_pending_css_draw_seed = h->seed;
            {
                unsigned port;
                for (port = 0; port < 4; ++port) {
                    final_pending_css_pad_copy(
                        &h->final_pending_css_draw_raw[port], &raw[port]);
                }
            }
        }
    }
#endif
    return result;
}

static int host_draw_render(MeleeWebMenuHost* h,char* e,size_t n)
{
    h->drawing=1;
    /* The source screen camera scales its authored viewport by the current
     * VI mode. Bootstrap owns HSD objects; Aurora owns display startup. Supply
     * the same NTSC mode selected by gmMain for this scoped source draw. */
    GXRenderModeObj saved_mode=*HSD_VIGetRenderMode();
    *HSD_VIGetRenderMode()=GXNtsc480IntDf;
    GXInvalidateVtxCache();GXInvalidateTexAll();
    HSD_StartRender(HSD_RP_SCREEN);HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_GObj_80390FC0();HSD_Init_803755A8();
    *HSD_VIGetRenderMode()=saved_mode;
    h->drawing=0;
    if(!melee_web_menu_clock_present())return fail(e,n,"Original scene presentation clock lost ownership");
    return ok(e,n);
}

int melee_web_menu_host_draw(MeleeWebMenuHost* h,char* e,size_t n){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (h && h == owner && h->first_sss_pair_state != 0) {
        if (h->source_scene == MELEE_WEB_HOST_SCENE_SSS)
            ++h->first_sss_pair_host_draw_calls;
        first_sss_pair_fail(h,
            "First SSS constructor pair refuses source draws until checked cleanup");
        return fail(e,n,h->first_sss_pair_error);
    }
    final_pending_css_draw_invalidate(h);
#endif
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu draw requires an idle live scene");
    if(h->transition!=0)return ok(e,n);
    return host_draw_render(h,e,n);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
int melee_web_menu_host_arm_final_pending_css_draw(MeleeWebMenuHost* h,
    unsigned input_ordinal, unsigned consumed_pad_sequence,
    const PADStatus raw[4], char* e, size_t n)
{
    unsigned port;
    if (!h || h != owner || h->final_pending_css_draw_state != 0 ||
        h->final_pending_css_draw_witness != 1 || raw == NULL ||
        input_ordinal != 148 || consumed_pad_sequence != 1574 ||
        h->final_pending_css_draw_tick_result !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED) {
        if (h && h == owner) h->final_pending_css_draw_state = 2;
        return fail(e, n,
                    "Final CSS draw arm requires its exact retained final input");
    }
    for (port = 0; port < 4; ++port) {
        if (!final_pending_css_pad_equal(&h->final_pending_css_draw_raw[port],
                                         &raw[port])) {
            h->final_pending_css_draw_state = 2;
            return fail(e, n,
                        "Final CSS draw arm PAD differs from the consumed input");
        }
    }
    if (!final_pending_css_draw_owner_live(h, e, n)) {
        h->final_pending_css_draw_state = 2;
        return 0;
    }
    h->final_pending_css_draw_returned = 0;
    h->final_pending_css_draw_input_ordinal = input_ordinal;
    h->final_pending_css_draw_pad_sequence = consumed_pad_sequence;
    h->final_pending_css_draw_state = 1;
    return ok(e, n);
}

int melee_web_menu_host_draw_final_pending_css(MeleeWebMenuHost* h,
                                                char* e, size_t n)
{
    if (!h || h != owner || h->final_pending_css_draw_state != 1 ||
        h->final_pending_css_draw_witness != 1 ||
        h->final_pending_css_draw_input_ordinal != 148 ||
        h->final_pending_css_draw_pad_sequence != 1574) {
        if (h && h == owner) h->final_pending_css_draw_state = 2;
        return fail(e, n,
                    "Final CSS draw requires its one-use retained host authorization");
    }
    /* Consume before any HSD/GX work. A failed renderer or clock check cannot
     * retry the same terminal transition draw. */
    h->final_pending_css_draw_state = 2;
    if (!final_pending_css_draw_owner_live(h, e, n)) return 0;
    if (!host_draw_render(h,e,n)) return 0;
    h->final_pending_css_draw_returned = 1;
    return ok(e,n);
}
#endif

static int host_leave_source_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES];
    MeleeWebPadState* next_input;

    if (h->transition == 0) {
        return fail(e, n, "Original title/main scene has no completed transition");
    }
    if (h->opening_active) {
        int request = 0;
        int pending_mode;
        int* source_exit_payload = NULL;
        melee_web_pad_state_capture(bytes);
        next_input = melee_web_pad_state_decode(bytes, sizeof(bytes), e, n);
        if (next_input == NULL) return 0;
        if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE &&
            h->opening_state_id == 2) {
            source_exit_payload = (int*) gm_GetCurrentSceneExitData();
            if (source_exit_payload != &h->title_exit_payload ||
                h->source_state.info.exit_data != source_exit_payload) {
                melee_web_pad_state_free(next_input);
                return fail(e, n,
                    "Opening Title exit payload lost its persistent GameSceneInfo owner");
            }
        } else if (h->source_scene != MELEE_WEB_HOST_SCENE_OPENING) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                "Opening state exit requires its authored Title/movie scene owner");
        }
        if (h->opening_scene_handler != NULL &&
            h->opening_scene_handler->on_exit != NULL) {
            h->opening_scene_handler->on_exit(h->source_state.info.exit_data);
        }
        if (!h->opening_state_exit_called && h->source_state.on_exit != NULL) {
            h->source_state.on_exit(&h->source_state);
            h->opening_state_exit_called = 1;
        }
        if (!melee_web_menu_clock_request(&request) || request == 0) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                "Opening source exit lost its authored menu-clock request");
        }
        pending_mode = melee_web_vs_mode_pending_mode();
        if (pending_mode >= 0) {
            if (pending_mode == GM_MENU && h->opening_state_id == 2) {
                h->source_target_mode = GM_MENU;
                h->source_previous_mode = GM_OPENING_MV;
                h->opening_active = 0;
                h->opening_state_id = -1;
                h->opening_scene_handler = NULL;
                if (!melee_web_vs_mode_set_route(GM_MENU, GM_OPENING_MV)) {
                    melee_web_pad_state_free(next_input);
                    return fail(e, n,
                        "Opening Title Start could not transfer its source GM_MENU route");
                }
            } else if (pending_mode == GM_TITLE) {
                h->source_target_mode = GM_TITLE;
                h->source_previous_mode = GM_OPENING_MV;
                h->opening_active = 0;
                h->opening_state_id = -1;
                h->opening_scene_handler = NULL;
                if (!melee_web_vs_mode_set_route(GM_TITLE, GM_OPENING_MV)) {
                    melee_web_pad_state_free(next_input);
                    return fail(e, n,
                        "Opening interruption could not transfer its source GM_TITLE route");
                }
            } else {
                if (e && n) {
                    if (source_exit_payload != NULL) {
                        snprintf(e, n,
                            "Opening Title requested unsupported destination %d from buttons 0x%x",
                            pending_mode, *source_exit_payload);
                    } else {
                        snprintf(e, n,
                            "Opening state %d requested unsupported destination %d",
                            h->opening_state_id, pending_mode);
                    }
                }
                melee_web_pad_state_free(next_input);
                return 0;
            }
        } else {
            const int next = melee_web_opening_mode_advance_state();
            if (next < 0) {
                melee_web_pad_state_free(next_input);
                return fail(e, n,
                    "Opening source state has no authored continuation");
            }
            h->source_target_mode = GM_OPENING_MV;
            h->opening_state_id = next;
            h->opening_scene_handler = NULL;
            h->opening_active = 0;
        }
        h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
        h->entered = 0;
        h->transition = 0;
        h->opening_scene_entered = 0;
        h->opening_state_exit_called = 0;
        melee_web_pad_state_free(h->input);
        h->input = next_input;
        restore_context(h);
        return ok(e, n);
    }
    melee_web_pad_state_capture(bytes);
    next_input = melee_web_pad_state_decode(bytes, sizeof(bytes), e, n);
    if (next_input == NULL) {
        return 0;
    }
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE) {
        int* source_exit_payload = (int*) gm_GetCurrentSceneExitData();
        if (source_exit_payload != &h->title_exit_payload ||
            h->source_state.info.exit_data != source_exit_payload) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                        "Original title exit payload lost its persistent GameSceneInfo owner");
        }
        h->source_state.on_exit(&h->source_state);
        const int requested_mode = melee_web_vs_mode_pending_mode();
        if (requested_mode == GM_MENU &&
            (*source_exit_payload & HSD_PAD_START) != 0) {
            h->source_target_mode = requested_mode;
            h->source_previous_mode = GM_TITLE;
            if (!melee_web_vs_mode_set_route(requested_mode, GM_TITLE)) {
                melee_web_pad_state_free(next_input);
                return fail(e, n, "Original title exit could not set GM_MENU provenance");
            }
        } else if (requested_mode == GM_OPENING_MV &&
                   *source_exit_payload == 0) {
            /* Retail Title timeout enters Opening mode using the mode state
             * selected by gmTitleMode_OnExit.  Let its own OnLoad callback
             * install that state; do not reinterpret the timeout as Start. */
            if (gm_801BF718() != 1 ||
                !melee_web_vs_mode_set_route(GM_OPENING_MV, GM_TITLE)) {
                melee_web_pad_state_free(next_input);
                return fail(e, n,
                            "Original title idle requested an unsupported Opening mode state");
            }
            gm_Mode_Opening_OnLoad();
            h->source_target_mode = requested_mode;
        } else {
            melee_web_pad_state_free(next_input);
            if (e && n)
                snprintf(e, n,
                         "Original title requested unsupported destination %d from buttons 0x%x",
                         requested_mode, *source_exit_payload);
            return 0;
        }
    } else if (h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        const int requested_mode = h->main_exit.pending_mode;
        if (requested_mode != GM_TITLE && requested_mode != GM_MENU &&
            requested_mode != GM_VS && requested_mode != GM_TRAINING) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main scene requested an unsupported mode");
        }
        h->source_state.info.exit_data = &h->main_exit;
        h->source_state.on_exit(&h->source_state);
        if (melee_web_vs_mode_pending_mode() != requested_mode) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main exit changed its checked destination");
        }
        if (requested_mode == GM_VS) {
            /* mnmainrule/mnitemsw commit into the live source globals before
             * requesting GM_VS. Capture only after the original Main mode
             * OnExit has consumed that request, so returning through CSS
             * cannot restore the pre-rules values saved at CSS exit. */
            h->selected_rules = *gmMainLib_GetGameRules();
            h->selected_preferences = *gmMainLib_8015CC58();
            h->selected_characters = *gmMainLib_GetUnlockedCharactersBitmaskPtr();
            h->selected_stages = *gmMainLib_8015EDA4();
            if (!melee_web_save_profile_owner_capture_preferences(
                    h->profile, &h->persisted_preferences, e, n)) {
                melee_web_pad_state_free(next_input);
                return 0;
            }
        }
        h->source_target_mode = requested_mode;
        h->source_previous_mode = GM_MENU;
        if (!melee_web_vs_mode_set_route(requested_mode, GM_MENU)) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main exit could not set mode provenance");
        }
        h->source_mode_kind = requested_mode;
        if (requested_mode == GM_TRAINING) {
            if (!h->training_mode_initialized) {
                gm_Mode_Training_OnInit();
                h->training_mode_initialized = 1;
            }
            gm_Mode_Training_OnLoad();
            h->training_start_pending = 0;
        }
        h->selected_preferences = *gmMainLib_8015CC58();
    } else {
        melee_web_pad_state_free(next_input);
        return fail(e, n, "Source scene is not title or main");
    }
    /* Title and Main transitions destroy their temporary world before the
     * next source scene is prepared. Preserve the complete live preference
     * block before restore_context restores the caller's copy; otherwise the
     * next Title/Main/VS scene reapplies the stale selection and loses menu
     * writes such as Settings > Sound's SaveData balance. */
    h->selected_preferences = *gmMainLib_8015CC58();
    h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
    h->entered = 0;
    h->transition = 0;
    h->css_parent_route_requested = 0;
    melee_web_pad_state_free(h->input);
    h->input = next_input;
    restore_context(h);
    return ok(e, n);
}

/* Eject is a host teardown, not a source menu choice.  Title and Main have no
 * scene OnExit callback; their GameModeState OnExit callbacks schedule a real
 * destination and must not be called just to retire browser-owned resources.
 * GameplayMenuWorld::close() owns GObj/card/audio teardown after this retires
 * the host's live-scene lease. */
static int host_abort_source_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    if ((h->source_scene != MELEE_WEB_HOST_SCENE_TITLE &&
         h->source_scene != MELEE_WEB_HOST_SCENE_MAIN &&
         h->source_scene != MELEE_WEB_HOST_SCENE_OPENING &&
         h->source_scene != MELEE_WEB_HOST_SCENE_OPENING_VS) ||
        !h->entered || h->drawing) {
        return fail(e, n,
            "Source-scene abort requires an idle live Title/Main/Opening scene");
    }
    if (h->opening_active) {
        /* Opening's movie-state OnExit owns the THP reader/alarm cleanup.
         * State 2 is the Title mode callback: Eject must not turn its current
         * exit payload into an ordinary GM_MENU/Challenger transition. */
        if (h->source_scene == MELEE_WEB_HOST_SCENE_OPENING &&
            h->opening_scene_handler != NULL &&
            h->opening_scene_handler->on_exit != NULL) {
            h->opening_scene_handler->on_exit(h->source_state.info.exit_data);
        }
        if (!h->opening_state_exit_called && h->opening_state_id != 2 &&
            h->source_state.on_exit != NULL) {
            h->source_state.on_exit(&h->source_state);
        }
        h->opening_active = 0;
        h->opening_scene_entered = 0;
        h->opening_match_suspended = 0;
        h->opening_state_exit_called = 0;
        h->opening_state_id = -1;
        h->opening_scene_handler = NULL;
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
    h->entered = 0;
    h->transition = 0;
    h->source_target_mode = -1;
    h->css_parent_route_requested = 0;
    h->aborted_source_scene = 1;
    restore_context(h);
    return ok(e, n);
}

int melee_web_menu_host_leave(MeleeWebMenuHost* h,int abort_scene,char* e,size_t n){
    if (abort_scene)
        host_sss_continuation_invalidate(h == owner ? h : owner);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    final_pending_css_draw_invalidate(h);
    if (h && h == owner && h->first_css_return_state)
        h->first_css_return_state = 3;
#endif
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu leave requires an idle live scene");
    if (!sync_source_preference_changes(h,e,n)) return 0;
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE ||
        h->source_scene == MELEE_WEB_HOST_SCENE_MAIN ||
        h->source_scene == MELEE_WEB_HOST_SCENE_OPENING ||
        h->source_scene == MELEE_WEB_HOST_SCENE_OPENING_VS) {
        if (abort_scene) {
            return host_abort_source_scene(h, e, n);
        }
        return host_leave_source_scene(h, e, n);
    }
    uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(bytes);
    MeleeWebPadState* input=melee_web_pad_state_decode(bytes,sizeof(bytes),e,n);
    if(!input)return 0;
    const int was_sss=melee_web_menu_phase(h->session)==MELEE_WEB_MENU_SSS;
    const int was_css=melee_web_menu_phase(h->session)==MELEE_WEB_MENU_CSS;
    const int result=abort_scene?melee_web_menu_abort(h->session,e,n):
        melee_web_menu_phase(h->session)==MELEE_WEB_MENU_CSS?melee_web_menu_leave_css(h->session,e,n):melee_web_menu_leave_sss(h->session,e,n);
    if(!result){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
        if (h->first_sss_pair_state != 0)
            first_sss_pair_fail(h, e && n && e[0] ? e :
                "First SSS pair failed during the ordinary CSS leave");
#endif
        melee_web_pad_state_free(input);return 0;
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if (h->first_sss_pair_state != 0) {
        if (!abort_scene && was_css &&
            melee_web_menu_phase(h->session) == MELEE_WEB_MENU_SSS_READY &&
            h->first_sss_pair_state == 1) {
            h->first_sss_pair_state = 2;
        } else {
            first_sss_pair_fail(h,
                "First SSS pair did not retain its ordinary CSS-to-SSS route");
        }
    }
#endif
    /* SSS has no SIS table of its own; the scene preparation heap is ours. */
    if(was_sss)HSD_SisLib_803A5FBC();
    h->selected_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->selected_stages=*gmMainLib_8015EDA4();
    h->selected_rules=*gmMainLib_GetGameRules();
    h->selected_preferences=*gmMainLib_8015CC58();
    if (!abort_scene && was_css) {
        const CSSData* css = melee_web_menu_css(h->session);
        if (css != NULL && css->pending_scene_change == CSSPendingSceneChange_2) {
            h->source_target_mode = GM_MENU;
        }
    }
    melee_web_pad_state_free(h->input);h->input=input;
    h->entered=0;h->source_scene=MELEE_WEB_HOST_SCENE_NONE;
    /* The source transition has been consumed by menu_leave_* above. Keep
     * only the separately captured route target; a closed READY host must
     * not retain a stale transition or parent-route request that prevents
     * owned teardown or contaminates a later scene entry. */
    h->transition=0;h->css_parent_route_requested=0;
    if (!abort_scene && was_css && h->source_mode_kind == GM_VS &&
        melee_web_menu_phase(h->session) == MELEE_WEB_MENU_SSS_READY) {
        if (!host_sss_continuation_arm(h,e,n)) {
            char arm_error[160] = {0};
            if (e && n) snprintf(arm_error,sizeof(arm_error),"%s",e);
            return host_restore_failed_continuation(h,
                arm_error[0] ? arm_error :
                    "CSS-to-SSS counter continuation capture failed",e,n);
        }
        if (!restore_context_checked(h,e,n)) {
            host_sss_continuation_invalidate(h);
            return 0;
        }
        if (h->sss_continuation_state != HOST_SSS_CONTINUATION_AVAILABLE)
            return fail(e,n,"CSS-to-SSS counter continuation was not armed");
        return ok(e,n);
    }
    restore_context(h);return ok(e,n);
}
int melee_web_menu_host_phase(const MeleeWebMenuHost* h){return h&&h==owner?melee_web_menu_phase(h->session):MELEE_WEB_MENU_CLOSED;}
int melee_web_menu_host_source_scene(const MeleeWebMenuHost* h){
    return h&&h==owner?h->source_scene:MELEE_WEB_HOST_SCENE_NONE;
}
int melee_web_menu_host_source_observe(
    const MeleeWebMenuHost* h, MeleeWebMenuSourceObservation* out,
    char* e, size_t n)
{
    const GameRules* rules;
    const struct gmm_x1CB0* preferences;
    if (!h || h != owner || !h->entered || h->audio == NULL || !out ||
        (h->source_scene != MELEE_WEB_HOST_SCENE_MAIN &&
         h->source_scene != MELEE_WEB_HOST_SCENE_CSS &&
         h->source_scene != MELEE_WEB_HOST_SCENE_SSS)) {
        return fail(e, n,
                    "Source observation requires an active original menu scene");
    }
    memset(out, 0, sizeof(*out));
    rules = gmMainLib_GetGameRules();
    preferences = gmMainLib_8015CC58();
    out->source_scene = h->source_scene;
    if (h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        out->menu_kind = mn_804A04F0.cur_menu;
        out->previous_menu_kind = mn_804A04F0.prev_menu;
        out->hovered_selection = mn_804A04F0.hovered_selection;
        out->confirmed_selection = mn_804A04F0.confirmed_selection;
        out->menu_buttons = mn_804A04F0.buttons;
        out->item_input_locked = out->menu_kind == 0x10 &&
                                 mnItemSw_804D6BEC != 0;
    }
    out->rule_mode = rules->mode;
    out->stock_count = rules->stock_count;
    out->time_limit = rules->time_limit;
    out->stock_time_limit = rules->stock_time_limit;
    out->handicap = rules->handicap;
    out->damage_ratio = rules->damage_ratio;
    out->friendly_fire = rules->friendly_fire;
    out->pause = rules->pause;
    out->item_frequency = (int) (int8_t) preferences->item_freq;
    out->item_mask = preferences->item_mask;
    if (h->source_scene == MELEE_WEB_HOST_SCENE_CSS) {
        const CSSData* css = melee_web_menu_css(h->session);
        if (css == NULL) {
            return fail(e, n, "CSS team observation requires the live original CSS payload");
        }
        out->css_setup_valid = 1;
        out->css_is_teams = css->vs.start.rules.is_teams;
        for (int i = 0; i < 4; ++i)
            out->css_player_teams[i] = css->vs.start.players[i].team;
    }
    return ok(e, n);
}
int melee_web_menu_host_route_target_mode(const MeleeWebMenuHost* h){
    return h&&h==owner&&!h->entered&&
        h->source_scene==MELEE_WEB_HOST_SCENE_NONE?h->source_target_mode:-1;
}
int melee_web_menu_host_route_target_state(const MeleeWebMenuHost* h){
    return h&&h==owner&&!h->entered&&
        h->source_scene==MELEE_WEB_HOST_SCENE_NONE&&
        h->source_target_mode==GM_OPENING_MV?gm_GetCurrentSceneIndex():-1;
}
int melee_web_menu_host_opening_target_state(const MeleeWebMenuHost* h){
    if(!h||h!=owner)return -1;
    if(h->opening_active)return h->opening_state_id;
    return melee_web_menu_host_route_target_state(h);
}

int melee_web_menu_host_reenter_css_after_parent(MeleeWebMenuHost* h,
                                                   MeleeWebAudio* audio,
                                                   char* e, size_t n)
{
    MeleeWebMenuPhase actual;
    if (!host_sss_continuation_alternative_entry(h,e,n)) return 0;
    actual = melee_web_menu_phase(h->session);

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE ||
        h->entered || h->audio || h->source_target_mode != GM_VS ||
        actual != MELEE_WEB_MENU_CLOSED || !h->vs_mode_owned) {
        return fail(e, n,
                    "CSS re-entry requires a completed checked GM_MENU -> GM_VS route");
    }
    if (!host_prepare_world(h, audio, MELEE_WEB_MENU_CSS_READY, 0, NULL, e, n)) {
        return 0;
    }
    if (!melee_web_menu_reopen_css_after_parent(h->session, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_CSS;
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
int melee_web_menu_host_provenance(const MeleeWebMenuHost* h,MeleeWebPipelineSourceContext* out){
    if(!h||h!=owner||!out)return 0;
    const int phase=melee_web_menu_phase(h->session);
    const int sss=phase==MELEE_WEB_MENU_SSS||phase==MELEE_WEB_MENU_SSS_READY;
    const CSSData* css=melee_web_menu_css(h->session);
    const SSSData* stages=melee_web_menu_sss(h->session);
    if(!css||!stages)return 0;
    const StartMeleeData* start=sss?&stages->vs.start:&css->vs.start;
    memset(out,0,sizeof(*out));
    for(unsigned i=0;i<4;++i){out->players[i].motion_id=-1;out->players[i].stocks=-1;}
    out->scene=sss?MELEE_WEB_PIPELINE_SCENE_SSS:MELEE_WEB_PIPELINE_SCENE_CSS;
    out->phase=MELEE_WEB_PIPELINE_PHASE_INTERACTIVE;
    out->world_generation=melee_web_gameplay_generation();
    out->source_tick=melee_web_gameplay_provenance_tick();
    out->owner_kind=MELEE_WEB_PIPELINE_OWNER_MENU_SCENE;
    out->owner_id=out->scene;
    out->stage=start->rules.stkind;out->hud_layout=start->rules.x0_3;
    const MeleeWebStageContent* stage=melee_web_stage_content(start->rules.stkind);
    out->ground=stage?stage->ground_kind:UINT32_MAX;
    int count=melee_web_menu_active_player_count(start);
    if(count<0||count>4)return 0;
    out->active_player_count=(uint32_t)count;
    for(int i=0, compact=0;i<MELEE_WEB_MENU_MAX_PLAYERS;++i){
        const PlayerInitData* source=&start->players[i];
        if(source->slot_type==Gm_PKind_NA)continue;
        const MeleeWebFighterContent* fighter=melee_web_fighter_content(source->ckind);
        out->players[compact].character=source->ckind;
        out->players[compact].fighter_kind=fighter?fighter->fighter_kind:UINT32_MAX;
        out->players[compact].costume=source->color;out->players[compact].subcolor=source->sub_color;
        out->players[compact].effect_bank=fighter?fighter->effect_bank:UINT32_MAX;
        out->players[compact].motion_id=-1;out->players[compact].stocks=source->stocks;
        ++compact;
    }
    return 1;
}
#endif
static int host_selection_from_vs(const MeleeWebMenuHost* h,
                                  const VsModeData* vs,
                                  MeleeWebMenuMatchSelection* out,
                                  char* e,size_t n){
    out->start=vs->start;
    const int count=melee_web_menu_active_player_count(&vs->start);
    if(count<MELEE_WEB_MENU_MIN_PLAYERS||count>MELEE_WEB_MENU_MAX_PLAYERS)
        return fail(e,n,"Original menu did not commit two through four active players");
    memset(out->players,0,sizeof(out->players));
    out->player_count=(uint32_t)count;
    for(unsigned i=0;i<MELEE_WEB_MENU_MAX_PLAYERS;i++){
        const PlayerInitData* p=&vs->start.players[i];
        if(p->slot_type==Gm_PKind_NA)continue;
        const unsigned port=p->slot?p->slot-1:i;
        const MeleeWebFighterContent* content=melee_web_fighter_content(p->ckind);
        if(!content||port!=i||p->color>=content->costumes||p->sub_color>4)
            return fail(e,n,"Unsupported original menu port, costume or tint");
        out->players[i].controller=port;out->players[i].stocks=p->stocks;
        out->players[i].costume=p->color;out->players[i].sub_color=p->sub_color;
    }
    if(vs->start.rules.x0_3<1||vs->start.rules.x0_3>6)
        return fail(e,n,"Original HUD layout is unsupported");
    out->hud_layout=vs->start.rules.x0_3;
    out->random_seed=h->seed;
    out->unlocked_characters=h->selected_characters;
    out->unlocked_stages=h->selected_stages;
    out->save_profile_present=1;
    return ok(e,n);
}
const VsModeData* melee_web_menu_host_post_vs_mode(const MeleeWebMenuHost* h){
    if(!h||h!=owner||h->entered||h->audio||seed_ptr!=&h->seed)return NULL;
    return melee_web_menu_post_vs_mode(h->session);
}
int melee_web_menu_host_selection(const MeleeWebMenuHost* h,MeleeWebMenuMatchSelection* out,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed)
        return fail(e,n,"Selection requires a closed menu scene with owned RNG");
    const VsModeData* vs=melee_web_menu_ready_vs(h->session);
    if(!vs||!melee_web_menu_sss_selection_valid(melee_web_menu_sss(h->session)))
        return fail(e,n,"Original menus have not committed a supported selection");
    return host_selection_from_vs(h,vs,out,e,n);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
int melee_web_menu_host_enable_stadium_c1a(MeleeWebMenuHost* h,
                                            char* e,size_t n){
    if(!h||h!=owner||h->entered||h->source_scene!=MELEE_WEB_HOST_SCENE_NONE||
       h->audio||h->source_target_mode!=-1||h->source_mode_kind!=GM_VS||
       h->vs_mode_owned||h->opening_active||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_CREATED||
       h->stadium_c1a_enabled)
        return fail(e,n,"Stadium C1a requires a fresh, unentered VS menu host");
    if(!melee_web_menu_enable_stadium_c1a(h->session,e,n))return 0;
    h->stadium_c1a_enabled=1;
    return ok(e,n);
}

int melee_web_menu_host_stadium_c1a_raw_selection(
    const MeleeWebMenuHost* h,StartMeleeData* out,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed||
       !h->stadium_c1a_enabled||
       !melee_web_menu_stadium_c1a_ready_selection_valid(h->session))
        return fail(e,n,"Stadium C1a raw selection requires its committed source SSS payload");
    const SSSData* sss=melee_web_menu_sss(h->session);
    if(!sss||!sss->start_game||sss->vs.start.rules.stkind!=St_Kind_PStadium)
        return fail(e,n,"Stadium C1a raw selection lost source StKind 3");
    *out=sss->vs.start;
    return ok(e,n);
}

int melee_web_menu_host_stadium_c1a_selection(
    const MeleeWebMenuHost* h,MeleeWebMenuMatchSelection* out,
    char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed||
       !h->stadium_c1a_enabled||
       !melee_web_menu_stadium_c1a_ready_selection_valid(h->session))
        return fail(e,n,"Stadium C1a requires its source-selected prepared SSS payload");
    const VsModeData* vs=melee_web_menu_ready_vs(h->session);
    if(!vs||vs->start.rules.stkind!=St_Kind_PStadium)
        return fail(e,n,"Stadium C1a selection lost its original StKind");
    return host_selection_from_vs(h,vs,out,e,n);
}
#endif
int melee_web_menu_host_raw_selection(const MeleeWebMenuHost* h,StartMeleeData* out,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Raw selection requires a completed closed SSS scene");
    const SSSData* sss=melee_web_menu_sss(h->session);
    if(!sss||!sss->start_game||!melee_web_menu_sss_selection_valid(sss))
        return fail(e,n,"Original SSS has not committed a supported raw selection");
    *out=sss->vs.start;return ok(e,n);
}
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost* h){
    if(!h||h!=owner||h->entered||h->audio||melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)return NULL;
    return h->input;
}
int melee_web_menu_host_match_finished(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||h->results_active||seed_ptr!=&h->seed||melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Match must restore its source ownership before returning to CSS");
    MeleeWebPadState* input=melee_web_pad_state_decode(bytes,MELEE_WEB_PAD_STATE_BYTES,e,n);
    if(!input)return 0;
    melee_web_pad_state_free(h->input);h->input=input;
    h->seed=seed;return ok(e,n);
}
static void restore_results_route(MeleeWebMenuHost* h){
    *gmVsMelee_GetVsData()=h->route_saved_vs;
    gmVsMelee_VsExitInfo=h->route_saved_exit;
    gmVsMelee_StartData=h->route_saved_start;
    gmVsMelee_SuddenDeathExitInfo=h->route_saved_sudden_death_exit;
    gmVsMelee_ResultsEnterData=h->route_saved_result;
    *gm_GetChallengerData()=h->route_saved_challenger;
    memcpy(gmVsMelee_GetKOCounts(),h->route_saved_ko,sizeof(h->route_saved_ko));
    *gmMainLib_GetGameRules()=h->route_saved_rules;
    *gmMainLib_8015CC58()=h->route_saved_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->route_saved_characters;
    *gmMainLib_8015EDA4()=h->route_saved_stages;
    if(!melee_web_vs_mode_end())abort();
    h->results_active=h->results_exited=h->results_committed=h->prize_active=0;
    h->sudden_death_active=0;
    h->sudden_death_claimed=0;
    h->sudden_death_claim_consumed=0;
    h->sudden_death_owner_id=0;
    memset(&h->sudden_death_start,0,sizeof(h->sudden_death_start));
    memset(&h->sudden_death_scene_info,0,sizeof(h->sudden_death_scene_info));
    h->sudden_death_saved_scene_info=NULL;
    h->sudden_death_scene_active=0;
}
static int begin_vs_match_route(MeleeWebMenuHost* h,
    const MatchExitInfo* exit_info,uint32_t seed,int* next,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||h->results_active||
       seed_ptr!=&h->seed||!exit_info||!next||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Results routing requires a completed closed match");
    const VsModeData* prepared=melee_web_menu_post_vs_mode(h->session);
    if(!prepared)return fail(e,n,"VS route requires its retained original entry state");
    if(!melee_web_vs_mode_begin())return fail(e,n,"Original VS mode is already owned");
    h->route_saved_vs=*gmVsMelee_GetVsData();
    h->route_saved_exit=gmVsMelee_VsExitInfo;
    h->route_saved_start=gmVsMelee_StartData;
    h->route_saved_sudden_death_exit=gmVsMelee_SuddenDeathExitInfo;
    h->route_saved_result=gmVsMelee_ResultsEnterData;
    h->route_saved_challenger=*gm_GetChallengerData();
    memcpy(h->route_saved_ko,gmVsMelee_GetKOCounts(),sizeof(h->route_saved_ko));
    h->route_saved_rules=*gmMainLib_GetGameRules();
    h->route_saved_preferences=*gmMainLib_8015CC58();
    h->route_saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->route_saved_stages=*gmMainLib_8015EDA4();
    h->results_active=1;h->results_exited=h->results_committed=0;h->seed=seed;
    *gmMainLib_GetGameRules()=h->selected_rules;
    *gmMainLib_8015CC58()=h->selected_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->selected_characters;
    *gmMainLib_8015EDA4()=h->selected_stages;
    const CSSData* css=melee_web_menu_css(h->session);
    *gmVsMelee_GetVsData()=*prepared;
    memcpy(gmVsMelee_GetKOCounts(),css->ko_counts,sizeof(h->route_saved_ko));
    gmVsMelee_VsExitInfo=*exit_info;
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Vs))abort();
    gm_Mode_Vs_States[2].on_exit(&gm_Mode_Vs_States[2]);
    *next=melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
    return ok(e,n);
}
static void enter_results_route(ResultsMatchInfo* result){
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Results))abort();
    gm_Mode_Vs_States[4].on_enter(&gm_Mode_Vs_States[4]);
    *result=gmVsMelee_ResultsEnterData;
}
int melee_web_menu_host_results_begin(MeleeWebMenuHost* h,
    const MatchExitInfo* exit_info,uint32_t seed,ResultsMatchInfo* result,
    char* e,size_t n){
    int next;
    if(!result)return fail(e,n,"Results routing requires a completed closed match");
    if(!begin_vs_match_route(h,exit_info,seed,&next,e,n))return 0;
    if(next!=gmVsMode_State_Results){
        restore_results_route(h);
        if(e&&n)snprintf(e,n,"Original VS requested unsupported state %d before Results",next);
        return 0;
    }
    enter_results_route(result);
    return ok(e,n);
}
int melee_web_menu_host_match_continuation_begin(MeleeWebMenuHost* h,
    const MatchExitInfo* exit_info,uint32_t seed,
    MeleeWebMenuMatchContinuation* continuation,char* e,size_t n){
    int next;
    if(!continuation)return fail(e,n,"Match continuation output is required");
    memset(continuation,0,sizeof(*continuation));
    if(!begin_vs_match_route(h,exit_info,seed,&next,e,n))return 0;
    if(next==gmVsMode_State_Results){
        enter_results_route(&continuation->payload.results);
        continuation->kind=MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS;
        return ok(e,n);
    }
    if(next==gmVsMode_State_SuddenDeath){
        if(!next_sudden_death_owner_id){
            restore_results_route(h);
            return fail(e,n,"Sudden Death continuation identity space is exhausted");
        }
        if(!melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath))abort();
        gm_Mode_Vs_States[3].on_enter(&gm_Mode_Vs_States[3]);
        continuation->payload.sudden_death_start=gmVsMelee_StartData;
        continuation->kind=MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH;
        continuation->owner_id=next_sudden_death_owner_id++;
        h->sudden_death_owner_id=continuation->owner_id;
        h->sudden_death_start=gmVsMelee_StartData;
        h->sudden_death_claimed=0;
        h->sudden_death_claim_consumed=0;
        h->sudden_death_active=1;
        return ok(e,n);
    }
    restore_results_route(h);
    if(e&&n)snprintf(e,n,"Original VS requested unsupported state %d",next);
    return 0;
}
int melee_web_menu_host_sudden_death_finish(MeleeWebMenuHost* h,
    uint64_t owner_id,const MatchExitInfo* exit_info,uint32_t seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],
    MeleeWebMenuMatchContinuation* continuation,char* e,size_t n){
    if(!continuation)
        return fail(e,n,"Sudden Death Results continuation output is required");
    memset(continuation,0,sizeof(*continuation));
    if(!h||h!=owner||h->entered||h->audio||!h->results_active||
       !h->sudden_death_active||h->sudden_death_claimed||
       h->sudden_death_scene_active||
       !owner_id||h->sudden_death_owner_id!=owner_id||
       h->results_exited||!exit_info||
       !input||
       seed_ptr!=&h->seed||melee_web_gameplay_generation()||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Sudden Death must close before its original Results handoff");
    MeleeWebPadState* next_input=melee_web_pad_state_decode(
        input,MELEE_WEB_PAD_STATE_BYTES,e,n);
    if(!next_input)return 0;
    /* Match teardown restores the previous RNG pointer, not its final value.
     * Transfer the closed Sudden Death match seed before source callbacks. */
    h->seed=seed;
    gmVsMelee_SuddenDeathExitInfo=*exit_info;
    if(!melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath))abort();
    gm_Mode_Vs_States[3].on_exit(&gm_Mode_Vs_States[3]);
    const int next=melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
    if(next!=gmVsMode_State_Results){
        /* OnExit may already have merged source MatchEnd state. End this
         * continuation before returning so callers cannot invoke it twice. */
        restore_results_route(h);
        melee_web_pad_state_free(next_input);
        if(e&&n)snprintf(e,n,"Original Sudden Death requested unsupported state %d",next);
        return 0;
    }
    enter_results_route(&continuation->payload.results);
    melee_web_pad_state_free(h->input);
    h->input=next_input;
    continuation->kind=MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS;
    h->sudden_death_active=0;
    h->sudden_death_owner_id=0;
    h->sudden_death_claim_consumed=0;
    memset(&h->sudden_death_start,0,sizeof(h->sudden_death_start));
    return ok(e,n);
}

static int sudden_death_selection(const MeleeWebMenuHost* h,
    const MeleeWebMenuMatchContinuation* continuation,
    MeleeWebMenuMatchSelection* out,char* e,size_t n){
    if(!out)return fail(e,n,"Sudden Death selection output is required");
    memset(out,0,sizeof(*out));
    if(!h||h!=owner||h->entered||h->audio||!continuation||
       continuation->kind!=MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH||
       !continuation->owner_id||!h->sudden_death_active||
       h->sudden_death_owner_id!=continuation->owner_id||
       memcmp(&h->sudden_death_start,&continuation->payload.sudden_death_start,
              sizeof(h->sudden_death_start))!=0||
       seed_ptr!=&h->seed||melee_web_gameplay_generation()||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Sudden Death continuation is stale or no longer owned by this host");
    out->start=continuation->payload.sudden_death_start;
    /* gm_Scene_SuddenDeath_OnEnter performs this write before fn_8016E730. */
    out->start.rules.x6=1;
    unsigned count=0;
    for(unsigned slot=0;slot<MELEE_WEB_MENU_MAX_PLAYERS;++slot){
        const PlayerInitData* source=&out->start.players[slot];
        if(source->slot_type==Gm_PKind_NA)continue;
        const unsigned port=source->slot?source->slot-1u:slot;
        const MeleeWebFighterContent* content=melee_web_fighter_content(source->ckind);
        if(port!=slot||!content||source->stocks<1||source->stocks>5||
           source->color>=content->costumes||source->sub_color>4)
            return fail(e,n,"Sudden Death source player identity is unsupported");
        out->players[slot].controller=port;
        out->players[slot].stocks=source->stocks;
        out->players[slot].costume=source->color;
        out->players[slot].sub_color=source->sub_color;
        ++count;
    }
    if(count<MELEE_WEB_MENU_MIN_PLAYERS||count>MELEE_WEB_MENU_MAX_PLAYERS||
       out->start.players[4].slot_type!=Gm_PKind_NA||
       out->start.players[5].slot_type!=Gm_PKind_NA||
       out->start.rules.x0_3<1||out->start.rules.x0_3>6)
        return fail(e,n,"Sudden Death payload has an unsupported source roster or HUD layout");
    out->player_count=count;
    out->hud_layout=out->start.rules.x0_3;
    out->random_seed=h->seed;
    out->unlocked_characters=h->selected_characters;
    out->unlocked_stages=h->selected_stages;
    out->save_profile_present=1;
    out->sudden_death=1;
    return ok(e,n);
}
int melee_web_menu_host_sudden_death_selection(
    const MeleeWebMenuHost* h,const MeleeWebMenuMatchContinuation* continuation,
    MeleeWebMenuMatchSelection* out,char* e,size_t n){
    return sudden_death_selection(h,continuation,out,e,n);
}
int melee_web_menu_host_sudden_death_match_claim(
    MeleeWebMenuHost* h,const MeleeWebMenuMatchContinuation* continuation,
    MeleeWebMenuMatchSelection* out,char* e,size_t n){
    if(!h||h!=owner)
        return fail(e,n,"Sudden Death match claim requires the live owning host");
    if(h->sudden_death_claimed||h->sudden_death_claim_consumed)
        return fail(e,n,"Sudden Death continuation already has a live match owner");
    if(!sudden_death_selection(h,continuation,out,e,n))return 0;
    h->sudden_death_claimed=1;
    h->sudden_death_claim_consumed=1;
    return ok(e,n);
}
int melee_web_menu_host_sudden_death_match_release(
    MeleeWebMenuHost* h,uint64_t owner_id,char* e,size_t n){
    if(!h||h!=owner||!h->sudden_death_active||!h->sudden_death_claimed||
       !owner_id||h->sudden_death_owner_id!=owner_id||
       h->sudden_death_scene_active||
       seed_ptr!=&h->seed||melee_web_gameplay_generation())
        return fail(e,n,"Sudden Death match must close before releasing its host claim");
    h->sudden_death_claimed=0;
    return ok(e,n);
}
int melee_web_menu_host_sudden_death_scene_begin(
    MeleeWebMenuHost* h,uint64_t owner_id,char* e,size_t n){
    if(!h||h!=owner||!h->sudden_death_active||!h->sudden_death_claimed||
       !h->sudden_death_claim_consumed||!owner_id||
       h->sudden_death_owner_id!=owner_id||h->sudden_death_scene_active||
       h->entered||h->audio||seed_ptr!=&h->seed||melee_web_gameplay_generation()||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Sudden Death scene requires its claimed live continuation before world construction");
    GameSceneInfo* current=melee_web_current_scene_info();
    if(current!=&h->source_scene_info||current->scene_kind!=GS_SSS||
       memcmp(&gmVsMelee_StartData,&h->sudden_death_start,
              sizeof(h->sudden_death_start))!=0)
        return fail(e,n,"Sudden Death scene requires the current original SSS scene and its exact source payload");
    h->sudden_death_saved_scene_info=current;
    gmVsMelee_StartData.rules.x6=true;
    h->sudden_death_scene_info.scene_kind=GS_SUDDEN_DEATH;
    h->sudden_death_scene_info.enter_data=&gmVsMelee_StartData;
    h->sudden_death_scene_info.exit_data=&gmVsMelee_SuddenDeathExitInfo;
    gm_801A4B88(&h->sudden_death_scene_info);
    h->sudden_death_scene_active=1;
    return ok(e,n);
}
int melee_web_menu_host_sudden_death_scene_end(
    MeleeWebMenuHost* h,uint64_t owner_id,char* e,size_t n){
    if(!h||h!=owner||!h->sudden_death_active||!h->sudden_death_claimed||
       !owner_id||h->sudden_death_owner_id!=owner_id||
       !h->sudden_death_scene_active||
       melee_web_current_scene_info()!=&h->sudden_death_scene_info||
       melee_web_gameplay_generation()||seed_ptr!=&h->seed||
       (h->sudden_death_saved_scene_info!=&h->source_scene_info&&
        h->sudden_death_saved_scene_info!=&h->source_state.info))
        return fail(e,n,"Sudden Death scene can restore only after its owned world closes");
    gm_801A4B88(h->sudden_death_saved_scene_info);
    h->sudden_death_saved_scene_info=NULL;
    h->sudden_death_scene_active=0;
    memset(&h->sudden_death_scene_info,0,sizeof(h->sudden_death_scene_info));
    return ok(e,n);
}
static int commit_results_route(MeleeWebMenuHost* h,char* e,size_t n){
    if(!melee_web_menu_commit_results(h->session,gmVsMelee_GetVsData(),
        gmVsMelee_GetKOCounts(),e,n))return 0;
    h->selected_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->selected_stages=*gmMainLib_8015EDA4();
    h->selected_rules=*gmMainLib_GetGameRules();
    h->selected_preferences=*gmMainLib_8015CC58();
    h->results_committed=1;
    return ok(e,n);
}
static int results_handoff_owned(MeleeWebMenuHost* h,const char* phase,char* e,size_t n){
    char profile_error[256];
    if(!melee_web_save_profile_owner_live(h->profile,profile_error,sizeof(profile_error))){
        if(e&&n)snprintf(e,n,"Results %s: %s",phase,profile_error);
        return 0;
    }
    return melee_web_results_context_check_handoff(phase,e,n);
}
int melee_web_menu_host_results_exit(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->results_active||h->sudden_death_active||h->results_exited)
        return fail(e,n,"Results mode exit requires its live source world");
    if(!results_handoff_owned(h,"mode OnExit entry",e,n))return 0;
    gm_Mode_Vs_States[4].on_exit(&gm_Mode_Vs_States[4]);
    /* The callback is consumed even if its postcondition fails. Never replay
     * its save writes, allocations or RNG on a rejected handoff. */
    h->results_exited=1;
    if(!results_handoff_owned(h,"mode OnExit",e,n))return 0;
    const int next=melee_web_vs_mode_next_state();
    if(next==gmVsMode_State_Prize)return ok(e,n);
    if(next!=gmVsMode_State_Css){
        if(e&&n)snprintf(e,n,"Original Results requested unsupported state %d",next);
        return 0;
    }
    if(!commit_results_route(h,e,n))return 0;
    return results_handoff_owned(h,"route commit",e,n);
}
int melee_web_menu_host_results_end(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||!h->results_active||h->sudden_death_active||!h->results_exited||h->prize_active||
       melee_web_gameplay_generation()||seed_ptr!=&h->seed)
        return fail(e,n,"Results must tear down before restoring its route owner");
    if(!h->results_committed){
        if(melee_web_vs_mode_next_state()!=gmVsMode_State_Prize)
            return fail(e,n,"Results has no supported continuation");
        MeleeWebPadState* next_input=melee_web_pad_state_decode(input,MELEE_WEB_PAD_STATE_BYTES,e,n);
        if(!next_input)return 0;
        melee_web_pad_state_free(h->input);h->input=next_input;h->seed=seed;
        return ok(e,n);
    }
    restore_results_route(h);
    return melee_web_menu_host_match_finished(h,seed,input,e,n);
}
int melee_web_menu_host_results_destination(const MeleeWebMenuHost* h){
    if(!h||h!=owner)return -1;
    return h->results_active&&h->results_exited?melee_web_vs_mode_next_state():gmVsMode_State_Css;
}
int melee_web_menu_host_prize_enter(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->results_active||!h->results_exited||h->results_committed||
       h->prize_active||!melee_web_gameplay_generation()||
       melee_web_vs_mode_next_state()!=gmVsMode_State_Prize)
        return fail(e,n,"Prize mode entry requires the retained Results route and a prepared world");
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Prize))abort();
    gm_Mode_Vs_States[7].on_enter(&gm_Mode_Vs_States[7]);
    h->prize_active=1;
    return ok(e,n);
}
int melee_web_menu_host_prize_exit(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->prize_active||h->results_committed||
       !melee_web_prize_context_exit_ready())
        return fail(e,n,"Prize mode exit requires its completed live source scene");
    gm_Mode_Vs_States[7].on_exit(&gm_Mode_Vs_States[7]);
    const int next=melee_web_vs_mode_next_state();
    if(next!=gmVsMode_State_Css){
        if(e&&n)snprintf(e,n,"Original Prize requested unsupported state %d",next);
        return 0;
    }
    return commit_results_route(h,e,n);
}
int melee_web_menu_host_prize_end(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||!h->prize_active||!h->results_committed||
       melee_web_gameplay_generation()||seed_ptr!=&h->seed)
        return fail(e,n,"Prize must tear down before restoring its route owner");
    restore_results_route(h);
    return melee_web_menu_host_match_finished(h,seed,input,e,n);
}
int melee_web_menu_host_destroy(MeleeWebMenuHost* h,char* e,size_t n){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    final_pending_css_draw_invalidate(h);
#endif
    if(!h||h!=owner||h->entered||h->source_scene!=MELEE_WEB_HOST_SCENE_NONE||
       h->transition||h->audio||h->opening_active||h->opening_match_suspended||
       h->sudden_death_claimed||h->sudden_death_scene_active||
       (h->sudden_death_active&&melee_web_gameplay_generation())||
       seed_ptr!=&h->seed)
        return fail(e,n,"Close native menu scene and restore RNG before destroying host");
    host_sss_continuation_invalidate(h);
    const int owns_scene_info =
        melee_web_current_scene_info() == &h->source_scene_info ||
        melee_web_current_scene_info() == &h->source_state.info;
    if (h->aborted_source_scene && !owns_scene_info) {
        return fail(e, n,
                    "Aborted source scene lost its checked GameSceneInfo ownership");
    }
    if(h->results_active)restore_results_route(h);
    if(h->vs_mode_owned){
        if(!melee_web_vs_mode_end())return fail(e,n,"Original VS mode lease did not release at host destroy");
        h->vs_mode_owned=0;
    }
    if(melee_web_menu_parent_route_pending(h->session) &&
       !melee_web_menu_cancel_parent_route(h->session,e,n))return 0;
    if(!melee_web_menu_session_destroy(h->session,e,n))return 0;
    if(!melee_web_save_profile_owner_deactivate(h->profile,e,n)||
       !melee_web_save_profile_owner_destroy(h->profile,e,n))return 0;
    if (owns_scene_info)
        gm_801A4B88(h->saved_scene_info);
    seed_ptr=h->saved_seed;owner=NULL;melee_web_pad_state_free(h->input);free(h);return ok(e,n);
}
