#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_match_rules.h"

#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/types.h>
#include <melee/pl/player.h>

#include <stdio.h>
#include <string.h>

static MeleeWebGameplayStats g_stats = {.generation = 1};
static lbl_8046B6A0_t g_match_state;
static StaticPlayer g_players[GM_MAX_PLAYERS];
static unsigned g_prepare_calls;
static unsigned g_init_calls;
static uint64_t g_prepared_item_mask;
static uint64_t g_initialized_item_mask;

MeleeWebGameplayStats melee_web_gameplay_stats(void) { return g_stats; }
lbl_8046B6A0_t* gm_16AE_GetUnkData_0(void) { return &g_match_state; }
HSD_GObj* Player_GetEntity(s32 slot)
{
    (void) slot;
    return NULL;
}
StaticPlayer* Player_GetPtrForSlot(int slot) { return &g_players[slot]; }
void Player_InitOrResetPlayer(s32 slot)
{
    memset(&g_players[slot], 0, sizeof(g_players[slot]));
}
void gm_SetupRulesDefaults(StartMeleeRules* rules)
{
    memset(rules, 0, sizeof(*rules));
    rules->x20 = UINT64_MAX;
    rules->game_speed = 1.0f;
}
void melee_web_match_source_result_reset(void) {}
void melee_web_match_source_refresh_ratio(void) {}
void melee_web_match_source_demo_end(void) {}
int melee_web_match_prepare_source(StartMeleeData* start, int opening_demo)
{
    if (opening_demo) return 0;
    ++g_prepare_calls;
    g_prepared_item_mask = start->rules.x20;
    return 1;
}
int melee_web_match_init_source(StartMeleeData* start)
{
    ++g_init_calls;
    g_initialized_item_mask = start->rules.x20;
    return 1;
}

static void set_supported_vs_payload(StartMeleeData* start, int item_frequency,
                                     uint64_t item_mask, int is_teams,
                                     const int teams[4], int player_count)
{
    memset(start, 0, sizeof(*start));
    start->rules.match_kind = MatchKind_Stock;
    start->rules.is_stock = 1;
    start->rules.is_vs = 1;
    start->rules.is_teams = (u8) is_teams;
    start->rules.xB = (int8_t) item_frequency;
    start->rules.x20 = item_mask;
    start->rules.game_speed = 1.0f;
    start->rules.stkind = St_Kind_Last;
    for (int i = 0; i < GM_MAX_PLAYERS; ++i)
        start->players[i].slot_type = Gm_PKind_NA;
    for (int i = 0; i < player_count; ++i) {
        start->players[i].ckind = CKIND_MARIO;
        start->players[i].slot_type = Gm_PKind_Human;
        start->players[i].stocks = 4;
        start->players[i].slot = 0;
        start->players[i].team = (u8) teams[i];
    }
}

/* Refusal text for a payload whose stock/stage/item rules are unsupported and
 * for one whose Team Battle setup is outside the source-derived rule. */
static const char kRulesRefusal[] =
    "Menu payload does not match the supported stock/stage rules";
static const char kTeamRefusal[] =
    "Team Battle payload is outside the supported setups: two to four "
    "active source players, team colours 0-2, and at least two players on "
    "different teams";

/* refusal == NULL expects the source preparation/initialization boundary to
 * run once with the item mask intact; otherwise the payload must be rejected
 * with exactly that message before either source boundary runs. */
static int check_route(int item_frequency, uint64_t item_mask,
                       int is_teams, const int teams[4], int player_count,
                       const char* refusal, int use_init)
{
    char error[192] = {0};
    StartMeleeData start;
    MeleeWebMatchRules* owner;
    int result;

    set_supported_vs_payload(&start, item_frequency, item_mask, is_teams,
                             teams, player_count);
    owner = melee_web_match_rules_begin(error, sizeof(error));
    if (!owner) {
        fprintf(stderr, "begin source rules: %s\n", error);
        return 0;
    }
    g_prepare_calls = 0;
    g_init_calls = 0;
    g_prepared_item_mask = 0;
    g_initialized_item_mask = 0;
    result = use_init
        ? melee_web_match_rules_init_from_menu(owner, &start, error, sizeof(error))
        : melee_web_match_rules_prepare_from_menu(owner, &start, 0,
                                                   error, sizeof(error));
    if (refusal == NULL) {
        if (!result ||
            (use_init && (g_init_calls != 1 || g_initialized_item_mask != item_mask)) ||
            (!use_init && (g_prepare_calls != 1 || g_prepared_item_mask != item_mask))) {
            fprintf(stderr, "None-frequency source setup did not preserve its item mask: %s\n",
                    error);
            melee_web_match_rules_end(owner, NULL, 0);
            return 0;
        }
    } else if (result || g_prepare_calls != 0 || g_init_calls != 0 ||
               strcmp(error, refusal) != 0) {
        fprintf(stderr, "unsupported source setup crossed the boundary or "
                        "reported the wrong refusal: %s\n", error);
        melee_web_match_rules_end(owner, NULL, 0);
        return 0;
    }
    if (!melee_web_match_rules_end(owner, error, sizeof(error))) {
        fprintf(stderr, "end source rules: %s\n", error);
        return 0;
    }
    return 1;
}

