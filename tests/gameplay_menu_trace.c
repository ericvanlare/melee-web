#include "gameplay_menu.h"
#include "gameplay_content.h"

#include <melee/ft/forward.h>
#include <melee/pl/forward.h>

#include <stdio.h>
#include <string.h>

/* These stubs exercise the lifecycle contract only. They deliberately do not
 * claim that the source CSS/SSS assets or their HSD services execute here. */
void gm_InitVsMode(VsModeData* vs)
{
    memset(vs, 0, sizeof(*vs));
    vs->start.rules.x0_3 = 4;
    vs->start.rules.match_kind = MatchKind_Time;
    vs->start.rules.xB = 2;
    vs->start.rules.x20 = UINT64_MAX;
    vs->start.rules.x30 = 1.0f;
    vs->start.rules.game_speed = 1.0f;
    for (int i = 0; i < GM_MAX_PLAYERS; ++i) {
        vs->start.players[i].ckind = CHKIND_NONE;
        vs->start.players[i].slot_type = Gm_PKind_NA;
        vs->start.players[i].x5 = -1;
        vs->start.players[i].handicap = 9;
        vs->start.players[i].nametag = 120;
        vs->start.players[i].xC_b1 = true;
        vs->start.players[i].cpu_kind = 4;
        vs->start.players[i].attack_ratio = 1.0f;
        vs->start.players[i].defense_ratio = 1.0f;
        vs->start.players[i].model_scale = 1.0f;
    }
}

int melee_web_vs_prepare_start_source(StartMeleeData* start,
                                      const VsModeData* menu)
{
    *start = menu->start;
    start->rules.match_kind = MatchKind_Stock;
    start->rules.is_stock = true;
    start->rules.is_vs = true;
    start->rules.xB = -1;
    for (int i = 0; i < GM_MAX_PLAYERS; ++i) {
        start->players[i].stocks = 4;
        start->players[i].rumble_enabled = i < 2;
    }
    return 1;
}

static CSSData* active_css;
static SSSData* active_sss;
static int transition_request;
static int invalid_exit;
static int source_order_enabled;
static int source_order;

void mnCharSel_Scene_OnEnter(void* data) { active_css = data; }
void mnCharSel_Scene_OnFrame(void) { if (source_order_enabled) source_order = 1; }
void mnCharSel_Scene_OnExit(void* data)
{
    (void) data;
    active_css->pending_scene_change = 1;
    if (invalid_exit == 1) active_css->vs.start.players[0].ckind = CKIND_CAPTAIN;
    active_css = NULL;
}
void mnStageSel_Scene_OnEnter(void* data) { active_sss = data; }
void mnStageSel_Scene_OnFrame(void) { if (source_order_enabled) source_order = 1; }
void mnStageSel_Scene_OnExit(void* data)
{
    (void) data;
    active_sss->start_game = true;
    if (invalid_exit == 2) active_sss->vs.start.rules.stkind = 25;
    active_sss = NULL;
}

static int check(void* user, MeleeWebMenuScene scene, char* error, size_t n)
{
    (void) user;
    (void) scene;
    if (error != NULL && n != 0) error[0] = '\0';
    return 1;
}

static int scheduler(void* user, char* error, size_t n)
{
    (void) user;
    if (source_order_enabled && source_order == 1) source_order = 2;
    if (error != NULL && n != 0) error[0] = '\0';
    return 1;
}

static int transition(void* user, MeleeWebMenuScene scene, int* requested,
                     char* error, size_t n)
{
    (void) user;
    (void) scene;
    if (source_order_enabled && transition_request != 0 && source_order == 2)
        source_order = 3;
    *requested = transition_request;
    transition_request = 0;
    if (error != NULL && n != 0) error[0] = '\0';
    return 1;
}

static void setup(CSSData* css)
{
    memset(css, 0, sizeof(*css));
    gm_InitVsMode(&css->vs);
    css->match_type = VS_MELEE;
    css->vs.start.rules.stkind = MELEE_WEB_MENU_FD_ST_KIND;
    for (int i = 0; i < 2; ++i) {
        css->vs.start.players[i].slot_type = Gm_PKind_Human;
        css->vs.start.players[i].ckind = CKIND_MARIO;
    }
}

