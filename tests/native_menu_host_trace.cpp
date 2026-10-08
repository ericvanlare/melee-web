#include "gameplay_menu_world.hpp"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_content.h"
#include <algorithm>
#include "gameplay_menu_host.h"
#include "gameplay_save_profile.h"
#include "gameplay_match_session.hpp"
#include "gameplay_results_session.hpp"
#include "gameplay_prize_session.hpp"
#include "gameplay_match_rules.h"
#include "gameplay_bootstrap.h"
#include "gameplay_audio_stream.h"
#include "gameplay_retail_recipe.hpp"
#include "gameplay_source_files.h"
#include "gameplay_stage_map.h"
#include "gameplay_stage_stadium.h"
#include "native_menu_fighter_input.h"
#include "native_menu_stage_input.h"
#include "stadium_c1_stage_state_probe.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "dat_archive.hpp"
#include "dat_color_animation.hpp"
#include "dat_effect_banks.hpp"
#include "dat_item_article.hpp"
#include "dat_item_registry.hpp"
#include "dat_item_registry_native.hpp"
#include "dat_native_stage.hpp"
#include "dat_native_joint.hpp"
#include "dat_scene.hpp"
#include "dat_stage.hpp"
#include "dat_stage_items.hpp"
#include "dat_stage_yaku.hpp"
#include "gameplay_effect_banks.h"
#include "gameplay_ground_data.h"
#include "gameplay_item_runtime.h"
#include "gameplay_stage_map.h"
#include "native_dat.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include "stadium_screen_roots_probe.h"
#include "stadium_c1_e8_call_observer.h"
#include "stadium_c1_item_owner_negative_cases.hpp"
#include "stadium_c1_item_owner_preflight.hpp"
#include "stadium_live_image_consumer.hpp"
#include "stadium_buffer_consumer.hpp"
#include "stadium_ground_owner_contract.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include <limits>
#include <optional>
#endif
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
extern "C" {
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmresultplayer.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lbfile.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnmain.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include <melee/gr/grdatfiles.h>
#include <melee/gr/stage.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/objalloc.h>
#endif
#include <melee/ty/forward.h>
#include <melee/ty/toy.h>
#include <melee/ty/types.h>
extern HSD_Archive* _Toy_sbss_804D6ED0;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_ObjAllocData gobjproc_alloc_data;
#endif
}
#include <melee/gr/forward.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#endif
#include <sysdolphin/baselib/random.h>
extern "C" {
#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/rumble.h>
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
}
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>
extern "C" int melee_web_vs_mode_begin(void);
extern "C" int melee_web_vs_mode_end(void);
extern "C" int melee_web_vs_mode_select_state(int);
extern "C" int melee_web_vs_mode_resolve_next_state(GameModeState*);
extern "C" int melee_web_vs_mode_set_route(int current_mode, int previous_mode);
extern "C" void* melee_web_current_scene_info(void);
extern "C" void* melee_web_grpstadium_exchange_yakumono(void* value);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern "C" int melee_web_stage_selection_begin(int stage_kind);
extern "C" int melee_web_stage_selection_end(void);
#endif
static void check(int value,const char* error){if(!value){std::cerr<<"Check failed before teardown: "<<error<<"\n";throw std::runtime_error(error);}}

namespace {
void run_vs_sudden_death_source_control()
{
    char error[256]{};
    check(melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                           error, sizeof(error)), error);
    bool mode_owned = false;
    bool session_active = true;
    try {
        check(melee_web_vs_mode_begin(),
              "Original VS source-mode lease did not begin");
        mode_owned = true;
        check(melee_web_vs_mode_set_route(GM_VS, GM_MENU),
              "Original VS source route was not selected");

        VsModeData* const vs = gmVsMelee_GetVsData();
        std::memset(vs, 0, sizeof(*vs));
        vs->start.rules.match_kind = MatchKind_Stock;
        vs->start.rules.is_stock = true;
        vs->start.rules.is_vs = true;
        vs->start.rules.timer_enabled = true;
        vs->start.rules.time_limit = 60;
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
            vs->start.players[i].slot_type = Gm_PKind_NA;
        vs->start.players[0].slot_type = Gm_PKind_Cpu;
        vs->start.players[0].ckind = CKIND_MARIO;
        vs->start.players[0].slot = 1;
        vs->start.players[0].stocks = 4;
        vs->start.players[0].color = 2;
        vs->start.players[0].sub_color = 1;
        vs->start.players[1].slot_type = Gm_PKind_Cpu;
        vs->start.players[1].ckind = CKIND_FOX;
        vs->start.players[1].slot = 2;
        vs->start.players[1].stocks = 4;
        vs->start.players[1].color = 1;
        vs->start.players[1].sub_color = 2;

        const auto route_vs_result = [](MatchOutcome outcome,
                                        unsigned winner_count) {
            MatchExitInfo& exit = gmVsMelee_VsExitInfo;
            std::memset(&exit, 0, sizeof(exit));
            exit.match_end.outcome = outcome;
            exit.match_end.match_kind = MatchKind_Stock;
            exit.match_end.n_winners = static_cast<u8>(winner_count);
            for (auto& player : exit.match_end.player_standings)
                player.slot_type = Gm_PKind_NA;
            for (unsigned i = 0; i < 2; ++i) {
                auto& standing = exit.match_end.player_standings[i];
                standing.slot_type = Gm_PKind_Cpu;
                standing.ckind = i == 0 ? CKIND_MARIO : CKIND_FOX;
                standing.stocks = winner_count == 2 || i == 0 ? 1 : 0;
                standing.is_big_loser = winner_count != 2 && i != 0;
            }
            check(melee_web_vs_mode_select_state(gmVsMode_State_Vs),
                  "Original VS state could not be selected");
            gm_Mode_Vs_States[gmVsMode_State_Vs].on_exit(
                &gm_Mode_Vs_States[gmVsMode_State_Vs]);
            return melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
        };
        check(route_vs_result(OUTCOME_TIMEOUT, 1) == gmVsMode_State_Results,
              "Original single-winner timeout did not select Results");
        check(route_vs_result(OUTCOME_ELIMINATION, 1) == gmVsMode_State_Results,
              "Original single-winner elimination did not select Results");
        check(route_vs_result(OUTCOME_TIMEOUT, 2) ==
                  gmVsMode_State_SuddenDeath,
              "Original tied timeout did not select Sudden Death");

        const StartMeleeData before_sudden_death = vs->start;
        StartMeleeData expected_start{};
        expected_start.rules = before_sudden_death.rules;
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
            expected_start.players[i] = before_sudden_death.players[i];
        gm_SetupSubColors(&expected_start);
        gm_LoadRumbleEnabled(&expected_start);
        gm_SetupSuddenDeath(&expected_start,
                            &gmVsMelee_VsExitInfo.match_end);

        StartMeleeData excluded_start{};
        excluded_start.players[0].slot_type = Gm_PKind_Cpu;
        excluded_start.players[1].slot_type = Gm_PKind_Cpu;
        MatchEnd excluded_end{};
        excluded_end.match_kind = MatchKind_Stock;
        excluded_end.outcome = OUTCOME_TIMEOUT;
        excluded_end.player_standings[0].slot_type = Gm_PKind_Cpu;
        excluded_end.player_standings[0].ckind = CKIND_MARIO;
        excluded_end.player_standings[0].stocks = 0;
        excluded_end.player_standings[0].is_big_loser = false;
        excluded_end.player_standings[1].slot_type = Gm_PKind_Cpu;
        excluded_end.player_standings[1].ckind = CKIND_FOX;
        excluded_end.player_standings[1].stocks = 1;
        excluded_end.player_standings[1].is_big_loser = true;
        gm_SetupSuddenDeath(&excluded_start, &excluded_end);
        check(excluded_start.players[0].slot_type == Gm_PKind_NA &&
                  excluded_start.players[1].slot_type == Gm_PKind_NA,
              "Original Sudden Death helper retained zero-stock or big-loser players");

        check(melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath),
              "Original Sudden Death state could not be selected");
        gm_Mode_Vs_States[gmVsMode_State_SuddenDeath].on_enter(
            &gm_Mode_Vs_States[gmVsMode_State_SuddenDeath]);
        check(std::memcmp(&gmVsMelee_StartData, &expected_start,
                          sizeof(expected_start)) == 0,
              "Original Sudden Death callback changed source-generated StartMeleeData");
        check(gmVsMelee_StartData.rules.match_kind == MatchKind_Stock &&
                  !gmVsMelee_StartData.rules.timer_enabled &&
                  gmVsMelee_StartData.players[0].slot_type == Gm_PKind_Cpu &&
                  gmVsMelee_StartData.players[1].slot_type == Gm_PKind_Cpu &&
                  gmVsMelee_StartData.players[0].stocks == 1 &&
                  gmVsMelee_StartData.players[1].stocks == 1,
              "Original Sudden Death callback did not produce its source rules and players");
        check(!melee_web_vs_mode_begin(),
              "Sudden Death did not retain its original VS mode owner");

        MatchExitInfo sudden_death_exit{};
        sudden_death_exit.x0 = 0x13579;
        sudden_death_exit.x4 = 0x2468;
        sudden_death_exit.x8 = 0x11223344;
        MatchEnd& end = sudden_death_exit.match_end;
        end.match_kind = MatchKind_Stock;
        end.outcome = OUTCOME_ELIMINATION;
        end.frame_count = 9876;
        end.n_winners = 1;
        end.winners[0] = 1;
        for (auto& player : end.player_standings)
            player.slot_type = Gm_PKind_NA;
        end.player_standings[1].slot_type = Gm_PKind_Cpu;
        end.player_standings[1].ckind = CKIND_FOX;
        end.player_standings[1].stocks = 1;
        gmVsMelee_SuddenDeathExitInfo = sudden_death_exit;

        MatchEnd expected_result = gmVsMelee_VsExitInfo.match_end;
        MatchEnd sudden_death_match_end = end;
        gm_80166CCC(&expected_result, &sudden_death_match_end);
        check(melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath),
              "Original Sudden Death state could not be reselected for exit");
        gm_Mode_Vs_States[gmVsMode_State_SuddenDeath].on_exit(
            &gm_Mode_Vs_States[gmVsMode_State_SuddenDeath]);
        check(melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States) ==
                  gmVsMode_State_Results,
              "Original Sudden Death state table did not select Results");
        check(std::memcmp(&gmVsMelee_VsExitInfo.match_end, &expected_result,
                          sizeof(expected_result)) == 0,
              "Original Sudden Death exit did not preserve its authored MatchEnd merge");
        check(gmVsMelee_VsExitInfo.x0 == 0 &&
                  gmVsMelee_VsExitInfo.x4 == 0 &&
                  gmVsMelee_VsExitInfo.x8 == 0,
              "Sudden Death source callback changed non-MatchEnd VS exit fields");

        check(melee_web_vs_mode_select_state(gmVsMode_State_Results),
              "Original Results state could not be selected after Sudden Death");
        gm_Mode_Vs_States[gmVsMode_State_Results].on_enter(
            &gm_Mode_Vs_States[gmVsMode_State_Results]);
        check(std::memcmp(&gmVsMelee_ResultsEnterData.match_end,
                          &expected_result, sizeof(expected_result)) == 0,
              "Original Results callback changed the Sudden Death MatchEnd payload");
        check(melee_web_vs_mode_end(),
              "Original VS source-mode lease did not retire after Results");
        mode_owned = false;
        check(melee_web_vs_mode_begin(),
              "Original VS source-mode lease could not be reacquired after retirement");
        check(melee_web_vs_mode_end(),
              "Reacquired Original VS source-mode lease did not retire");

        auto* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        MeleeWebMenuMatchContinuation rejected_continuation{};
        MatchExitInfo rejected_exit{};
        check(!melee_web_menu_host_match_continuation_begin(
                  host, &rejected_exit, 1, &rejected_continuation,
                  error, sizeof(error)),
              "Typed host accepted a match continuation before source menus closed");
        check(rejected_continuation.kind == 0,
              "Rejected typed continuation retained a stale payload");
        rejected_continuation.kind = MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS;
        check(!melee_web_menu_host_sudden_death_finish(
                  host,0,&rejected_exit,2,nullptr,
                  &rejected_continuation, error, sizeof(error)) &&
                  rejected_continuation.kind == 0,
              "Typed host accepted a Sudden Death finish without its live route");
        check(melee_web_vs_mode_begin(),
              "Rejected typed continuation leaked its original VS owner");
        check(melee_web_vs_mode_end(),
              "Typed continuation ownership control could not retire the VS lease");
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);

        MeleeWebMenuMatchContinuation unowned_sudden_death{};
        unowned_sudden_death.kind=MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH;
        unowned_sudden_death.owner_id=1;
        MeleeWebMenuMatchSelection refused_selection{};
        check(!melee_web_menu_host_sudden_death_selection(
                  nullptr,&unowned_sudden_death,&refused_selection,
                  error,sizeof(error))&&refused_selection.player_count==0,
              "Sudden Death host accepted a continuation without its live owner");
        MeleeWebMenuMatchSelection bypass_selection{};
        bypass_selection.sudden_death=1;
        bool rejected_bypass=false;
        try{melee_web::GameplayMatchSession invalid(empty_files,bypass_selection);}
        catch(const std::exception&){rejected_bypass=true;}
        check(rejected_bypass,
              "Native Sudden Death session accepted a source payload without its host claim");

        check(melee_web_gameplay_session_end(error, sizeof(error)), error);
        session_active = false;
    } catch (...) {
        if (mode_owned)
            (void)melee_web_vs_mode_end();
        if (session_active)
            (void)melee_web_gameplay_session_end(error, sizeof(error));
        throw;
    }
    std::cout << "Original VS timeout/tie and Sudden Death-to-Results source callbacks passed; "
                 "asset-free callback control only, no gameplay claim\n";
}

void run_sudden_death_host_control(
    MeleeWebMenuHost* host, const MeleeWebMenuMatchSelection& selection,
    const StartMeleeData& source_start,
    char* error, std::size_t error_size)
{
    check(host != nullptr && selection.player_count >= 2 &&
              selection.player_count <= GM_MAX_PLAYERS,
          "Sudden Death host control requires the committed source menu selection");
    check(melee_web_menu_host_input(host) != nullptr,
          "Closed SSS did not retain its source PAD owner");

    const std::uint32_t menu_seed = selection.random_seed;
    std::uint32_t vs_final_seed = menu_seed ^ 0x6d2b79f5U;
    if (vs_final_seed == menu_seed) ++vs_final_seed;
    std::uint32_t sudden_death_final_seed = vs_final_seed ^ 0xa5c31e27U;
    if (sudden_death_final_seed == vs_final_seed) ++sudden_death_final_seed;

    const StartMeleeData original_start = gmVsMelee_StartData;
    const MatchExitInfo original_sudden_death_exit =
        gmVsMelee_SuddenDeathExitInfo;

    MatchExitInfo tied_timeout{};
    tied_timeout.match_end.outcome = OUTCOME_TIMEOUT;
    tied_timeout.match_end.match_kind = source_start.rules.match_kind;
    tied_timeout.match_end.n_winners =
        static_cast<std::uint8_t>(selection.player_count);
    tied_timeout.match_end.frame_count = 3600;
    for (auto& player : tied_timeout.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    for (unsigned i = 0; i < selection.player_count; ++i) {
        const auto& source = source_start.players[i];
        auto& standing = tied_timeout.match_end.player_standings[i];
        check(source.slot_type != Gm_PKind_NA,
              "Committed VS source payload has a gap before its active players");
        standing.slot_type = source.slot_type;
        standing.ckind = source.ckind;
        standing.stocks = 1;
        standing.is_big_loser = false;
        tied_timeout.match_end.winners[i] = static_cast<u8>(i);
    }

    StartMeleeData expected_sudden_death{};
    expected_sudden_death.rules = source_start.rules;
    for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
        expected_sudden_death.players[i] = source_start.players[i];
    gm_SetupSubColors(&expected_sudden_death);
    gm_LoadRumbleEnabled(&expected_sudden_death);
    gm_SetupSuddenDeath(&expected_sudden_death, &tied_timeout.match_end);

    MeleeWebMenuMatchContinuation continuation{};
    check(melee_web_menu_host_match_continuation_begin(
              host, &tied_timeout, vs_final_seed, &continuation,
              error, error_size), error);
    check(continuation.kind == MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH &&
              std::memcmp(&continuation.payload.sudden_death_start,
                          &expected_sudden_death,
                          sizeof(expected_sudden_death)) == 0 &&
              std::memcmp(&gmVsMelee_StartData, &expected_sudden_death,
                          sizeof(expected_sudden_death)) == 0,
          "Typed host did not retain the original tie-selected Sudden Death payload");
    const std::uint64_t sudden_death_owner_id=continuation.owner_id;
    check(sudden_death_owner_id!=0,
          "Typed Sudden Death continuation has no checked owner identity");
    MeleeWebMenuMatchSelection sudden_death_selection{};
    check(melee_web_menu_host_sudden_death_selection(
              host,&continuation,&sudden_death_selection,error,error_size)&&
              sudden_death_selection.sudden_death&&
              sudden_death_selection.start.rules.x6,
          "Typed Sudden Death selection did not preserve original scene setup");
    check(melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),error);
    check(!melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),
          "Typed Sudden Death continuation allowed a duplicate match owner");
    check(!melee_web_menu_host_destroy(host,error,error_size),
          "Typed Sudden Death host was destroyed before its match owner");
    check(!melee_web_menu_host_sudden_death_scene_begin(
              host,sudden_death_owner_id+1,error,error_size),
          "Typed Sudden Death scene accepted a stale owner identity");
    const void* saved_sudden_death_scene=melee_web_current_scene_info();
    check(melee_web_menu_host_sudden_death_scene_begin(
              host,sudden_death_owner_id,error,error_size),error);
    const auto* sudden_death_scene=
        static_cast<const GameModeState::GameSceneInfo*>(
            melee_web_current_scene_info());
    check(sudden_death_scene&&sudden_death_scene->scene_kind==GS_SUDDEN_DEATH,
          "Typed match owner did not assign the original GS_SUDDEN_DEATH scene");
    check(gmVsMelee_StartData.rules.x6,
          "Original GS_SUDDEN_DEATH scene setup did not set the global source payload");
    check(gm_GetCurrentSceneEnterData()==&gmVsMelee_StartData&&
              gm_GetCurrentSceneExitData()==&gmVsMelee_SuddenDeathExitInfo,
          "Sudden Death scene identity did not retain the original global entry and exit payloads");
    const auto* retained_sudden_death_scene=melee_web_current_scene_info();
    const StartMeleeData retained_sudden_death_start=gmVsMelee_StartData;
    const MatchExitInfo retained_sudden_death_exit=gmVsMelee_SuddenDeathExitInfo;
    std::uint8_t input_before_premature_finish[MELEE_WEB_PAD_STATE_BYTES];
    std::uint8_t input_after_premature_finish[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(input_before_premature_finish);
    MeleeWebMenuMatchContinuation premature_finish{};
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&tied_timeout,sudden_death_final_seed,
              input_before_premature_finish,&premature_finish,error,error_size)&&
              premature_finish.kind==0,
          "Typed host accepted Results handoff before its SD owner released the scene");
    melee_web_pad_state_capture(input_after_premature_finish);
    check(melee_web_current_scene_info()==retained_sudden_death_scene&&
              std::memcmp(&gmVsMelee_StartData,&retained_sudden_death_start,
                          sizeof(retained_sudden_death_start))==0&&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo,
                          &retained_sudden_death_exit,
                          sizeof(retained_sudden_death_exit))==0&&
              std::memcmp(input_before_premature_finish,
                          input_after_premature_finish,
                          sizeof(input_before_premature_finish))==0,
          "Rejected early Results handoff changed SD scene, source payloads or PAD state");
    MeleeWebMenuMatchSelection after_premature_selection{};
    check(melee_web_menu_host_selection(host,&after_premature_selection,
                                        error,error_size)&&
              after_premature_selection.random_seed==vs_final_seed&&
              !melee_web_gameplay_generation(),
          "Rejected early Results handoff changed the VS owner seed or world boundary");
    check(!melee_web_menu_host_sudden_death_match_release(
              host,sudden_death_owner_id,error,error_size),
          "Typed Sudden Death match claim released before its source scene restored");
    check(!melee_web_menu_host_sudden_death_scene_end(
              host,sudden_death_owner_id+1,error,error_size),
          "Typed Sudden Death scene restored for a stale owner identity");
    check(melee_web_menu_host_sudden_death_scene_end(
              host,sudden_death_owner_id,error,error_size),error);
    check(melee_web_current_scene_info()==saved_sudden_death_scene&&
              gm_GetCurrentSceneEnterData()!=&gmVsMelee_StartData,
          "Sudden Death scene end did not restore the exact prior host scene");
    check(melee_web_menu_host_sudden_death_match_release(
              host,sudden_death_owner_id,error,error_size),error);
    check(!melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),
          "Typed Sudden Death continuation allowed a second match after release");
    MeleeWebMenuMatchSelection observed{};
    check(melee_web_menu_host_selection(host, &observed, error, error_size) &&
              observed.random_seed == vs_final_seed,
          "Typed VS continuation did not transfer its explicit match RNG seed");
    check(!melee_web_vs_mode_begin(),
          "Typed Sudden Death continuation released its original VS mode lease");

    MatchExitInfo sudden_death_exit{};
    sudden_death_exit.match_end.match_kind = source_start.rules.match_kind;
    sudden_death_exit.match_end.outcome = OUTCOME_ELIMINATION;
    sudden_death_exit.match_end.frame_count = 9876;
    sudden_death_exit.match_end.n_winners = 1;
    const unsigned winner = selection.player_count - 1;
    sudden_death_exit.match_end.winners[0] = static_cast<u8>(winner);
    for (auto& player : sudden_death_exit.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    auto& winning_standing =
        sudden_death_exit.match_end.player_standings[winner];
    winning_standing.slot_type = source_start.players[winner].slot_type;
    winning_standing.ckind = source_start.players[winner].ckind;
    winning_standing.stocks = 1;

    MatchEnd expected_result = tied_timeout.match_end;
    MatchEnd sudden_death_result = sudden_death_exit.match_end;
    gm_80166CCC(&expected_result, &sudden_death_result);
    std::uint8_t final_sudden_death_input[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(final_sudden_death_input);
    MeleeWebMenuMatchContinuation rejected_finish{};
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id+1,&sudden_death_exit,
              sudden_death_final_seed,final_sudden_death_input,
              &rejected_finish,error,error_size)&&rejected_finish.kind==0,
          "Typed Sudden Death finish accepted a stale match owner identity");
    check(melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&sudden_death_exit,sudden_death_final_seed,
              final_sudden_death_input,
              &continuation, error, error_size), error);
    check(continuation.kind == MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS &&
              std::memcmp(&continuation.payload.results.match_end,
                          &expected_result, sizeof(expected_result)) == 0 &&
              std::memcmp(&gmVsMelee_VsExitInfo.match_end, &expected_result,
                          sizeof(expected_result)) == 0 &&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo, &sudden_death_exit,
                          sizeof(sudden_death_exit)) == 0,
          "Typed Sudden Death finish did not preserve the source Results merge");
    check(melee_web_menu_host_selection(host, &observed, error, error_size) &&
              observed.random_seed == sudden_death_final_seed,
          "Typed Sudden Death finish did not transfer its explicit final RNG seed");
    std::uint8_t transferred_input[MELEE_WEB_PAD_STATE_BYTES];
    check(melee_web_menu_host_input(host)!=nullptr,
          "Sudden Death Results handoff lost its transferred source PAD owner");
    melee_web_pad_state_apply(melee_web_menu_host_input(host));
    melee_web_pad_state_capture(transferred_input);
    check(std::memcmp(transferred_input,final_sudden_death_input,
                      sizeof(transferred_input))==0,
          "Sudden Death Results handoff changed the captured full PAD bank");
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&sudden_death_exit,sudden_death_final_seed,
              final_sudden_death_input,
              &continuation, error, error_size) && continuation.kind == 0,
          "Typed host replayed the Sudden Death exit callback");
    check(melee_web_menu_host_destroy(host, error, error_size), error);
    check(std::memcmp(&gmVsMelee_StartData, &original_start,
                      sizeof(original_start)) == 0 &&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo,
                          &original_sudden_death_exit,
                          sizeof(original_sudden_death_exit)) == 0,
          "Typed host did not restore its Sudden Death route globals");
    check(melee_web_vs_mode_begin(),
          "Typed Sudden Death host destruction leaked its VS mode lease");
    check(melee_web_vs_mode_end(),
          "Typed Sudden Death host control could not retire the VS lease");
    std::cout << "Original CSS/SSS typed VS tie to Sudden Death to Results handoff "
                 "retained both final RNG seeds and restored route globals; "
                 "menu-backed constructed-result host control only\n";
}

