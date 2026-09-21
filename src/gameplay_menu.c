#include "gameplay_menu.h"
#include "gameplay_content.h"
#include "gameplay_match_rules.h"
#include "gameplay_player_selection.h"

#include <melee/gm/gm_1601.h>
#include <melee/mn/mncharsel.h>
#include <melee/mn/mnmain.h>
#include <melee/mn/mnstagesel.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct MeleeWebMenuSession {
    MeleeWebMenuRuntime runtime;
    CSSData css;
    SSSData sss;
    VsModeData match_vs;
    u8 css_ko_counts[GM_MAX_PLAYERS];
    uint64_t ticks;
    MeleeWebMenuPhase phase;
    int css_open;
    int sss_open;
    int selection_rejected;
    int transition_failed;
    int transition_requested;
};

static MeleeWebMenuSession* owner;

/* The original game-mode layer re-initializes the HSD gobj library at every
 * scene change (gm_801A4BD4), which destroys the previous scene's gobj entity
 * lists. The retained one-world port shares those lists with its own retained
 * owners, so instead the host snapshots each list's head when the scene's
 * original enter runs and destroys exactly the gobjs created since that
 * snapshot when the scene leaves, with the source's own per-gobj teardown
 * primitive (HSD_GObjPLink_80390228, as mn_8022F0F0 walks it for menu
 * back-outs). Without this, menu gobjs such as the CSS confirm-tag processor
 * fn_80262F44 keep running their processes inside later scenes; its confirm
 * rumble then faults against the next world's rumble state and stops the
 * player. */
#define MELEE_WEB_MENU_TEARDOWN_LISTS 15
static HSD_GObj* menu_gobj_heads[MELEE_WEB_MENU_TEARDOWN_LISTS];
static void melee_web_menu_gobj_snapshot(void)
{
    if(!HSD_GObj_Entities)return;
    for(unsigned i=1;i<MELEE_WEB_MENU_TEARDOWN_LISTS;++i)
        menu_gobj_heads[i]=((HSD_GObj**)HSD_GObj_Entities)[i];
}
static void melee_web_menu_gobj_teardown(void)
{
    if(!HSD_GObj_Entities)return;
    for(unsigned i=1;i<MELEE_WEB_MENU_TEARDOWN_LISTS;++i){
        HSD_GObj* stop=menu_gobj_heads[i];
        HSD_GObj* curr=((HSD_GObj**)HSD_GObj_Entities)[i];
        while(curr&&curr!=stop){
            HSD_GObj* next=curr->next;
            HSD_GObjPLink_80390228(curr);
            curr=next;
        }
    }
}

extern int melee_web_vs_prepare_start_source(StartMeleeData*,
                                              const VsModeData*);

static int fail(char* error, size_t error_size, const char* message)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", message);
    }
    return MELEE_WEB_MENU_RESULT_ERROR;
}

static int failf(char* error, size_t error_size, const char* format,
                 int value)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, format, value);
    }
    return MELEE_WEB_MENU_RESULT_ERROR;
}

static int ok(char* error, size_t error_size)
{
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
    return 1;
}

static int session_live(const MeleeWebMenuSession* session, char* error,
                        size_t error_size)
{
    if (session == NULL || session != owner)
    {
        return fail(error, error_size, "Menu session is not the live owner");
    }
    return 1;
}

static int check_runtime(MeleeWebMenuSession* session, MeleeWebMenuScene scene,
                         char* error, size_t error_size)
{
    if (session->runtime.check == NULL) {
        return fail(error, error_size,
                    "Native menu runtime service check is required");
    }
    if (!session->runtime.check(session->runtime.user, scene, error,
                                error_size))
    {
        if (error == NULL || error_size == 0) {
            return fail(error, error_size,
                        "Native menu runtime service check failed");
        }
        return 0;
    }
    return 1;
}