int main(void)
{
    CSSData css;
    SSSData sss;
    setup(&css);
    if (!melee_web_menu_character_available(CKIND_MARIO) ||
        !melee_web_menu_character_available(CKIND_FOX) ||
        melee_web_menu_character_available(CKIND_CAPTAIN) ||
        !melee_web_menu_stage_available(MELEE_WEB_MENU_FD_ST_KIND) ||
        !melee_web_menu_stage_available(St_Kind_Story) ||
        melee_web_menu_stage_available(25) ||
        !melee_web_menu_css_selection_valid(&css))
        return 1;
    css.vs.start.players[1].ckind = CKIND_FOX;
    css.vs.start.players[1].color = 3;
    if (!melee_web_menu_css_selection_valid(&css)) return 2;
    css.vs.start.players[1].color = 4;
    if (melee_web_menu_css_selection_valid(&css)) return 52;
    css.vs.start.players[1].ckind = CKIND_CAPTAIN;
    css.vs.start.players[1].color = 0;
    if (melee_web_menu_css_selection_valid(&css)) return 53;
    css.vs.start.players[1].ckind = CKIND_FALCO;
    css.vs.start.players[1].color = 3;
    css.vs.start.rules.stkind = St_Kind_Battle;
    if (!melee_web_menu_css_selection_valid(&css) ||
        !melee_web_menu_character_available(CKIND_FALCO) ||
        !melee_web_menu_stage_available(St_Kind_Battle) ||
        melee_web_fighter_content(CKIND_FALCO)->fighter_kind != FTKIND_FALCO ||
        melee_web_stage_content(St_Kind_Battle)->ground_kind != Gr_Kind_Battle)
        return 48;
    css.vs.start.players[1].color = 4;
    if (melee_web_menu_css_selection_valid(&css)) return 49;
    setup(&css);
    memset(&sss, 0, sizeof(sss));
    sss.force_stage_id = -1;
    sss.vs = css.vs;
    sss.vs.start.rules.stkind = MELEE_WEB_MENU_FD_ST_KIND;
    if (!melee_web_menu_sss_selection_valid(&sss)) return 3;
    sss.vs.start.rules.stkind = 25;
    if (melee_web_menu_sss_selection_valid(&sss)) return 4;
    sss.vs.start.rules.stkind = St_Kind_Battle;
    sss.vs.start.players[0].ckind = CKIND_FALCO;
    if (!melee_web_menu_sss_selection_valid(&sss)) return 50;
    sss.vs.start.rules.stkind = St_Kind_Story;
    sss.vs.start.players[0].ckind = CKIND_FOX;
    if (!melee_web_menu_sss_selection_valid(&sss)) return 54;

    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 5;
        css = *(CSSData*) melee_web_menu_css(session);
        css.vs.start.players[0].ckind = CKIND_CAPTAIN;
        *(CSSData*) melee_web_menu_css(session) = css;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_SELECTION_REJECTED)
            return 6;
        if (melee_web_menu_leave_css(session, error, sizeof(error))) return 7;
        if (!melee_web_menu_abort(session, error, sizeof(error)) ||
            !melee_web_menu_session_destroy(session, error, sizeof(error)))
            return 8;
    }

    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 9;
        /* gm_801A4D34 observes OnFrame, runs the scheduler, then publishes the
         * transition request. Keep this order visible to the host boundary so
         * a preparation gate cannot move the request before source teardown. */
        source_order_enabled = 1;
        source_order = 0;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED)
            return 10;
        source_order_enabled = 0;
        if (source_order != 3) return 51;
        if (!melee_web_menu_leave_css(session, error, sizeof(error)) ||
            melee_web_menu_phase(session) != MELEE_WEB_MENU_SSS_READY ||
            !melee_web_menu_session_destroy(session, error, sizeof(error)))
            return 11;
    }

    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        const CSSData* result;
        const VsModeData* ready;
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 12;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_css(session, error, sizeof(error)) ||
            !melee_web_menu_enter_sss(session, error, sizeof(error)))
            return 13;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_sss(session, error, sizeof(error)) ||
            melee_web_menu_phase(session) != MELEE_WEB_MENU_READY)
            return 14;
        result = melee_web_menu_css(session);
        ready = melee_web_menu_ready_vs(session);
        if (result == NULL || ready == NULL ||
            result->vs.start.players[0].ckind != CKIND_MARIO ||
            result->vs.start.players[0].stocks != 0 ||
            result->vs.start.players[1].stocks != 0 ||
            ready->start.rules.match_kind != MatchKind_Stock ||
            !ready->start.rules.is_stock || !ready->start.rules.is_vs ||
            ready->start.rules.xB != -1 ||
            ready->start.rules.x20 != UINT64_MAX ||
            ready->start.players[0].stocks != 4 ||
            ready->start.players[1].stocks != 4 ||
            ready->start.players[2].stocks != 4)
            return 15;
        if (!melee_web_menu_return_to_css(session, error, sizeof(error)))
            return 16;
        if (melee_web_menu_leave_css(session, error, sizeof(error))) return 17;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_css(session, error, sizeof(error)) ||
            !melee_web_menu_session_destroy(session, error, sizeof(error)))
            return 18;
    }
    /* Source-private state can be published only by OnExit. The host must
     * never expose a READY payload that was invalidated by that callback. */
    for (invalid_exit = 1; invalid_exit <= 2; ++invalid_exit) {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        if (!session || !melee_web_menu_enter_css(session, error, sizeof(error)))
            return 19;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
            MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED) return 20;
        if (invalid_exit == 1) {
            if (melee_web_menu_leave_css(session, error, sizeof(error))) return 21;
        } else {
            if (!melee_web_menu_leave_css(session, error, sizeof(error)) ||
                !melee_web_menu_enter_sss(session, error, sizeof(error))) return 22;
            transition_request = 1;
            if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
                melee_web_menu_leave_sss(session, error, sizeof(error))) return 23;
        }
        if (melee_web_menu_phase(session) != MELEE_WEB_MENU_CLOSED ||
            melee_web_menu_ready_vs(session) != NULL ||
            !melee_web_menu_session_destroy(session, error, sizeof(error))) return 24;
    }
    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, NULL};
        char error[128];
        if (melee_web_menu_session_create(&runtime, NULL, error, sizeof(error)))
            return 25;
    }
    puts("native menu lifecycle contract trace: passed");
    return 0;
}