std::string hex32(uint32_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(8)<<value;return out.str();}
std::string hex64(uint64_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();}
std::string stream_name(MeleeWebAudio* audio){
 const char* path=melee_web_audio_stream_path(audio);if(!path)return {};
 std::string result(path);const auto slash=result.find_last_of("/\\");return slash==std::string::npos?result:result.substr(slash+1);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
void run_stadium_yakumono_exchange_control()
{
    check(melee_web_stadium_c1_yakumono_exchange_baseline_empty() &&
              melee_web_stadium_c1_stage_object_failures() ==
                  MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE,
          "Asset-free exchange control must start before source Ground/GObj construction");
    char snapshot_error[160]{};
    MeleeWebStadiumC1StageInfoSnapshot* const stage_snapshot =
        melee_web_stadium_c1_stage_info_snapshot_begin(
            snapshot_error, sizeof(snapshot_error));
    check(stage_snapshot != nullptr, snapshot_error);
    std::array<unsigned char, sizeof(HSD_GObjLibInitData)> init_before{};
    std::array<unsigned char, sizeof(HSD_GObj_804CE3E4)> dispatch_before{};
    std::memcpy(init_before.data(), &HSD_GObjLibInitData,
                sizeof(HSD_GObjLibInitData));
    std::memcpy(dispatch_before.data(), &HSD_GObj_804CE3E4,
                sizeof(HSD_GObj_804CE3E4));
    const HSD_GObjList* entities_before = HSD_GObj_Entities;
    GObjFunc* funcs_before = HSD_GObj_804D7810;
    HSD_GObj* callback_a_before = HSD_GObj_804D7814;
    HSD_GObj* callback_b_before = HSD_GObj_804D7818;
    HSD_GObj* callback_c_before = HSD_GObj_804D781C;
    HSD_GObj** object_lists_before = HSD_GObj_804D7820;
    HSD_GObj** gx_lists_before = HSD_GObjGXLinkHead;
    HSD_GObj** low_lists_before = plinklow_gobjs;
    HSD_GObjProc* proc_a_before = HSD_GObj_804D7830;
    const s32 proc_a_count_before = HSD_GObj_804D7834;
    HSD_GObjProc* proc_b_before = HSD_GObj_804D7838;
    const s32 proc_b_count_before = HSD_GObj_804D783C;
    HSD_GObjProc** proc_lists_a_before = HSD_GObj_804D7840;
    HSD_GObjProc** proc_lists_b_before = HSD_GObj_804D7844;

    // Distinct, naturally aligned source-shaped sentinels; the exchange never dereferences them.
    MeleeWebStadiumYakumono a{};
    MeleeWebStadiumYakumono b{};
    void* original = melee_web_grpstadium_exchange_yakumono(&a);
    void* previous_b = melee_web_grpstadium_exchange_yakumono(&b);
    void* previous_a = melee_web_grpstadium_exchange_yakumono(&a);
    void* previous_restore = melee_web_grpstadium_exchange_yakumono(original);
    void* previous_verify = melee_web_grpstadium_exchange_yakumono(nullptr);

    check(original == nullptr && previous_b == &a && previous_a == &b &&
              previous_restore == &a && previous_verify == nullptr,
          "Stadium yakumono exchange did not preserve NULL/A/B/A/NULL pointer ownership");
    check(melee_web_stadium_c1_stage_info_snapshot_matches(stage_snapshot),
          "Stadium yakumono exchange changed byte-exact source StageInfo");
    check(melee_web_stadium_c1_stage_object_failures() ==
              MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE,
          "Stadium yakumono exchange changed source Stage/GObj owner state");
    check(HSD_GObj_Entities == entities_before &&
              HSD_GObj_804D7810 == funcs_before &&
              HSD_GObj_804D7814 == callback_a_before &&
              HSD_GObj_804D7818 == callback_b_before &&
              HSD_GObj_804D781C == callback_c_before &&
              HSD_GObj_804D7820 == object_lists_before &&
              HSD_GObjGXLinkHead == gx_lists_before &&
              plinklow_gobjs == low_lists_before &&
              HSD_GObj_804D7830 == proc_a_before &&
              HSD_GObj_804D7834 == proc_a_count_before &&
              HSD_GObj_804D7838 == proc_b_before &&
              HSD_GObj_804D783C == proc_b_count_before &&
              HSD_GObj_804D7840 == proc_lists_a_before &&
              HSD_GObj_804D7844 == proc_lists_b_before &&
              std::memcmp(init_before.data(), &HSD_GObjLibInitData,
                          sizeof(HSD_GObjLibInitData)) == 0 &&
              std::memcmp(dispatch_before.data(), &HSD_GObj_804CE3E4,
                          sizeof(HSD_GObj_804CE3E4)) == 0,
          "Stadium yakumono exchange changed original GObj registry/dispatch state");
    check(melee_web_stadium_c1_stage_info_snapshot_release_unchanged(
              stage_snapshot, snapshot_error, sizeof(snapshot_error)),
          snapshot_error);
    std::cout << "Stadium yakumono exchange asset-free control passed; pointer restored and "
                 "source StageInfo/GObj owner state unchanged\n";
}

void check_stadium_preflight_stage_empty()
{
    check(melee_web_stage_map_archives() == nullptr,
          "C1 context preflight found a pre-existing native stage map scope");
    const uint32_t failures = melee_web_stadium_c1_stage_state_failures();
    std::string detail = "C1 context preflight stage-state bridge reported mask=" +
                         std::to_string(failures) + ":";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE)
        detail += " HSD GObj list unavailable";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ)
        detail += " StageInfo.map_gobjs occupied";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_INSTANCE)
        detail += " stage instance GObj present";
    if (failures & MELEE_WEB_STADIUM_C1_GROUND_GOBJ)
        detail += " Ground GObj present";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_ITEMS)
        detail += " StageInfo.itemdata published";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_LIGHTS)
        detail += " StageInfo.map_plit published";
    if (failures & MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT)
        detail += " ordinary grDatFiles slot occupied";
    check(failures == 0, detail.c_str());
}

struct GroundStoragePreCallFacts {
    MeleeWebSourceMemoryReadStatus memory_status{};
    MeleeWebSourceMemoryContext memory{};
    int os_heap = -1;
    int hsd_heap = -1;
    uint64_t world_generation = 0;
    bool world_active = false;
    bool memory_healthy = false;
    bool storage_available = false;
    MeleeWebGroundMapStorageView storage{};
    bool map_registry_empty = false;
    bool stage_gobj_list_empty = false;
    bool stage_markers_empty = false;
    bool dispatch_quiet = false;
};

bool ground_storage_pre_call_supported(const GroundStoragePreCallFacts& facts)
{
    return facts.memory_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
           facts.os_heap >= 0 && facts.os_heap == facts.hsd_heap &&
           facts.memory.source_heap_handle == facts.hsd_heap &&
           facts.memory.world_generation != 0 &&
           facts.memory.world_generation == facts.world_generation &&
           facts.world_active && facts.memory_healthy &&
           facts.storage_available && facts.storage.payload == nullptr &&
           facts.storage.requested_bytes == 64 &&
           facts.map_registry_empty && facts.stage_gobj_list_empty &&
           facts.stage_markers_empty && facts.dispatch_quiet;
}

void ground_storage_pre_call_controls()
{
    GroundStoragePreCallFacts valid{};
    valid.memory_status = MELEE_WEB_SOURCE_MEMORY_READ_OK;
    valid.memory = {7, 19, 23};
    valid.os_heap = 7;
    valid.hsd_heap = 7;
    valid.world_generation = 19;
    valid.world_active = true;
    valid.memory_healthy = true;
    valid.storage_available = true;
    valid.storage = {nullptr, 64};
    valid.map_registry_empty = true;
    valid.stage_gobj_list_empty = true;
    valid.stage_markers_empty = true;
    valid.dispatch_quiet = true;
    check(ground_storage_pre_call_supported(valid),
          "Ground storage pure gate rejected its supported synthetic context");
    auto rejected = [&](auto change, const char* message) {
        auto invalid = valid;
        change(invalid);
        check(!ground_storage_pre_call_supported(invalid), message);
    };
    rejected([](auto& f) { f.memory_status = MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE; },
             "Ground storage pure gate accepted inactive source memory");
    rejected([](auto& f) { f.hsd_heap += 1; },
             "Ground storage pure gate accepted a different HSD heap");
    rejected([](auto& f) { f.world_generation += 1; },
             "Ground storage pure gate accepted a stale world generation");
    rejected([](auto& f) { f.memory_healthy = false; },
             "Ground storage pure gate accepted unhealthy source memory");
    rejected([](auto& f) { f.storage_available = false; },
             "Ground storage pure gate accepted an already-owned buffer");
    rejected([](auto& f) { f.storage.payload = reinterpret_cast<void*>(1); },
             "Ground storage pure gate accepted a live buffer before begin");
    rejected([](auto& f) { f.storage.requested_bytes = 63; },
             "Ground storage pure gate accepted a changed authored request");
    rejected([](auto& f) { f.map_registry_empty = false; },
             "Ground storage pure gate accepted an occupied map registry");
    rejected([](auto& f) { f.stage_gobj_list_empty = false; },
             "Ground storage pure gate accepted an existing Stage GObj");
    rejected([](auto& f) { f.stage_markers_empty = false; },
             "Ground storage pure gate accepted pre-existing StageInfo marker owners");
    rejected([](auto& f) { f.dispatch_quiet = false; },
             "Ground storage pure gate accepted active object dispatch");
}

struct FtDeviceGlobalSnapshot {
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot = nullptr;

    FtDeviceGlobalSnapshot() { capture(); }
    FtDeviceGlobalSnapshot(const FtDeviceGlobalSnapshot&) = delete;
    FtDeviceGlobalSnapshot& operator=(const FtDeviceGlobalSnapshot&) = delete;

    ~FtDeviceGlobalSnapshot()
    {
        if (snapshot != nullptr)
            melee_web_stadium_c1_ft_device_snapshot_release(snapshot);
    }

    void capture()
    {
        if (snapshot != nullptr) {
            check(melee_web_stadium_c1_ft_device_snapshot_release(snapshot),
                  "Typed ftDevice snapshot release failed before recapture");
            snapshot = nullptr;
        }
        snapshot = melee_web_stadium_c1_ft_device_snapshot_create();
        check(snapshot != nullptr,
              "Typed ftDevice source snapshot allocation failed");
    }

    void restore() const
    {
        check(melee_web_stadium_c1_ft_device_snapshot_restore(snapshot),
              "Typed ftDevice source snapshot restoration failed");
    }

    bool matches() const
    {
        return melee_web_stadium_c1_ft_device_snapshot_matches(snapshot) != 0;
    }

    std::array<const void*, 6> addresses() const
    {
        std::array<const void*, 6> result{};
        check(melee_web_stadium_c1_ft_device_snapshot_addresses(
                  snapshot, result.data(), result.size()) == result.size(),
              "Typed ftDevice snapshot did not expose its source identities");
        return result;
    }
};

bool ground_dispatch_quiet()
{
    return HSD_GObj_804D781C == nullptr && HSD_GObj_804D7818 == nullptr &&
           HSD_GObj_804D7814 == nullptr && HSD_GObj_804D7830 == nullptr &&
           HSD_GObj_804D7838 == nullptr && HSD_GObj_804CE3E4.flags == 0;
}

uint32_t source_stage_gobj_count()
{
    check(HSD_GObj_Entities != nullptr,
          "Ground map probe has no original GObj list owner");
    auto** heads = reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    const uint32_t used = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    uint32_t traversed = 0;
    uint32_t stage_count = 0;
    for (uint32_t link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link) {
        for (HSD_GObj* current = heads[link]; current;
             current = current->next) {
            check(++traversed <= used && current->p_link == link,
                  "Original GObj list is cyclic or disagrees with its pool owner");
            if (current->classifier == HSD_GOBJ_CLASS_STAGE) ++stage_count;
        }
    }
    return stage_count;
}

bool source_stage_registry_empty()
{
    for (size_t i = 0;
         i < melee_web_stadium_c1_ground_map_slot_count(); ++i)
        if (melee_web_stadium_c1_ground_map_slot(i) != nullptr) return false;
    return true;
}

bool source_jobj_owned_by(HSD_JObj* node, HSD_JObj* root)
{
    for (HSD_JObj* current = node; current;
         current = HSD_JObjGetParent(current))
        if (current == root) return true;
    return false;
}

std::vector<HSD_JObj*>
source_stage_marker_snapshot()
{
    std::vector<HSD_JObj*> result;
    const size_t count = melee_web_stadium_c1_ground_marker_slot_count();
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(static_cast<HSD_JObj*>(
            melee_web_stadium_c1_ground_marker_slot(i)));
    return result;
}

std::vector<void*>
source_stage_map_registry_snapshot()
{
    std::vector<void*> result;
    const size_t count = melee_web_stadium_c1_ground_map_slot_count();
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(melee_web_stadium_c1_ground_map_slot(i));
    return result;
}

size_t source_stage_marker_count()
{
    return melee_web_stadium_c1_ground_marker_slot_count();
}

bool source_stage_markers_empty()
{
    const auto markers = source_stage_marker_snapshot();
    return std::all_of(markers.begin(), markers.end(),
                       [](HSD_JObj* marker) { return marker == nullptr; });
}

struct GroundStorageLease {
    GroundStoragePreCallFacts facts{};
    FtDeviceGlobalSnapshot devices_before{};
    FtDeviceGlobalSnapshot devices_after_begin{};
    const void* payload = nullptr;
    MeleeWebSourceMemoryAllocation allocation{};
    std::vector<uint8_t> bytes_after_begin;
    MeleeWebSourceMemoryContext context_after_competing_begin{};
    uint32_t stage_gobj_count_before = 0;
    bool begin_attempted = false;
    bool owned = false;
    bool ever_owned = false;
    bool devices_restored = false;
    bool finished = false;

    void begin()
    {
        check(!begin_attempted && !owned && !finished,
              "Ground storage lease begin was requested more than once");
        ground_storage_pre_call_controls();
        facts.memory_status =
            melee_web_source_memory_context_read(&facts.memory);
        const auto world = melee_web_gameplay_stats();
        facts.os_heap = __OSCurrHeap;
        facts.hsd_heap = HSD_GetHeap();
        facts.world_generation = world.generation;
        facts.world_active = melee_web_gameplay_world_exists() != 0;
        facts.memory_healthy = melee_web_source_memory_healthy() != 0;
        facts.storage_available = melee_web_ground_map_storage_available() != 0;
        check(melee_web_ground_map_storage_read(&facts.storage) != 0,
              "Ground storage preflight cannot observe its original owner");
        facts.map_registry_empty = source_stage_registry_empty();
        stage_gobj_count_before = source_stage_gobj_count();
        facts.stage_gobj_list_empty = stage_gobj_count_before == 0;
        facts.stage_markers_empty = source_stage_markers_empty();
        facts.dispatch_quiet = ground_dispatch_quiet();
        check(melee_web_stage_map_archives() == nullptr,
              "Ground storage preflight found a pre-existing native stage map scope");
        check(ground_storage_pre_call_supported(facts),
              "Ground storage owner preconditions are not satisfied");

        begin_attempted = true;
        if (melee_web_ground_map_storage_begin()) {
            owned = true;
            ever_owned = true;
        } else {
            MeleeWebGroundMapStorageView failed_view{};
            if (!melee_web_ground_map_storage_read(&failed_view)) {
                std::cerr << "Ground storage begin failed without a readable owner state; preserving it\n";
                std::abort();
            }
            owned = failed_view.payload != nullptr;
            ever_owned = owned;
            check(0, "Original Ground storage begin did not acquire its owner");
        }

        MeleeWebGroundMapStorageView storage{};
        check(melee_web_ground_map_storage_read(&storage) &&
                  storage.payload != nullptr && storage.requested_bytes == 64,
              "Original Ground storage accessor disagrees with its authored request");
        check(storage.requested_bytes <= UINT32_MAX,
              "Ground source allocation request exceeds observer representation");
        payload = storage.payload;
        const auto lease_status = melee_web_source_memory_allocation_read(
            payload, &allocation);
        check(melee_web::test::stadium_buffer::exact_new_allocation_supported(
                  lease_status, allocation, facts.memory,
                  static_cast<uint32_t>(storage.requested_bytes)) &&
                  allocation.source_heap_handle == facts.hsd_heap &&
                  allocation.world_generation == facts.world_generation,
              "Ground storage allocation identity differs from its exact live source owner");
        const auto* storage_bytes = static_cast<const uint8_t*>(storage.payload);
        check(std::all_of(storage_bytes,
                          storage_bytes + storage.requested_bytes,
                          [](uint8_t value) { return value == 0; }),
              "Original Ground storage bytes were not zeroed by Ground_801C0378");

        devices_after_begin.capture();
        const auto world_after_begin = melee_web_gameplay_stats();
        MeleeWebSourceMemoryContext before_competing_begin{};
        check(melee_web_source_memory_context_read(&before_competing_begin) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK,
              "Ground storage context became unavailable after original begin");
        bytes_after_begin.assign(storage_bytes,
                                 storage_bytes + storage.requested_bytes);
        check(melee_web_ground_map_storage_begin() == 0,
              "Competing Ground storage begin did not reject an owned buffer");
        MeleeWebGroundMapStorageView competing_view{};
        MeleeWebSourceMemoryAllocation competing_lease{};
        check(melee_web_ground_map_storage_read(&competing_view) &&
                  competing_view.payload == storage.payload &&
                  competing_view.requested_bytes == storage.requested_bytes &&
                  melee_web_source_memory_context_read(
                      &context_after_competing_begin) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  context_after_competing_begin.source_heap_handle ==
                      before_competing_begin.source_heap_handle &&
                  context_after_competing_begin.world_generation ==
                      before_competing_begin.world_generation &&
                  context_after_competing_begin.allocation_generation_watermark ==
                      before_competing_begin.allocation_generation_watermark &&
                  melee_web_source_memory_allocation_read(
                      storage.payload, &competing_lease) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  competing_lease.live == 1 &&
                  competing_lease.allocation_generation ==
                      allocation.allocation_generation &&
                  std::equal(bytes_after_begin.begin(), bytes_after_begin.end(),
                             storage_bytes) &&
                  devices_after_begin.matches() &&
                  melee_web_gameplay_stats().generation ==
                      world_after_begin.generation &&
                  melee_web_gameplay_stats().ticks == world_after_begin.ticks,
              "Competing Ground storage begin changed its live owner or source state");
    }