static int run_scheduler(MeleeWebMenuSession* session, char* error,
                         size_t error_size)
{
    if (session->runtime.scheduler == NULL) {
        return fail(error, error_size,
                    "Native menu scheduler callback is required");
    }
    if (!session->runtime.scheduler(session->runtime.user, error, error_size)) {
        if (error == NULL || error_size == 0) {
            return fail(error, error_size,
                        "Native menu scheduler callback failed");
        }
        return 0;
    }
    return 1;
}

static int observe_transition(MeleeWebMenuSession* session,
                              MeleeWebMenuScene scene, int* requested,
                              char* error, size_t error_size)
{
    if (session->runtime.transition == NULL) {
        return fail(error, error_size,
                    "Native menu transition observer is required");
    }
    *requested = 0;
    if (!session->runtime.transition(session->runtime.user, scene, requested,
                                     error, error_size)) {
        if (error == NULL || error_size == 0) {
            return fail(error, error_size,
                        "Native menu transition observer failed");
        }
        return 0;
    }
    if (*requested < 0 || *requested > 2) {
        return fail(error, error_size, "Invalid original menu transition kind");
    }
    return 1;
}

int melee_web_menu_character_available(int ckind)
{
    /* Availability is intentionally narrower than the retail unlock table.
     * All CSS entries may be unlocked by the host; only implemented source
     * owners are admitted to this development boundary. */
    return melee_web_fighter_content(ckind) != NULL;
}

int melee_web_menu_stage_available(int stkind)
{
    return melee_web_stage_content(stkind) != NULL;
}

int melee_web_menu_active_player_count(const StartMeleeData* start)
{
    int count = 0;

    if (start == NULL) {
        return 0;
    }
    while (count < MELEE_WEB_MENU_MAX_PLAYERS &&
           start->players[count].slot_type != Gm_PKind_NA) {
        ++count;
    }
    if (count < MELEE_WEB_MENU_MIN_PLAYERS) {
        return 0;
    }
    for (int i = count; i < GM_MAX_PLAYERS; ++i) {
        if (start->players[i].slot_type != Gm_PKind_NA) {
            return 0;
        }
    }
    return count;
}

int melee_web_menu_css_selection_valid(const CSSData* css)
{
    int i;
    int count;

    if (css == NULL || css->match_type != VS_MELEE ||
        css->vs.start.rules.match_kind != MatchKind_Time ||
        css->vs.start.rules.is_stock || css->vs.start.rules.is_vs ||
        css->vs.start.rules.is_teams ||
        css->vs.start.rules.timer_enabled || css->vs.start.rules.xB != 2 ||
        css->vs.start.rules.x20 != UINT64_MAX ||
        !melee_web_menu_stage_available(css->vs.start.rules.stkind))
    {
        return 0;
    }
    count = melee_web_menu_active_player_count(&css->vs.start);
    if (count == 0)
    {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const PlayerInitData* player=&css->vs.start.players[i];
        const MeleeWebFighterContent* content=melee_web_fighter_content(player->ckind);
        if(!melee_web_player_selection_supported(player) ||
           !melee_web_menu_character_available(player->ckind) ||
           player->stocks != 0 ||
           !content || (player->slot?player->slot-1:i)!=i ||
           player->color>=content->costumes || player->sub_color>4) return 0;
    }
    for (i = count; i < GM_MAX_PLAYERS; i++) {
        if (css->vs.start.players[i].slot_type != Gm_PKind_NA) {
            return 0;
        }
    }
    return 1;
}

