/* Original VS Team Battle admission: the shared source-derived rule and the
 * patched fn_8016DCC0 validator that consumes it.
 *
 * MELEE_WEB_PATCHED_VALIDATOR names a file holding the exact
 * melee_web_match_validate_source_start text extracted from
 * patches/melee-gameplay.patch, so the generated source and this test cannot
 * drift apart. Native traced only: no browser, retail or timing claim. */
#include "gameplay_content.h"
#include "gameplay_match_rules.h"
#include "gameplay_player_selection.h"

#include <melee/gm/gm_16AE.h>
#include <melee/gm/types.h>
#include <melee/pl/player.h>

#include <stdio.h>
#include <string.h>

HSD_GObj* Player_GetEntity(s32 slot)
{
    (void) slot;
    return NULL;
}
void gm_80183218(void) {}
/* Timer and item rules are outside this check; the match-rules trace owns
 * them. This source setup runs with the timer disabled. */
int melee_web_match_timer_supported(const struct StartMeleeRules* rules)
{
    return rules != NULL && !rules->timer_enabled;
}

#include MELEE_WEB_PATCHED_VALIDATOR

/* fn_80262F44 (mnCharSel.c) shows Start for contiguous active doors when at
 * least two are active and some pair (i < count - 1, j >= i) differs in team.
 * Transcribed literally as an independent oracle. */
static int source_css_start_visible(const int teams[4], int count)
{
    int i;
    int j;
    if (count < 2) return 0;
    for (i = 0; i < count - 1; i++)
        for (j = i; j < count; j++)
            if (teams[i] != teams[j]) return 1;
    return 0;
}

/* The port admits 2-4 contiguous doors, cycleTeam's three colours, and (for
 * the strict boundary) the source Start predicate. */
static int expected_supported(int is_teams, const int teams[4], int count,
                              int require_opposing)
{
    int i;
    if (is_teams > 1) return 0;
    if (!is_teams) return 1;
    if (count < 2 || count > 4) return 0;
    for (i = 0; i < count; i++)
        if (teams[i] < 0 || teams[i] > 2) return 0;
    return !require_opposing || source_css_start_visible(teams, count);
}

static void build_start(StartMeleeData* start, int is_teams,
                        const int teams[4], int count)
{
    int i;
    memset(start, 0, sizeof(*start));
    start->rules.match_kind = MatchKind_Stock;
    start->rules.is_stock = 1;
    start->rules.is_vs = 1;
    start->rules.is_teams = (u8) is_teams;
    start->rules.xB = -1;
    start->rules.x20 = UINT64_MAX;
    start->rules.game_speed = 1.0f;
    start->rules.stkind = St_Kind_Last;
    for (i = 0; i < GM_MAX_PLAYERS; ++i)
        start->players[i].slot_type = Gm_PKind_NA;
    for (i = 0; i < count; ++i) {
        start->players[i].ckind = CKIND_MARIO;
        start->players[i].slot_type = Gm_PKind_Cpu;
        start->players[i].cpu_kind = 4;
        start->players[i].cpu_level = 9;
        start->players[i].stocks = 4;
        start->players[i].team = (u8) teams[i];
    }
}

static int check_named(const char* name, int is_teams, const int teams[4],
                       int count, int accepted)
{
    StartMeleeData start;
    build_start(&start, is_teams, teams, count);
    if (melee_web_match_validate_source_start(&start, 0, 0) != accepted) {
        fprintf(stderr, "patched validator %s: expected %s\n", name,
                accepted ? "accept" : "refuse");
        return 0;
    }
    return 1;
}