    void end()
    {
        if (!begin_attempted || finished) return;
        MeleeWebSourceMemoryContext before_free_context{};
        auto before_free_context_status =
            MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
        if (owned) {
            if (melee_web_stadium_c1_ground_map_lookup(1) != nullptr ||
                !source_stage_registry_empty() ||
                source_stage_gobj_count() != stage_gobj_count_before ||
                !source_stage_markers_empty()) {
                std::cerr << "Ground storage end refused while source map owners remain; preserving Ground storage\n";
                std::abort();
            }
            MeleeWebGroundMapStorageView before_free_storage{};
            MeleeWebSourceMemoryAllocation before_free_allocation{};
            before_free_context_status =
                melee_web_source_memory_context_read(&before_free_context);
            const auto before_free_allocation_status =
                melee_web_source_memory_allocation_read(
                    payload, &before_free_allocation);
            const auto world_before_free = melee_web_gameplay_stats();
            check(melee_web_ground_map_storage_read(&before_free_storage) &&
                      before_free_storage.payload == payload &&
                      before_free_storage.requested_bytes ==
                          allocation.requested_bytes &&
                      before_free_context_status ==
                          MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      before_free_context.source_heap_handle ==
                          facts.memory.source_heap_handle &&
                      before_free_context.world_generation ==
                          facts.memory.world_generation &&
                      melee_web::test::stadium_buffer::
                          exact_live_allocation_matches_context(
                          before_free_allocation_status, before_free_allocation,
                          before_free_context, allocation.requested_bytes,
                          allocation.allocation_generation) &&
                      __OSCurrHeap == facts.os_heap &&
                      HSD_GetHeap() == facts.hsd_heap &&
                      world_before_free.generation == facts.world_generation &&
                      melee_web_gameplay_world_exists() &&
                      melee_web_source_memory_healthy(),
                  "Ground storage end refused a changed exact live lease or owner context");
            if (!melee_web_ground_map_storage_end()) {
                std::cerr << "Ground storage end refused; preserving the live source owner\n";
                std::abort();
            }
            owned = false;
            devices_before.restore();
            devices_restored = true;
        } else if (!devices_restored) {
            devices_before.restore();
            devices_restored = true;
        }
        finished = true;
        if (!ever_owned) return;

        MeleeWebGroundMapStorageView ended_storage{};
        MeleeWebSourceMemoryAllocation retired_lease{};
        MeleeWebSourceMemoryContext after_end{};
        const auto retired_lease_status =
            melee_web_source_memory_allocation_read(payload, &retired_lease);
        const auto after_end_status =
            melee_web_source_memory_context_read(&after_end);
        const auto world_after_end = melee_web_gameplay_stats();
        check(melee_web_ground_map_storage_available() &&
                  melee_web_ground_map_storage_read(&ended_storage) &&
                  ended_storage.payload == nullptr &&
                  ended_storage.requested_bytes ==
                      allocation.requested_bytes &&
                  melee_web::test::stadium_buffer::
                      exact_retired_allocation_supported(
                      retired_lease_status, retired_lease,
                      before_free_context_status, before_free_context,
                      after_end_status, after_end) &&
                  after_end.source_heap_handle ==
                      facts.memory.source_heap_handle &&
                  after_end.world_generation == facts.memory.world_generation &&
                  __OSCurrHeap == facts.os_heap &&
                  HSD_GetHeap() == facts.hsd_heap &&
                  world_after_end.generation == facts.world_generation &&
                  melee_web_gameplay_world_exists() &&
                  devices_before.matches() && melee_web_source_memory_healthy(),
              "Ground storage end did not retire its exact lease and restore typed devices");
    }
};

melee_web::test::stadium_ground::SourceMarkerSelection
checked_ground_source_markers(
    const std::shared_ptr<const melee_web::DatArchive>& archive,
    const melee_web::DatStage& stage,
    const melee_web::DatStageEntry& map_entry,
    std::vector<uint16_t>& row_joint_indices)
{
    using melee_web::test::stadium_ground::SourceMarkerRow;
    check(archive && stage.joint_reference_table.count != 0 &&
              stage.joint_reference_table.data_offset.has_value(),
          "Stadium source marker reference table has no bounded authored rows");
    check(map_entry.joint_offset.has_value() &&
              map_entry.joint_indices.element_bytes == sizeof(uint16_t) &&
              (!map_entry.joint_indices.count ||
               map_entry.joint_indices.data_offset.has_value()),
          "Stadium map1 source joint-index row is incomplete");
    const uint32_t selected_root = *map_entry.joint_offset;
    std::vector<SourceMarkerRow> rows;
    rows.reserve(stage.joint_reference_table.count);
    for (uint32_t row_index = 0;
         row_index < stage.joint_reference_table.count; ++row_index) {
        const uint32_t marker_row =
            *stage.joint_reference_table.data_offset + row_index * 12;
        const auto marker_root = archive->pointer(marker_row, 64);
        check(marker_root.has_value(),
              "Stadium marker row has no bounded authored root pointer");
        const auto root_entry = std::find_if(
            stage.entries.begin(), stage.entries.end(),
            [&](const auto& entry) {
                return entry.joint_offset &&
                       *entry.joint_offset == *marker_root;
            });
        check(root_entry != stage.entries.end(),
              "Stadium marker root does not resolve to an authored stage entry");
        melee_web::DatNativeJoint joint_owner(archive, *marker_root);
        const auto& graph = joint_owner.graph();
        check(graph.joint_count != 0 && graph.root == 0 &&
                  graph.joints[0].source_offset == *marker_root,
              "Stadium marker row root differs from its checked source joint graph");

        const int32_t signed_pair_count =
            std::bit_cast<int32_t>(archive->be32(marker_row + 8));
        check(signed_pair_count >= 0,
              "Stadium marker row has a negative source signed pair count");
        const uint32_t pair_count = static_cast<uint32_t>(signed_pair_count);
        const uint64_t pair_bytes = uint64_t(pair_count) * 4;
        check(pair_bytes <= std::numeric_limits<size_t>::max(),
              "Stadium marker pair extent exceeds the host archive size type");
        std::optional<uint32_t> pairs;
        if (pair_count)
            pairs = archive->pointer(marker_row + 4,
                                     static_cast<size_t>(pair_bytes));
        check(pair_count == 0 || pairs.has_value(),
              "Stadium marker row pairs are not bounded by the checked archive");

        SourceMarkerRow row;
        row.root_offset = *marker_root;
        row.bindings.reserve(pair_count);
        for (uint32_t i = 0; i < pair_count; ++i) {
            const uint16_t joint_index = archive->be16(*pairs + i * 4);
            const uint16_t marker_id = archive->be16(*pairs + i * 4 + 2);
            check(joint_index < graph.joint_count &&
                      marker_id < source_stage_marker_count(),
                  "Stadium marker pair exceeds its own graph or actual marker-slot bound");
            row.bindings.push_back({joint_index, marker_id});
        }
        rows.push_back(std::move(row));
    }

    melee_web::DatNativeJoint map_joint_owner(archive, selected_root);
    const auto& map_graph = map_joint_owner.graph();
    check(map_graph.joint_count != 0 && map_graph.root == 0 &&
              map_graph.joints[0].source_offset == selected_root,
          "Stadium map1 root differs from its checked source joint graph");
    row_joint_indices.reserve(map_entry.joint_indices.count);
    for (uint32_t i = 0; i < map_entry.joint_indices.count; ++i) {
        const uint32_t at = *map_entry.joint_indices.data_offset +
                            i * map_entry.joint_indices.element_bytes;
        const int16_t index = std::bit_cast<int16_t>(archive->be16(at));
        check(index >= 0 && static_cast<uint16_t>(index) < map_graph.joint_count,
              "Stadium map1 source joint index is outside its checked joint graph");
        row_joint_indices.push_back(static_cast<uint16_t>(index));
    }
    return melee_web::test::stadium_ground::select_source_markers_for_root(
        rows, selected_root);
}

void run_stadium_ground_map1_owner(
    const std::shared_ptr<const melee_web::DatArchive>& archive,
    melee_web::DatNativeMap& map_owner,
    GroundStorageLease& storage_scope)
{
    constexpr int map_id = 1;
    namespace screen = melee_web::test::stadium_screen;
    check(archive && map_owner.map_head() != nullptr,
          "Ground map1 owner probe lost its retained C0 archive/map owner");
    check(storage_scope.owned && storage_scope.payload != nullptr &&
              storage_scope.allocation.live == 1,
          "Ground map1 constructor requires the already-owned source storage lease");

    const melee_web::DatStage source_stage(*archive);
    check(map_id < source_stage.entry_table.count &&
              map_id < source_stage.entries.size() &&
              map_id < melee_web_stadium_c1_ground_map_slot_count(),
          "Stadium map1 is outside an authored map or StageInfo table bound");
    check(std::find(melee_web::test::stadium_resident_ids.begin(),
                    melee_web::test::stadium_resident_ids.end(), map_id) !=
              melee_web::test::stadium_resident_ids.end(),
          "C0 Stadium contract does not retain map1 in this archive");
    const auto& map_entry = source_stage.entries[map_id];
    check(map_entry.index == map_id && map_entry.joint_offset.has_value() &&
              stadium_screen_map_entry_joint(map_owner.map_head(), map_id) != nullptr,
          "Original Ground map1 has no resident source joint root");
    check(map_entry.collision_bindings.count == 0,
          "Stadium map1 has authored collision bindings; original removal is unsafe");
    MeleeWebStadiumC1GroundStageProfile stage_profile{};
    check(melee_web_stadium_c1_ground_map_profile(map_id, &stage_profile) &&
              stage_profile.grkind == Gr_Kind_PStadium &&
              stage_profile.callback_row_present &&
              stage_profile.callback_flags_b2 == 0,
          "Stadium map1 callback row enables a secondary camera");
    check(stage_profile.joint_table_present,
          "Stadium source collision joint table has no owner");
    check(!stage_profile.collision_row_present,
          "Stadium authored GrJoint table contains a map1 collision row");

    std::vector<uint16_t> row_joint_indices;
    const auto marker_selection = checked_ground_source_markers(
        archive, source_stage, map_entry, row_joint_indices);
    const auto& source_markers = marker_selection.map_bindings;
    check(marker_selection.matched_row_count == 0 && source_markers.empty(),
          "Bounded map1 probe requires no authored marker row for its exact root");
    check(melee_web_stadium_c1_stage_object_failures() == 0 &&
              source_stage_registry_empty() && source_stage_gobj_count() == 0,
          "Ground map1 preflight found an existing source Stage GObj owner");

    const auto world = melee_web_gameplay_stats();
    const auto stage_registry_before = source_stage_map_registry_snapshot();
    const auto stage_markers_before = source_stage_marker_snapshot();
    check(std::all_of(stage_markers_before.begin(), stage_markers_before.end(),
                      [](HSD_JObj* marker) { return marker == nullptr; }),
          "Ground map1 constructor refused pre-existing StageInfo marker owners");
    const auto classes_before = screen::live_class_counts();
    const auto pools_before = screen::live_pool_counts();
    const uint32_t gobj_used_before = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    const uint32_t gobjproc_used_before =
        HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
    const uint32_t stage_gobj_count_before = source_stage_gobj_count();
    check(stage_gobj_count_before == storage_scope.stage_gobj_count_before,
          "E8 source setup changed the pre-begin Stage GObj baseline");
    const int scheduler_cycle_before = HSD_GObj_804D783C;
    const uint64_t gameplay_ticks_before = world.ticks;

    HSD_GObj* map_object = nullptr;
    auto cleanup = [&]() {
        if (map_object == nullptr)
            map_object = static_cast<HSD_GObj*>(
                melee_web_stadium_c1_ground_map_lookup(map_id));
        if (map_object != nullptr) {
            MeleeWebStadiumC1GroundMapObjectView ground_view{};
            if (map_object->classifier != HSD_GOBJ_CLASS_STAGE ||
                !melee_web_stadium_c1_ground_map_object_view(
                    map_object->user_data, &ground_view) ||
                ground_view.map_id != map_id) {
                std::cerr << "Ground map1 teardown refused to remove an unowned object\n";
                std::abort();
            }
            if (!melee_web_stadium_c1_ground_map_remove(map_object)) {
                std::cerr << "Ground map1 original removal rejected its owned object\n";
                std::abort();
            }
            map_object = nullptr;
        }
        if (melee_web_stadium_c1_ground_map_lookup(map_id) != nullptr ||
            !source_stage_registry_empty() ||
            source_stage_gobj_count() != stage_gobj_count_before) {
            std::cerr << "Ground map1 teardown left a source Stage GObj owner; preserving Ground storage\n";
            std::abort();
        }
    };

    try {
        MeleeWebGroundMapStorageView storage{};
        check(melee_web_ground_map_storage_read(&storage) &&
                  storage.payload == storage_scope.payload &&
                  storage.requested_bytes == 64,
              "Original Ground storage owner changed before map1 construction");
        MeleeWebSourceMemoryContext before_constructor{};
        MeleeWebSourceMemoryAllocation live_lease{};
        check(melee_web_source_memory_context_read(&before_constructor) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  before_constructor.source_heap_handle ==
                      storage_scope.facts.memory.source_heap_handle &&
                  before_constructor.world_generation ==
                      storage_scope.facts.memory.world_generation &&
                  melee_web_source_memory_allocation_read(
                      storage_scope.payload, &live_lease) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  live_lease.live == 1 &&
                  live_lease.allocation_generation ==
                      storage_scope.allocation.allocation_generation &&
                  live_lease.requested_bytes == storage.requested_bytes &&
                  __OSCurrHeap == storage_scope.facts.os_heap &&
                  HSD_GetHeap() == storage_scope.facts.hsd_heap &&
                  melee_web_gameplay_world_exists() &&
                  melee_web_source_memory_healthy(),
              "E8 preparation changed the original Ground storage lease context");
        const auto* storage_bytes =
            static_cast<const uint8_t*>(storage.payload);
        check(std::all_of(storage_bytes,
                          storage_bytes + storage.requested_bytes,
                          [](uint8_t value) { return value == 0; }),
              "Original Ground storage bytes were not zeroed by Ground_801C0378");

        check(source_stage_registry_empty() &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  ground_dispatch_quiet(),
              "Ground storage preparation changed Stage GObj or dispatch state");
        map_object = static_cast<HSD_GObj*>(
            melee_web_stadium_c1_ground_map_create(map_id));
        check(map_object != nullptr &&
                  melee_web_stadium_c1_ground_map_lookup(map_id) == map_object &&
                  map_object->classifier == HSD_GOBJ_CLASS_STAGE &&
                  map_object->user_data != nullptr && map_object->hsd_obj != nullptr,
              "Original Ground map1 constructor did not publish its exact source object");
        MeleeWebStadiumC1GroundMapObjectView ground_view{};
        check(melee_web_stadium_c1_ground_map_object_view(
                  map_object->user_data, &ground_view) &&
                  ground_view.map_id == map_id &&
                  ground_view.gobj == map_object &&
                  ground_view.camera == nullptr && map_object->render_cb == nullptr &&
                  map_object->gx_link == HSD_GOBJ_GXLINK_NONE,
              "Original Ground map1 object has a different owner, camera, or render link");
        check(source_stage_gobj_count() == stage_gobj_count_before + 1 &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before + 1 &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before + 2,
              "Ground map1 constructor changed the source Stage GObj/process counts unexpectedly");
        check(map_object->proc != nullptr && map_object->proc->child != nullptr &&
                  map_object->proc->child->child == nullptr &&
                  map_object->proc->gobj == map_object &&
                  map_object->proc->child->gobj == map_object &&
                  map_object->proc->on_invoke != nullptr &&
                  map_object->proc->child->on_invoke != nullptr,
              "Original Ground generic processes were not registered exactly once");

        HSD_JObj* const loaded_root =
            static_cast<HSD_JObj*>(map_object->hsd_obj);
        for (uint16_t joint_index : row_joint_indices) {
            HSD_JObj* const indexed = static_cast<HSD_JObj*>(
                melee_web_stadium_c1_ground_map_joint(map_object,
                                                      joint_index));
            check(indexed != nullptr && source_jobj_owned_by(indexed, loaded_root),
                  "Original Ground map1 row joint index did not resolve in its loaded JObj owner");
        }
        for (size_t i = 0; i < source_stage_marker_count(); ++i) {
            auto* current_marker = static_cast<HSD_JObj*>(
                melee_web_stadium_c1_ground_marker_slot(i));
            check(current_marker == stage_markers_before[i],
                  "Ground map1 changed the source StageInfo x280 marker baseline");
        }
        check(source_stage_markers_empty() &&
                  source_stage_marker_snapshot() == stage_markers_before,
              "Ground map1 changed the empty StageInfo x280 marker baseline");
        check(HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Ground generic process or stage callback dispatched during construction");

        check(melee_web_stadium_c1_ground_map_remove(map_object),
              "Original Ground map1 removal rejected its owned object");
        map_object = nullptr;
        check(source_stage_registry_empty() &&
                  source_stage_map_registry_snapshot() == stage_registry_before &&
                  source_stage_marker_snapshot() == stage_markers_before &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before &&
                  HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Original Ground map1 removal did not restore registry/marker/process state");
        cleanup();
        storage_scope.end();
        check(storage_scope.finished && storage_scope.devices_restored &&
                  storage_scope.devices_before.matches() &&
                  source_stage_map_registry_snapshot() == stage_registry_before &&
                  source_stage_marker_snapshot() == stage_markers_before &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before &&
                  screen::live_class_counts() == classes_before &&
                  screen::live_pool_counts() == pools_before &&
                  HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Ground map1 owner cleanup did not restore its exact source baselines");

        const auto device_addresses =
            storage_scope.devices_before.addresses();
        std::cout << "{\"probe\":\"stadium-ground-map1-owner\","
                     "\"scope\":\"one original Ground_GetStageGObj(1)/Ground_801C4A08 lifetime\","
                     "\"ground_buffer\":\""
                  << hex64(reinterpret_cast<uintptr_t>(storage_scope.payload))
                  << "\",\"requested_bytes\":" << storage.requested_bytes
                  << ",\"source_heap\":"
                  << storage_scope.allocation.source_heap_handle
                  << ",\"world_generation\":"
                  << storage_scope.allocation.world_generation
                  << ",\"allocation_generation\":"
                  << storage_scope.allocation.allocation_generation
                  << ",\"ft_device_owner_ids\":[\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[0]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[1]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[2]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[3]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[4]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[5]))
                  << "\"],\"authored_marker_pair_count\":"
                  << marker_selection.authored_pair_count
                  << ",\"map1_matched_marker_pair_count\":"
                  << source_markers.size()
                  << ",\"stage_info_marker_baseline_empty\":true"
                  << ",\"map1_joint_indices\":" << row_joint_indices.size()
                  << ",\"map_id\":1,\"device_bytes_restored\":true,"
                     "\"buffer_retired\":true,\"callback_dispatch\":false,"
                     "\"proc_ticks\":0,\"rendered\":false,"
                     "\"single_constructor_removal\":true}\n";
    } catch (...) {
        try {
            cleanup();
        } catch (...) {
            std::abort();
        }
        throw;
    }
}

melee_web::RuntimeFiles exact_stadium_runtime_union(
    const melee_web::RuntimeFiles& menu_files,
    const std::vector<std::string>& selected_names,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir)
{
    melee_web::RuntimeFiles result = menu_files;
    std::set<std::string, std::less<>> expected_names;
    for (const auto& [name, _] : menu_files) expected_names.insert(name);
    for (const auto& name : selected_names) {
        expected_names.insert(name);
        if (result.contains(name)) continue;

        const std::filesystem::path menu_path = menu_dir / name;
        const std::filesystem::path game_path = game_dir / name;
        const auto path = std::filesystem::is_regular_file(menu_path)
                              ? menu_path
                              : game_path;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error(
                "Missing exact C1 source RuntimeFiles entry: " + name);
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error(
                "Cannot read exact C1 source RuntimeFiles entry: " + name);
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(input)), {});
        if (input.bad())
            throw std::runtime_error(
                "Cannot finish reading exact C1 source RuntimeFiles entry: " +
                name);
        result.emplace(name, std::move(bytes));
    }

    check(result.size() == expected_names.size(),
          "C1 reopened RuntimeFiles is not the exact menu/selection union");
    for (const auto& [name, _] : result)
        check(expected_names.contains(name),
              "C1 reopened RuntimeFiles contains an unselected extra entry");
    return result;
}

void check_stadium_selection_preserved(
    MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& expected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>&
        expected_baseline)
{
    check(host != nullptr &&
              melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY &&
              melee_web_menu_host_source_scene(host) == 0,
          "C1 context preflight changed the closed source SSS selection");
    MeleeWebMenuMatchSelection observed{};
    char error[256]{};
    check(melee_web_menu_host_stadium_c1a_selection(
              host, &observed, error, sizeof(error)), error);
    check(observed.start.rules.stkind == St_Kind_PStadium &&
              std::memcmp(&observed.start, &expected.start,
                          sizeof(expected.start)) == 0,
          "C1 context preflight changed the source-selected StKind 3 payload");
    check(std::memcmp(observed.players, expected.players,
                      sizeof(expected.players)) == 0 &&
              observed.player_count == expected.player_count &&
              observed.random_seed == expected.random_seed &&
              observed.hud_layout == expected.hud_layout &&
              observed.unlocked_characters == expected.unlocked_characters &&
              observed.unlocked_stages == expected.unlocked_stages &&
              observed.save_profile_present == expected.save_profile_present &&
              observed.opening_demo == expected.opening_demo,
          "C1 context preflight changed retained menu save or RNG provenance");
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> baseline{};
    check(melee_web_menu_host_snapshot_card_data(
              host, 1, baseline.data(), baseline.size(), error,
              sizeof(error)), error);
    check(baseline == expected_baseline,
          "C1 context preflight changed the retained save-owner baseline");
}
#endif
void run_results_source_smoke(const melee_web::RuntimeFiles& files,
                              MeleeWebMenuHost* host,
                              const MatchExitInfo& exit_info,
                              uint32_t& seed,
                              uint8_t input_bytes[MELEE_WEB_PAD_STATE_BYTES])
{
    ResultsMatchInfo result{};
    char error[256]{};
    const auto resets=*gmMainLib_GetMatchResetCounter();
    const auto stock_matches=*gmMainLib_GetStockMatchTotal();
    check(melee_web_menu_host_results_begin(host,&exit_info,seed,&result,error,sizeof(error)),error);
    const bool canceled=exit_info.match_end.outcome==OUTCOME_NO_CONTEST;
    check(*gmMainLib_GetMatchResetCounter()==resets+(canceled?1:0)&&
          *gmMainLib_GetStockMatchTotal()==stock_matches+(canceled?0:1),
          "Original VS exit did not update exactly one persistent result counter");
    check(std::memcmp(&result.match_end,&exit_info.match_end,sizeof(result.match_end))==0,
          "VS mode Results entry changed the completed MatchEnd");
    std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> decoded(
        melee_web_pad_state_decode(input_bytes,MELEE_WEB_PAD_STATE_BYTES,error,sizeof(error)),
        melee_web_pad_state_free);
    check(decoded!=nullptr,error);
    melee_web::GameplayResultsSession session(files,result,seed,*decoded);
    check(!melee_web_menu_host_results_exit(host,error,sizeof(error)),
          "Mode exit was accepted before Results scene OnExit");
    float pcm[1068];unsigned audio_phase=0;
    auto tick=[&](const PADStatus pads[4]){
        session.tick(pads);
        audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
        check(melee_web_audio_render(session.audio(),pcm,count,error,sizeof(error)),error);
    };
    PADStatus neutral[4]{};neutral[2].err=neutral[3].err=-1;
    for(unsigned t=0;t<240;t++)tick(neutral);
    for(unsigned t=0;t<600&&!session.requested();t++){
        PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
        // The original Results confirmation is Start; A changes stats pages.
        if(t%90==0)pads[0].button=pads[1].button=PAD_BUTTON_START;
        tick(pads);
    }
    check(session.requested(),"Original Results scene did not request its source exit");
    session.exit_scene();
    check(melee_web_menu_host_results_exit(host,error,sizeof(error)),error);
    check(!melee_web_menu_host_results_exit(host,error,sizeof(error)),
          "Results mode exit ran twice");
    seed=session.random_seed();
    melee_web_pad_state_capture(input_bytes);
    session.close();
    check(melee_web_menu_host_results_end(host,seed,input_bytes,error,sizeof(error)),error);
    if(melee_web_menu_host_results_destination(host)==192){
        const MeleeWebPadState* retained=melee_web_menu_host_input(host);
        check(retained!=nullptr,"Results did not retain Prize PAD input");
        melee_web::GameplayPrizeSession prize(files,host,seed,*retained);
        check(!melee_web_menu_host_prize_exit(host,error,sizeof(error)),
              "Prize mode exited before source confirmation");
        for(unsigned t=0;t<3600&&!prize.requested();++t){
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            if(t>=20&&t%20==0)pads[0].button=PAD_BUTTON_START;
            prize.tick(pads);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(prize.audio(),pcm,count,error,sizeof(error)),error);
        }
        check(prize.requested(),"Original Prize did not finish within the bounded confirmation script");
        prize.exit_scene();
        check(melee_web_menu_host_prize_exit(host,error,sizeof(error)),error);
        check(!melee_web_menu_host_prize_exit(host,error,sizeof(error)),"Prize mode exit ran twice");
        seed=prize.random_seed();melee_web_pad_state_capture(input_bytes);
        std::cout<<"Original Prize confirmed in "<<prize.source_frames()<<" source ticks\n";
        prize.close();
        check(melee_web_menu_host_prize_end(host,seed,input_bytes,error,sizeof(error)),error);
    }
}
void write_player(std::ostream& out,const PlayerInitData& player){
 const unsigned flags_c=(unsigned(player.rumble_enabled)<<7)|(unsigned(player.xC_b1)<<6)|
  (unsigned(player.xC_b2)<<5)|(unsigned(player.xC_b3)<<4)|(unsigned(player.vs_invisible)<<3)|
  (unsigned(player.xC_b5)<<2)|(unsigned(player.xC_b6)<<1)|unsigned(player.xC_b7);
 const unsigned flags_d=(unsigned(player.xD_b0)<<7)|(unsigned(player.xD_b1)<<6)|
  (unsigned(player.xD_b2)<<5)|(unsigned(player.xD_b3)<<4)|(unsigned(player.xD_b4)<<3)|
  (unsigned(player.xD_b5)<<2)|(unsigned(player.xD_b6)<<1)|unsigned(player.xD_b7);
 out<<"{\"ckind\":"<<int(player.ckind)<<",\"slot_type\":"<<unsigned(player.slot_type)
    <<",\"stocks\":"<<int(player.stocks)<<",\"color\":"<<unsigned(player.color)
    <<",\"slot\":"<<unsigned(player.slot)<<",\"spawn\":"<<int(player.x5)
    <<",\"spawn_direction\":"<<int(player.spawn_dir)<<",\"sub_color\":"<<unsigned(player.sub_color)
    <<",\"handicap\":"<<int(player.handicap)<<",\"team\":"<<unsigned(player.team)
    <<",\"nametag\":"<<unsigned(player.nametag)<<",\"flags_c\":"<<flags_c
    <<",\"flags_d\":"<<flags_d<<",\"cpu_kind\":"<<unsigned(player.cpu_kind)
    <<",\"cpu_level\":"<<unsigned(player.cpu_level)<<",\"damage_10\":"<<player.x10
    <<",\"damage_12\":"<<player.x12<<",\"hp\":"<<player.hp
    <<",\"attack_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.attack_ratio))
    <<"\",\"defense_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.defense_ratio))
    <<"\",\"model_scale_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.model_scale))<<"\"}";
}
void write_selection(std::ostream& out,const MeleeWebMenuMatchSelection& selection){
 const auto& rules=selection.start.rules;
 out<<"{\"rules\":{\"match_kind\":"<<unsigned(rules.match_kind)
    <<",\"hud_layout\":"<<unsigned(rules.x0_3)
    <<",\"timer_enabled\":"<<(rules.timer_enabled?"true":"false")
    <<",\"timer_counts_up\":"<<(rules.timer_counts_up?"true":"false")
    <<",\"friendly_fire\":"<<(rules.friendly_fire?"true":"false")
    <<",\"is_stock\":"<<(rules.is_stock?"true":"false")
    <<",\"single_button\":"<<(rules.single_button?"true":"false")
    <<",\"disable_pausing\":"<<(rules.disable_pausing?"true":"false")
    <<",\"is_vs\":"<<(rules.is_vs?"true":"false")
    <<",\"is_teams\":"<<unsigned(rules.is_teams)<<",\"item_frequency\":"<<int(rules.xB)
    <<",\"stage_kind\":"<<rules.stkind<<",\"time_limit\":"<<rules.time_limit
    <<",\"item_mask\":\""<<hex64(rules.x20)<<"\",\"damage_ratio_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.x30))<<"\",\"game_speed_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.game_speed))<<"\"},\"players\":[";
 for(unsigned i=0;i<4;++i){if(i)out<<',';write_player(out,selection.start.players[i]);}
 out<<"]}";
}
class TransitionTrace {
 std::ofstream output;unsigned run_=0,index_=0;std::map<uint64_t,unsigned> epochs;
 unsigned epoch(MeleeWebAudio* audio){
  const uint64_t generation=melee_web_audio_generation(audio);auto found=epochs.find(generation);
  if(found!=epochs.end())return found->second;const unsigned result=epochs.size();epochs[generation]=result;return result;
 }
public:
 explicit TransitionTrace(const char* path,const char* source_revision,
                          const char* input_recipe){
  if(!path)return;const std::string revision=source_revision?source_revision:"";
  if(revision.size()!=40||revision.find_first_not_of("0123456789abcdef")!=std::string::npos)
   throw std::runtime_error("Transition trace requires a full lowercase source revision");
  output.open(path,std::ios::trunc);if(!output)throw std::runtime_error("Cannot open transition trace output");
  output<<"{\"record\":\"header\",\"schema\":\"melee-web-transition-trace\",\"version\":1,"
          "\"producer\":\"port\",\"game_revision\":\"GALE01r2\",\"source_revision\":\""
        <<revision<<"\",\"build_configuration\":\"browser-release\"";
  if(input_recipe)output<<",\"input_recipe\":\""<<input_recipe<<"\"";
  output<<"}\n";
 }
 void begin_run(unsigned run){run_=run;index_=0;epochs.clear();}
 void event(const char* name,MeleeWebAudio* audio,const char* route=nullptr,
            const MeleeWebMenuMatchSelection* selection=nullptr,const uint32_t* rng=nullptr){
  if(!output)return;const auto stream=stream_name(audio);
  output<<"{\"record\":\"event\",\"run\":"<<run_<<",\"index\":"<<index_++
        <<",\"event\":\""<<name<<"\",\"audio\":{\"active\":"
        <<(!stream.empty()?"true":"false")<<",\"owner_epoch\":"<<epoch(audio)
        <<",\"stream\":\""<<stream<<"\"}";
  if(route)output<<",\"route\":\""<<route<<"\"";
  const uint32_t* event_rng=rng?rng:seed_ptr;
  if(event_rng)output<<",\"rng\":"<<*event_rng;
  if(selection){
   output<<",\"selection\":";
   write_selection(output,*selection);
  }
  output<<"}\n";output.flush();
 }
};