static int match_selection_valid(const StartMeleeData* start)
{
    int i;
    int count;

    if (start == NULL || start->rules.match_kind != MatchKind_Stock ||
        !start->rules.is_stock || !start->rules.is_vs ||
        start->rules.is_teams || !melee_web_match_timer_supported(&start->rules) ||
        start->rules.xB != -1 || start->rules.x20 != UINT64_MAX ||
        !melee_web_menu_stage_available(start->rules.stkind))
    {
        return 0;
    }
    count = melee_web_menu_active_player_count(start);
    if (count == 0) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        const PlayerInitData* player = &start->players[i];
        const MeleeWebFighterContent* content =
            melee_web_fighter_content(player->ckind);
        if (!melee_web_player_selection_supported(player) || player->stocks < 1 ||
            player->stocks > 5 ||
            player->rumble_enabled != (player->slot_type == Gm_PKind_Human) ||
            content == NULL || !melee_web_menu_character_available(player->ckind) ||
            (player->slot ? player->slot - 1 : i) != i ||
            player->color >= content->costumes || player->sub_color > 4)
        {
            return 0;
        }
    }
    for (; i < GM_MAX_PLAYERS; ++i) {
        if (start->players[i].slot_type != Gm_PKind_NA ||
            start->players[i].rumble_enabled)
        {
            return 0;
        }
    }
    return 1;
}

/* Picking up a token and unplugging a controller are legitimate in-progress
 * CSS states. Only a completed selection may enter SSS or a match. */
static int css_progress_valid(const CSSData* css)
{
    CSSData view = *css;
    for (unsigned i = 0; i < MELEE_WEB_MENU_MAX_PLAYERS; i++) {
        PlayerInitData* p = &view.vs.start.players[i];
        /* Preserve the existing unplugged-controller allowance for the two
         * initial doors.  Inactive P3/P4 slots must remain dormant. */
        if (i < MELEE_WEB_MENU_MIN_PLAYERS && p->slot_type == Gm_PKind_NA) {
            p->slot_type = Gm_PKind_Human;
        }
        /* A newly joined door has a live slot before the original CSS has
         * assigned its character icon.  Keep the transient source state
         * valid for progress checks without waking dormant doors. */
        if (p->slot_type != Gm_PKind_NA && p->ckind == CHKIND_NONE) {
            p->ckind = CKIND_MARIO;
        }
    }
    return melee_web_menu_css_selection_valid(&view);
}

static int vs_selection_valid(const VsModeData* vs)
{
    CSSData view;
    if (vs == NULL) {
        return 0;
    }
    memset(&view, 0, sizeof(view));
    view.match_type = VS_MELEE;
    view.vs = *vs;
    return melee_web_menu_css_selection_valid(&view);
}

int melee_web_menu_sss_selection_valid(const SSSData* sss)
{
    if (sss == NULL || sss->force_stage_id != -1 ||
        !melee_web_menu_stage_available(sss->vs.start.rules.stkind))
    {
        return 0;
    }
    return vs_selection_valid(&sss->vs);
}