int main(void)
{
    static const int ffa[4] = {0, 0, 0, 0};
    static const int one_v_one[4] = {0, 1, 0, 0};
    static const int two_v_two[4] = {0, 0, 1, 1};
    static const int two_v_two_split[4] = {0, 1, 0, 1};
    static const int three_v_one[4] = {2, 2, 2, 0};
    static const int two_v_one_v_one[4] = {1, 0, 1, 2};
    static const int one_v_one_v_one[4] = {0, 1, 2, 0};
    static const int two_v_one[4] = {1, 0, 1, 0};
    static const int four_same[4] = {2, 2, 2, 2};
    static const int three_same[4] = {1, 1, 1, 0};
    static const int fourth_out_of_range[4] = {0, 0, 1, 3};
    static const int second_out_of_range[4] = {0, 3, 0, 0};
    int is_teams;
    int count;
    int require;
    int code;
    int teams[4];
    StartMeleeData start;

    /* Named source CSS setups, through the patched fn_8016DCC0 validator. */
    if (!check_named("2v2", 1, two_v_two, 4, 1) ||
        !check_named("2v2 interleaved", 1, two_v_two_split, 4, 1) ||
        !check_named("3v1", 1, three_v_one, 4, 1) ||
        !check_named("2v1v1", 1, two_v_one_v_one, 4, 1) ||
        !check_named("1v1v1", 1, one_v_one_v_one, 3, 1) ||
        !check_named("2v1", 1, two_v_one, 3, 1) ||
        !check_named("1v1", 1, one_v_one, 2, 1) ||
        !check_named("free-for-all x4", 0, ffa, 4, 1) ||
        !check_named("free-for-all x3", 0, ffa, 3, 1) ||
        !check_named("free-for-all x2", 0, ffa, 2, 1) ||
        /* Setups the source CSS never starts, or the port does not own. */
        !check_named("four on one team", 1, four_same, 4, 0) ||
        !check_named("three on one team", 1, three_same, 3, 0) ||
        !check_named("two on one team", 1, ffa, 2, 0) ||
        !check_named("fourth team colour", 1, fourth_out_of_range, 4, 0) ||
        !check_named("second team colour out of range", 1, second_out_of_range,
                     2, 0) ||
        !check_named("single player", 0, ffa, 1, 0) ||
        !check_named("single player teams", 1, ffa, 1, 0))
        return 1;

    /* A gap between active doors is not a source CSS roster the port owns. */
    build_start(&start, 1, two_v_two, 4);
    start.players[1].slot_type = Gm_PKind_NA;
    if (melee_web_match_validate_source_start(&start, 0, 0)) {
        fprintf(stderr, "validator accepted a non-contiguous Team Battle\n");
        return 2;
    }
    /* Team flag values other than the source boolean are refused. */
    build_start(&start, 2, two_v_two, 4);
    if (melee_web_match_validate_source_start(&start, 0, 0) ||
        melee_web_team_setup_supported(&start, 4, 1)) {
        fprintf(stderr, "validator accepted an out-of-range team flag\n");
        return 3;
    }
    /* The opening demo is a four-CPU free-for-all and never a Team Battle. */
    build_start(&start, 1, two_v_two, 4);
    start.rules.x1_2 = 1;
    start.rules.x1_3 = 1;
    start.rules.disable_pausing = 1;
    start.rules.xB = 2;
    start.rules.on_match_start = gm_80183218;
    if (melee_web_match_validate_source_start(&start, 1, 0)) {
        fprintf(stderr, "opening demo validator accepted a Team Battle\n");
        return 4;
    }

    /* Exhaustive: every flag, roster size and colour assignment the CSS
     * progress and strict boundaries can see, against the source oracle. */
    for (is_teams = 0; is_teams <= 2; ++is_teams)
        for (count = 0; count <= 5; ++count)
            for (code = 0; code < 256; ++code) {
                for (int i = 0; i < 4; ++i) teams[i] = (code >> (2 * i)) & 3;
                build_start(&start, is_teams, teams, count > 4 ? 4 : count);
                for (require = 0; require <= 1; ++require)
                    if (melee_web_team_setup_supported(&start,
                            count > 4 ? 5 : count, require) !=
                        expected_supported(is_teams, teams, count, require)) {
                        fprintf(stderr,
                                "shared rule: teams=%d count=%d code=%d require=%d\n",
                                is_teams, count, code, require);
                        return 5;
                    }
                if (count >= 1 && count <= 4) {
                    const int accepted = count >= 2 &&
                        expected_supported(is_teams, teams, count, 1);
                    if (melee_web_match_validate_source_start(&start, 0, 0) !=
                        accepted) {
                        fprintf(stderr,
                                "patched validator: teams=%d count=%d code=%d\n",
                                is_teams, count, code);
                        return 6;
                    }
                }
            }
    puts("source team setup rule and patched validator admit original CSS team setups");
    return 0;
}