void replay_start_marker(const char* marker, int value = -1) {
    std::cout << "{\"record\":\"b4_replay_start_marker\",\"marker\":\""
              << marker << "\"";
    if (value >= 0) std::cout << ",\"value\":" << value;
    std::cout << "}\n" << std::flush;
}

void run_v10_css_replay_start_prefix(const melee_web::RuntimeFiles& files,
                                     const char* recipe_path,
                                     TransitionTrace& trace) {
    std::ifstream input(recipe_path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open the exact MWRC v10 reducer recipe");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
    auto recipe = melee_web::read_retail_replay(bytes);
    check(recipe.version == melee_web::kRetailReplayFighterVersion &&
              recipe.seed == 3336171383U && recipe.frames.size() == 50394 &&
              bytes.size() == 2241622 && recipe.initial_css && recipe.initial_input &&
              recipe.match_setups.size() == melee_web::kRetailReplayMaxMatchSetups &&
              !recipe.spans.empty() &&
              recipe.spans.front().scene == melee_web::kRetailReplayCss &&
              recipe.spans.front().first_frame == 0,
          "Reducer input is not the complete frozen MWRC v10 CSS-first recipe");
    replay_start_marker("full_v10_recipe_verified");

    char error[256]{};
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    std::unique_ptr<melee_web::GameplayMenuWorld> world;
    bool host_entered = false;
    auto best_effort_teardown = [&]() noexcept {
        if (host && host_entered) {
            char cleanup_error[256]{};
            (void)melee_web_menu_host_leave(host, 1, cleanup_error,
                                            sizeof(cleanup_error));
            host_entered = false;
        }
        if (world) {
            try { world->verify_immutable_archives(); } catch (...) {}
            try { world->close(); } catch (...) {}
            world.reset();
        }
        if (host) {
            char cleanup_error[256]{};
            (void)melee_web_menu_host_destroy(host, cleanup_error,
                                             sizeof(cleanup_error));
            host = nullptr;
        }
    };

    try {
        world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_apply_replay_context(
                  host, recipe.seed, recipe.pad_bytes.data(),
                  recipe.initial_css->css_data.data(),
                  recipe.initial_css->ko_counts.data(),
                  recipe.initial_css->game_rules.data(),
                  recipe.initial_css->save_data.data(), error, sizeof(error)),
              error);
        replay_start_marker("fresh_css_enter_begin");
        check(melee_web_menu_host_enter(host, world->audio(), error,
                                        sizeof(error)), error);
        host_entered = true;
        check(melee_web_menu_host_source_scene(host) ==
                  MELEE_WEB_MENU_HOST_SCENE_CSS,
              "Fresh reducer owner did not enter original CSS");
        trace.begin_run(0);
        trace.event("fresh_css_enter_complete", world->audio());
        replay_start_marker("fresh_css_enter_complete");

        replay_start_marker("retail_replay_session_initial_begin");
        melee_web::retail_replay_session_initial(recipe);
        std::cout << std::flush;
        replay_start_marker("retail_replay_session_initial_returned");

        float pcm[1068]{};
        unsigned audio_phase = 0;
        replay_start_marker("original_css_host_tick_begin");
        const int tick_result = melee_web_menu_host_tick(
            host, recipe.frames[0].pads.data(), error, sizeof(error));
        check(tick_result == 1 || tick_result == 3, error);
        trace.event("original_css_host_tick_returned", world->audio());
        replay_start_marker("original_css_host_tick_returned", tick_result);

        replay_start_marker("retail_replay_frame_css_begin");
        melee_web::retail_replay_frame(recipe, 0,
                                       melee_web::kRetailReplayCss);
        std::cout << std::flush;
        replay_start_marker("retail_replay_frame_css_returned");

        replay_start_marker("ordinary_audio_boundary_begin");
        audio_phase += 32000;
        const unsigned samples = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world->audio(), pcm, samples, error,
                                     sizeof(error)), error);
        trace.event("ordinary_audio_boundary_returned", world->audio());
        replay_start_marker("ordinary_audio_boundary_returned",
                            static_cast<int>(samples));

        replay_start_marker("source_teardown_begin");
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        host_entered = false;
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
        replay_start_marker("source_teardown_returned");
        std::cout << "{\"record\":\"b4_replay_start_prefix\","
                     "\"result\":\"bounded_one_frame_returned\","
                     "\"recipe_version\":10,\"recipe_frames\":50394,"
                     "\"source_frames_consumed\":1,\"draws\":0,"
                     "\"full_session_comparison\":false}\n" << std::flush;
    } catch (...) {
        best_effort_teardown();
        throw;
    }
}

void run_title_main_abort_smoke(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    float pcm[1068]{};
    const void* saved_scene_info = melee_web_current_scene_info();

    auto check_full_roster = [] {
        const uint16_t expected =
            static_cast<uint16_t>((1U << NUM_UNLOCKABLE_CHARACTERS) - 1U);
        check(expected == 0x07ff,
              "Pinned source unlock table no longer declares the existing 0x07ff roster");
        check(*gmMainLib_GetUnlockedCharactersBitmaskPtr() == expected,
              "Original fresh menu profile did not retain the all-unlocked character mask");
        for (int index = 0; index < NUM_UNLOCKABLE_CHARACTERS; ++index) {
            check(gm_IsCKindUnlocked(gm_GetCKindByUnlockIndex(index)),
                  "A source-unlocked character is unavailable to original CSS");
        }
    };

    auto tick = [&](MeleeWebMenuHost* host,
                    melee_web::GameplayMenuWorld& world, PADStatus raw[4],
                    unsigned& audio_phase) {
        const int result = melee_web_menu_host_tick(host, raw, error,
                                                    sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world.audio(), pcm, count, error,
                                     sizeof(error)), error);
        return result;
    };
    auto start_title = [&](MeleeWebMenuHost*& host,
                           std::unique_ptr<melee_web::GameplayMenuWorld>& world,
                           PADStatus raw[4], unsigned& audio_phase) {
        host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        raw[2].err = raw[3].err = -1;
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original title left before its Start input");
    };

    auto abort_and_destroy = [&](MeleeWebMenuHost* host,
                                 std::unique_ptr<melee_web::GameplayMenuWorld>& world,
                                 unsigned& audio_phase, const char* label) {
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        check(melee_web_menu_host_source_scene(host) == 0,
              "Eject did not retire the source-scene lease");
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        check(melee_web_current_scene_info() == saved_scene_info,
              "Eject did not restore the caller's GameSceneInfo owner");
        check(!melee_web_gameplay_world_exists(),
              "Eject retained the source SDK world");

        auto* css_host = melee_web_menu_host_create(error, sizeof(error));
        check(css_host != nullptr, error);
        auto css_world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_enter(css_host, css_world->audio(), error,
                                        sizeof(error)), error);
        check(melee_web_menu_host_source_scene(css_host) == 1,
              "A new source session did not enter original CSS after Eject");
        check_full_roster();
        PADStatus neutral[4]{};
        neutral[2].err = neutral[3].err = -1;
        for (unsigned frame = 0; frame < 4; ++frame) {
            check(tick(css_host, *css_world, neutral, audio_phase) == 1, error);
        }
        check(melee_web_menu_host_leave(css_host, 1, error, sizeof(error)), error);
        css_world->close();
        css_world.reset();
        check(melee_web_menu_host_destroy(css_host, error, sizeof(error)), error);
        check(melee_web_current_scene_info() == saved_scene_info,
              "CSS re-entry teardown changed the caller's GameSceneInfo owner");
        std::cout << "Original " << label
                  << " Eject released source ownership and allowed CSS re-entry\n";
    };

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        abort_and_destroy(host, world, audio_phase, "Title");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        check_full_roster();
        raw[0].button = PAD_BUTTON_START;
        int result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        check(result == 3, "Original Title did not expose its Start route");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
              "Source Title Start did not preserve its GM_MENU destination");
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original Main left before its Back input");
        raw[0].button = PAD_BUTTON_B;
        result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        check(result == 3, "Original Main did not expose its Back route");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
              "Source Main Back did not preserve its GM_TITLE destination");
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Returned Title left before Eject");
        abort_and_destroy(host, world, audio_phase,
                          "Title/Main route before Eject");
    }

    {
        MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        std::unique_ptr<melee_web::GameplayMenuWorld> world =
            std::make_unique<melee_web::GameplayMenuWorld>(
                files, melee_web::GameplayMenuScene::Title);
        PADStatus raw[4]{};
        raw[2].err = raw[3].err = -1;
        unsigned audio_phase = 0;
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        check_full_roster();

        /* A held Start edge during the source guard is consumed there. It
         * cannot be replayed as a synthetic Start after the guard expires. */
        raw[1].button = PAD_BUTTON_START;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Held P2 Start bypassed the original Title input guard");
        raw[1].button = 0;
        check(tick(host, *world, raw, audio_phase) == 1,
              "Title left while Player 2 released Start");
        raw[1].button = PAD_BUTTON_START;
        check(raw[0].button == 0,
              "Player 1 must stay neutral in the Player 2 Title route test");
        check(tick(host, *world, raw, audio_phase) == 3,
              "Original Title did not accept a fresh Start edge from Player 2");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
              "Player 2 Title Start did not preserve the source GM_MENU destination");
        raw[1].button = 0;
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        abort_and_destroy(host, world, audio_phase, "Player 2 Title route");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        /* Recreate an unclaimed character unlock through source state
         * synchronization and award routines. The resulting notification is
         * genuinely pending and Title therefore requests Challenger Approach. */
        const u8 ckind = gm_GetCKindByUnlockIndex(0);
        gm_80164A0C(ckind);
        gm_801729EC();
        gm_UnlockCKind(static_cast<CharacterKind>(ckind));
        check(gm_801721EC(),
              "Original character unlock routine did not create pending source work");
        check_full_roster();
        raw[1].button = PAD_BUTTON_START;
        int result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[1].button = 0;
        check(result == 3, "Original Title did not expose its Start route");
        check(!melee_web_menu_host_leave(host, 0, error, sizeof(error)) &&
                  std::string(error).find("unsupported destination 20") != std::string::npos,
              "An unsupported Title destination was not rejected explicitly");
        check(std::string(error).find("buttons 0x") != std::string::npos,
              "Unsupported Challenger route did not retain the source-written Title payload");
        abort_and_destroy(host, world, audio_phase,
                          "unsupported Title route recovery");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        int result = 1;
        unsigned callbacks_to_timeout = 0;
        for (; callbacks_to_timeout < 700 && result != 3;
             ++callbacks_to_timeout)
            result = tick(host, *world, raw, audio_phase);
        check(result == 3, "Original Title timeout did not request its source exit");
        check(callbacks_to_timeout == 501,
              "Title timeout diverged from the 20-frame guard and 601-frame source timer");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_OPENING_MV,
              "Title timeout did not preserve the source GM_OPENING_MV destination");
        check(melee_web_menu_host_route_target_state(host) == 1,
              "Title timeout did not preserve the source VS-demo state selected by Title");
        world->verify_immutable_archives();
        world->close();
        world.reset();

        /* Reduce the supported Opening VS handoff to its deterministic
         * boundary.  The retail demo match itself needs its separate match
         * assets and capture; this fixture verifies the source-selected
         * payload, the host's suspend boundary, and the retained PAD owner
         * before retiring the route. */
        MeleeWebOpeningPreview opening_preview{};
        check(melee_web_menu_host_opening_preview(host, &opening_preview,
                                                  error, sizeof(error)), error);
        auto opening_world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_opening(host, opening_world->audio(),
                                                error, sizeof(error)), error);
        MeleeWebMenuMatchSelection opening_selection{};
        check(melee_web_menu_host_opening_selection(
                  host, &opening_selection, error, sizeof(error)), error);
        check(opening_selection.opening_demo == 1 &&
                  opening_selection.player_count == 4 &&
                  opening_selection.start.rules.stkind == opening_preview.stage_kind &&
                  opening_selection.start.rules.match_kind == opening_preview.match_kind,
              "Opening VS handoff did not preserve its source-selected demo payload");
        const unsigned expected_opening_stocks =
            opening_selection.start.rules.match_kind == 1 ? 99u : 0u;
        for (unsigned i = 0; i < opening_selection.player_count; ++i) {
            const PlayerInitData& player = opening_selection.start.players[i];
            check(player.slot_type == Gm_PKind_Cpu && player.cpu_kind == CpuKind_4 &&
                      player.cpu_level == 9 && player.stocks == expected_opening_stocks,
                  "Opening VS handoff changed its authored four-CPU setup");
        }
        check(melee_web_menu_host_opening_input(host) == nullptr,
              "Opening VS exposed retained PAD input before source suspend");
        uint8_t opening_input_bytes[MELEE_WEB_PAD_STATE_BYTES];
        melee_web_pad_state_capture(opening_input_bytes);
        check(melee_web_menu_host_opening_match_suspend(host, error, sizeof(error)),
              error);
        const MeleeWebPadState* opening_input =
            melee_web_menu_host_opening_input(host);
        check(opening_input != nullptr,
              "Opening VS suspend did not retain a PAD input owner");
        opening_world->verify_immutable_archives();
        opening_world->close();
        opening_world.reset();
        check(melee_web_menu_host_opening_match_finish(
                  host, 0x13579bdfU, opening_input_bytes, error, sizeof(error)),
              error);
        check(melee_web_menu_host_opening_target_state(host) == 2 &&
                  melee_web_menu_host_opening_input(host) == nullptr,
              "Opening VS finish did not release the completed state owner");
        auto continuation_world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_opening(
                  host, continuation_world->audio(), error, sizeof(error)), error);
        check(melee_web_menu_host_opening_target_state(host) == 2 &&
                  melee_web_menu_host_source_scene(host) ==
                      MELEE_WEB_MENU_HOST_SCENE_TITLE,
              "Opening VS finish could not enter the authored next Title state");
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        continuation_world->verify_immutable_archives();
        continuation_world->close();
        continuation_world.reset();
        check(melee_web_menu_host_opening_input(host) == nullptr,
              "Opening next-state cleanup retained a PAD input route");
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
        check(melee_web_current_scene_info() == saved_scene_info,
              "Opening-mode handoff did not restore its caller GameSceneInfo owner");
        check(!melee_web_gameplay_world_exists(),
              "Opening-mode handoff retained the Title SDK world");

        std::cout << "Original Opening VS handoff selected four CPUs, suspended with retained PAD input, and cleaned up\n";

        /* An unsupported next owner can retire the completed Title world and
         * import the disc again without a page reload. */
        auto* css_host = melee_web_menu_host_create(error, sizeof(error));
        check(css_host != nullptr, error);
        auto css_world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_enter(css_host, css_world->audio(), error,
                                        sizeof(error)), error);
        check(melee_web_menu_host_source_scene(css_host) == 1,
              "Disc reimport after Title idle did not start at original CSS");
        check_full_roster();
        for (unsigned frame = 0; frame < 4; ++frame)
            check(tick(css_host, *css_world, raw, audio_phase) == 1,
                  "CSS after Title idle left before Eject");
        check(melee_web_menu_host_leave(css_host, 1, error, sizeof(error)), error);
        css_world->close();
        css_world.reset();
        check(melee_web_menu_host_destroy(css_host, error, sizeof(error)), error);
        std::cout << "Original Title timeout selected GM_OPENING_MV state 1 and allowed clean CSS reimport\n";
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        /* Establish a checked Main entry fixture independently of the retail
         * Title exit route.  The retail profile can legitimately request the
         * unsupported Challenger Approach route from Title Start. */
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        check(melee_web_vs_mode_set_route(GM_MENU, GM_TITLE),
              "Could not establish the supported GM_MENU test route");
        world->rebuild_scene(melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original Main left before Eject");
        abort_and_destroy(host, world, audio_phase, "Main");
    }

    {
        auto* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        auto world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        PADStatus raw[4]{};
        raw[2].err = raw[3].err = -1;
        unsigned audio_phase = 0;
        check(melee_web_menu_host_enter(host, world->audio(), error,
                                        sizeof(error)), error);
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original CSS left before the normal-close regression");
        raw[0].button = PAD_BUTTON_START;
        int result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        check(result == 3, "Original CSS did not complete its SSS transition");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_phase(host) == 2,
              "Normal CSS leave did not retain the closed SSS-ready session");
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        check(!melee_web_gameplay_world_exists(),
              "Normal CSS leave retained the owned source world");
        std::cout << "Normal CSS->SSS leave cleared its consumed transition before host teardown\n";
    }
    std::cout << "Original all-unlocked CSS roster, P1/P2 Title Start edges, unsupported Challenger, Title timeout to Opening state 1 and recovery passed\n";
}