MeleeWebMenuSession* melee_web_menu_session_create(
    const MeleeWebMenuRuntime* runtime, const MeleeWebMenuConfig* config,
    char* error, size_t error_size)
{
    MeleeWebMenuConfig defaults = {4, 0, 0, 0, 0, 0};
    const MeleeWebMenuConfig* selected = config != NULL ? config : &defaults;
    MeleeWebMenuSession* session;
    unsigned player_count = selected->player_count != 0
                                ? selected->player_count
                                : MELEE_WEB_MENU_MIN_PLAYERS;
    int i;

    if (owner != NULL) {
        fail(error, error_size, "A native menu session is already active");
        return NULL;
    }
    if (runtime == NULL || runtime->check == NULL || runtime->scheduler == NULL ||
        runtime->transition == NULL) {
        fail(error, error_size,
             "Menu creation requires native service, scheduler and transition callbacks");
        return NULL;
    }
    if (selected->stocks < 1 || selected->stocks > 5) {
        fail(error, error_size,
             "Native menu supports source stock counts 1 through 5");
        return NULL;
    }
    if (player_count < MELEE_WEB_MENU_MIN_PLAYERS ||
        player_count > MELEE_WEB_MENU_MAX_PLAYERS) {
        fail(error, error_size,
             "Native menu supports two through four active players");
        return NULL;
    }

    session = calloc(1, sizeof(*session));
    if (session == NULL) {
        fail(error, error_size, "Unable to allocate native menu session");
        return NULL;
    }
    session->runtime = *runtime;
    session->phase = MELEE_WEB_MENU_CREATED;
    gm_InitVsMode(&session->css.vs);
    session->css.unk_0x0 = 0;
    session->css.match_type = VS_MELEE;
    session->css.pending_scene_change = 0;
    session->css.ko_counts = session->css_ko_counts;
    /* Retail keeps gm_InitVsMode's raw menu payload through SSS OnExit. The
     * stock/no-item policy is applied later by the original VS-entry path. */
    session->css.vs.start.rules.stkind = MELEE_WEB_MENU_FD_ST_KIND;
    for (i = 0; i < (int)player_count; i++) {
        PlayerInitData* player = &session->css.vs.start.players[i];
        player->ckind = CKIND_MARIO;
        player->slot_type = i < MELEE_WEB_MENU_MIN_PLAYERS
                                ? Gm_PKind_Human
                                : Gm_PKind_Cpu;
        player->cpu_kind = CpuKind_4;
        player->cpu_level = i < MELEE_WEB_MENU_MIN_PLAYERS ? 0 : 1;
        player->color = i == 0 ? selected->player0_color
                      : i == 1 ? selected->player1_color
                      : i == 2 ? selected->player2_color
                               : selected->player3_color;
        /* Original slot 0 means use this player index; nonzero is port + 1. */
        player->slot = 0;
        player->rumble_enabled = player->slot_type == Gm_PKind_Human;
        player->nametag = 0x78;
    }
    session->sss.unk_stage = 0;
    session->sss.x1 = 0;
    session->sss.no_lras = 0;
    session->sss.force_stage_id = -1;
    session->sss.start_game = false;
    session->sss.vs = session->css.vs;
    owner = session;
    ok(error, error_size);
    return session;
}

int melee_web_menu_session_destroy(MeleeWebMenuSession* session, char* error,
                                   size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->css_open || session->sss_open) {
        return fail(error, error_size,
                    "Leave or abort the live native menu scene before destroy");
    }
    session->phase = MELEE_WEB_MENU_CLOSED;
    owner = NULL;
    free(session);
    return ok(error, error_size);
}

static int enter_css(MeleeWebMenuSession* session, int after_match,
                     char* error, size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->phase != MELEE_WEB_MENU_CREATED &&
        session->phase != MELEE_WEB_MENU_CSS_READY &&
        (!after_match || session->phase != MELEE_WEB_MENU_READY))
    {
        return fail(error, error_size,
                    "CSS can only be entered from a new or stage-cancelled session");
    }
    if (!check_runtime(session, MELEE_WEB_MENU_SCENE_CSS, error, error_size)) {
        return 0;
    }
    if (!observe_transition(session, MELEE_WEB_MENU_SCENE_CSS,
                            &session->transition_requested, error,
                            error_size)) {
        return 0;
    }
    if (session->transition_requested != 0) {
        return fail(error, error_size,
                    "CSS transition request was pending before enter");
    }
    session->css.pending_scene_change = 0;
    session->css.match_type = VS_MELEE;
    session->selection_rejected = 0;
    session->transition_failed = 0;
    session->transition_requested = 0;
    melee_web_menu_gobj_snapshot();
    mnCharSel_Scene_OnEnter(&session->css);
    session->css_open = 1;
    session->phase = MELEE_WEB_MENU_CSS;
    return ok(error, error_size);
}

int melee_web_menu_enter_css(MeleeWebMenuSession* session, char* error,
                             size_t error_size)
{
    return enter_css(session, 0, error, error_size);
}

int melee_web_menu_return_to_css(MeleeWebMenuSession* session, char* error,
                                 size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->phase != MELEE_WEB_MENU_READY) {
        return fail(error, error_size,
                    "Returning to CSS requires a torn-down ready match");
    }
    return enter_css(session, 1, error, error_size);
}

