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
#include <math.h>
#include <stdint.h>
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
    HSD_GObj** gobj_snapshot;
    size_t gobj_snapshot_count;
    HSD_GObjList* gobj_snapshot_entities;
    u8 gobj_snapshot_p_link_max;
    int gobj_snapshot_active;
};

_Static_assert(sizeof(StartMeleeRules) == 0x60,
               "Source StartMeleeRules layout changed; update CSS decoder");
_Static_assert(sizeof(PlayerInitData) == 0x24,
               "Source PlayerInitData layout changed; update CSS decoder");
_Static_assert(sizeof(StartMeleeData) == 0x138,
               "Source StartMeleeData layout changed; update CSS decoder");
_Static_assert(sizeof(CSSData) == 0x148 && offsetof(CSSData, vs) == 0x08,
               "Source CSSData layout changed; update CSS decoder");

static MeleeWebMenuSession* owner;

static int fail(char* error, size_t error_size, const char* message);
static int ok(char* error, size_t error_size);

/* The original game-mode layer re-initializes the HSD gobj library at every
 * scene change (gm_801A4BD4), which destroys the previous scene's gobj entity
 * lists. The retained one-world port shares those lists with its own retained
 * owners, so instead the host snapshots every object present when the
 * scene's original enter runs and destroys exactly the objects absent from
 * that snapshot when the scene leaves. This uses the source's own per-gobj
 * teardown primitive (HSD_GObjPLink_80390228, as mn_8022F0F0 walks it for
 * menu back-outs). A list-head sentinel is not sufficient because the source
 * inserts GObjs by priority and can place a new object behind a retained
 * object. Without this ownership boundary, menu gobjs such as the CSS
 * confirm-tag processor fn_80262F44 keep running their processes inside later
 * scenes; its confirm rumble then faults against the next world's rumble
 * state and stops the player. */
static void melee_web_menu_gobj_snapshot_clear(MeleeWebMenuSession* session)
{
    free(session->gobj_snapshot);
    session->gobj_snapshot = NULL;
    session->gobj_snapshot_count = 0;
    session->gobj_snapshot_entities = NULL;
    session->gobj_snapshot_p_link_max = 0;
    session->gobj_snapshot_active = 0;
}

static int melee_web_menu_gobj_snapshot(MeleeWebMenuSession* session,
                                        char* error, size_t error_size)
{
    HSD_GObj** links;
    HSD_GObj* object;
    size_t link_count;
    size_t object_count = 0;
    size_t index = 0;

    if (session->gobj_snapshot_active) {
        return fail(error, error_size,
                    "Native menu GObj snapshot is already active");
    }
    if (HSD_GObj_Entities == NULL) {
        return fail(error, error_size,
                    "Native menu GObj entity lists are required");
    }
    link_count = (size_t) HSD_GObjLibInitData.p_link_max + 1;
    links = (HSD_GObj**) HSD_GObj_Entities;
    for (size_t link = 0; link < link_count; ++link) {
        for (object = links[link]; object != NULL; object = object->next) {
            ++object_count;
        }
    }
    if (object_count != 0) {
        if (object_count > SIZE_MAX / sizeof(*session->gobj_snapshot)) {
            return fail(error, error_size,
                        "Native menu GObj snapshot is too large");
        }
        session->gobj_snapshot = calloc(object_count,
                                        sizeof(*session->gobj_snapshot));
        if (session->gobj_snapshot == NULL) {
            return fail(error, error_size,
                        "Cannot allocate native menu GObj snapshot");
        }
    }
    for (size_t link = 0; link < link_count; ++link) {
        for (object = links[link]; object != NULL; object = object->next) {
            session->gobj_snapshot[index++] = object;
        }
    }
    session->gobj_snapshot_count = object_count;
    session->gobj_snapshot_entities = HSD_GObj_Entities;
    session->gobj_snapshot_p_link_max = HSD_GObjLibInitData.p_link_max;
    session->gobj_snapshot_active = 1;
    return ok(error, error_size);
}