struct SoundRouteTick {
    int result;
    unsigned audio_phase;
};

SoundRouteTick sound_route_tick(MeleeWebMenuHost* host,
                                melee_web::GameplayMenuWorld& world,
                                PADStatus raw[4], char* error,
                                float pcm[1068], unsigned audio_phase)
{
    const int result = melee_web_menu_host_tick(host, raw, error, 256);
    check(result == 1 || result == 3, error);
    audio_phase += 32000;
    const unsigned count = audio_phase / 60;
    audio_phase %= 60;
    if (!melee_web_audio_render(world.audio(), pcm, count, error, 256)) {
        std::cerr << "sound-route: audio render rejected result=" << result
                  << " frames=" << count
                  << " audio_generation=" << melee_web_audio_generation(world.audio())
                  << " menu_phase=" << melee_web_menu_host_phase(host)
                  << " source_scene=" << melee_web_menu_host_source_scene(host)
                  << " error=" << error << "\n";
        throw std::runtime_error(error);
    }
    return {result, audio_phase};
}

unsigned sound_route_neutral(MeleeWebMenuHost* host,
                             melee_web::GameplayMenuWorld& world,
                             PADStatus raw[4], char* error, float pcm[1068],
                             unsigned audio_phase, unsigned frames)
{
    raw[0].button = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto tick = sound_route_tick(host, world, raw, error, pcm,
                                           audio_phase);
        audio_phase = tick.audio_phase;
        check(tick.result == 1,
              "Original menu exited during a neutral guard");
    }
    return audio_phase;
}

unsigned sound_route_press(MeleeWebMenuHost* host,
                           melee_web::GameplayMenuWorld& world,
                           PADStatus raw[4], char* error, float pcm[1068],
                           unsigned audio_phase, u16 button)
{
    audio_phase = sound_route_neutral(host, world, raw, error, pcm,
                                      audio_phase, 8);
    raw[0].button = button;
    const auto tick = sound_route_tick(host, world, raw, error, pcm,
                                       audio_phase);
    audio_phase = tick.audio_phase;
    check(tick.result == 1,
          "Original menu exited while sampling an input edge");
    return sound_route_neutral(host, world, raw, error, pcm, audio_phase, 8);
}

void run_main_sound_mix_route(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    float pcm[1068]{};
    unsigned audio_phase = 0;
    const void* saved_scene_info = melee_web_current_scene_info();
    MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    PADStatus raw[4]{};
    raw[2].err = raw[3].err = -1;
    std::unique_ptr<melee_web::GameplayMenuWorld> world =
        std::make_unique<melee_web::GameplayMenuWorld>(files);
    check(melee_web_menu_host_enter(host, world->audio(), error,
                                    sizeof(error)), error);

    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R;
    auto route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                       audio_phase);
    int result = route_tick.result;
    audio_phase = route_tick.audio_phase;
    raw[0].button = 0;
    check(result == 3, "Original CSS did not accept its LR+Start parent route");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Original CSS LR+Start lost its GM_MENU destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    check(mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
              mn_804A04F0.hovered_selection == SEL_MAIN_1P,
          "CSS parent return did not enter the original Main 1P root");
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);

    raw[0].button = PAD_BUTTON_B;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Main Back did not reach Title before Sound entry");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
          "Initial Main Back lost its GM_TITLE destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                         sizeof(error)), error);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Title Start did not return to Main before Sound entry");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Initial Title Start lost its GM_MENU destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);

    check(gmMainLib_8015ED74() == 0,
          "Fresh source profile did not initialize sound balance to its authored center");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_LEFT);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Sound callback did not decrement the saved mix by five units");

    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    check(melee_web_menu_host_source_scene(host) == 4,
          "Sound Back did not remain in the original Main scene");
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Sound Back did not retain the source-written saved mix");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 12);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Re-entering original Sound changed the saved mix");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    check(melee_web_menu_host_source_scene(host) == 4,
          "Sound route Back navigation did not return to original Main");

    raw[0].button = PAD_BUTTON_B;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Main Back did not return to Title");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
          "Original Main Back lost its GM_TITLE destination");
    std::vector<uint8_t> sound_card(MELEE_WEB_SAVE_PROFILE_CARD_BYTES);
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, sound_card.data(), sound_card.size(), error, sizeof(error)), error);
    check(sound_card[0x45C] == static_cast<uint8_t>(-5),
          "Closed Main snapshot lost the source Sound balance");

    world->verify_immutable_archives();
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                         sizeof(error)), error);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Main-to-Title transition lost the source Sound balance");
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Returned Title did not accept its original Start route");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Returned Title Start lost its GM_MENU destination");
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, sound_card.data(), sound_card.size(), error, sizeof(error)), error);
    check(sound_card[0x45C] == static_cast<uint8_t>(-5),
          "Closed Title snapshot lost the source Sound balance");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Title-to-Main transition lost the source Sound balance");
    check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
    world->verify_immutable_archives();
    world->close();
    world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    check(melee_web_current_scene_info() == saved_scene_info,
          "Original Sound route did not restore its caller scene owner");
    check(!melee_web_gameplay_world_exists(),
          "Original Sound route retained its native world after teardown");
    std::cout << "Original Main Settings Sound changed SaveData mix to -5, retained through Title/Main, returned, re-entered, and cleaned up\n";
}

void run_opening_movie_preload_smoke(melee_web::RuntimeFiles files)
{
    char error[256]{};
    float pcm[1068]{};
    const void* saved_scene_info = melee_web_current_scene_info();
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    auto title_world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, title_world->audio(), error,
                                          sizeof(error)), error);
    PADStatus raw[4]{};
    raw[2].err = raw[3].err = -1;
    unsigned audio_phase = 0;
    for (unsigned frame = 0; frame < 120; ++frame) {
        check(melee_web_menu_host_tick(host, raw, error, sizeof(error)) == 1,
              error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(title_world->audio(), pcm, count, error,
                                     sizeof(error)), error);
    }
    int result = 1;
    unsigned callbacks_to_timeout = 0;
    for (; callbacks_to_timeout < 700 && result == 1; ++callbacks_to_timeout) {
        result = melee_web_menu_host_tick(host, raw, error, sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(title_world->audio(), pcm, count, error,
                                     sizeof(error)), error);
    }
    check(result == 3,
          "Opening movie probe did not request its authored Opening route");
    check(callbacks_to_timeout == 501,
          "Opening movie probe diverged from the source Title timeout");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_source_scene(host) == 0,
          "Opening movie probe did not retire its Title source owner at timeout");
    check(melee_web_menu_host_route_target_mode(host) == GM_OPENING_MV,
          "Opening movie probe lost its authored Opening route");
    check(melee_web_vs_mode_select_state(0),
          "Opening movie probe could not select authored state 0");
    title_world->close();
    title_world.reset();

    auto movie_world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(!melee_web_menu_host_enter_opening(host, movie_world->audio(), error,
                                             sizeof(error)),
          "Opening movie probe entered without a source heap owner");
    check(std::string(error).find("active source lbMemory/lbHeap owner") !=
              std::string::npos,
          "Opening movie probe did not reject its missing source heap owner explicitly");
    check(melee_web_menu_host_source_scene(host) == 0,
          "Rejected Opening movie probe retained a source scene owner");
    movie_world->verify_immutable_archives();
    movie_world->close();
    movie_world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    check(melee_web_current_scene_info() == saved_scene_info,
          "Opening movie probe did not restore its caller scene owner");
    check(!melee_web_gameplay_world_exists(),
          "Opening movie probe retained its source world");
    std::cout << "Original Opening movie route selected state 0 and rejected missing source heap/cache ownership explicitly; no movie decode or retail-route claim\n";
}

void run_trophy_baseline_smoke(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    auto world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_initialize_profile_baseline(
              host, error, sizeof(error)), error);

    const auto* save = gmMainLib_GetSaveData();
    check(save->trophy_count == TY_TROPHY_COUNT,
          "Original TyDatai-backed Everything baseline omitted trophies");
    for (size_t trophy = 0; trophy < TY_TROPHY_COUNT; ++trophy)
        check((save->trophy_flags[trophy] & 0x8000) != 0 &&
                  (save->trophy_flags[trophy] & 0x00FF) == 1,
              "Original TyDatai-backed trophy award diverged from Toy_SetUnlockState");
    check(gm_80164ABC() && gm_80164600() &&
              save->x1A68 == ((UINT64_C(1) << 51) - 1) &&
              gmMainLib_8015CF94(),
          "Original source roster, stage, or event baseline was not initialized");
    check(save->unk_1A8.x4 && save->unk_1A8.x5 && save->unk_1A8.x6,
          "Original source completion flags were not initialized");

    check(!gm_801721EC(),
          "Everything baseline retained transient new-completion notifications");
    check((save->x186C & 0x0F) == 0x0F && (save->x186C & 0xF0) == 0,
          "Everything baseline did not derive only the four source-supported feature bits");
    for (int selkind = 0; selkind < SELKIND_COUNT; ++selkind) {
        const auto ckind = static_cast<CharacterKind>(gm_SelKindToCKind((u8) selkind));
        const int clear_ids[] = {
            gm_80160474(ckind, GM_CLASSIC),
            gm_80160474(ckind, GM_ADVENTURE),
            gm_80160474(ckind, GM_ALLSTAR),
        };
        for (size_t mode = 0; mode < sizeof(clear_ids) / sizeof(clear_ids[0]); ++mode)
                check(gmMainLib_8015DA90(clear_ids[mode]) != 0,
                      "Everything baseline omitted a source-mapped 1P reward from the persisted ledger");
    }
    {
        size_t completed_challenges = 0;
        for (int challenge = 0; challenge < 0x100; ++challenge) {
            const int excluded = challenge == 9 || challenge == 0x29 ||
                challenge == 0x42 || challenge == 0x43 ||
                challenge == 0xB9 || challenge == 0xC9 || challenge == 0xCA;
            check((gmMainLib_8015DADC(challenge) != 0) == !excluded,
                  "Everything baseline challenge flags diverged from the source inventory");
            completed_challenges += !excluded;
        }
        check(completed_challenges == 249 && gmMainLib_8015D8D8(0x123),
              "Everything baseline omitted the source all-challenges award");
    }

    world->verify_immutable_archives();
    world->close_prepared();
    world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    std::cout << "Original TyDatai-backed save baseline initialized after source-file ownership; "
                 "trophy and source unlock tables passed\n";
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
uint32_t stadium_archive_symbol_offset(const melee_web::DatArchive& archive,
                                       const char* name)
{
    const auto found = std::find_if(
        archive.public_symbols().begin(), archive.public_symbols().end(),
        [name](const auto& symbol) { return symbol.name == name; });
    if (found == archive.public_symbols().end())
        throw melee_web::DatError(std::string("Missing Stadium source public root: ") +
                                  name);
    return found->data_offset;
}

void run_stadium_e8_request(
    const melee_web::RuntimeFiles& reopened_files,
    MeleeWebMenuHost* host,
    melee_web::GameplayMenuWorld* world,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before,
    bool perform_ground_map1_owner,
    TransitionTrace& trace)
{
    using namespace melee_web;
    char error[256]{};
    check(host && world && reopened_files.contains("GrPs.usd"),
          "E8 request requires its retained source host and exact GrPs.usd entry");
    const auto& raw_bytes = reopened_files.at("GrPs.usd");
    const std::vector<std::uint8_t> raw_before = raw_bytes;
    auto archive = std::make_shared<const DatArchive>(
        raw_bytes, DatExternalPolicy::ResolveNull);
    const std::uint32_t ground_root =
        stadium_archive_symbol_offset(*archive, "grGroundParam");
    const std::uint32_t itemdata_root =
        stadium_archive_symbol_offset(*archive, "itemdata");
    const std::uint32_t yaku_root =
        stadium_archive_symbol_offset(*archive, "ALDYakuAll");
    const std::uint32_t yakumono_root =
        stadium_archive_symbol_offset(*archive, "yakumono_param");
    (void)stadium_archive_symbol_offset(*archive, "map_plit");
    const std::uint32_t quake_root =
        stadium_archive_symbol_offset(*archive, "quake_model_set");

    check(archive->be32(itemdata_root) == 0 &&
              !archive->has_relocation(itemdata_root) &&
              !archive->pointer(itemdata_root, 4),
          "C0 itemdata root is not the authored null word");
    const std::vector<std::uint8_t> archive_data_before(
        archive->data().begin(), archive->data().end());
    check(melee_web_stadium_c1_stage_object_failures() == 0,
          "E8 pre-request context contains stage objects or published item/light roots");

    MeleeWebStadiumC1StageInfoSnapshot* snapshot = nullptr;
    MeleeWebStageMap* stage_map = nullptr;
    void* previous_ground_param = nullptr;
    bool ground_param_published = false;
    bool effect_bank_attached = false;
    bool stage_selection_owned = false;
    bool observer_window_owned = false;
    bool cleanup_complete = false;
    std::unique_ptr<NativeDatArena> scalar_owner;
    std::unique_ptr<DatNativeMap> map_owner;
    std::unique_ptr<DatStageYaku> random_yaku;
    std::unique_ptr<DatEffectBanks> effects;
    std::unique_ptr<DatScene> quake;
    std::unique_ptr<DatStageItems> items;
    MeleeWebStadiumE8CallObservation observed{};
    MeleeWebStadiumC1StageInfoView before_view{};
    MeleeWebStadiumC1StageInfoView after_view{};
    std::unique_ptr<GroundStorageLease> ground_storage;
    void* ground_data = nullptr;
    void* yakumono_data = nullptr;
    uint32_t seed_before = 0;
    const uint32_t* seed_owner = seed_ptr;
    check(seed_owner != nullptr,
          "E8 request lost the source seed owner before preparation");
    seed_before = *seed_owner;
    const char* grps_resolved_name = lbFileGetFullName("/GrPs");
    check(grps_resolved_name &&
              std::strcmp(grps_resolved_name, "/GrPs.usd") == 0,
          "US source resolution did not preserve the authored /GrPs.usd path");

    auto cleanup = [&]() {
        if (observer_window_owned) {
            MeleeWebStadiumE8CallObservation discarded{};
            check(melee_web_stadium_e8_call_observer_end(&discarded),
                  "Could not close the E8 source-call observation window");
            observer_window_owned = false;
        }
        if (ground_storage) ground_storage->end();
        if (effect_bank_attached) {
            check(melee_web_effect_bank_detach(effects->bank(), error,
                                                sizeof(error)), error);
            effect_bank_attached = false;
        }
        if (ground_param_published) {
            MeleeWebStadiumC1StageInfoView current_view{};
            check(melee_web_stadium_c1_stage_info_current_view(&current_view) &&
                      current_view.param == ground_data,
                  "E8 cleanup refused to overwrite a replaced GroundParam owner");
            void* const detached_ground_param =
                melee_web_ground_data_publish(previous_ground_param);
            ground_param_published = false;
            check(detached_ground_param == ground_data,
                  "Could not restore the prior source GroundParam owner");
        }
        if (stage_map) {
            grDatFiles_801C6288();
            check(melee_web_stage_map_close(stage_map, error, sizeof(error)),
                  error);
            stage_map = nullptr;
        }
        if (stage_selection_owned) {
            check(melee_web_stage_selection_end(),
                  "E8 request lost the source stage-selection scope");
            stage_selection_owned = false;
        }
        if (snapshot) {
            check(melee_web_stadium_c1_stage_info_snapshot_restore(
                      snapshot, error, sizeof(error)), error);
            check(melee_web_stadium_c1_stage_info_snapshot_release(
                      snapshot, error, sizeof(error)), error);
            snapshot = nullptr;
        }
        items.reset();
        quake.reset();
        effects.reset();
        random_yaku.reset();
        map_owner.reset();
        scalar_owner.reset();
        archive.reset();
        cleanup_complete = true;
    };

    try {
        if (perform_ground_map1_owner) {
            ground_storage = std::make_unique<GroundStorageLease>();
            ground_storage->begin();
        }
        snapshot = melee_web_stadium_c1_stage_info_snapshot_begin(
            error, sizeof(error));
        check(snapshot != nullptr, error);
        check(melee_web_stadium_c1_stage_info_snapshot_view(
                  snapshot, &before_view),
              "Could not inspect the full source StageInfo snapshot");
        check(before_view.itemdata == nullptr && before_view.map_plit == nullptr,
              "E8 route requires the source-authored empty item/light roots");

        scalar_owner = std::make_unique<NativeDatArena>(archive);
        ground_data = melee_web_ground_data_decode(
            scalar_owner->reader(), ground_root);
        yakumono_data = melee_web_stadium_yakumono_decode(
            scalar_owner->reader(), yakumono_root);
        check(ground_data != nullptr && yakumono_data != nullptr,
              "C0 typed scalar owners did not decode the Stadium roots");
        map_owner = std::make_unique<DatNativeMap>(
            archive, test::stadium_contract);
        check(map_owner->map_head() != nullptr && map_owner->collision() != nullptr,
              "C0 typed map owner did not decode map_head/coll_data");
        random_yaku = std::make_unique<DatStageYaku>(archive, yaku_root);
        check(random_yaku->native_data() != nullptr,
              "C0 ALDYakuAll typed owner is absent");
        effects = std::make_unique<DatEffectBanks>(
            archive, "map_ptcl", "map_texg", 0x40);
        check(effects->command_root() != nullptr &&
                  effects->texture_root() != nullptr,
              "C0 map_ptcl/map_texg typed owner is absent");
        quake = std::make_unique<DatScene>(
            archive, "quake_model_set", DatSceneRootKind::DynamicModel);
        check(quake->single_model() != nullptr && quake->model_count() == 1,
              "C0 quake_model_set typed owner is absent");
        items = std::make_unique<DatStageItems>(archive);
        check(items->items().empty(),
              "C0 authored-null itemdata unexpectedly decoded stage items");
        check(melee_web_stadium_c1_stage_object_failures() == 0,
              "E8 typed preparation published a stage object or item/light root");

        const std::vector<MeleeWebArchiveSymbol> symbols{
            {"GrPs.usd", "map_head", map_owner->map_head()},
            {"GrPs.usd", "coll_data", map_owner->collision()},
            {"GrPs.usd", "grGroundParam", ground_data},
            {"GrPs.usd", "ALDYakuAll", random_yaku->native_data()},
            {"GrPs.usd", "map_ptcl", effects->command_root()},
            {"GrPs.usd", "map_texg", effects->texture_root()},
            {"GrPs.usd", "yakumono_param", yakumono_data},
            {"GrPs.usd", "quake_model_set", quake->single_model()},
        };
        stage_map = melee_web_stage_map_publish(
            map_owner->map_head(), error, sizeof(error));
        check(stage_map != nullptr, error);
        check(melee_web_stage_map_set_public(
                  stage_map, symbols.data(), symbols.size(), error,
                  sizeof(error)), error);
        previous_ground_param = melee_web_ground_data_publish(ground_data);
        ground_param_published = true;
        check(previous_ground_param == before_view.param,
              "GroundParam publication did not retain the prior StageInfo owner");
        check(melee_web_effect_bank_attach(
                  effects->bank(), error, sizeof(error)), error);
        effect_bank_attached = true;
        check(melee_web_stage_selection_begin(St_Kind_PStadium),
              "Could not scope original StageInfo selection for StKind 3");
        stage_selection_owned = true;
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                  lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "E8 request lost its source VS and two-language scopes");
        check_stadium_selection_preserved(host, selected, baseline);

        check(melee_web_stadium_e8_call_observer_begin(),
              "Could not open the bounded E8 source-call window");
        observer_window_owned = true;
        Stage_802251E8(St_Kind_PStadium, NULL);
        check(melee_web_stadium_e8_call_observer_end(&observed),
              "Could not close the bounded E8 source-call window");
        observer_window_owned = false;

        const char* const expected_size_name = "/GrPs.usd";
        check(observed.source_size_calls == 1 &&
                  observed.source_size_successes == 1 &&
                  observed.source_size_name_mismatches == 0 &&
                  observed.source_size_bytes == raw_bytes.size() &&
                  std::strcmp(observed.source_size_name,
                              expected_size_name) == 0,
              "E8 source-size call did not resolve exact /GrPs.usd bytes");
        check(observed.typed_open_calls == 1 &&
                  observed.typed_open_successes == 1 &&
                  observed.typed_open_name_mismatches == 0 &&
                  observed.typed_handle_mismatches == 0 &&
                  std::strcmp(observed.typed_open_name,
                              expected_size_name) == 0 &&
                  observed.typed_archive_handle != nullptr,
              "E8 typed open did not preserve and resolve exact /GrPs.usd identity");
        check(observed.map_head_calls == 1 &&
                  observed.map_head_archive == observed.typed_archive_handle &&
                  observed.map_head_value == map_owner->map_head() &&
                  observed.coll_data_calls == 1 &&
                  observed.coll_data_value == map_owner->collision() &&
                  observed.ground_param_calls == 0 &&
                  observed.itemdata_calls == 0 &&
                  observed.ald_yaku_all_calls == 1 &&
                  observed.ald_yaku_all_value == random_yaku->native_data() &&
                  observed.map_ptcl_calls == 1 &&
                  observed.map_ptcl_value == effects->command_root() &&
                  observed.map_texg_calls == 1 &&
                  observed.map_texg_value == effects->texture_root() &&
                  observed.yakumono_param_calls == 1 &&
                  observed.yakumono_param_value == yakumono_data &&
                  observed.map_plit_calls == 0 &&
                  observed.quake_model_set_calls == 1 &&
                  observed.quake_model_set_value == quake->single_model() &&
                  observed.other_public_calls == 0,
              "E8 source public lookup results differed from the typed owners");

        check(melee_web_stadium_c1_stage_info_current_view(&after_view),
              "Could not inspect StageInfo after the source E8 request");
        check(after_view.grkind == Gr_Kind_PStadium &&
                  after_view.param == ground_data &&
                  after_view.x6E4[0] == -1 &&
                  after_view.x6E4[1] == before_view.x6E4[1] &&
                  after_view.coll_data == map_owner->collision() &&
                  after_view.ald_yaku_all == random_yaku->native_data() &&
                  after_view.map_ptcl == effects->command_root() &&
                  after_view.map_texg == effects->texture_root() &&
                  after_view.yakumono_param == yakumono_data &&
                  after_view.quake_model_set == quake->single_model(),
              "Source StageInfo did not retain the checked typed owner pointers");
        check(after_view.itemdata == before_view.itemdata &&
                  after_view.map_plit == before_view.map_plit &&
                  after_view.itemdata == nullptr && after_view.map_plit == nullptr,
              "E8 request changed the source-authored empty item/light roots");
        check(melee_web_stadium_c1_stage_object_failures() == 0,
              "E8 request entered stage objects, Ground, item, or light state");
        check_stadium_selection_preserved(host, selected, baseline);
        check(seed_ptr == seed_owner && *seed_ptr == seed_before,
              "E8 request changed the source seed owner or value");
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                  lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "E8 request changed source VS or language state");
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        check(melee_web_menu_host_snapshot_card_data(
                  host, 0, save_after.data(), save_after.size(), error,
                  sizeof(error)), error);
        check(save_after == save_before,
              "E8 request changed the live source save owner");
        check(raw_bytes == raw_before &&
                  std::equal(archive_data_before.begin(),
                             archive_data_before.end(), archive->data().begin(),
                             archive->data().end()),
              "E8 request changed the immutable source archive bytes");
        if (perform_ground_map1_owner) {
            check(ground_storage != nullptr,
                  "Ground map1 owner lost its pre-E8 storage lease");
            run_stadium_ground_map1_owner(archive, *map_owner,
                                          *ground_storage);
            check(melee_web_stadium_c1_stage_object_failures() == 0 &&
                      source_stage_registry_empty(),
                  "Ground map1 component did not restore its empty StageInfo owner");
        }
        trace.event("stadium_e8_request_returned", world->audio(),
                    perform_ground_map1_owner
                        ? "typed-catalog-request-plus-map1-owner-component"
                        : "typed-catalog-request-only",
                    &selected, &seed_before);
        cleanup();
        check(cleanup_complete && stage_map == nullptr && snapshot == nullptr &&
                  !melee_web_stage_map_archives(),
              "E8 typed teardown left map or StageInfo snapshot owners live");
        check(melee_web_stadium_c1_stage_state_failures() == 0,
              "E8 teardown did not restore the empty source stage context");
        check(melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 != nullptr &&
                  (Toy_804A284C[3] & 4) != 0,
              "E8 typed teardown changed the reopened MenuWorld or Toy owner");
        check(melee_web_menu_host_source_scene(host) == 0 &&
                  melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
              "E8 request entered an original source menu scene");
        check(seed_ptr == seed_owner && *seed_ptr == seed_before,
              "E8 teardown changed the retained source seed owner or value");
        std::cout << "{\"probe\":\"stadium-e8-request\","
                     "\"scope\":\""
                  << (perform_ground_map1_owner
                          ? "one original E8 request plus one Ground map1 lifetime"
                          : "one original E8 request and checked typed teardown only")
                  << "\","
                     "\"source_size_name\":\""
                  << observed.source_size_name
                  << "\",\"typed_open_name\":\""
                  << observed.typed_open_name
                  << "\",\"source_size_bytes\":" << observed.source_size_bytes
                  << ",\"map_head\":true,\"coll_data\":true,"
                     "\"grGroundParam\":true,\"ALDYakuAll\":true,"
                     "\"map_ptcl\":true,\"map_texg\":true,"
                     "\"yakumono_param\":true,\"quake_model_set\":true,"
                     "\"itemdata_public_calls\":0,\"map_plit_public_calls\":0,"
                     "\"stage_info_xA0_observed_only\":"
                  << after_view.xA0 << ",\"stage_info_x6E4\":["
                  << after_view.x6E4[0] << ',' << after_view.x6E4[1]
                  << "],\"source_seed_unchanged\":true,"
                     "\"save_owner_unchanged\":true,"
                     "\"stage_objects_started\":"
                  << (perform_ground_map1_owner ? "true" : "false")
                  << ",\"checked_teardown\":true}\n";
    } catch (...) {
        if (!cleanup_complete) {
            try {
                cleanup();
            } catch (...) {
                std::abort();
            }
        }
        throw;
    }
}