int melee_web_menu_enter_sss(MeleeWebMenuSession* session, char* error,
                             size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->phase != MELEE_WEB_MENU_SSS_READY ||
        !melee_web_menu_css_selection_valid(&session->css))
    {
        return fail(error, error_size,
                    "SSS requires a valid committed Mario CSS selection");
    }
    if (!check_runtime(session, MELEE_WEB_MENU_SCENE_SSS, error, error_size)) {
        return 0;
    }
    if (!observe_transition(session, MELEE_WEB_MENU_SCENE_SSS,
                            &session->transition_requested, error,
                            error_size)) {
        return 0;
    }
    if (session->transition_requested != 0) {
        return fail(error, error_size,
                    "SSS transition request was pending before enter");
    }
    session->sss.vs = session->css.vs;
    session->sss.vs.start.rules.stkind = MELEE_WEB_MENU_FD_ST_KIND;
    session->sss.force_stage_id = -1;
    session->sss.start_game = false;
    session->selection_rejected = 0;
    session->transition_failed = 0;
    session->transition_requested = 0;
    melee_web_menu_gobj_snapshot();
    mnStageSel_Scene_OnEnter(&session->sss);
    session->sss_open = 1;
    session->phase = MELEE_WEB_MENU_SSS;
    return ok(error, error_size);
}

int melee_web_menu_tick(MeleeWebMenuSession* session, char* error,
                        size_t error_size)
{
    int rejected = 0;
    MeleeWebMenuScene scene;

    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->selection_rejected || session->transition_failed ||
        session->transition_requested != 0) {
        return fail(error, error_size,
                    "Menu scene is faulted; abort and re-enter the scene");
    }
    if (session->phase == MELEE_WEB_MENU_CSS && session->css_open) {
        scene = MELEE_WEB_MENU_SCENE_CSS;
    } else if (session->phase == MELEE_WEB_MENU_SSS && session->sss_open) {
        scene = MELEE_WEB_MENU_SCENE_SSS;
    } else {
        return fail(error, error_size,
                    "Menu tick requires an entered CSS or SSS scene");
    }
    if (!check_runtime(session, scene, error, error_size)) {
        return 0;
    }

    /* gm_801A4D34 invokes the scene callback before HSD_GObj_80390CFC. */
    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        mnCharSel_Scene_OnFrame();
    } else {
        mnStageSel_Scene_OnFrame();
    }
    if (!run_scheduler(session, error, error_size)) {
        return 0;
    }
    session->ticks++;

    /* OnFrame may request a game-mode change (for example, SSS back).  The
     * observer reads and clears that request through the host's checked game
     * mode adapter.  Leave is explicit so the source OnExit remains the only
     * owner of archive release. */
    if (!observe_transition(session, scene, &session->transition_requested,
                            error, error_size)) {
        session->transition_failed = 1;
        return 0;
    }

    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        if (!css_progress_valid(&session->css)) {
            rejected = 1;
        }
    } else if (!melee_web_menu_sss_selection_valid(&session->sss)) {
        rejected = 1;
    }
    if (rejected) {
        session->selection_rejected = 1;
        if (error != NULL && error_size != 0) {
            snprintf(error, error_size,
                     "Native menu selection was rejected; abort and re-enter");
        }
        return MELEE_WEB_MENU_RESULT_SELECTION_REJECTED;
    }
    if (session->transition_requested != 0) {
        ok(error, error_size);
        return MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED;
    }
    ok(error, error_size);
    return MELEE_WEB_MENU_RESULT_TICKED;
}

