#include "gameplay_menu.h"
#include "gameplay_content.h"
#include "gameplay_player_selection.h"
#include "native_menu_fighter_input.h"

#include <melee/ft/forward.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/objalloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These stubs exercise the lifecycle contract only. They deliberately do not
 * claim that the source CSS/SSS assets or their HSD services execute here. */

/* Keep the fixture's configured link range explicit so the menu boundary is
 * exercised against the same p_link_max source contract as the runtime. */
#define MENU_TRACE_P_LINK_MAX 63
#define MENU_TRACE_GOBJ_CAPACITY 128
static HSD_GObj* menu_trace_gobj_heads[MENU_TRACE_P_LINK_MAX + 1];
static HSD_GObj* menu_trace_gobj_low[MENU_TRACE_P_LINK_MAX + 1];
static HSD_GObj menu_trace_gobj_storage[MENU_TRACE_GOBJ_CAPACITY];
static size_t menu_trace_gobj_count;
HSD_GObjList* HSD_GObj_Entities = (HSD_GObjList*)menu_trace_gobj_heads;
HSD_GObj** plinklow_gobjs = menu_trace_gobj_low;
HSD_GObjLibInitDataType HSD_GObjLibInitData = {
    MENU_TRACE_P_LINK_MAX, MENU_TRACE_P_LINK_MAX, 0, NULL, NULL
};
HSD_ObjAllocData gobj_alloc_data;
HSD_GObj* HSD_GObj_804D781C;
__typeof__(HSD_GObj_804CE3E4) HSD_GObj_804CE3E4;

void __assert(char* file, u32 line, char* condition)
{
    fprintf(stderr, "GObj assertion failed at %s:%u: %s\n", file, line,
            condition);
    abort();
}

/* Use the source allocator entry point with a fixture-owned pool. The linked
 * gobjplink.c supplies the real gobj_first_lower_prio/GObj_PReorder path, so
 * equal-priority objects exercise the actual source ordering. */
void* HSD_ObjAlloc(HSD_ObjAllocData* data)
{
    if (data != &gobj_alloc_data ||
        menu_trace_gobj_count == MENU_TRACE_GOBJ_CAPACITY) {
        return NULL;
    }
    ++data->used;
    return &menu_trace_gobj_storage[menu_trace_gobj_count++];
}

void HSD_ObjFree(HSD_ObjAllocData* data, void* object)
{
    if (data == &gobj_alloc_data && object != NULL) {
        --data->used;
    }
}

void GObj_RemoveUserData(HSD_GObj* gobj) { (void)gobj; }
void HSD_GObjObject_80390B0C(HSD_GObj* gobj)
{
    gobj->obj_kind = HSD_GOBJ_OBJ_NONE;
    gobj->hsd_obj = NULL;
}
void HSD_GObjProc_8038FED4(HSD_GObj* gobj) { (void)gobj; }
void HSD_GObjGXLink_8039084C(HSD_GObj* gobj) { (void)gobj; }

static HSD_GObj* menu_trace_gobj_create(u16 classifier, u8 p_link,
                                        u8 priority)
{
    return GObj_Create(classifier, p_link, priority);
}

static int menu_trace_link_only(u8 p_link, const HSD_GObj* expected)
{
    return menu_trace_gobj_heads[p_link] == expected &&
           (expected == NULL || expected->next == NULL);
}

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
        start->players[i].rumble_enabled =
            i < 4 && start->players[i].slot_type == Gm_PKind_Human;
    }
    return 1;
}

static CSSData* active_css;
static SSSData* active_sss;
static int transition_request;
static int invalid_exit;
static int source_order_enabled;
static int source_order;