MeleeWebStadiumC1ItemRuntimeGlobalsView stadium_item_runtime_globals()
{
    MeleeWebStadiumC1ItemRuntimeGlobalsView view{};
    check(melee_web_stadium_c1_item_runtime_globals_view(&view),
          "C1 item-state preflight cannot read source item globals");
    return view;
}

void check_stadium_item_runtime_globals(
    const MeleeWebStadiumC1ItemRuntimeGlobalsView& expected)
{
    const auto observed = stadium_item_runtime_globals();
    check(observed.public_data == expected.public_data &&
              observed.common_articles == expected.common_articles &&
              observed.common_data == expected.common_data &&
              observed.pokemon_articles == expected.pokemon_articles &&
              observed.character_articles == expected.character_articles &&
              observed.bounce_data == expected.bounce_data &&
              observed.color_rows == expected.color_rows,
          "C1 item-state preflight did not restore item globals");
}

struct StadiumItemRuntimeEndGuard {
    MeleeWebItemRuntime* runtime = nullptr;

    void end()
    {
        if (!runtime) return;
        char error[256]{};
        check(melee_web_item_runtime_end(runtime, error, sizeof(error)), error);
        runtime = nullptr;
    }

    ~StadiumItemRuntimeEndGuard()
    {
        if (!runtime) return;
        char error[256]{};
        if (!melee_web_item_runtime_end(runtime, error, sizeof(error))) {
            std::cerr << "C1 item-state preflight teardown: " << error << '\n';
            std::abort();
        }
    }
};

void run_stadium_c1_item_state_preflight(
    const melee_web::RuntimeFiles& reopened_files,
    MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before)
{
    using namespace melee_web;
    check(host != nullptr, "C1 item-state preflight requires a retained menu host");
    stadium_c1_item_owner::require_retained_inputs(reopened_files);

    const auto& itco_bytes = reopened_files.at("ItCo.usd");
    const auto& grps_bytes = reopened_files.at("GrPs.usd");
    const std::vector<std::uint8_t> itco_before = itco_bytes;
    const std::vector<std::uint8_t> grps_before = grps_bytes;
    const auto itco_archive = std::make_shared<const DatArchive>(
        itco_bytes, DatExternalPolicy::PreserveUnresolved);
    const auto grps_archive = std::make_shared<const DatArchive>(
        grps_bytes, DatExternalPolicy::ResolveNull);
    const auto yaku_root = stadium_archive_symbol_offset(
        *grps_archive, "ALDYakuAll");
    const DatStageYaku random_yaku(grps_archive, yaku_root);
    const auto& scripts = random_yaku.scripts();

    const auto* const seed_owner = seed_ptr;
    check(seed_owner != nullptr,
          "C1 item-state preflight lost the retained source RNG owner");
    const std::uint32_t seed_before = *seed_owner;
    const MeleeWebGameplayStats stats_before = melee_web_gameplay_stats();
    check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
          "C1 item-state preflight found a pre-existing item object");
    check_stadium_preflight_stage_empty();
    check_stadium_selection_preserved(host, selected, baseline);

    for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
        NativeDatArena public_data_arena(itco_archive);
        const DatItemRegistry source_registry(*itco_archive);
        const auto random_index = static_cast<std::size_t>(
            It_PKind_Random - It_Kind_Kuriboh);
        check(random_index < source_registry.articles.size(),
              "Original Random Pokémon Article index exceeds the source registry");

        DatItemRegistryNative registered_articles(itco_archive);
        void* const random_article = registered_articles.articles()[random_index];
        stadium_c1_item_owner::require_random_article(
            source_registry.articles[random_index], random_article);
        DatItemArticle random_article_owner(
            itco_archive, *source_registry.articles[random_index],
            It_PKind_Random, random_article);
        std::array<void*, 8> script_rows_before{};
        void* random_states = nullptr;
        stadium_c1_item_owner::require_state_capacity(
            scripts, random_article_owner.state_count());
        for (std::size_t row = 1; row < scripts.size(); ++row) {
            if (!scripts[row]) continue;
            check(row < random_article_owner.state_count(),
                  "ALDYakuAll consumer exceeds the authored Random Article state table");
            void* row_table = nullptr;
            check(melee_web_stadium_c1_random_article_state_row(
                      random_article, static_cast<std::uint32_t>(row), &row_table,
                      &script_rows_before[row]),
                  "Random Pokémon Article source state row is unavailable");
            if (!random_states) random_states = row_table;
            check(row_table == random_states,
                  "Random Pokémon Article state rows do not share one source table");
        }
        check(random_states != nullptr,
              "Stadium ALDYakuAll has no checked Random Article state consumers");

        const auto item_root = source_registry.root_offset;
        const auto color_root = itco_archive->pointer(item_root + 20, 8);
        if (!color_root)
            throw DatError("Original ItCo color-animation root is absent");
        const auto color_count = stadium_c1_item_owner::checked_color_row_count(
            *itco_archive, *color_root);
        DatColorAnimation color_owner(itco_archive, *color_root, color_count);
        void* const source_item = melee_web_item_public_data_decode(
            public_data_arena.reader(), item_root,
            registered_articles.articles(), MELEE_WEB_ITEM_REGISTRY_COUNT);
        MeleeWebStadiumC1ItemPublicDataView source_view{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_view) &&
                  source_view.common_data != nullptr &&
                  source_view.common_articles != nullptr &&
                  source_view.character_articles != nullptr &&
                  source_view.pokemon_articles != nullptr &&
                  source_view.bounce_data != nullptr,
              "Original ItCo public-data root did not retain all Article tables");
        check(source_view.character_articles ==
                  static_cast<const void*>(registered_articles.articles()),
              "ItCo public-data root did not borrow the checked character Article registry");

        const auto globals_before = stadium_item_runtime_globals();
        const auto source_color_before = source_view.color_rows;
        if (lifetime == 0) {
            const MeleeWebGameplayStats negative_stats_before =
                melee_web_gameplay_stats();
            const auto* const negative_seed_owner = seed_ptr;
            check(negative_seed_owner == seed_owner &&
                      *negative_seed_owner == seed_before,
                  "C1 item-state negative checks lost the retained RNG snapshot");
            check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
                  "C1 item-state negative checks found an item object");

            stadium_c1_item_owner::run_synthetic_negative_cases(scripts);

            check_stadium_item_runtime_globals(globals_before);
            MeleeWebStadiumC1ItemPublicDataView source_after_negatives{};
            check(melee_web_stadium_c1_item_public_data_view(
                      source_item, &source_after_negatives) &&
                      source_after_negatives.common_data == source_view.common_data &&
                      source_after_negatives.common_articles == source_view.common_articles &&
                      source_after_negatives.character_articles == source_view.character_articles &&
                      source_after_negatives.pokemon_articles == source_view.pokemon_articles &&
                      source_after_negatives.bounce_data == source_view.bounce_data &&
                      source_after_negatives.color_rows == source_color_before,
                  "Synthetic negatives changed the retained source color/public-data pointers");
            check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
                  "Synthetic negatives created an item object");
            for (std::size_t row = 1; row < scripts.size(); ++row) {
                if (!scripts[row]) continue;
                void* row_table = nullptr;
                void* script = nullptr;
                check(melee_web_stadium_c1_random_article_state_row(
                          random_article, static_cast<std::uint32_t>(row),
                          &row_table, &script) &&
                          row_table == random_states &&
                          script == script_rows_before[row],
                      "Synthetic negatives changed a retained Random Article state link");
            }
            const MeleeWebGameplayStats negative_stats_after =
                melee_web_gameplay_stats();
            check(negative_stats_after.generation == negative_stats_before.generation &&
                      negative_stats_after.ticks == negative_stats_before.ticks &&
                      negative_stats_after.generation == stats_before.generation &&
                      negative_stats_after.ticks == stats_before.ticks,
                  "Synthetic negatives advanced source ticks or changed generation");
            check(seed_ptr == negative_seed_owner &&
                      *negative_seed_owner == seed_before,
                  "Synthetic negatives changed retained source RNG ownership or value");
            check(itco_bytes == itco_before && grps_bytes == grps_before,
                  "Synthetic negatives changed immutable retained ItCo/GrPs bytes");
            std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>
                negative_save_after{};
            char negative_error[256]{};
            check(melee_web_menu_host_snapshot_card_data(
                      host, 0, negative_save_after.data(),
                      negative_save_after.size(), negative_error,
                      sizeof(negative_error)), negative_error);
            check(negative_save_after == save_before,
                  "Synthetic negatives changed the retained source save owner");
            check_stadium_selection_preserved(host, selected, baseline);
            check_stadium_preflight_stage_empty();
        }
        char error[256]{};
        StadiumItemRuntimeEndGuard runtime;
        runtime.runtime = melee_web_item_runtime_prepare_source(
            source_item, color_owner.table(), color_count,
            error, sizeof(error));
        check(runtime.runtime != nullptr, error);
        const auto globals_active = stadium_item_runtime_globals();
        MeleeWebStadiumC1ItemPublicDataView source_active{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_active) &&
                  globals_active.public_data == source_item &&
                  globals_active.common_articles == source_active.common_articles &&
                  globals_active.common_data == source_active.common_data &&
                  globals_active.pokemon_articles == source_active.pokemon_articles &&
                  globals_active.character_articles == source_active.character_articles &&
                  globals_active.character_articles ==
                      static_cast<const void*>(registered_articles.articles()) &&
                  ((void**) globals_active.character_articles)[random_index] ==
                      random_article &&
                  globals_active.bounce_data == source_active.bounce_data &&
                  globals_active.color_rows == source_active.color_rows &&
                  source_active.color_rows != source_color_before,
              "Prepared item-state globals do not reach the checked Random Article and color rows");
        for (std::size_t row = 1; row < scripts.size(); ++row) {
            if (!scripts[row]) continue;
            void* row_table = nullptr;
            void* script = nullptr;
            check(row < random_article_owner.state_count() &&
                      melee_web_stadium_c1_random_article_state_row(
                          random_article, static_cast<std::uint32_t>(row),
                          &row_table, &script) &&
                      row_table == random_states &&
                      script == script_rows_before[row],
                  "Item-state preflight attached a Stadium script before Ground_801C0800");
        }

        char competing_error[256]{};
        check(melee_web_item_runtime_prepare_source(
                  source_item, color_owner.table(), color_count,
                  competing_error, sizeof(competing_error)) == nullptr &&
                  std::string_view(competing_error).find(
                      "Item startup requires checked data") !=
                      std::string_view::npos,
              "A competing active item-state owner was not refused explicitly");
        const auto globals_competing = stadium_item_runtime_globals();
        check(globals_competing.character_articles ==
                  source_active.character_articles &&
                  ((void**) globals_competing.character_articles)[random_index] ==
                      random_article,
              "Competing item-state owner changed the active Random Article registry");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "C1 item-state preflight created an item object");

        runtime.end();
        check_stadium_item_runtime_globals(globals_before);
        MeleeWebStadiumC1ItemPublicDataView source_after{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_after) &&
                  source_after.color_rows == source_color_before,
              "Item runtime teardown did not restore the source color pointer before owner destruction");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "Item runtime teardown left an item object");
    }

    const MeleeWebGameplayStats stats_after = melee_web_gameplay_stats();
    check(stats_after.generation == stats_before.generation &&
              stats_after.ticks == stats_before.ticks,
          "C1 item-state preflight advanced the source runtime or changed its generation");
    check(seed_ptr == seed_owner && *seed_owner == seed_before,
          "C1 item-state preflight changed retained source RNG ownership or value");
    check(grps_bytes == grps_before,
          "C1 item-state preflight changed immutable GrPs.usd input bytes");
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
    char error[256]{};
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, save_after.data(), save_after.size(), error,
              sizeof(error)), error);
    check(save_after == save_before,
          "C1 item-state preflight changed the retained source save owner");
    check_stadium_selection_preserved(host, selected, baseline);
    check_stadium_preflight_stage_empty();
}

void run_stadium_screen_roots_preflight(
    const melee_web::RuntimeFiles& files, MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before)
{
    using namespace melee_web;
    namespace screen = melee_web::test::stadium_screen;
    const auto& raw = files.at("GrPs.usd");
    const auto raw_before = raw;
    auto archive = std::make_shared<const DatArchive>(raw, DatExternalPolicy::ResolveNull);
    // ResolveNull clears validated external-link slots in the archive's owned
    // copy. Preserve that decoded baseline separately from immutable input.
    const std::vector<std::uint8_t> archive_before(archive->data().begin(), archive->data().end());
    const auto image_offset = screen::root(*archive, screen::image_name);
    const auto sis_offset = screen::root(*archive, screen::sis_name);
    check(image_offset == 35276 && archive->be16(image_offset+4) == 16 &&
              archive->be16(image_offset+6) == 16 && archive->be32(image_offset+8) == 0 &&
              sis_offset == 0x13ca80 &&
              archive->next_target_offset(sis_offset)-sis_offset == 88,
          "Screen roots differ from the retained C0 source contract");
    const auto globals_before = stadium_item_runtime_globals();
    const auto stats_before = melee_web_gameplay_stats();
    const auto* seed_owner = seed_ptr;
    check(seed_owner != nullptr, "Screen preflight lacks a retained RNG owner");
    const auto seed_before = *seed_owner;
    check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
          "Screen preflight found an existing item object");
    auto invariants = [&] {
        check_stadium_item_runtime_globals(globals_before);
        const auto stats = melee_web_gameplay_stats();
        check(stats.ticks == stats_before.ticks && stats.generation == stats_before.generation,
              "Screen descriptor checks changed source ticks/generation");
        check(seed_ptr == seed_owner && *seed_owner == seed_before,
              "Screen descriptor checks changed RNG owner/value");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "Screen descriptor checks created an item object");
        check(raw == raw_before,
              "Screen descriptor checks changed raw GrPs bytes");
        check(std::equal(archive->data().begin(), archive->data().end(), archive_before.begin()),
              "Screen descriptor checks changed the decoded GrPs owner");
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        char error[256]{};
        check(melee_web_menu_host_snapshot_card_data(host, 0, save_after.data(), save_after.size(), error, sizeof(error)), error);
        check(save_after == save_before, "Screen descriptor checks changed retained save");
        check_stadium_selection_preserved(host, selected, baseline);
        check_stadium_preflight_stage_empty();
    };
    invariants();
    melee_web::test::stadium_buffer::run_original_constructor_lifetimes(
        invariants);
    invariants();
    screen::synthetic_checks(invariants);
    invariants();
    for (unsigned lifetime=0; lifetime<2; ++lifetime) {
        {
            DatNativeMap map(archive, melee_web::test::stadium_contract);
            DatSis sis(archive, screen::sis_name);
            check(sis.entry_count() == 22, "Screen SIS changed authored 22-slot count");
            {
                DatNativeMap foreign(archive, melee_web::test::stadium_contract);
                screen::synthetic::rejects([&] {
                    screen::identity(map, 1, foreign.image_descriptor(image_offset));
                }, "unique map descriptor");
            }
            invariants();
            screen::catalog_checks(*archive, map, sis, 1, image_offset);
            invariants();
            auto* image = static_cast<HSD_ImageDesc*>(map.image_descriptor(image_offset));
            screen::identity(map, 1, image);
            const MeleeWebArchiveSymbol symbols[] = {
                {"GrPs.usd", screen::image_name, image},
                {"GrPs.usd", screen::sis_name, sis.descriptor()},
            };
            screen::Catalog catalog("GrPs.usd", symbols, 2);
            check(stadium_screen_source_public(catalog.handle, screen::image_name) == image &&
                      stadium_screen_source_public(catalog.handle, screen::sis_name) == sis.descriptor(),
                  "Live consumer catalog lost canonical IMAGE/SIS owners");
            DatNativeMap foreign(archive, melee_web::test::stadium_contract);
            auto* foreign_image = static_cast<HSD_ImageDesc*>(foreign.image_descriptor(image_offset));
            auto* descriptor = static_cast<HSD_Joint*>(
                stadium_screen_map_entry_joint(map.map_head(), 1));
            screen::live_source_consumer_lifetime(descriptor, image, foreign_image, invariants);
            invariants();
        }
        invariants();
    }
    std::cout << "C1 screen-root preflight preserved canonical IMAGE, writable SIS and two owner/catalog lifetimes; no stage entry or ticks\n";
    std::cout << "C1 live Stadium IMAGE source hit/miss/remove passed twice; no stage entry or ticks\n";
    std::cout << "C1 original Stadium auxiliary IMAGE constructor/remove passed twice without callback dispatch or stage entry\n";
}