int melee_web_menu_leave_css(MeleeWebMenuSession* session, char* error,
                             size_t error_size)
{
    u8 pending;

    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->phase != MELEE_WEB_MENU_CSS || !session->css_open) {
        return fail(error, error_size, "CSS is not the live menu scene");
    }
    if (session->selection_rejected || session->transition_failed) {
        return fail(error, error_size,
                    "Cannot commit a faulted CSS scene; abort first");
    }
    if (session->transition_requested == 0) {
        return fail(error, error_size,
                    "CSS has no completed original transition request");
    }
    if (!melee_web_menu_css_selection_valid(&session->css)) {
        return fail(error, error_size,
                    "Cannot commit an unavailable character selection");
    }
    mnCharSel_Scene_OnExit(NULL);
    melee_web_menu_gobj_teardown();
    session->css_open = 0;
    session->transition_requested = 0;
    pending = session->css.pending_scene_change;
    if (pending == CSSPendingSceneChange_2) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return ok(error, error_size);
    }
    if (pending != 1) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return failf(error, error_size,
                     "CSS exited with unsupported pending scene %d", pending);
    }
    /* OnExit may publish source-private selection state. Validate that
     * payload before making the next scene available to the host. */
    if (!melee_web_menu_css_selection_valid(&session->css)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return fail(error, error_size, "CSS published an unavailable selection");
    }
    session->phase = MELEE_WEB_MENU_SSS_READY;
    return ok(error, error_size);
}

int melee_web_menu_leave_sss(MeleeWebMenuSession* session, char* error,
                             size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->phase != MELEE_WEB_MENU_SSS || !session->sss_open) {
        return fail(error, error_size, "SSS is not the live menu scene");
    }
    if (session->selection_rejected || session->transition_failed) {
        return fail(error, error_size,
                    "Cannot commit a faulted SSS scene; abort first");
    }
    if (session->transition_requested == 0) {
        return fail(error, error_size,
                    "SSS has no completed original transition request");
    }
    if (!melee_web_menu_sss_selection_valid(&session->sss)) {
        return fail(error, error_size,
                    "Cannot commit an unavailable stage selection");
    }
    mnStageSel_Scene_OnExit(NULL);
    melee_web_menu_gobj_teardown();
    session->sss_open = 0;
    session->transition_requested = 0;
    if (!melee_web_menu_sss_selection_valid(&session->sss)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return fail(error, error_size, "SSS published an unavailable selection");
    }
    if (session->sss.start_game) {
        session->css.vs = session->sss.vs;
        session->match_vs = session->sss.vs;
        if (!melee_web_vs_prepare_start_source(&session->match_vs.start,
                                                &session->sss.vs) ||
            !match_selection_valid(&session->match_vs.start))
        {
            session->phase = MELEE_WEB_MENU_CLOSED;
            return fail(error, error_size,
                        "Original VS entry produced an unsupported match payload");
        }
        session->phase = MELEE_WEB_MENU_READY;
    } else {
        session->css.vs = session->sss.vs;
        session->phase = MELEE_WEB_MENU_CSS_READY;
    }
    return ok(error, error_size);
}

int melee_web_menu_abort(MeleeWebMenuSession* session, char* error,
                         size_t error_size)
{
    if (!session_live(session, error, error_size)) {
        return 0;
    }
    if (session->css_open) {
        mnCharSel_Scene_OnExit(NULL);
        session->css_open = 0;
    }
    if (session->sss_open) {
        mnStageSel_Scene_OnExit(NULL);
        session->sss_open = 0;
    }
    melee_web_menu_gobj_teardown();
    session->phase = MELEE_WEB_MENU_CLOSED;
    return ok(error, error_size);
}

MeleeWebMenuPhase melee_web_menu_phase(const MeleeWebMenuSession* session)
{
    return session != NULL && session == owner ? session->phase
                                                : MELEE_WEB_MENU_CLOSED;
}

const CSSData* melee_web_menu_css(const MeleeWebMenuSession* session)
{
    if (session == NULL || session != owner) {
        return NULL;
    }
    return &session->css;
}

const SSSData* melee_web_menu_sss(const MeleeWebMenuSession* session)
{
    if (session == NULL || session != owner) {
        return NULL;
    }
    return &session->sss;
}

const VsModeData* melee_web_menu_ready_vs(const MeleeWebMenuSession* session)
{
    if (session == NULL || session != owner ||
        session->phase != MELEE_WEB_MENU_READY)
    {
        return NULL;
    }
    return &session->match_vs;
}