void mnCharSel_Scene_OnEnter(void* data)
{
    active_css = data;
    /* The source CSS enter creates its fog on p_link 2 at priority 0. */
    menu_trace_gobj_create(HSD_GOBJ_CLASS_UI, 2, 0);
}
void mnCharSel_Scene_OnFrame(void) { if (source_order_enabled) source_order = 1; }
void mnCharSel_Scene_OnExit(void* data)
{
    (void) data;
    active_css->pending_scene_change = 1;
    if (invalid_exit == 1) active_css->vs.start.players[0].ckind = CKIND_PLAYABLE_COUNT;
    active_css = NULL;
}
void mnStageSel_Scene_OnEnter(void* data)
{
    active_sss = data;
    /* The source SSS enter creates its fog on p_link 15 at priority 0. */
    menu_trace_gobj_create(HSD_GOBJ_CLASS_UI, 15, 0);
}
void mnStageSel_Scene_OnFrame(void) { if (source_order_enabled) source_order = 1; }
void mnStageSel_Scene_OnExit(void* data)
{
    (void) data;
    active_sss->start_game = true;
    if (invalid_exit == 2) active_sss->vs.start.rules.stkind = St_Kind_Dummy;
    if (invalid_exit == 3) active_sss->vs.start.rules.stkind = 25;
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
    /* CSS has 25 icons because Zelda and Sheik share an icon, while its
     * CharacterKind namespace has 26 playable entries, ending in Ganondorf. */
    MeleeWebFighterInputObservation observed={0};
    observed.held_door=-1;observed.target_left=-1;observed.target_right=1;
    observed.target_bottom=-1;observed.target_top=1;
    for(int kind=0;kind<CKIND_PLAYABLE_COUNT;kind++)
        if(!melee_web_fighter_input_observe_valid(&observed,kind))return 100;
    if(melee_web_fighter_input_observe_valid(&observed,-1)||
       melee_web_fighter_input_observe_valid(&observed,CKIND_PLAYABLE_COUNT))return 101;
    CSSData css;
    SSSData sss;
    setup(&css);
    HSD_GObj* retained_p2 = menu_trace_gobj_create(0x101, 2, 0);
    HSD_GObj* retained_p15 = menu_trace_gobj_create(0x102, 15, 0);
    HSD_GObj* retained_p1 = menu_trace_gobj_create(0x104, 1, 1);
    if (retained_p2 == NULL || retained_p15 == NULL ||
        retained_p1 == NULL || !menu_trace_link_only(2, retained_p2) ||
        !menu_trace_link_only(15, retained_p15) ||
        !menu_trace_link_only(1, retained_p1))
        return 104;
    if (!melee_web_menu_character_available(CKIND_MARIO) ||
        !melee_web_menu_character_available(CKIND_FOX) ||
        !melee_web_menu_character_available(CKIND_CAPTAIN) ||
        !melee_web_menu_character_available(CKIND_DONKEY) ||
        melee_web_menu_character_available(CKIND_PLAYABLE_COUNT) ||
        !melee_web_menu_stage_available(MELEE_WEB_MENU_FD_ST_KIND) ||
        !melee_web_menu_stage_available(St_Kind_Story) ||
        !melee_web_menu_stage_available(St_Kind_Shrine) ||
        melee_web_menu_stage_available(25) ||
        !melee_web_menu_css_selection_valid(&css))
        return 1;
    css.vs.start.players[1].ckind = CKIND_FOX;
    css.vs.start.players[1].color = 3;
    if (!melee_web_menu_css_selection_valid(&css)) return 2;
    /* The bounded multi-player shape keeps slots/ports contiguous and lets
     * the original source own CPU type/level initialization. */
    css.vs.start.players[2].slot_type = Gm_PKind_Cpu;
    css.vs.start.players[2].cpu_kind = 4;
    css.vs.start.players[2].cpu_level = 5;
    css.vs.start.players[2].ckind = CKIND_MARIO;
    css.vs.start.players[2].slot = 0;
    css.vs.start.players[3].slot_type = Gm_PKind_Cpu;
    css.vs.start.players[3].cpu_kind = 4;
    css.vs.start.players[3].cpu_level = 9;
    css.vs.start.players[3].ckind = CKIND_FALCO;
    css.vs.start.players[3].slot = 0;
    if (melee_web_menu_active_player_count(&css.vs.start) != 4 ||
        !melee_web_menu_css_selection_valid(&css)) return 67;
    css.vs.start.players[4].slot_type = Gm_PKind_Cpu;
    if (melee_web_menu_active_player_count(&css.vs.start) != 0) return 68;
    css.vs.start.players[4].slot_type = Gm_PKind_NA;
    css.vs.start.players[3].slot_type = Gm_PKind_NA;
    if (melee_web_menu_active_player_count(&css.vs.start) != 3 ||
        !melee_web_menu_css_selection_valid(&css)) return 69;
    css.vs.start.players[2].slot_type = Gm_PKind_NA;
    css.vs.start.players[1].color = 4;
    if (melee_web_menu_css_selection_valid(&css)) return 52;
    css.vs.start.players[1].ckind = CKIND_DONKEY;
    css.vs.start.players[1].color = 4;
    if (!melee_web_menu_css_selection_valid(&css) ||
        melee_web_fighter_content(CKIND_DONKEY)->fighter_kind != FTKIND_DONKEY)
        return 102;
    css.vs.start.players[1].color = 5;
    if (melee_web_menu_css_selection_valid(&css)) return 53;
    css.vs.start.players[1].ckind = CKIND_PLAYABLE_COUNT;
    css.vs.start.players[1].color = 0;
    if (melee_web_menu_css_selection_valid(&css)) return 103;
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
    /* The same ordinary-VS CPU payload must survive both menu boundaries.
     * Reject event/training behaviors and out-of-range difficulty explicitly. */
    css.vs.start.players[1].slot_type = Gm_PKind_Cpu;
    for (int level = 1; level <= 9; ++level) {
        css.vs.start.players[1].cpu_level = level;
        if (!melee_web_menu_css_selection_valid(&css)) return 55;
    }
    css.vs.start.players[1].cpu_level = 0;
    if (melee_web_menu_css_selection_valid(&css)) return 56;
    css.vs.start.players[1].cpu_level = 10;
    if (melee_web_menu_css_selection_valid(&css)) return 57;
    css.vs.start.players[1].cpu_level = 9;
    css.vs.start.players[1].cpu_kind = 0;
    if (melee_web_menu_css_selection_valid(&css)) return 58;
    css.vs.start.players[1].cpu_kind = 4;
    css.vs.start.players[1].rumble_enabled = true;
    /* CSS has not run gm_LoadRumbleEnabled yet. */
    if (!melee_web_menu_css_selection_valid(&css)) return 64;
    if (melee_web_match_player_supported(&css.vs.start.players[1])) return 65;
    css.vs.start.players[1].rumble_enabled = false;
    if (!melee_web_match_player_supported(&css.vs.start.players[1])) return 66;
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
        uint8_t css_context[0x148] = {0};
        uint8_t ko_counts[GM_MAX_PLAYERS] = {1, 2, 3, 4, 5, 6};
        css_context[0] = 0;
        css_context[1] = 1;
        css_context[2] = VS_MELEE;
        css_context[4] = 0x80;
        css_context[5] = 0x54;
        css_context[6] = 0;
        css_context[7] = 0;
        css_context[0x10] = 2 << 2;
        css_context[0x10 + 0x0B] = 2;
        css_context[0x10 + 0x0E] = 0;
        /* Retail first CSS carries its unset stage cache until SSS commits. */
        css_context[0x10 + 0x0F] = St_Kind_Dummy;
        memset(css_context + 0x10 + 0x20, 0xff, 8);
        for (int i = 0; i < GM_MAX_PLAYERS; ++i) {
            const size_t base = 0x70 + (size_t) i * 0x24;
            css_context[base] = i == 1 ? CKIND_FOX : CKIND_MARIO;
            css_context[base + 1] = i < 2 ? Gm_PKind_Human : Gm_PKind_NA;
            css_context[base + 4] = 0;
            css_context[base + 0x0C] = i < 2 ? 0x80 : 0;
            css_context[base + 0x18] = 0x3f;
            css_context[base + 0x1C] = 0x80;
            css_context[base + 0x20] = 0x00;
        }
        if (!session || !melee_web_menu_apply_reference_css_context(
                session, css_context, ko_counts, error, sizeof(error)) ||
            !melee_web_menu_enter_css(session, error, sizeof(error)))
            return 59;
        if (active_css->unk_0x0 != 1 || active_css->vs.start.players[1].ckind != CKIND_FOX ||
            active_css->ko_counts[0] != 1 || active_css->ko_counts[5] != 6)
            return 108;
        transition_request = 0;
        if (active_css->vs.start.rules.stkind != St_Kind_Dummy ||
            melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED ||
            active_css->vs.start.rules.stkind != St_Kind_Dummy)
            return 109;
        /* Original v15 first-CSS OnEnter publishes four inactive doors with
         * CKIND_PLAYABLE_COUNT, cpu_level=1 and the uncommitted stage=0.
         * Reduce that observed boundary without mutating the source state
         * during validation. The callback is stubbed in this contract test;
         * the actual linked browser route remains a separate check. */
        for (int i = 0; i < 4; ++i) {
            active_css->vs.start.players[i].slot_type = Gm_PKind_NA;
            active_css->vs.start.players[i].ckind = CKIND_PLAYABLE_COUNT;
            active_css->vs.start.players[i].cpu_level = 1;
        }
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED) return 111;
        for (int i = 0; i < 4; ++i) {
            if (active_css->vs.start.players[i].slot_type != Gm_PKind_NA ||
                active_css->vs.start.players[i].ckind != CKIND_PLAYABLE_COUNT)
                return 112;
        }
        active_css->vs.start.players[0].slot_type = Gm_PKind_Human;
        active_css->vs.start.players[0].ckind = CKIND_MARIO;
        active_css->vs.start.players[1].ckind = CKIND_FOX;
        active_css->vs.start.players[1].slot_type = Gm_PKind_Cpu;
        active_css->vs.start.players[1].cpu_kind = 4;
        active_css->vs.start.players[1].cpu_level = 9;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_css(session, error, sizeof(error)) ||
            !menu_trace_link_only(2, retained_p2) ||
            !melee_web_menu_enter_sss(session, error, sizeof(error))) return 60;
        transition_request = 0;
        if (active_sss->vs.start.rules.stkind != St_Kind_Dummy ||
            melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED ||
            active_sss->vs.start.rules.stkind != St_Kind_Dummy)
            return 110;
        active_sss->vs.start.rules.stkind = MELEE_WEB_MENU_FD_ST_KIND;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_sss(session, error, sizeof(error)) ||
            !menu_trace_link_only(15, retained_p15)) return 61;
        const VsModeData* ready = melee_web_menu_ready_vs(session);
        if (!ready || ready->start.players[1].slot_type != Gm_PKind_Cpu ||
            ready->start.players[1].cpu_kind != 4 ||
            ready->start.players[1].cpu_level != 9 ||
            ready->start.players[1].stocks != 4 ||
            ready->start.players[1].rumble_enabled) return 62;
        if (!melee_web_menu_return_to_css(session, error, sizeof(error)) ||
            active_css->vs.start.players[1].slot_type != Gm_PKind_Cpu ||
            active_css->vs.start.players[1].cpu_level != 9 ||
            !melee_web_menu_abort(session, error, sizeof(error)) ||
            !melee_web_menu_session_destroy(session, error, sizeof(error))) return 63;
    }
    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        HSD_GObj* post_scene_owner;
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 105;
        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            !melee_web_menu_leave_css(session, error, sizeof(error)))
            return 106;
        /* An owner created after leave must survive an abort with no open
         * scene. A stale global snapshot would remove this priority-0 object
         * before reaching its old saved head. */
        post_scene_owner = menu_trace_gobj_create(0x103, 1, 0);
        if (post_scene_owner == NULL ||
            menu_trace_gobj_heads[1] != post_scene_owner ||
            post_scene_owner->next != retained_p1 ||
            !melee_web_menu_abort(session, error, sizeof(error)) ||
            menu_trace_gobj_heads[1] != post_scene_owner ||
            post_scene_owner->next != retained_p1 ||
            !melee_web_menu_session_destroy(session, error, sizeof(error)))
            return 107;
    }
    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 70;

        /* Original CSS briefly exposes a joined P3/P4 door before assigning
         * its character icon.  Progress checks may accept that state, but a
         * transition must still wait for both icons to be selected. */
        transition_request = 0;
        active_css->vs.start.players[2].slot_type = Gm_PKind_Cpu;
        active_css->vs.start.players[2].cpu_kind = CpuKind_4;
        active_css->vs.start.players[2].cpu_level = 5;
        active_css->vs.start.players[2].ckind = CHKIND_NONE;
        active_css->vs.start.players[2].slot = 0;
        active_css->vs.start.players[3].ckind = CHKIND_NONE;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED ||
            active_css->vs.start.players[3].slot_type != Gm_PKind_NA ||
            active_css->vs.start.players[3].ckind != CHKIND_NONE)
            return 71;

        active_css->vs.start.players[3].slot_type = Gm_PKind_Cpu;
        active_css->vs.start.players[3].cpu_kind = CpuKind_4;
        active_css->vs.start.players[3].cpu_level = 9;
        active_css->vs.start.players[3].ckind = CHKIND_NONE;
        active_css->vs.start.players[3].slot = 0;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED)
            return 72;

        transition_request = 1;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED ||
            melee_web_menu_leave_css(session, error, sizeof(error)) ||
            melee_web_menu_phase(session) != MELEE_WEB_MENU_CSS)
            return 73;

        active_css->vs.start.players[2].ckind = CKIND_MARIO;
        active_css->vs.start.players[3].ckind = CKIND_FALCO;
        if (!melee_web_menu_leave_css(session, error, sizeof(error)) ||
            melee_web_menu_phase(session) != MELEE_WEB_MENU_SSS_READY ||
            !melee_web_menu_abort(session, error, sizeof(error)) ||
            !melee_web_menu_session_destroy(session, error, sizeof(error)))
            return 74;
    }
    {
        MeleeWebMenuRuntime runtime = {NULL, check, scheduler, transition};
        char error[128];
        MeleeWebMenuSession* session =
            melee_web_menu_session_create(&runtime, NULL, error, sizeof(error));
        if (session == NULL || !melee_web_menu_enter_css(session, error,
                                                          sizeof(error)))
            return 5;
        css = *(CSSData*) melee_web_menu_css(session);
        css.vs.start.players[0].ckind = CKIND_PLAYABLE_COUNT;
        *(CSSData*) melee_web_menu_css(session) = css;
        transition_request = 0;
        if (melee_web_menu_tick(session, error, sizeof(error)) !=
                MELEE_WEB_MENU_RESULT_TICKED ||
            melee_web_menu_css_selection_valid(melee_web_menu_css(session)) ||
            melee_web_menu_css(session)->vs.start.players[0].ckind !=
                CKIND_PLAYABLE_COUNT) return 113;
        /* The same value is not an admitted CPU fighter. */
        active_css->vs.start.players[0].slot_type = Gm_PKind_Cpu;
        active_css->vs.start.players[0].cpu_kind = CpuKind_4;
        active_css->vs.start.players[0].cpu_level = 9;
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
    const int invalid_exit_cases = 3;
    for (invalid_exit = 1; invalid_exit <= invalid_exit_cases; ++invalid_exit) {
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