int main(void)
{
    const uint64_t menu_mask = UINT64_MAX ^ UINT64_C(1);
    static const int ffa[4] = {0, 0, 0, 0};
    static const int one_v_one[4] = {0, 1, 0, 0};
    static const int same_team[4] = {0, 0, 0, 0};
    static const int out_of_range[4] = {0, 3, 0, 0};
    /* Source CSS team setups: fn_80262F44 needs two active doors on
     * different teams; cycleTeam keeps each team ID in 0..2. */
    static const int two_v_two[4] = {0, 0, 1, 1};
    static const int three_v_one[4] = {2, 2, 2, 0};
    static const int two_v_one_v_one[4] = {1, 0, 1, 2};
    static const int one_v_one_v_one[4] = {0, 1, 2, 0};
    static const int two_v_one[4] = {1, 0, 1, 0};
    static const int four_same[4] = {2, 2, 2, 2};
    static const int fourth_out_of_range[4] = {0, 0, 1, 3};
    if (/* Free-for-all and two-player Teams keep their prior admission. */
        !check_route(-1, menu_mask, 0, ffa, 2, NULL, 0) ||
        !check_route(-1, menu_mask, 0, ffa, 2, NULL, 1) ||
        !check_route(-1, menu_mask, 1, one_v_one, 2, NULL, 0) ||
        !check_route(-1, menu_mask, 1, one_v_one, 2, NULL, 1) ||
        !check_route(-1, menu_mask, 0, ffa, 4, NULL, 0) ||
        /* Two-player and team-colour refusals now name the team rule. */
        !check_route(-1, menu_mask, 1, same_team, 2, kTeamRefusal, 0) ||
        !check_route(-1, menu_mask, 1, out_of_range, 2, kTeamRefusal, 1) ||
        !check_route(-1, menu_mask, 2, one_v_one, 2, kTeamRefusal, 0) ||
        !check_route(-1, menu_mask, 1, ffa, 1, kTeamRefusal, 0) ||
        /* Four- and three-player Team Battle setups the source CSS starts. */
        !check_route(-1, menu_mask, 1, two_v_two, 4, NULL, 0) ||
        !check_route(-1, menu_mask, 1, two_v_two, 4, NULL, 1) ||
        !check_route(-1, menu_mask, 1, three_v_one, 4, NULL, 0) ||
        !check_route(-1, menu_mask, 1, two_v_one_v_one, 4, NULL, 1) ||
        !check_route(-1, menu_mask, 1, one_v_one_v_one, 3, NULL, 0) ||
        !check_route(-1, menu_mask, 1, two_v_one, 3, NULL, 1) ||
        /* Setups the source CSS never starts remain explicit refusals. */
        !check_route(-1, menu_mask, 1, four_same, 4, kTeamRefusal, 0) ||
        !check_route(-1, menu_mask, 1, four_same, 3, kTeamRefusal, 1) ||
        !check_route(-1, menu_mask, 1, fourth_out_of_range, 4, kTeamRefusal, 0) ||
        /* Item-enabled rules are refused before the team rule is consulted. */
        !check_route(0, menu_mask, 0, ffa, 2, kRulesRefusal, 0) ||
        !check_route(0, menu_mask, 0, ffa, 2, kRulesRefusal, 1) ||
        !check_route(0, menu_mask, 1, two_v_two, 4, kRulesRefusal, 0) ||
        !check_route(-1, UINT64_MAX, 0, ffa, 2, NULL, 1))
        return 1;
    puts("source match rules preserve item masks and admit original CSS team setups");
    return 0;
}