void run_stadium_c1_context_preflight(
    const melee_web::RuntimeFiles& menu_files,
    MeleeWebMenuHost*& host,
    std::unique_ptr<melee_web::GameplayMenuWorld>& world,
    const MeleeWebMenuMatchSelection& selected,
    const std::vector<std::string>& selected_names,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    bool perform_e8_request,
    bool perform_item_state_preflight,
    bool perform_screen_roots_preflight,
    bool perform_ground_map1_owner,
    TransitionTrace& trace)
{
    char error[256]{};
    const int previous_mode = gm_GetCurrentGameMode();
    const int previous_language = lbLang_GetLanguageSetting();
    const int previous_saved_language = lbLang_GetSavedLanguage();
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_before{};
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> baseline{};
    check(selected.start.rules.stkind == St_Kind_PStadium &&
              selected.save_profile_present,
          "C1 context preflight requires source StKind 3 and its retained save owner");
    check(melee_web_menu_host_snapshot_card_data(
              host, 1, baseline.data(), baseline.size(), error,
              sizeof(error)), error);
    check_stadium_selection_preserved(host, selected, baseline);
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, save_before.data(), save_before.size(), error,
              sizeof(error)), error);
    check(melee_web_source_files_active(),
          "C1 source handoff lost its menu files");
    check(_Toy_sbss_804D6ED0 == nullptr,
          "C1 source handoff retained a Toy archive alias past MenuWorld close");
    check((Toy_804A284C[3] & 4) != 0,
          "C1 source handoff lost the retained Toy category baseline");
    check_stadium_preflight_stage_empty();
    world->verify_immutable_archives();
    world->close();
    world.reset();

    bool vs_mode_owned = false;
    bool language_scope_owned = false;
    bool cleanup_complete = false;
    melee_web::RuntimeFiles reopened_files;
    auto restore_context = [&]() {
        if (language_scope_owned) {
            lbLang_SetLanguageSetting(previous_language);
            lbLang_SetSavedLanguage(previous_saved_language);
            language_scope_owned = false;
        }
        if (vs_mode_owned) {
            check(melee_web_vs_mode_end(),
                  "C1 context preflight lost its source VS mode lease");
            vs_mode_owned = false;
        }
        check(gm_GetCurrentGameMode() == previous_mode,
              "C1 context preflight did not restore the source game mode");
        check(lbLang_GetLanguageSetting() == previous_language &&
                  lbLang_GetSavedLanguage() == previous_saved_language,
              "C1 context preflight did not restore both source language settings");
    };
    auto cleanup = [&]() {
        if (world) {
            world->close_prepared();
            world.reset();
        }
        check(!melee_web_source_files_active(),
              "C1 context preflight left a RuntimeFiles scope active");
        check(_Toy_sbss_804D6ED0 == nullptr,
              "C1 context preflight left Toy aliases past MenuWorld close");
        restore_context();
        if (host) {
            check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
            host = nullptr;
        }
        cleanup_complete = true;
    };

    try {
        check(!melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 == nullptr,
              "Closing the selected SSS world did not release its file and Toy owners");
        check((Toy_804A284C[3] & 4) != 0,
              "Closing MenuWorld erased the retained Toy category baseline");
        reopened_files = exact_stadium_runtime_union(
            menu_files, selected_names, menu_dir, game_dir);
        check(reopened_files.contains("GrPs.usd"),
              "Exact C1 RuntimeFiles union omitted GrPs.usd");

        check(melee_web_vs_mode_begin(),
              "C1 context preflight could not acquire the source VS mode lease");
        vs_mode_owned = true;
        language_scope_owned = true;
        lbLang_SetLanguageSetting(LANG_US);
        lbLang_SetSavedLanguage(LANG_US);
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode(),
              "Reopened C1 context is not source GM_VS non-1P mode");
        check(lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "Reopened C1 context did not set both source language scopes to US");
        check_stadium_selection_preserved(host, selected, baseline);

        world = std::make_unique<melee_web::GameplayMenuWorld>(reopened_files);
        check(melee_web_source_files_active(),
              "Reopened MenuWorld did not activate its exact RuntimeFiles union");
        check(melee_web_menu_host_source_scene(host) == 0 &&
                  melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
              "Reopened MenuWorld entered a source menu scene");
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode(),
              "Reopened MenuWorld did not preserve the explicit source VS context");
        check(lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "Reopened MenuWorld changed one of the scoped US language settings");
        check(_Toy_sbss_804D6ED0 == nullptr,
              "Fresh MenuWorld unexpectedly retained Toy aliases from the closed world");
        check_stadium_preflight_stage_empty();

        const std::string grps_name = lbFileGetFullName("GrPs");
        check(grps_name == "GrPs.usd",
              "US C1 source resolution did not select the exact GrPs.usd filename");
        const auto grps = reopened_files.find(grps_name);
        check(grps != reopened_files.end() && !grps->second.empty(),
              "Exact C1 RuntimeFiles union has no GrPs.usd bytes");
        size_t resolved_size = 0;
        size_t root_path_size = 0;
        check(melee_web_source_file_size(grps_name.c_str(), &resolved_size) &&
                  resolved_size == grps->second.size() &&
                  lbFileGetSize("GrPs") == resolved_size,
              "Original lbFile GrPs resolution changed the exact RuntimeFiles size");
        check(melee_web_source_file_size("/GrPs.usd", &root_path_size) &&
                  root_path_size == resolved_size,
              "Retail root-path GrPs.usd resolution changed the exact RuntimeFiles size");
        const int grps_entry = melee_web_source_file_entry(grps_name.c_str());
        check(grps_entry > 0 && melee_web_source_file_entry_owned(grps_entry),
              "Reopened source file owner did not retain the GrPs.usd DVD entry");

        check((Toy_804A284C[3] & 4) != 0,
              "C1 preflight lost the menu host's existing Toy category baseline");
        Toy_803124BC();
        check(_Toy_sbss_804D6ED0 != nullptr && (Toy_804A284C[3] & 4) != 0,
              "Reopened source files did not restore Toy aliases over the retained baseline");
        const std::string toy_name = lbFileGetFullName("TyDatai");
        check(toy_name == "TyDatai.usd" &&
                  melee_web_source_file_size(toy_name.c_str(), &resolved_size) &&
                  resolved_size == reopened_files.at(toy_name).size(),
              "US Toy alias did not resolve through the reopened RuntimeFiles owner");
        check_stadium_selection_preserved(host, selected, baseline);
        if (perform_item_state_preflight) {
            run_stadium_c1_item_state_preflight(
                reopened_files, host, selected, baseline, save_before);
        }
        if (perform_screen_roots_preflight) {
            run_stadium_screen_roots_preflight(reopened_files, host, selected, baseline, save_before);
        }
        world->verify_immutable_archives();
        check_stadium_preflight_stage_empty();

        if (perform_e8_request || perform_ground_map1_owner) {
            run_stadium_e8_request(reopened_files, host, world.get(), selected,
                                   baseline, save_before,
                                   perform_ground_map1_owner, trace);
            check_stadium_preflight_stage_empty();
        }

        world->close_prepared();
        world.reset();
        check(!melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 == nullptr,
              "Prepared MenuWorld teardown retained source-file or Toy aliases");
        restore_context();
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        check(melee_web_menu_host_snapshot_card_data(
                  host, 0, save_after.data(), save_after.size(), error,
                  sizeof(error)), error);
        check(save_after == save_before,
              "C1 reopened-context preflight changed the live source save owner");
        check_stadium_selection_preserved(host, selected, baseline);
        check((Toy_804A284C[3] & 4) != 0,
              "C1 context teardown changed the retained Toy category baseline");
        cleanup();
        check(host == nullptr && !melee_web_source_files_active(),
              "C1 context preflight did not release host and source-file owners");
        if (perform_ground_map1_owner) {
            std::cout << "C1 reopened-context lifecycle and one E8 typed request, "
                         "plus one isolated Ground map1 lifetime passed; "
                         "no rendered stage entry or source menu entry\n";
        } else if (perform_e8_request) {
            std::cout << "C1 reopened-context lifecycle preflight and one E8 typed request passed; "
                         "no stage object or source menu entry\n";
        } else if (perform_item_state_preflight) {
            std::cout << "C1 reopened-context lifecycle and item-state-owner preflight passed; "
                         "no E8 request, stage publication, or source menu entry\n";
        } else {
            std::cout << "C1 reopened-context lifecycle preflight passed; no E8 request, "
                         "stage publication, or source menu entry\n";
        }
    } catch (...) {
        if (!cleanup_complete) {
            try {
                cleanup();
            } catch (...) {
                std::abort();
            }
        }
        throw;
    }
}