static int melee_web_menu_gobj_was_present(
    const MeleeWebMenuSession* session, const HSD_GObj* object)
{
    for (size_t i = 0; i < session->gobj_snapshot_count; ++i) {
        if (session->gobj_snapshot[i] == object) {
            return 1;
        }
    }
    return 0;
}

static int melee_web_menu_gobj_teardown(MeleeWebMenuSession* session,
                                        char* error, size_t error_size)
{
    HSD_GObj** links;

    if (!session->gobj_snapshot_active) {
        return ok(error, error_size);
    }
    if (HSD_GObj_Entities == NULL ||
        HSD_GObj_Entities != session->gobj_snapshot_entities ||
        HSD_GObjLibInitData.p_link_max != session->gobj_snapshot_p_link_max) {
        melee_web_menu_gobj_snapshot_clear(session);
        return fail(error, error_size,
                    "Native menu GObj entity lists changed during scene");
    }
    links = (HSD_GObj**) HSD_GObj_Entities;
    for (size_t link = 0;
         link <= (size_t) HSD_GObjLibInitData.p_link_max; ++link) {
        HSD_GObj* object = links[link];
        while (object != NULL) {
            HSD_GObj* next = object->next;
            if (!melee_web_menu_gobj_was_present(session, object)) {
                HSD_GObjPLink_80390228(object);
            }
            object = next;
        }
    }
    melee_web_menu_gobj_snapshot_clear(session);
    return ok(error, error_size);
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

/* CSSData is observed in the source's PowerPC byte order.  The browser
 * target has the same generated layouts but a little-endian data bus, so
 * copying the 0x148-byte object would reverse every scalar and retain guest
 * callback addresses.  Decode the authored fields one by one and install
 * owner pointers below. */
static u16 reference_be16(const uint8_t* bytes)
{
    return (u16) bytes[0] << 8 | bytes[1];
}

static u32 reference_be32(const uint8_t* bytes)
{
    return (u32) bytes[0] << 24 | (u32) bytes[1] << 16 |
           (u32) bytes[2] << 8 | bytes[3];
}

static u64 reference_be64(const uint8_t* bytes)
{
    return (u64) reference_be32(bytes) << 32 | reference_be32(bytes + 4);
}

static float reference_be_float(const uint8_t* bytes)
{
    const u32 bits = reference_be32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void decode_reference_rules(StartMeleeRules* target,
                                   const uint8_t source[0x60])
{
    memset(target, 0, sizeof(*target));
    target->match_kind = source[0] >> 5;
    target->x0_3 = (source[0] >> 2) & 7;
    target->timer_enabled = (source[0] >> 1) & 1;
    target->timer_counts_up = source[0] & 1;
    target->x1_0 = (source[1] >> 7) & 1;
    target->x1_1 = (source[1] >> 6) & 1;
    target->x1_2 = (source[1] >> 5) & 1;
    target->x1_3 = (source[1] >> 4) & 1;
    target->x1_4 = (source[1] >> 3) & 1;
    target->x1_5 = (source[1] >> 2) & 1;
    target->timer_shows_hours = (source[1] >> 1) & 1;
    target->friendly_fire = source[1] & 1;
    target->is_stock = (source[2] >> 7) & 1;
    target->x2_1 = (source[2] >> 6) & 1;
    target->x2_2 = (source[2] >> 5) & 1;
    target->single_button = (source[2] >> 4) & 1;
    target->disable_pausing = (source[2] >> 3) & 1;
    target->x2_5 = (source[2] >> 2) & 1;
    target->x2_6 = (source[2] >> 1) & 1;
    target->x2_7 = source[2] & 1;
    target->x3_0 = (source[3] >> 7) & 1;
    target->x3_1 = (source[3] >> 6) & 1;
    target->x3_2 = (source[3] >> 5) & 1;
    target->x3_3 = (source[3] >> 4) & 1;
    target->x3_4 = (source[3] >> 3) & 1;
    target->x3_5 = (source[3] >> 2) & 1;
    target->x3_6 = (source[3] >> 1) & 1;
    target->x3_7 = source[3] & 1;
    target->x4_0 = (source[4] >> 7) & 1;
    target->is_vs = (source[4] >> 6) & 1;
    target->x4_2 = (source[4] >> 5) & 1;
    target->x4_3 = (source[4] >> 4) & 1;
    target->x4_4 = (source[4] >> 3) & 1;
    target->x4_5 = (source[4] >> 2) & 1;
    target->x4_6 = (source[4] >> 1) & 1;
    target->x4_7 = source[4] & 1;
    target->x5_0 = (source[5] >> 7) & 1;
    target->x5_1 = (source[5] >> 6) & 1;
    target->x5_2 = (source[5] >> 5) & 1;
    target->x5_3 = (source[5] >> 4) & 1;
    target->x5_4 = (source[5] >> 3) & 1;
    target->x5_5 = (source[5] >> 2) & 1;
    target->x5_6 = (source[5] >> 1) & 1;
    target->x5_7 = source[5] & 1;
    target->x6 = source[6];
    target->x7 = source[7];
    target->is_teams = source[8];
    target->x9 = source[9];
    target->xA = source[10];
    target->xB = (s8) source[11];
    target->xC = (s8) source[12];
    target->xD = source[13];
    target->stkind = reference_be16(source + 14);
    target->time_limit = reference_be32(source + 16);
    target->x14 = source[20];
    target->x18 = reference_be32(source + 24);
    target->x1C_pad[0] = reference_be32(source + 28);
    target->x20 = reference_be64(source + 32);
    target->x28 = (int) reference_be32(source + 40);
    target->x2C = reference_be_float(source + 44);
    target->x30 = reference_be_float(source + 48);
    target->game_speed = reference_be_float(source + 52);
    /* Source callback/data pointers occupy [0x38, 0x60).  The caller rejects
     * them before this decoder runs; all corresponding owner fields remain
     * zero, which is the only portable translation for the CSS entry object. */
}

static void decode_reference_player(PlayerInitData* target,
                                    const uint8_t source[0x24])
{
    memset(target, 0, sizeof(*target));
    target->ckind = (s8) source[0];
    target->slot_type = source[1];
    target->stocks = (s8) source[2];
    target->color = source[3];
    target->slot = source[4];
    target->x5 = (s8) source[5];
    target->spawn_dir = (s8) source[6];
    target->sub_color = source[7];
    target->handicap = (s8) source[8];
    target->team = source[9];
    target->nametag = source[10];
    target->xB = source[11];
    target->rumble_enabled = (source[12] >> 7) & 1;
    target->xC_b1 = (source[12] >> 6) & 1;
    target->xC_b2 = (source[12] >> 5) & 1;
    target->xC_b3 = (source[12] >> 4) & 1;
    target->vs_invisible = (source[12] >> 3) & 1;
    target->xC_b5 = (source[12] >> 2) & 1;
    target->xC_b6 = (source[12] >> 1) & 1;
    target->xC_b7 = source[12] & 1;
    target->xD_b0 = (source[13] >> 7) & 1;
    target->xD_b1 = (source[13] >> 6) & 1;
    target->xD_b2 = (source[13] >> 5) & 1;
    target->xD_b3 = (source[13] >> 4) & 1;
    target->xD_b4 = (source[13] >> 3) & 1;
    target->xD_b5 = (source[13] >> 2) & 1;
    target->xD_b6 = (source[13] >> 1) & 1;
    target->xD_b7 = source[13] & 1;
    target->cpu_kind = source[14];
    target->cpu_level = source[15];
    target->x10 = reference_be16(source + 16);
    target->x12 = reference_be16(source + 18);
    target->hp = reference_be16(source + 20);
    target->attack_ratio = reference_be_float(source + 24);
    target->defense_ratio = reference_be_float(source + 28);
    target->model_scale = reference_be_float(source + 32);
}

/* The capture is taken at the CSS boundary, before the source CSS enter has
 * assigned its door icons.  Retail uses CHKIND_NONE for the pre-enter
 * initialized doors and CKIND_PLAYABLE_COUNT for an unassigned door after
 * CSS construction; an unassigned human door can carry the latter while its
 * cursor is being joined.  These are source-owned transient values, not
 * playable fighter selections.  Validate the shape here without imposing
 * the later VS CPU kind/level or stage-commit rules. */
static int reference_initial_css_players_valid(const CSSData* css)
{
    int saw_inactive = 0;

    for (int i = 0; i < GM_MAX_PLAYERS; ++i) {
        const PlayerInitData* player = &css->vs.start.players[i];
        if (player->slot_type == Gm_PKind_NA) {
            if (player->ckind != CHKIND_NONE &&
                player->ckind != CKIND_PLAYABLE_COUNT)
                return 0;
            saw_inactive = 1;
            continue;
        }
        if (saw_inactive ||
            (player->slot_type != Gm_PKind_Human &&
             player->slot_type != Gm_PKind_Cpu))
            return 0;
        if (player->ckind == CKIND_PLAYABLE_COUNT) {
            /* mnCharSel_Scene_OnEnter uses this sentinel for an unassigned
             * human door and converts that door to NA. A CPU door must carry
             * an authored fighter before it is admitted. */
            if (player->slot_type != Gm_PKind_Human)
                return 0;
        } else if (!melee_web_menu_character_available(player->ckind)) {
            return 0;
        }
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

int melee_web_menu_apply_reference_css_context(
    MeleeWebMenuSession* session, const uint8_t source[0x148],
    const uint8_t ko_counts[GM_MAX_PLAYERS], char* error, size_t error_size)
{
    CSSData decoded;

    if (!session_live(session, error, error_size) || source == NULL ||
        ko_counts == NULL || session->phase != MELEE_WEB_MENU_CREATED ||
        session->css_open || session->sss_open)
    {
        return fail(error, error_size,
                    "First-CSS context requires an unentered menu session");
    }
    if (source[2] != VS_MELEE || source[3] != 0)
    {
        return fail(error, error_size,
                    "First-CSS context is not an ordinary initial VS CSS");
    }
    /* StartMeleeRules begins at CSSData+0x10.  The source callbacks, Events,
     * x54 and x58 are all guest addresses in [rules+0x38,rules+0x60); no
     * replay owner can safely retain them across a browser scene boundary. */
    for (size_t i = 0; i < 0x24; ++i) {
        if (source[0x10 + 0x38 + i] != 0)
            return fail(error, error_size,
                        "First-CSS context contains an unsupported source callback or pointer");
    }

    memset(&decoded, 0, sizeof(decoded));
    decoded.unk_0x0 = reference_be16(source);
    decoded.match_type = source[2];
    decoded.pending_scene_change = source[3];
    decoded.vs.loser = (s8) source[8];
    decoded.vs.ordered_stage_index = (s8) source[9];
    decoded.vs.winner = (s8) source[10];
    decoded.vs.unk_0x3 = source[11];
    decoded.vs.unk_0x4 = source[12];
    decoded.vs.unk_0x5 = source[13];
    decoded.vs.unk_0x6 = source[14];
    decoded.vs.unk_0x7 = source[15];
    decode_reference_rules(&decoded.vs.start.rules, source + 0x10);
    if (!isfinite(decoded.vs.start.rules.x2C) ||
        !isfinite(decoded.vs.start.rules.x30) ||
        !isfinite(decoded.vs.start.rules.game_speed))
        return fail(error, error_size,
                    "First-CSS context contains a non-finite source rule float");
    for (size_t i = 0; i < GM_MAX_PLAYERS; ++i) {
        decode_reference_player(&decoded.vs.start.players[i],
                                source + 0x70 + i * 0x24);
        if (!isfinite(decoded.vs.start.players[i].attack_ratio) ||
            !isfinite(decoded.vs.start.players[i].defense_ratio) ||
            !isfinite(decoded.vs.start.players[i].model_scale))
            return fail(error, error_size,
                        "First-CSS context contains a non-finite player float");
    }
    if (!reference_initial_css_players_valid(&decoded))
        return fail(error, error_size,
                    "First-CSS context contains an unsupported source player slot");
    memcpy(session->css_ko_counts, ko_counts, GM_MAX_PLAYERS);
    decoded.ko_counts = session->css_ko_counts;
    decoded.vs.start.rules.on_unpause_override = NULL;
    decoded.vs.start.rules.on_pause_override = NULL;
    decoded.vs.start.rules.check_for_pauser_override = NULL;
    decoded.vs.start.rules.on_match_start = NULL;
    decoded.vs.start.rules.on_frame_start = NULL;
    decoded.vs.start.rules.on_frame_end = NULL;
    decoded.vs.start.rules.on_match_end = NULL;
    decoded.vs.start.rules.x54 = NULL;
    decoded.vs.start.rules.x58 = NULL;
    session->css = decoded;
    session->sss.vs = decoded.vs;
    return ok(error, error_size);
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

static int stage_selection_valid(int stkind, int allow_unselected)
{
    /* Retail's first VS CSS can retain St_Kind_Dummy (0) while the stage
     * selector has not committed a stage yet.  That source cache value is
     * valid menu progress, but it is never a valid match handoff. */
    return (allow_unselected && stkind == St_Kind_Dummy) ||
           melee_web_menu_stage_available(stkind);
}

static int css_selection_valid_internal(const CSSData* css,
                                        int allow_unselected_stage)
{
    int i;
    int count;

    if (css == NULL || css->match_type != VS_MELEE ||
        css->vs.start.rules.match_kind != MatchKind_Time ||
        css->vs.start.rules.is_stock || css->vs.start.rules.is_vs ||
        css->vs.start.rules.is_teams ||
        css->vs.start.rules.timer_enabled || css->vs.start.rules.xB != 2 ||
        css->vs.start.rules.x20 != UINT64_MAX ||
        !stage_selection_valid(css->vs.start.rules.stkind,
                               allow_unselected_stage))
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

int melee_web_menu_css_selection_valid(const CSSData* css)
{
    /* fn_80262F44 uses this guard before accepting Start. CSS has selected
     * fighters, but the following SSS still owns the stage selection. Keep
     * the original unset cache value until that scene commits its stage. */
    return css_selection_valid_internal(css, 1);
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
            /* CSS construction (fn_8026407C) stores CKIND_PLAYABLE_COUNT when its icon
             * search finds no selected fighter. It is an inactive-door
             * sentinel here, not an admitted Master Hand selection. Change
             * only the validation copy before its synthetic human slot. */
            if (p->ckind == CKIND_PLAYABLE_COUNT) p->ckind = CKIND_MARIO;
            p->slot_type = Gm_PKind_Human;
        }
        /* A newly joined door has a live slot before the original CSS has
         * assigned its character icon.  Keep the transient source state
         * valid for progress checks without waking dormant doors. */
        if (p->slot_type != Gm_PKind_NA &&
            (p->ckind == CHKIND_NONE ||
             (p->slot_type == Gm_PKind_Human &&
              p->ckind == CKIND_PLAYABLE_COUNT))) {
            /* CursorThink joins an inactive human door before assigning
             * the held token's icon. Its 26 sentinel remains uncommitted;
             * the strict CSS exit/match validators still reject it. */
            p->ckind = CKIND_MARIO;
        }
    }
    return css_selection_valid_internal(&view, 1);
}

static int vs_selection_valid(const VsModeData* vs, int allow_unselected_stage)
{
    CSSData view;
    if (vs == NULL) {
        return 0;
    }
    memset(&view, 0, sizeof(view));
    view.match_type = VS_MELEE;
    view.vs = *vs;
    return css_selection_valid_internal(&view, allow_unselected_stage);
}

int melee_web_menu_sss_selection_valid(const SSSData* sss)
{
    if (sss == NULL || sss->force_stage_id != -1 ||
        !stage_selection_valid(sss->vs.start.rules.stkind, 1))
    {
        return 0;
    }
    return vs_selection_valid(&sss->vs, 1);
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
    if (session->css_open || session->sss_open ||
        session->gobj_snapshot_active) {
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
    if (!melee_web_menu_gobj_snapshot(session, error, error_size)) {
        return 0;
    }
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
        !css_selection_valid_internal(&session->css, 1))
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
    session->sss.force_stage_id = -1;
    session->sss.start_game = false;
    session->selection_rejected = 0;
    session->transition_failed = 0;
    session->transition_requested = 0;
    if (!melee_web_menu_gobj_snapshot(session, error, error_size)) {
        return 0;
    }
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
            const StartMeleeData* start = scene == MELEE_WEB_MENU_SCENE_CSS
                ? &session->css.vs.start : &session->sss.vs.start;
            snprintf(error, error_size,
                     "Native menu selection was rejected: scene=%d stage=%d "
                     "mode=%d stock=%d vs=%d teams=%d timer=%d xB=%d "
                     "players=%d/%d,%d/%d,%d/%d,%d/%d; abort and re-enter",
                     scene, start->rules.stkind, start->rules.match_kind,
                     start->rules.is_stock, start->rules.is_vs,
                     start->rules.is_teams, start->rules.timer_enabled,
                     start->rules.xB,
                     start->players[0].ckind, start->players[0].slot_type,
                     start->players[1].ckind, start->players[1].slot_type,
                     start->players[2].ckind, start->players[2].slot_type,
                     start->players[3].ckind, start->players[3].slot_type);
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
    if (!css_selection_valid_internal(&session->css, 1)) {
        return fail(error, error_size,
                    "Cannot commit an unavailable character selection");
    }
    mnCharSel_Scene_OnExit(NULL);
    session->css_open = 0;
    session->transition_requested = 0;
    if (!melee_web_menu_gobj_teardown(session, error, error_size)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return 0;
    }
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
    if (!css_selection_valid_internal(&session->css, 1)) {
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
    session->sss_open = 0;
    session->transition_requested = 0;
    if (!melee_web_menu_gobj_teardown(session, error, error_size)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return 0;
    }
    if (!melee_web_menu_sss_selection_valid(&session->sss)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return fail(error, error_size, "SSS published an unavailable selection");
    }
    if (session->sss.start_game) {
        if (!melee_web_menu_stage_available(session->sss.vs.start.rules.stkind)) {
            session->phase = MELEE_WEB_MENU_CLOSED;
            return fail(error, error_size,
                        "Original VS entry committed an unavailable stage");
        }
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
    if (!melee_web_menu_gobj_teardown(session, error, error_size)) {
        session->phase = MELEE_WEB_MENU_CLOSED;
        return 0;
    }
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

int melee_web_menu_commit_results(MeleeWebMenuSession* session,
                                  const VsModeData* vs,
                                  const uint8_t ko_counts[GM_MAX_PLAYERS],
                                  char* error, size_t error_size)
{
    if (!session_live(session, error, error_size) || !vs || !ko_counts ||
        session->css_open || session->sss_open ||
        session->phase != MELEE_WEB_MENU_READY)
        return fail(error, error_size, "Results commit requires closed source menus");
    session->css.vs = *vs;
    session->sss.vs = *vs;
    memcpy(session->css_ko_counts, ko_counts, sizeof(session->css_ko_counts));
    return ok(error, error_size);
}