void run_stadium_c1a_selection_smoke(
    const melee_web::RuntimeFiles& files,
    bool reopened_context_preflight,
    bool e8_request_trace,
    bool item_state_preflight,
    bool screen_roots_preflight,
    bool ground_map1_owner,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    TransitionTrace& trace)
{
    char error[256]{};
    MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    check(melee_web_menu_host_enable_stadium_c1a(host, error, sizeof(error)), error);
    auto world = std::make_unique<melee_web::GameplayMenuWorld>(files);
    check(melee_web_menu_host_enter(host, world->audio(), error, sizeof(error)), error);

    PADStatus raw[4]{};
    melee_web_stage_input_neutral(raw);
    float pcm[1068]{};
    unsigned audio_phase = 0;
    auto tick = [&]() {
        const int result = melee_web_menu_host_tick(host, raw, error, sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world->audio(), pcm, count,
                                     error, sizeof(error)), error);
        return result;
    };
    auto transition = [&]() {
        melee_web_stage_input_button(raw, PAD_BUTTON_START);
        int result = tick();
        melee_web_stage_input_neutral(raw);
        for (unsigned wait = 0; result != 3 && wait < 120; ++wait)
            result = tick();
        if (result != 3) {
            std::string detail = "Original menu input did not complete its C1a transition: phase=" +
                std::to_string(melee_web_menu_host_phase(host)) +
                " scene=" + std::to_string(melee_web_menu_host_source_scene(host));
            MeleeWebFighterInputObservation css{};
            if (melee_web_menu_host_phase(host) == MELEE_WEB_MENU_CSS &&
                melee_web_fighter_input_observe(CKIND_MARIO, &css)) {
                detail += " start_ready=" + std::to_string(css.source_start_ready) +
                    " start_cooldown=" + std::to_string(css.source_start_cooldown) +
                    " pending_scene=" + std::to_string(css.source_pending_scene) +
                    " last_start_trigger=" + std::to_string(css.source_last_start_trigger) +
                    " last_start_ready=" + std::to_string(css.source_last_start_ready) +
                    " last_start_pending=" + std::to_string(css.source_last_start_pending);
            }
            check(0, detail.c_str());
        }
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    };

    // The armed SSS still begins on an admitted stage. Its existing validation
    // remains live while the source cursor navigates toward Stadium.
    for (unsigned frame = 0; frame < 120; ++frame)
        check(tick() == 1, "C1a SSS navigation rejected its initial admitted stage");
    transition();
    check(melee_web_menu_host_phase(host) == MELEE_WEB_MENU_SSS_READY,
          "C1a CSS did not complete the original SSS transition");
    world->rebuild_scene(melee_web::GameplayMenuScene::Stages);
    check(melee_web_menu_host_enter(host, world->audio(), error, sizeof(error)), error);
    melee_web_stage_input_neutral(raw);
    for (unsigned frame = 0; frame < 120; ++frame)
        check(tick() == 1, "C1a rejected the ordinary initial SSS tile");

    bool at_stadium = false;
    for (unsigned frame = 0; frame < 180; ++frame) {
        MeleeWebStageInputObservation observed{};
        check(melee_web_stage_input_observe(St_Kind_PStadium, &observed),
              "Original SSS Stadium cursor observation is unavailable");
        const int state = melee_web_stage_input_drive(
            raw, &observed, St_Kind_PStadium);
        check(state != MELEE_WEB_STAGE_INPUT_INVALID,
              "Original SSS Stadium cursor target is invalid");
        if (state == MELEE_WEB_STAGE_INPUT_AT_TARGET) {
            check(observed.selected_stage_kind == St_Kind_PStadium,
                  "Raw PAD reached the Stadium target without source tile selection");
            at_stadium = true;
            break;
        }
        check(tick() == 1, "Original SSS transitioned during raw Stadium navigation");
    }
    check(at_stadium, "Raw PAD did not select the original Stadium SSS tile");
    check(melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium) &&
          !melee_web_menu_stage_available(St_Kind_PStadium),
          "C1a explicit confirmation did not remain separate from admission/random availability");
    transition();
    check(melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
          "Original SSS did not commit the C1a selection");

    check(!melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium),
          "C1a explicit-confirm permission survived source SSS exit");
    StartMeleeData raw_selection{};
    check(melee_web_menu_host_stadium_c1a_raw_selection(
              host, &raw_selection, error, sizeof(error)), error);
    check(raw_selection.rules.stkind == St_Kind_PStadium,
          "Source SSS raw payload did not retain StKind 3");
    SSSData unarmed_validation{};
    unarmed_validation.force_stage_id = -1;
    unarmed_validation.start_game = true;
    unarmed_validation.vs.start = raw_selection;
    check(!melee_web_menu_sss_selection_valid(&unarmed_validation),
          "The ordinary SSS validator admitted Stadium without its session gate");

    MeleeWebMenuMatchSelection ordinary{};
    check(!melee_web_menu_host_selection(host, &ordinary, error, sizeof(error)),
          "Ordinary match admission accepted Stadium through the diagnostic gate");
    MeleeWebMenuMatchSelection selected{};
    check(melee_web_menu_host_stadium_c1a_selection(
              host, &selected, error, sizeof(error)), error);
    check(selected.start.rules.stkind == St_Kind_PStadium,
          "Prepared C1a payload did not retain source StKind 3");
    const auto names = melee_web::stadium_c1a_asset_names(selected);
    for (const char* required : {
             "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat",
             "pstadium.hps", "pokesta.hps", "pstadium.ssm"})
        check(std::find(names.begin(), names.end(), required) != names.end(),
              "C1a source manifest omitted a Stadium dependency");
    check(!melee_web_stage_content(St_Kind_PStadium),
          "C1a diagnostic unexpectedly registered Stadium as playable");

    if (reopened_context_preflight) {
        run_stadium_c1_context_preflight(
            files, host, world, selected, names, menu_dir, game_dir,
            e8_request_trace, item_state_preflight, screen_roots_preflight,
            ground_map1_owner, trace);
    } else {
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
    }
    check(!melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium),
          "C1a explicit-confirm permission survived unload");
    if (ground_map1_owner) {
        std::cout << "C1a raw PAD CSS->SSS selection, one E8 typed request, and one "
                     "Ground map1 constructor/removal passed; admission remains closed\n";
    } else if (e8_request_trace) {
        std::cout << "C1a raw PAD CSS->SSS selection and one E8 typed request passed; "
                     "ordinary match admission and gameplay entry remain closed\n";
    } else {
        std::cout << "C1a raw PAD CSS->SSS Stadium selection and exact preparation manifest passed; "
                     "ordinary admission and source/stage construction remain closed\n";
    }
}
#endif
}
int main(int argc,char** argv){try{
 if(argc==2&&std::string_view(argv[1])=="--vs-sudden-death-source-control"){
  run_vs_sudden_death_source_control();return 0;
 }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(argc==2&&std::string_view(argv[1])=="--stadium-yakumono-exchange"){
  run_stadium_yakumono_exchange_control();return 0;
 }
#endif
 if(argc<3||argc>8)throw std::runtime_error("Expected menu/audio directories, optional stage kind, transition trace path, source revision and input recipe");
 const int stage_kind=argc>=4?std::stoi(argv[3]):St_Kind_Last;
 const char* trace_path=argc>=5?argv[4]:nullptr;
 const char* source_revision=argc>=6?argv[5]:nullptr;
 const char* input_recipe=argc>=7?argv[6]:nullptr;
 const char* replay_recipe_path=argc==8?argv[7]:nullptr;
 const bool retail_fd_recipe=input_recipe&&std::string(input_recipe)=="retail-stock-fd-v1";
 const bool results_mario_recipe=input_recipe&&std::string(input_recipe)=="results-mario-v1";
 const bool link_css_unload_recipe=input_recipe&&std::string(input_recipe)=="link-css-unload-v1";
 const bool title_main_abort_recipe=input_recipe&&std::string(input_recipe)=="title-main-abort-v1";
 const bool opening_movie_preload_recipe=input_recipe&&std::string(input_recipe)=="opening-movie-preload-v1";
 const bool trophy_baseline_recipe=input_recipe&&std::string(input_recipe)=="trophy-baseline-v1";
 const bool sound_settings_recipe=input_recipe&&std::string(input_recipe)=="main-settings-sound-v1";
 const bool sudden_death_host_recipe=input_recipe&&
     std::string(input_recipe)=="sudden-death-host-control-v1";
 const bool v10_css_replay_start_recipe=input_recipe&&
     std::string(input_recipe)=="whole-session-css-replay-start-v10-v1";
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 const bool stadium_c1a_recipe=input_recipe&&std::string(input_recipe)=="stadium-c1a-v1";
 const bool stadium_c1_context_preflight_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-c1-context-preflight-v1";
 const bool stadium_c1_item_state_preflight_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-c1-item-state-preflight-v1";
 const bool stadium_screen_roots_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-screen-roots-preflight-v1";
 const bool stadium_e8_request_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-e8-request-v1";
 const bool stadium_ground_map1_owner_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-ground-map1-owner-v1";
#else
 const bool stadium_c1a_recipe=false;
 const bool stadium_c1_context_preflight_recipe=false;
 const bool stadium_c1_item_state_preflight_recipe=false;
 const bool stadium_screen_roots_recipe=false;
 const bool stadium_e8_request_recipe=false;
 const bool stadium_ground_map1_owner_recipe=false;
#endif
 if(input_recipe&&!retail_fd_recipe&&!results_mario_recipe&&!link_css_unload_recipe&&
    !title_main_abort_recipe&&!opening_movie_preload_recipe&&!trophy_baseline_recipe&&
    !sound_settings_recipe&&!sudden_death_host_recipe&&!stadium_c1a_recipe&&
    !stadium_c1_context_preflight_recipe&&
    !stadium_c1_item_state_preflight_recipe&&!stadium_screen_roots_recipe&&
    !stadium_e8_request_recipe&&!stadium_ground_map1_owner_recipe&&
    !v10_css_replay_start_recipe)
    throw std::runtime_error("Unknown transition input recipe");
 if(v10_css_replay_start_recipe&&
    (argc!=8||!replay_recipe_path||!trace_path||!source_revision))
   throw std::runtime_error("MWRC v10 CSS replay-start reducer requires trace, source revision and exact recipe path");
 if(!v10_css_replay_start_recipe&&argc==8)
   throw std::runtime_error("Only the MWRC v10 CSS replay-start reducer accepts an exact recipe path");
 if((retail_fd_recipe||results_mario_recipe||sudden_death_host_recipe||
     v10_css_replay_start_recipe)&&
    stage_kind!=St_Kind_Last)
   throw std::runtime_error("Explicit FD recipes require Final Destination");
 if((stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
     stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
     stadium_e8_request_recipe||stadium_ground_map1_owner_recipe)&&
    stage_kind!=St_Kind_PStadium)
   throw std::runtime_error("C1a recipes require source StKind 3");
 TransitionTrace trace(trace_path,source_revision,input_recipe);
 melee_web::RuntimeFiles files;
 std::vector<std::string> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","GrNBa.dat","GrSt.dat","hyaku.hps","hyaku2.hps","sp_zako.hps","ystory.hps","ItCo.usd","EfMrData.dat","EfFxData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","fox.ssm","falco.ssm","mars.ssm","drmario.ssm","emblem.ssm","pupupu.ssm","dsp_coef.bin","sislib_font.bin"};
 if(sudden_death_host_recipe||stadium_c1a_recipe||
    stadium_c1_context_preflight_recipe||
    stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
    stadium_e8_request_recipe||stadium_ground_map1_owner_recipe||
    v10_css_replay_start_recipe||title_main_abort_recipe||opening_movie_preload_recipe||
    trophy_baseline_recipe||sound_settings_recipe)
  keys=melee_web::menu_asset_names();
 for(const auto& key:melee_web::menu_asset_names())
  if(std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);
 for(const auto& key:keys){
  const auto root=std::filesystem::exists(std::filesystem::path(argv[1])/key)?argv[1]:argv[2];
  std::ifstream input(std::filesystem::path(root)/key,std::ios::binary);if(!input)throw std::runtime_error("Missing owned menu host fixture: "+key);
  files[key]={(std::istreambuf_iterator<char>(input)),{}};
 }
 if(results_mario_recipe){
  for(const char* key:{"GmRst.usd","SdRst.usd","GmRstMMr.dat","ff_mario.hps","TyDatai.usd","IfPrize.usd","SdPrize.usd","s_info1.hps","s_info2.hps","s_info3.hps"}){
   std::ifstream input(std::filesystem::path(argv[2])/key,std::ios::binary);
   if(!input)throw std::runtime_error("Missing owned Results fixture");
   files[key]={(std::istreambuf_iterator<char>(input)),{}};
  }
 }
 char session_error[256]{};
 check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
 const auto session_allocation=melee_web_gameplay_allocation();
 if(v10_css_replay_start_recipe){
  run_v10_css_replay_start_prefix(files,replay_recipe_path,trace);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"MWRC v10 original CSS replay-start prefix returned after one recorded source tick; "
              "no draw or full-session comparison\n";
  return 0;
 }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
    stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
    stadium_e8_request_recipe||stadium_ground_map1_owner_recipe){
  run_stadium_c1a_selection_smoke(
      files, stadium_c1_context_preflight_recipe||
          stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
          stadium_e8_request_recipe||stadium_ground_map1_owner_recipe,
      stadium_e8_request_recipe||stadium_ground_map1_owner_recipe,
      stadium_c1_item_state_preflight_recipe, stadium_screen_roots_recipe,
      stadium_ground_map1_owner_recipe,
      argv[1], argv[2], trace);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  return 0;
 }
#endif
 if(title_main_abort_recipe){
  run_title_main_abort_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Title/Main checked abort and CSS re-entry smoke passed; no browser or retail-route claim\n";
  return 0;
 }
 if(opening_movie_preload_recipe){
  run_opening_movie_preload_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Opening movie preload ownership probe passed; no movie decode or retail-route claim\n";
  return 0;
 }
 if(trophy_baseline_recipe){
  run_trophy_baseline_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native trophy baseline initialization smoke passed; no browser or retail-route claim\n";
  return 0;
 }
 if(sound_settings_recipe){
  run_main_sound_mix_route(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Main Settings Sound source route passed; no browser or retail-route claim\n";
  return 0;
 }
 const unsigned cycle_count=(results_mario_recipe||sudden_death_host_recipe)?1:2;
 for(unsigned cycle=0;cycle<cycle_count;cycle++){
  trace.begin_run(cycle);
  if((retail_fd_recipe||results_mario_recipe)&&cycle==0)*seed_ptr=1840631306u;
  char error[256]{};auto* host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
  auto world=std::make_unique<melee_web::GameplayMenuWorld>(files);
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  trace.event("capture_begin",world->audio());
  PADStatus raw[4]{};raw[2].err=raw[3].err=-1;float pcm[1068];unsigned audio_phase=0;
  auto tick=[&](){
   int result=melee_web_menu_host_tick(host,raw,error,sizeof(error));
   check(result==1||result==3,error);
   audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(world->audio(),pcm,count,error,sizeof(error)),error);
   return result;
  };
  auto transition=[&](u16 button=PAD_BUTTON_START){
   raw[0].button=button;
   int result=tick();raw[0].button=0;
   for(unsigned wait=0;result!=3&&wait<120;wait++)result=tick();
   check(result==3,"Original menu input did not complete its transition");
   check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);
   world->verify_immutable_archives();
  };
  auto rebuild_menu_scene=[&](){
   MeleeWebAudio* retained=world->audio();
   const uint64_t generation=melee_web_audio_generation(retained);
   uint32_t completed=0,revisited=0,after_completed=0,after_revisited=0;
   check(generation!=0,"Original menu audio lifetime is unavailable");
   check(melee_web_audio_stream_progress(retained,&completed,&revisited),
         "Original menu HPS progress is unavailable before scene rebuild");
   const auto scene=melee_web_menu_host_phase(host)==2?
       melee_web::GameplayMenuScene::Stages:melee_web::GameplayMenuScene::Characters;
   world->rebuild_scene(scene);
   check(world->audio()==retained&&melee_web_audio_generation(world->audio())==generation,
         "CSS/SSS scene rebuild replaced the original menu audio lifetime");
   check(melee_web_audio_stream_progress(world->audio(),&after_completed,&after_revisited)&&
         after_completed==completed&&after_revisited==revisited,
         "CSS/SSS scene rebuild reset or advanced menu music outside an audio tick");
   check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  };
  auto select_stage=[&](){
  bool at_target=false;
  for(unsigned t=0;t<120;t++){
   MeleeWebStageInputObservation observed{};
   check(melee_web_stage_input_observe(stage_kind,&observed),"Original SSS cursor observation unavailable");
   const int state=melee_web_stage_input_drive(raw,&observed,stage_kind);
   check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"Original SSS target is invalid");
   if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET){
    check(observed.selected_stage_kind==stage_kind,"Cursor target and source selected tile differ");
    at_target=true;break;
   }
   check(tick()==1,"SSS cursor input unexpectedly transitioned");
  }
  check(at_target,"Original SSS cursor did not reach requested stage");
  };
  const unsigned first_css_neutral=retail_fd_recipe&&cycle==0?187:120;
  if(!retail_fd_recipe){
   // Owned LbRb row zero ends after four motor-on samples and one hard stop.
   // Exercise the menu host's normal raw-sample boundary, without adding
   // simulation steps or claiming physical actuator behavior.
   lb_80014574(0,0x4d57,0,0);
   check(HSD_Rumble_804C22E0[0].nb_list==1,
         "Menu startup did not publish an available source rumble pool");
  }
  for(unsigned t=0;t<first_css_neutral;t++){
   check(tick()==1,"Unexpected CSS transition");
   if(!retail_fd_recipe&&t==5)
    check(HSD_Rumble_804C22E0[0].nb_list==0,
          "Menu raw samples did not advance and release the source rumble program");
  }
  uint32_t initial_music_completed=0,initial_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&initial_music_completed,
                                         &initial_music_revisited)&&
        initial_music_completed>0,
        "Original CSS music did not load an HPS payload");
  if(cycle==1||link_css_unload_recipe){
   const int target_kind=link_css_unload_recipe?(cycle==0?CKIND_LINK:CKIND_CLINK):CKIND_FALCO;
   bool target_selected=false;
   for(unsigned t=0;t<180;t++){
    MeleeWebFighterInputObservation observed{};
    check(melee_web_fighter_input_observe(target_kind,&observed),
          "Original CSS fighter observation unavailable");
    const int state=melee_web_fighter_input_drive(raw,&observed,target_kind);
    check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,
          "Original CSS fighter target is invalid");
    if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED){
     target_selected=true;break;
    }
    if(state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||
       state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY)
      melee_web_fighter_input_button(raw,PAD_BUTTON_A);
    check(tick()==1,"CSS cursor input unexpectedly transitioned");
    melee_web_fighter_input_neutral(raw);
    check(tick()==1,"CSS button release unexpectedly transitioned");
   }
   check(target_selected,"Original CSS did not commit requested fighter through raw PAD input");
   // The source keeps the door/model confirmation animation active briefly
   // after the drop.  Give that original process time to reach its ordinary
   // Start-accepting state before requesting the scene transition.
   melee_web_fighter_input_neutral(raw);
   for(unsigned settle=0;settle<30;++settle)
    check(tick()==1,"CSS transitioned during fighter confirmation settle");
  }
  if(link_css_unload_recipe){
   check(melee_web_menu_host_phase(host)==1,"CSS unload recipe left the original CSS phase");
   check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
   world->verify_immutable_archives();
   world->close();world.reset();
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
   host=nullptr;
   std::cout<<"Original CSS "<<(cycle==0?"Link":"Young Link")<<" audio registry entered, aborted and unloaded\n";
   continue;
  }
  transition();check(melee_web_menu_host_phase(host)==2,"CSS did not choose original SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected SSS transition");
  // Exercise the real B cancellation before committing the match, with new
  // owned worlds for both directions and no direct source selection writes.
  transition(PAD_BUTTON_B);check(melee_web_menu_host_phase(host)==4,"Original SSS B did not return toward CSS");
  trace.event("sss_exit_complete",world->audio(),"css");
  rebuild_menu_scene();
  trace.event("css_enter_complete",world->audio());
  const unsigned second_css_neutral=retail_fd_recipe&&cycle==0?138:120;
  for(unsigned t=0;t<second_css_neutral;t++)check(tick()==1,"Unexpected cancelled CSS transition");
  transition();check(melee_web_menu_host_phase(host)==2,"Returned CSS did not choose SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected second SSS transition");
  // Move the original SSS cursor with raw PAD input. Random is deliberately
  // not used: adding an available stage must not change the FD regression.
  select_stage();
  uint32_t continued_music_completed=0,continued_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&continued_music_completed,
                                         &continued_music_revisited)&&
        continued_music_completed>initial_music_completed,
        "Original menu music did not continue loading across CSS/SSS scenes");
  transition();check(melee_web_menu_host_phase(host)==5,"SSS did not complete original selection");
  StartMeleeData raw_start{};
  check(melee_web_menu_host_raw_selection(host,&raw_start,error,sizeof(error)),error);
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  check(selection.start.rules.stkind==stage_kind,"Source SSS committed another stage");
  check(selection.start.players[0].ckind==(cycle==1?CKIND_FALCO:CKIND_MARIO),
        "Source CSS committed another P1 character");
  const uint32_t selection_rng=selection.random_seed;
  MeleeWebMenuMatchSelection raw_selection{};raw_selection.start=raw_start;
  raw_selection.random_seed=selection_rng;
  trace.event("sss_exit_complete",world->audio(),"match",&raw_selection,&selection_rng);
  world->close();world.reset();audio_phase=0;
  if(sudden_death_host_recipe){
   run_sudden_death_host_control(host,selection,raw_start,error,sizeof(error));
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&
         retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists(),
         "Typed Sudden Death host control changed the retained application arena");
   continue;
  }
  for(const auto& name:melee_web::match_asset_names(selection)){
   if(files.find(name)!=files.end())continue;
   auto path=std::filesystem::path(argv[1])/name;
   if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
   std::ifstream input(path,std::ios::binary);
   if(!input)throw std::runtime_error("Missing source match fixture: "+name);
   files[name]={(std::istreambuf_iterator<char>(input)),{}};
  }
  const MeleeWebPadState* menu_input=melee_web_menu_host_input(host);
  check(menu_input!=nullptr,"Original SSS did not retain PAD history for match entry");
  bool match_entry_recorded=false;
  if(cycle==0)for(unsigned stop:{0u,60u,100u}){
   // Unload both before and after Ready's stage-start callback, then rebuild
   // the full SDK world from the same immutable native selection.
   melee_web::GameplayMatchSession interrupted(files,selection,*menu_input);unsigned phase=0;
   if(!match_entry_recorded){const uint32_t rng=interrupted.random_seed();trace.event("match_enter_complete",interrupted.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   for(unsigned t=0;t<stop;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;interrupted.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(interrupted.audio(),pcm,count,error,sizeof(error)),error);
   }
   interrupted.close();interrupted.close();
  }
  if(cycle==0){
   // Exercise the source-owned pause/no-contest path from the same committed
   // menu payload before the ordinary stock run.  Every source tick still
   // drains the resident audio stream.
   melee_web::GameplayMatchSession no_contest(files,selection,*menu_input);unsigned phase=0;
   for(unsigned t=0;t<600&&!no_contest.ready();++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;no_contest.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.ready(),"Original Ready/Go did not complete for No Contest test");
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   no_contest.tick(pause);phase+=32000;unsigned count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!no_contest.paused();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.paused(),"Original P1 Start did not pause the match");
   for(unsigned t=0;t<12;t++){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   const uint32_t no_contest_lras=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;
   pause[0].button=no_contest_lras;
   no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   /* Keep the exact LRAS+A+Start sample held while the source ending drains;
    * the final HSD history handed back to CSS must be this real state. */
   for(unsigned t=0;t<500&&!no_contest.complete();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   int no_contest_winner=-1;
   check(no_contest.complete(),"Original No Contest did not complete its source ending");
   check(no_contest.outcome(no_contest_winner)==OUTCOME_NO_CONTEST&&no_contest_winner==-1,
         "Original No Contest outcome or winner was incorrect");
   uint8_t held_lras_input[MELEE_WEB_PAD_STATE_BYTES];
   melee_web_pad_state_capture(held_lras_input);
   uint32_t no_contest_seed=no_contest.random_seed();
   no_contest.close();
   MatchExitInfo canceled{};
   check(melee_web_match_rules_terminal_data(&canceled),"No Contest Results payload absent");
   std::cout<<"No Contest Results: outcome="<<unsigned(canceled.match_end.outcome)
            <<" winners="<<unsigned(canceled.match_end.n_winners)
            <<" kind="<<unsigned(canceled.match_end.match_kind)
            <<" selected-kind="<<unsigned(selection.start.rules.match_kind)<<'\n';
   check(
         canceled.match_end.outcome==OUTCOME_NO_CONTEST&&
         // Source ranking retains both equal-stock players even though the
         // canceled outcome has no gameplay winner. Do not normalize it.
         canceled.match_end.n_winners==selection.player_count&&
         canceled.match_end.match_kind==selection.start.rules.match_kind,
         "No Contest lost its complete original Results payload");
   if(results_mario_recipe){
    run_results_source_smoke(files,host,canceled,no_contest_seed,held_lras_input);
   }else check(melee_web_menu_host_match_finished(host,no_contest_seed,held_lras_input,error,sizeof(error)),error);

   /* Return immediately to CSS. The explicit Results recipe has handed the
    * source Results PAD back to the host, so let CSS settle on a neutral
    * sample before ordinary Start input selects the next match. The retail
    * recipe keeps its held-LRAS regression exactly as exercised above. */
   world=std::make_unique<melee_web::GameplayMenuWorld>(files);audio_phase=0;
   check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
   if(results_mario_recipe){
    raw[0].button=0;
    for(unsigned t=0;t<120;t++)check(tick()==1,"Results PAD history left CSS route unexpectedly");
   }else{
    raw[0].button=no_contest_lras;
    check(tick()==1,"Held LRAS unexpectedly transitioned CSS after No Contest");
    check(melee_web_menu_host_phase(host)==1,"Held LRAS did not remain in original CSS");
    raw[0].button=0;
    for(unsigned t=0;t<120;t++)check(tick()==1,"CSS release unexpectedly transitioned");
   }
   check(melee_web_menu_host_phase(host)==1,"CSS release left the original CSS route");
   transition();check(melee_web_menu_host_phase(host)==2,"CSS did not choose SSS after No Contest");
   rebuild_menu_scene();
   for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected post-No-Contest SSS transition");
   // Re-entering SSS can initialize a different source hover. Select the
   // requested tile through ordinary raw PAD input for every recipe.
   select_stage();
   transition();check(melee_web_menu_host_phase(host)==5,"SSS did not select the next match after No Contest");
   MeleeWebMenuMatchSelection next_selection{};
   check(melee_web_menu_host_selection(host,&next_selection,error,sizeof(error)),error);
   check(next_selection.start.rules.stkind==selection.start.rules.stkind&&
         next_selection.start.players[0].ckind==selection.start.players[0].ckind,
         "Ordinary CSS/SSS input changed the committed No Contest match selection");
   selection=next_selection;
   world->close();world.reset();audio_phase=0;
  }
  {
  const MeleeWebPadState* input=melee_web_menu_host_input(host);
   check(input!=nullptr,"Original SSS did not retain PAD history for ordinary match entry");
   melee_web::GameplayMatchSession match(files,selection,*input);audio_phase=0;
   MatchExitInfo fresh_result{};
   check(!melee_web_match_rules_terminal_data(&fresh_result),
         "Fresh source match retained a prior Results payload");
   if(!match_entry_recorded){const uint32_t rng=match.random_seed();trace.event("match_enter_complete",match.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   check(!match.ready(),"Original match intro was bypassed");
   const auto entry_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++)check(entry_stats.eyes[eye].image_is_base&&
      entry_stats.eyes[eye].image_index==UINT32_MAX&&entry_stats.eyes[eye].palette_is_base&&
      entry_stats.eyes[eye].palette_index==UINT32_MAX,
      "Original Entry eye telemetry did not retain owned base image/palette");
   const float ready_start_x=match.player_stats(0).position[0];
   unsigned intro_ticks=0;
   for(;intro_ticks<600&&!match.ready();++intro_ticks){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(intro_ticks<60)pads[0].stickX=80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    if(intro_ticks<60)check(match.player_stats(0).position[0]==ready_start_x,
                           "Fighter accepted movement before original Ready completion");
   }
   check(match.ready(),"Original Ready/Go did not reach gameplay");
   check(!match.paused(),"Original match entered gameplay already paused");
   const auto active_frame=match.source_frames();
   for(unsigned t=0;t<3;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;match.tick(pads);
    audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.source_frames()>active_frame,"Original active source frame did not advance");
   const auto wait_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++){
    check(wait_stats.eyes[eye].image_count>0,
          "Original Wait eye telemetry lost its owned animation bounds");
    check((wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index<wait_stats.eyes[eye].image_count),
          "Original Wait eye image state is outside its owned base/table representation");
    check((wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index<wait_stats.eyes[eye].palette_count),
          "Original Wait eye palette state is outside its owned base/table representation");
   }

   // Start pauses through the original pauser path.  The source scheduler and
   // audio continue to tick, but fighter actions/animation/positions and the
   // match frame must remain held while the pause is debounced.
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.paused(),"Original P1 Start did not enter pause");
   const auto paused_frame=match.source_frames();
   const auto paused_player0=match.player_stats(0);
   const auto paused_player1=match.player_stats(1);
   for(unsigned t=0;t<20;t++){
    pause[1].button=t==15?PAD_BUTTON_START:0;
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    pause[1].button=0;
    const auto held0=match.player_stats(0);
    const auto held1=match.player_stats(1);
    check(match.paused()&&match.source_frames()==paused_frame&&
          held0.motion_id==paused_player0.motion_id&&
          held0.animation_frame==paused_player0.animation_frame&&
          held0.position[0]==paused_player0.position[0]&&
          held0.position[1]==paused_player0.position[1]&&held0.stocks==paused_player0.stocks&&
          held1.motion_id==paused_player1.motion_id&&
          held1.animation_frame==paused_player1.animation_frame&&
          held1.position[0]==paused_player1.position[0]&&
          held1.position[1]==paused_player1.position[1]&&held1.stocks==paused_player1.stocks,
          "Wrong-port Start or paused source tick changed the match");
   }
   pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   auto resumed_frame=match.source_frames();
   for(unsigned t=0;t<30&&match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    resumed_frame=match.source_frames();
   }
   check(!match.paused()&&resumed_frame>paused_frame,
         "Original P1 Start did not resume after pause debounce");
   std::cout<<"Original Ready/Go completed at "<<intro_ticks<<" ticks\n";
   check(match.hud_damage(0)==0&&match.hud_damage(1)==0,"Original player damage HUD did not initialize");
   // Battlefield's authored spawn puts P2 on the upper platform. Use source
   // input to reach the main floor before testing a horizontal projectile.
   for(unsigned t=0;t<180&&
       std::abs(match.player_stats(1).position[1]-
                match.player_stats(0).position[1])>=5.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;pads[1].stickY=-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[1]-
                  match.player_stats(0).position[1])<5.0f,
         "Original input did not bring both fighters to the same stage level");
   // Approach with source input so the opponent lies inside the projectile's
   // actual lifetime and range.
   for(unsigned t=0;t<120&&
       std::abs(match.player_stats(1).position[0]-
                match.player_stats(0).position[0])>=35.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    pads[0].stickX=match.player_stats(1).position[0]>
                           match.player_stats(0).position[0]?80:-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[0]-
                  match.player_stats(0).position[0])<35.0f,
         "Original movement did not reach projectile test range");
   // Let the actual selected fighter projectile hit the opponent, then
   // observe the original HUD consumer catching up to source player damage.
   const float projectile_start_damage=match.player_stats(1).damage_percent;
   for(unsigned t=0;t<240&&
       match.player_stats(1).damage_percent==projectile_start_damage;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t%8==0)pads[0].button=PAD_BUTTON_B;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   for(unsigned t=0;t<30&&
       match.hud_damage(1)!=int(match.player_stats(1).damage_percent);++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.player_stats(1).damage_percent>projectile_start_damage&&
         match.hud_damage(1)==int(match.player_stats(1).damage_percent),
         "Original HUD did not display actual projectile damage");
   bool lost=false,jump=false;int stocks=4,respawns=0,winner=-1;unsigned t=0;
   unsigned ending_ticks=0;MeleeWebMatchStats held_players[2]{};
   for(;t<4000;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t>=20&&!lost)pads[0].stickX=80;if(jump)pads[0].button=PAD_BUTTON_X;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    auto player=match.player_stats(0);check(match.player_stats(1).stocks==4,"Stationary match opponent lost a stock");
    jump=!lost&&stocks<4&&player.ground_or_air==0&&player.position[0]>65;
    if(player.stocks<stocks){lost=true;stocks=player.stocks;}
    if(lost&&player.motion_id==14&&player.ground_or_air==0){lost=false;++respawns;}
    const int outcome=match.outcome(winner);
    if(match.ending()){
     check(outcome!=0,"Original ending started before the source outcome");
     for(unsigned slot=0;slot<2;++slot){
      const auto current=match.player_stats(slot);
      if(ending_ticks){
       check(current.motion_id==held_players[slot].motion_id&&
             current.animation_frame==held_players[slot].animation_frame&&
             current.position[0]==held_players[slot].position[0]&&
             current.position[1]==held_players[slot].position[1]&&
             current.stocks==held_players[slot].stocks,
             "Fighter processes advanced during original GAME freeze");
      }else held_players[slot]=current;
     }
     ++ending_ticks;
    }
    if(match.complete()){check(outcome!=0&&ending_ticks>0,"Source exit skipped the original ending");break;}
   }
   check(t<4000&&stocks==0&&respawns==3,"Native menu match did not complete original four-stock outcome");
   std::cout<<"Original GAME ending and source transition completed across "<<ending_ticks<<" frozen ticks\n";
   uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];
   melee_web_pad_state_capture(final_input);
   int timer_before_close=0;
   GetMatchTimer(&timer_before_close);
   MatchExitInfo published_once{},published_twice{};
   check(melee_web_match_rules_publish_result(),
         "Completed source match did not publish its Results payload");
   check(melee_web_match_rules_terminal_data(&published_once),
         "Completed source match did not retain its first Results payload");
   check(melee_web_match_rules_publish_result(),
         "Repeated source Results publication was not idempotent");
   check(melee_web_match_rules_terminal_data(&published_twice)&&
         std::memcmp(&published_once,&published_twice,sizeof(published_once))==0,
         "Repeated source Results publication changed the retained payload");
   uint32_t seed=match.random_seed();match.close();
   int terminal_outcome=0,winner_count=0,winners[6]{};
   check(melee_web_match_rules_terminal_result(&terminal_outcome,&winner_count,winners)&&
         terminal_outcome==OUTCOME_ELIMINATION&&winner_count==1&&winners[0]==1,
         "Native menu match did not publish the original elimination winner at close");
   MatchExitInfo result{};
   check(melee_web_match_rules_terminal_data(&result)&&
         result.x0==selection.start.rules.x18&&result.x4==uint32_t(timer_before_close)&&
         result.match_end.outcome==terminal_outcome&&
         result.match_end.match_kind==selection.start.rules.match_kind&&
         result.match_end.frame_count>0&&result.match_end.n_winners==1&&
         result.match_end.winners[0]==1,
         "Original VS OnExit did not retain complete Results metadata");
   for(unsigned slot=0;slot<selection.player_count;++slot){
    const auto& player=result.match_end.player_standings[slot];
    check(player.ckind==selection.start.players[slot].ckind&&
          player.slot_type==selection.start.players[slot].slot_type&&
          player.x3==selection.start.players[slot].color&&
          player.stocks==(slot==0?0:4),
          "Original Results player identity or final stocks were lost at teardown");
   }
   if(results_mario_recipe){
    run_results_source_smoke(files,host,result,seed,final_input);
   }else check(melee_web_menu_host_match_finished(host,seed,final_input,error,sizeof(error)),error);
  }
  world=std::make_unique<melee_web::GameplayMenuWorld>(files);audio_phase=0;
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected return CSS transition");
  check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
  world->verify_immutable_archives();world->close();world->close();world.reset();
  check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
  const auto retained=melee_web_gameplay_allocation();
  check(retained.identity==session_allocation.identity&&
        retained.generation==session_allocation.generation&&
        retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists(),
        "Menu/match teardown replaced the application's retained source arena");
 }
 check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
 if(link_css_unload_recipe)
  std::cout<<"Native Link/Young Link CSS audio registry and unload smoke passed; no match/rendered claim\n";
 else if(sudden_death_host_recipe)
  std::cout<<"Native menu-host Sudden Death callback handoff passed through source CSS/SSS; "
              "constructed callback control only, no GameplayMatchSession or Results-scene claim\n";
 else if(results_mario_recipe)
  std::cout<<"Native source Mario Results smoke (No Contest and elimination) returned to CSS; no retail/rendered claim\n";
 else
  std::cout<<"Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice; no browser or equivalence claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
