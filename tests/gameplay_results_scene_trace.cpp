#include "gameplay_compat.h"
#include "gameplay_results_assets.hpp"
#include "gameplay_results_session.hpp"
#include "gameplay_match_session.hpp"
#include "gameplay_match_rules.h"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_pad_state.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_menu_host.h"
#include "gameplay_menu_world.hpp"
#include "gameplay_fighter_assets.h"
#include "results_source_pad_schedule.hpp"
#include "gameplay_save_profile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <aurora/pipeline_prepare.h>
#include <dolphin/gx.h>
#include <emscripten.h>
#include <SDL3/SDL_hints.h>
#endif

extern "C" {
#include <melee/gm/types.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/cm/forward.h>
#include <melee/ft/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftZelda/forward.h>
#include <melee/ef/types.h>
#include <melee/ef/efasync.h>
#include <melee/ef/efdata.h>
#include <melee/ef/eflib.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/controller.h>
extern EF_DAT_Entry efAsync_DatEntries[51];
extern HSD_Archive* _Toy_sbss_804D6ED0;
HSD_GObj* Player_GetEntity(s32 slot);
// ftlib.h includes C-only Fighter fields (catch/throw). Use its existing
// read-only C entry points rather than duplicating a Fighter layout in C++.
enum_t ftLib_GetMotionId(HSD_GObj*);
FighterKind ftLib_GetKind(HSD_GObj*);
CmSubject* ftLib_80086B74(HSD_GObj*);
extern void* it_804D6D28;
extern void* it_804D6D40;
extern void* it_804D6D04;
extern void* it_804D6D20;
extern void* it_804D6D24;
extern void* it_804D6D30;
extern void* it_804D6D38;
uint32_t melee_web_camera_pool_allocation_generation_value(void);
int melee_web_camera_pool_last_subject_count_value(void);
void Item_80266FA8(void);
void Item_80266FCC(void);
extern CmSubject* cm_804D645C;
extern ResultsData lbl_8046DBE8;
int melee_web_vs_mode_begin(void);
int melee_web_vs_mode_end(void);
int melee_web_vs_mode_select_state(int);
int melee_web_vs_mode_next_state(void);
/* The mode table retains preload callbacks outside this Results-only target.
 * Reuse the existing fail-loud diagnostic boundary if one is reached. */
#include "native_menu_alarm_unavailable.c"
#include "native_menu_stage_input.c"
}

static void load_directory(melee_web::RuntimeFiles& files,
                           const std::filesystem::path& directory)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read owned Results input");
        files[entry.path().filename().string()] =
            {std::istreambuf_iterator<char>(input), {}};
    }
}

static ResultsMatchInfo make_results_match(int opponent_ckind,
                                           bool opponent_wins)
{
    ResultsMatchInfo result{};
    result.match_end.outcome = OUTCOME_ELIMINATION;
    result.match_end.match_kind = 0;
    result.match_end.is_teams = 0;
    result.match_end.n_winners = 1;
    const auto* opponent = melee_web_fighter_content(opponent_ckind);
    const auto* mario = melee_web_fighter_content(CKIND_MARIO);
    if (!opponent || !mario)
        throw std::runtime_error("Results fighter content is unavailable");
    for (auto& player : result.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    auto fill = [](auto& player, const MeleeWebFighterContent& fighter,
                   bool winner) {
        player.slot_type = Gm_PKind_Human;
        player.ckind = static_cast<s8>(fighter.character_kind);
        player.ftkind = static_cast<s8>(fighter.fighter_kind);
        player.x3 = 0;
        player.x4 = 0;
        player.is_big_loser = winner ? 0 : 1;
        player.is_small_loser = winner ? 0 : 1;
        player.team = 0;
        player.stocks = 0;
    };
    fill(result.match_end.player_standings[0], *opponent, opponent_wins);
    fill(result.match_end.player_standings[1], *mario, !opponent_wins);
    result.match_end.winners[0] = opponent_wins ? 0 : 1;
    return result;
}

static void check_results_teardown();
static void check_results_fighter_leases(const ResultsMatchInfo&);
static std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES>
results_pad_snapshot(std::uint32_t copy_winner_buttons = 0);

struct WinnerDemoControl {
    const char* button_name;
    std::uint32_t button;
    int variant;
    ftCommon_MotionState motion;
    const char* motion_name;
};
// fn_8017A67C consumes CopyPAD B/Y/X, then Player_80036F34 passes the variant
// to ftDemo_CreateFighter's on_create_fighter table. ft_0BEC supplies these
// authored demo states; DeadUpStarIce is enum value 5, not DeadUpStar (4).
static constexpr std::array<WinnerDemoControl, 3> winner_demo_controls{{
    {"b", PAD_BUTTON_B, 0, ftCo_MS_DeadDown, "ftCo_MS_DeadDown"},
    {"y", PAD_BUTTON_Y, 1, ftCo_MS_DeadRight, "ftCo_MS_DeadRight"},
    {"x", PAD_BUTTON_X, 2, ftCo_MS_DeadUpStarIce, "ftCo_MS_DeadUpStarIce"},
}};

static void check_constructed_winner_demo(const ResultsMatchInfo& result,
                                         const WinnerDemoControl& control)
{
    const auto& winner = result.match_end.player_standings[2];
    auto* entity = Player_GetEntity(2);
    if (result.match_end.n_winners != 1 || result.match_end.winners[0] != 2 ||
        winner.ckind != CKIND_ZELDA || winner.ftkind != FTKIND_SEAK ||
        HSD_PadCopyStatus[2].button != control.button || !entity || !entity->user_data)
        throw std::runtime_error("Winner demo lost Zelda-origin Sheik/CopyPAD input identity");
    const auto motion = ftLib_GetMotionId(entity);
    if (ftLib_GetKind(entity) != FTKIND_SEAK || motion != control.motion ||
        !ftLib_80086B74(entity))
        throw std::runtime_error("Constructed winner demo motion/subject differs: expected " +
            std::to_string(control.motion) + " observed " + std::to_string(motion));
    check_results_fighter_leases(result);
    std::cout << " winner_demo_variant=" << control.variant
              << " CopyPAD[2].held=" << control.button_name
              << " winner_demo_motion=" << motion
              << " name=" << control.motion_name << '\n' << std::flush;
}

static void draw_results(melee_web::GameplayResultsSession& session)
{
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
    aurora_update();
    if (!aurora_begin_frame())
        throw std::runtime_error("Rendered Results frame is unavailable");
    session.draw();
    aurora_end_frame();
    EM_ASM({ window.resultsFrame = $0; }, session.source_frames());
    emscripten_sleep(16);
#else
    (void) session;
    throw std::runtime_error("Results drawing requires an initialized rendered target");
#endif
}

static MeleeWebMenuHost* prepare_results_host(const melee_web::RuntimeFiles& files)
{
    // Reach the real host's closed-SSS state using source input. No match runs:
    // the caller explicitly supplies a synthetic MatchExitInfo afterwards.
    char error[256]{};
    const auto require = [&](int ok) {
        if (!ok) throw std::runtime_error(error);
    };
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    require(host != nullptr);
    melee_web::GameplayMenuWorld world(files);
    require(melee_web_menu_host_enter(host, world.audio(), error, sizeof(error)));
    PADStatus pads[4]{};
    pads[2].err = pads[3].err = -1;
    float pcm[1068]; unsigned audio_phase = 0;
    const auto tick = [&] {
        const int state = melee_web_menu_host_tick(host, pads, error, sizeof(error));
        require(state == 1 || state == 3);
        audio_phase += 32000;
        const auto samples = audio_phase / 60; audio_phase %= 60;
        require(melee_web_audio_render(world.audio(), pcm, samples, error, sizeof(error)));
        return state;
    };
    const auto transition = [&] {
        pads[0].button = PAD_BUTTON_START;
        int state = tick(); pads[0].button = 0;
        for (unsigned t = 0; t < 120 && state != 3; ++t) state = tick();
        if (state != 3) throw std::runtime_error("Results host fixture menu did not transition");
        require(melee_web_menu_host_leave(host, 0, error, sizeof(error)));
    };
    for (unsigned t = 0; t < 120; ++t) tick();
    transition();
    if (melee_web_menu_host_phase(host) != 2)
        throw std::runtime_error("Results host fixture did not reach SSS");
    world.rebuild_scene(melee_web::GameplayMenuScene::Stages);
    require(melee_web_menu_host_enter(host, world.audio(), error, sizeof(error)));
    for (unsigned t = 0; t < 120; ++t) tick();
    bool at_target = false;
    for (unsigned t = 0; t < 120; ++t) {
        MeleeWebStageInputObservation observed{};
        if (!melee_web_stage_input_observe(St_Kind_Last, &observed))
            throw std::runtime_error("Results host fixture cannot observe FD");
        const int state = melee_web_stage_input_drive(pads, &observed, St_Kind_Last);
        if (state == MELEE_WEB_STAGE_INPUT_INVALID)
            throw std::runtime_error("Results host fixture cannot drive FD");
        if (state == MELEE_WEB_STAGE_INPUT_AT_TARGET) { at_target = true; break; }
        tick();
    }
    if (!at_target) throw std::runtime_error("Results host fixture did not select FD");
    transition();
    if (melee_web_menu_host_phase(host) != 5)
        throw std::runtime_error("Results host fixture did not close SSS");
    world.close();
    return host;
}

static ResultsMatchInfo make_results_lineup(std::span<const int> ckind)
{
    ResultsMatchInfo result{};
    result.match_end.outcome = OUTCOME_ELIMINATION;
    result.match_end.match_kind = 0;
    result.match_end.is_teams = 0;
    result.match_end.n_winners = 1;
    for (auto& player : result.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    for (std::size_t slot = 0; slot < ckind.size(); ++slot) {
        const auto* fighter = melee_web_fighter_content(ckind[slot]);
        if (!fighter) throw std::runtime_error("Results lineup has an unknown character kind");
        auto& player = result.match_end.player_standings[slot];
        player.slot_type = Gm_PKind_Human;
        player.ckind = static_cast<s8>(fighter->character_kind);
        player.ftkind = static_cast<s8>(fighter->fighter_kind);
        player.x3 = 0;
        player.x4 = 0;
        player.is_big_loser = slot == 0 ? 0 : 1;
        player.is_small_loser = slot == 0 ? 0 : 1;
        player.team = 0;
        player.stocks = 0;
    }
    result.match_end.winners[0] = 0;
    return result;
}

static void (*handoff_original_mode_exit)(GameModeState*);
static unsigned handoff_mode_exit_calls;
static void count_handoff_mode_exit(GameModeState* state)
{
    ++handoff_mode_exit_calls;
    handoff_original_mode_exit(state);
}

static void check_results_handoff_rejection(
    MeleeWebMenuHost* host, melee_web::GameplayResultsSession& session)
{
    // Intentional entry faults, not a reproduction of the historical writer.
    // The callback probe forwards the real body if invoked; both faulty calls
    // must reject before reaching it. Restore faults only here in the fixture.
    auto* const profile = gmMainLib_804D3EE0;
    auto* const pool = cm_804D645C;
    if (!profile || profile != gmMainLib_GetProfileRoot() || !pool)
        throw std::runtime_error("Results handoff fixture has no owned profile/pool");
    gmm_x0 foreign_profile = *profile;
    std::array<std::uint8_t, sizeof(foreign_profile)> foreign_before{};
    std::memcpy(foreign_before.data(), &foreign_profile, foreign_before.size());
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES> profile_before{};
    std::memcpy(profile_before.data(), profile, profile_before.size());
    std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES> pad_before{};
    melee_web_pad_state_capture(pad_before.data());
    const auto before = melee_web_gameplay_stats();
    const auto frames = session.source_frames();
    const auto seed = session.random_seed();
    const int next = melee_web_vs_mode_next_state();
    const int phase = melee_web_menu_host_phase(host);
    handoff_original_mode_exit = gm_Mode_Vs_States[4].on_exit;
    handoff_mode_exit_calls = 0;
    struct RestoreFaults {
        gmm_x0* profile;
        CmSubject* pool;
        ~RestoreFaults() {
            gmMainLib_804D3EE0 = profile;
            cm_804D645C = pool;
            gm_Mode_Vs_States[4].on_exit = handoff_original_mode_exit;
        }
    } restore{profile, pool};
    gm_Mode_Vs_States[4].on_exit = count_handoff_mode_exit;
    const auto reject = [&](bool profile_fault) {
        char error[256]{};
        if (profile_fault) gmMainLib_804D3EE0 = &foreign_profile;
        else cm_804D645C = nullptr;
        const int accepted = melee_web_menu_host_results_exit(host, error, sizeof(error));
        gmMainLib_804D3EE0 = profile;
        cm_804D645C = pool;
        const std::string failure = error;
        const char* expected = profile_fault ?
            "Results mode OnExit entry: Original save/profile root is not the owned backing" :
            "Original Results camera pool ownership changed at mode OnExit entry";
        if (accepted || !failure.starts_with(expected) || handoff_mode_exit_calls)
            throw std::runtime_error("Results handoff did not reject before callback: " + failure);
        if (!profile_fault && (failure.find("context_pool=") == std::string::npos ||
                               failure.find("initial=") == std::string::npos))
            throw std::runtime_error("Results handoff omitted camera triple");
        std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES> pad_after{};
        melee_web_pad_state_capture(pad_after.data());
        const auto after = melee_web_gameplay_stats();
        if (std::memcmp(profile_before.data(), profile, profile_before.size()) ||
            std::memcmp(foreign_before.data(), &foreign_profile, foreign_before.size()) ||
            pad_after != pad_before || session.source_frames() != frames ||
            session.random_seed() != seed || melee_web_vs_mode_next_state() != next ||
            melee_web_menu_host_phase(host) != phase || before.ticks != after.ticks ||
            before.generation != after.generation || before.objects != after.objects ||
            before.processes != after.processes || before.heap_free_bytes != after.heap_free_bytes)
            throw std::runtime_error("Rejected Results handoff changed profile/PAD/source state");
        std::cout << " rejected " << (profile_fault ? "profile root" : "camera pool")
                  << " before mode OnExit" << std::flush;
    };
    reject(true);
    reject(false);
}

// Reads the original typed Results/PAD state; never supplies a page, phase,
// trigger, or confirmation flag. These source ticks are a chosen discriminator,
// not a conversion of the historical browser's wall-clock Enter pulses.
struct P1StatisticsControl {
    std::array<unsigned, 2> auto_page_tick{};
    std::array<unsigned, 4> last_pages{};
    const unsigned confirmation_tick;
    const bool browser_cadence;
    melee_web::ResultsSourcePadSchedule pad_schedule;
    unsigned scheduled_pad_buttons = 0;
    unsigned scheduled_pad_remaining = 0;
    unsigned edges = 0, releases = 0, held_ticks = 0;
    int last_phase = -1, last_stats_phase = -1;

    explicit P1StatisticsControl(bool use_browser_cadence)
        : confirmation_tick(use_browser_cadence ? 539U : 600U),
          browser_cadence(use_browser_cadence)
    {
        for (const unsigned tick : {180U, 360U, confirmation_tick}) {
            if (!pad_schedule.enqueue({tick, 0, PAD_BUTTON_START, 10}))
                throw std::runtime_error("P1 statistics: cannot arm exact source-tick Start schedule");
        }
    }

    bool held(unsigned tick) const
    {
        return (tick >= 180 && tick < 190) ||
               (tick >= 360 && tick < 370) ||
               (tick >= confirmation_tick && tick < confirmation_tick + 10);
    }
    void prepare(unsigned source_frame, PADStatus (&pads)[4])
    {
        if (source_frame == confirmation_tick) {
            const auto& data = lbl_8046DBE8;
            if (browser_cadence) {
                std::cout << "p1-statistics before-confirm source_frame=" << confirmation_tick
                          << " phase=" << unsigned(data.x1) << " pages="
                          << unsigned(data.player_data[0].page) << ','
                          << unsigned(data.player_data[1].page) << ','
                          << unsigned(data.player_data[2].page) << ','
                          << unsigned(data.player_data[3].page) << " auto_page_source_frames="
                          << auto_page_tick[0] << ',' << auto_page_tick[1]
                          << " scope=browser-observed-dispatch-brackets-not-historical\n"
                          << std::flush;
            } else {
                if (!auto_page_tick[0] || !auto_page_tick[1] || data.x1 != 3 ||
                    data.player_data[2].page != 1 || data.player_data[3].page != 1)
                    throw std::runtime_error("P1 statistics: both disconnected CPU pages must auto-advance before confirmation");
                std::cout << "p1-statistics before-confirm source_frame=600 auto_pages=1,1 auto_page_source_frames="
                          << auto_page_tick[0] << ',' << auto_page_tick[1] << '\n' << std::flush;
            }
        }
        melee_web::ResultsSourcePadEvent event{};
        const auto boundary = pad_schedule.before_tick(source_frame, event);
        if (boundary == melee_web::ResultsSourcePadSchedule::Boundary::missed)
            throw std::runtime_error("P1 statistics: missed exact source-tick Start target " +
                                     std::to_string(source_frame));
        if (boundary == melee_web::ResultsSourcePadSchedule::Boundary::due) {
            if (scheduled_pad_remaining || pads[event.port].err != 0)
                throw std::runtime_error("P1 statistics: scheduled Start overlaps or P1 is disconnected");
            scheduled_pad_buttons = event.buttons;
            scheduled_pad_remaining = event.duration;
        }
        if (scheduled_pad_remaining) {
            pads[0].button = static_cast<u16>(scheduled_pad_buttons);
            --scheduled_pad_remaining;
        }
    }
    void observe(unsigned completed_frames)
    {
        if (!completed_frames)
            throw std::runtime_error("P1 statistics: cannot observe a PAD sample before the first source tick");
        const unsigned source_frame = completed_frames - 1;
        const auto& data = lbl_8046DBE8;
        const bool edge = source_frame == 180 || source_frame == 360 ||
                          source_frame == confirmation_tick;
        const bool release = source_frame == 190 || source_frame == 370 ||
                             source_frame == confirmation_tick + 10;
        for (unsigned slot = 0; slot < 4; ++slot) {
            const auto& pad = HSD_PadCopyStatus[slot];
            if (pad.err != (slot < 2 ? 0 : -1) ||
                HSD_PadMasterStatus[slot].err != pad.err ||
                pad.button != (slot == 0 && held(source_frame) ? PAD_BUTTON_START : 0) ||
                pad.trigger != (slot == 0 && edge ? PAD_BUTTON_START : 0) ||
                pad.release != (slot == 0 && release ? PAD_BUTTON_START : 0))
                throw std::runtime_error("P1 statistics: source PAD edge/held/connectivity differs at source frame " + std::to_string(source_frame));
        }
        edges += edge; releases += release; held_ticks += held(source_frame);
        if (edge || release)
            std::cout << "p1-statistics input source_frame=" << source_frame
                      << " state_after_tick=" << completed_frames
                      << " port=0 held=" << held(source_frame)
                      << " trigger=" << edge << " release=" << release
                      << " phase=" << unsigned(data.x1) << '\n' << std::flush;
        std::array<unsigned, 4> pages{};
        for (unsigned slot = 0; slot < 4; ++slot) {
            pages[slot] = data.player_data[slot].page;
            if (slot < 2 && pages[slot] != 0)
                throw std::runtime_error("P1 statistics: connected neutral CPU unexpectedly changed page");
            if (slot >= 2 && pages[slot] != last_pages[slot]) {
                if (last_pages[slot] != 0 || pages[slot] != 1 ||
                    (!browser_cadence && (data.x1 != 3 || source_frame >= confirmation_tick)) ||
                    data.x0_23 != 2 ||
                    HSD_PadCopyStatus[slot].err != -1)
                    throw std::runtime_error("P1 statistics: disconnected CPU page transition is not the required source auto-advance");
                auto_page_tick[slot - 2] = source_frame;
                std::cout << "p1-statistics auto-page slot=" << slot
                          << " from=0 to=1 source_frame=" << source_frame
                          << " state_after_tick=" << completed_frames << '\n' << std::flush;
            }
        }
        if (last_phase != data.x1 || last_stats_phase != data.x0_23 || pages != last_pages) {
            std::cout << "p1-statistics state_after_tick=" << completed_frames
                      << " last_input_source_frame=" << source_frame
                      << " phase=" << unsigned(data.x1)
                      << " stats_phase=" << unsigned(data.x0_23) << " pages="
                      << pages[0] << ',' << pages[1] << ',' << pages[2] << ',' << pages[3]
                      << " timers=" << data.player_data[2].x2 << ',' << data.player_data[3].x2
                      << '\n' << std::flush;
            last_phase = data.x1; last_stats_phase = data.x0_23; last_pages = pages;
        }
        if (!browser_cadence && data.x1 == 4 &&
            (source_frame < confirmation_tick || !auto_page_tick[0] || !auto_page_tick[1]))
            throw std::runtime_error("P1 statistics: Results confirmed before both disconnected CPU pages advanced");
    }
    void finish(unsigned frames, unsigned draws, bool draw) const
    {
        if (frames <= confirmation_tick + 10 ||
            frames > (browser_cadence ? 700U : 900U) ||
            edges != 3 || releases != 3 || held_ticks != 30 ||
            !pad_schedule.all_consumed() || scheduled_pad_remaining ||
            (!browser_cadence && (!auto_page_tick[0] || !auto_page_tick[1])) ||
            lbl_8046DBE8.x1 != 4 ||
            draws != (draw ? frames : 0))
            throw std::runtime_error("P1 statistics: incomplete held-input/page/exit coverage");
        std::cout << "p1-statistics coverage frames=" << frames << " trigger_edges=" << edges
                  << " releases=" << releases << " held_ticks=" << held_ticks
                  << " auto_page_source_frames=" << auto_page_tick[0] << ',' << auto_page_tick[1]
                  << " source_draw_api_calls=" << draws << '\n' << std::flush;
    }
};

static int run_real_lineup(const melee_web::RuntimeFiles& files,
                           std::span<const int> roster,
                           const char* roster_name, bool sheik_confirm = false,
                           bool mode_exit = false, bool draw = false,
                           bool sheik_standing = false,
                           bool pool_guard = false, bool host_route = false,
                           bool stock = false, bool delayed_confirmation = false,
                           bool handoff_guard = false,
                           const WinnerDemoControl* winner_demo = nullptr,
                           bool p1_statistics = false,
                           bool p1_statistics_browser_cadence = false)
{
    char error[256]{};
    if (!melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                          error, sizeof(error)))
        throw std::runtime_error(error);
    bool ended = false;
    try {
        const auto input_bytes = results_pad_snapshot(winner_demo ? winner_demo->button : 0);
        std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)> input(
            melee_web_pad_state_decode(input_bytes.data(), input_bytes.size(),
                                       error, sizeof(error)),
            melee_web_pad_state_free);
        if (!input) throw std::runtime_error(error);
        auto* host = host_route ? prepare_results_host(files) : nullptr;
        auto result = make_results_lineup(roster);
        // A categorical control for the observed natural Stock Battle failure;
        // all other synthetic fixture inputs stay unchanged.
        if (stock) result.match_end.match_kind = MatchKind_Stock;
        if (sheik_confirm) {
            for (unsigned slot = 0; slot < 4; ++slot) {
                auto& p = result.match_end.player_standings[slot];
                p.slot_type = Gm_PKind_Cpu;
                p.is_big_loser = p.is_small_loser = slot != 2;
            }
            result.match_end.winners[0] = 2;
            result.match_end.player_standings[2].ftkind = FTKIND_SEAK;
            if (sheik_standing)
                result.match_end.player_standings[2].ckind = CKIND_SEAK;
        }
        if (host) {
            MatchExitInfo exit_info{};
            exit_info.match_end = result.match_end;
            if (!melee_web_menu_host_match_finished(host, 0x13579bdfU,
                    input_bytes.data(), error, sizeof(error)) ||
                !melee_web_menu_host_results_begin(host, &exit_info, 0x13579bdfU,
                    &result, error, sizeof(error)))
                throw std::runtime_error(error);
        }
        std::cout << "Results " << roster_name << " match_kind="
                  << static_cast<unsigned>(result.match_end.match_kind)
                  << " four-source lineup..."
                  << std::flush;
        if (sheik_confirm) {
            const auto& winner = result.match_end.player_standings[2];
            std::cout << " winner_ckind=" << static_cast<int>(winner.ckind)
                      << " winner_ftkind=" << static_cast<int>(winner.ftkind)
                      << std::flush;
        }
        melee_web::GameplayResultsSession session(files, result, 0x13579bdfU,
                                                   *input);
        P1StatisticsControl statistics_control(p1_statistics_browser_cadence);
        const auto entry_camera = session.camera_entry_snapshot();
        std::cout << "results-camera entry saved="
                  << entry_camera.saved_pool_at_context_begin
                  << " before_onenter=" << entry_camera.source_pool_before_onenter
                  << " owner_before_onenter=" << entry_camera.owner_pool_before_onenter
                  << " after_onenter=" << entry_camera.source_pool_after_onenter
                  << " after_collision="
                  << entry_camera.source_pool_after_collision_adoption
                  << " context_after_collision="
                  << entry_camera.context_pool_after_adoption
                  << " owner_after_collision=" << entry_camera.owner_pool_after_adoption
                  << " generation="
                  << entry_camera.source_camera_allocation_generation_before_onenter
                  << "->"
                  << entry_camera.source_camera_allocation_generation_after_onenter
                  << "->"
                  << entry_camera.source_camera_allocation_generation_after_collision_adoption
                  << " subjects="
                  << entry_camera.source_camera_allocation_subject_count_after_onenter
                  << "->"
                  << entry_camera.source_camera_allocation_subject_count_after_collision_adoption
                  << '\n' << std::flush;
        if (p1_statistics) {
            if (!host || result.match_end.match_kind != MatchKind_Stock ||
                result.match_end.player_standings[2].ckind != CKIND_ZELDA ||
                result.match_end.player_standings[2].ftkind != FTKIND_SEAK)
                throw std::runtime_error("P1 statistics lost original synthetic Stock 18/7 host setup");
            check_results_fighter_leases(result);
            std::cout << "\np1-statistics seed=324508639 connected=0,1 disconnected=2,3"
                         " pulse_ticks=180,360," << statistics_control.confirmation_tick
                      << " hold_ticks=10 cap=" << (p1_statistics_browser_cadence ? 700 : 900)
                      << " scope=" << (p1_statistics_browser_cadence ?
                             "browser-observed-dispatch-brackets-not-historical" :
                             "chosen-source-ticks-not-historical-replay")
                      << " host_profile=default-CSS-subset"
                      << " draw_scope=" << (draw ? "rendered-GPU" : "unrun native-state-only")
                      << '\n' << std::flush;
        }
        if (winner_demo) {
            check_constructed_winner_demo(result, *winner_demo);
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
            std::cout << "draw_scope=rendered-GPU\n" << std::flush;
#else
            std::cout << "draw_scope=unrun native-state-only\n" << std::flush;
#endif
        }
        const auto initial_pool = cm_804D645C;
        const auto trace_camera = [&](const char* phase) {
            std::cout << "results-camera boundary=" << phase
                      << " source_frame=" << session.source_frames()
                      << " source_pool=" << static_cast<const void*>(cm_804D645C)
                      << " allocation_generation="
                      << melee_web_camera_pool_allocation_generation_value()
                      << " last_subject_count="
                      << melee_web_camera_pool_last_subject_count_value()
                      << '\n' << std::flush;
        };
        const auto check_pool = [&](const char* phase) {
            if (cm_804D645C != initial_pool) {
                std::cerr << "Results pool first changed at " << phase
                          << " tick=" << session.source_frames()
                          << " initial=" << initial_pool
                          << " source=" << cm_804D645C << '\n';
                throw std::runtime_error("Results source camera pool changed");
            }
        };
        if (session.source_frames() != 0)
            throw std::runtime_error("Results scene advanced during construction");
        PADStatus neutral[4]{};
        neutral[2].err = neutral[3].err = -1;
        unsigned draw_api_calls = 0;
        const auto draw_frame = [&] {
            trace_camera("draw-before");
            draw_results(session);
            ++draw_api_calls;
            trace_camera("draw-after");
            check_pool("draw/submission");
        };
        for (unsigned tick = 0; tick < 2; ++tick) {
            trace_camera("tick-before");
            session.tick(neutral);
            trace_camera("tick-after");
            check_pool("tick");
            if (draw) draw_frame();
        }
        if (session.source_frames() != 2 || session.requested())
            throw std::runtime_error("Short Results tick changed source transition state");
        const auto report_fighter_owner = [&](const char* phase) {
            char ownership_error[256]{};
            if (melee_web_fighter_assets_check_owned(phase, ownership_error,
                                                     sizeof(ownership_error)))
                return;
            std::cerr << "Results camera-guard diagnostic first fighter owner change at "
                      << phase << ": " << ownership_error << '\n' << std::flush;
        };
        if (pool_guard) {
            // Deliberate fault injection, not a reproduction of the browser's
            // unknown writer. Each API must reject before advancing the scene
            // or destroying its owners; restore the test fault before cleanup.
            if (!initial_pool)
                throw std::runtime_error("Results pool guard fixture has no camera pool");
            const auto expect_rejected = [&](const char* phase, auto action) {
                const auto frames = session.source_frames();
                const auto seed = session.random_seed();
                std::string failure;
                cm_804D645C = nullptr;
                try { action(); }
                catch (const std::exception& e) { failure = e.what(); }
                cm_804D645C = initial_pool;
                report_fighter_owner(phase);
                const std::string prefix =
                    std::string("Original Results camera pool ownership changed at ") + phase;
                if (!failure.starts_with(prefix) ||
                    failure.find("context_pool=") == std::string::npos ||
                    failure.find("initial=") == std::string::npos)
                    throw std::runtime_error("Results pool guard missed boundary: " +
                                             std::string(phase) + " error=" + failure);
                if (session.source_frames() != frames || session.random_seed() != seed)
                    throw std::runtime_error("Rejected pool guard advanced Results state");
                std::cout << " rejected " << phase << std::flush;
            };
            expect_rejected("tick entry", [&] { session.tick(neutral); });
            expect_rejected("draw entry", [&] { session.draw(); });
            expect_rejected("scene exit entry", [&] { session.exit_scene(); });
            expect_rejected("close entry", [&] { session.close(); });
        }
        if (sheik_confirm) {
            float pcm[1068]; unsigned audio_phase = 0;
            for (unsigned tick = 0; !handoff_guard && tick < 900 && !session.requested() &&
                                   (!p1_statistics || session.source_frames() < 900); ++tick) {
                PADStatus pads[4]{};
                pads[2].err = pads[3].err = -1;
                if (p1_statistics)
                    statistics_control.prepare(session.source_frames(), pads);
                else if (tick >= (delayed_confirmation ? 600U : 240U) && tick % 90 == 0)
                    pads[0].button = pads[1].button = PAD_BUTTON_START;
                trace_camera("tick-before");
                session.tick(pads);
                trace_camera("tick-after");
                check_pool("tick");
                if (p1_statistics) statistics_control.observe(session.source_frames());
                if (draw) draw_frame();
                audio_phase += 32000;
                const unsigned samples = audio_phase / 60;
                audio_phase %= 60;
                if (!melee_web_audio_render(session.audio(), pcm, samples,
                                             error, sizeof(error)))
                    throw std::runtime_error(error);
            }
            if (!handoff_guard && !session.requested())
                throw std::runtime_error("Four-CPU Sheik-winner Results confirmation did not finish");
            if (p1_statistics)
                statistics_control.finish(session.source_frames(), draw_api_calls, draw);
            if (winner_demo) {
                if (session.source_frames() != 744 || draw_api_calls != (draw ? 744U : 0U))
                    throw std::runtime_error("Winner demo did not cover its declared delayed Results frames/draw calls");
                // Counts API calls, not GPU draws: source transition==2 retains
                // the existing source draw suppression inside the context.
                std::cout << " winner_demo_delayed_frames=" << session.source_frames()
                          << " source_draw_api_calls=" << draw_api_calls << '\n';
            }
        trace_camera("scene-exit-before");
        session.exit_scene();
        trace_camera("scene-exit-after");
        if (pool_guard) report_fighter_owner("after scene OnExit");
        check_pool("scene OnExit");
            if (host) {
                if (handoff_guard) check_results_handoff_rejection(host, session);
                trace_camera("host-OnExit-commit-before");
                if (!melee_web_menu_host_results_exit(host, error, sizeof(error)))
                    throw std::runtime_error(error);
                trace_camera("host-OnExit-commit-after");
                check_pool("host mode OnExit and route commit");
                if (p1_statistics && melee_web_menu_host_results_destination(host) != gmVsMode_State_Css)
                    throw std::runtime_error("P1 statistics: synthetic all-CPU host did not select CSS");
                std::cout << " host OnExit+commit tick=" << session.source_frames()
                          << " initial_pool=" << initial_pool
                          << " source_pool=" << cm_804D645C << '\n' << std::flush;
            } else if (mode_exit) {
                const auto saved_exit = gmVsMelee_VsExitInfo;
                const auto saved_enter = gmVsMelee_ResultsEnterData;
                const auto saved_vs = *gmVsMelee_GetVsData();
                gmVsMelee_VsExitInfo.match_end = result.match_end;
                gmVsMelee_ResultsEnterData = result;
                if (!melee_web_vs_mode_begin() ||
                    !melee_web_vs_mode_select_state(gmVsMode_State_Results))
                    throw std::runtime_error("Cannot own Results mode callback");
                std::cout << " mode OnExit winner_ckind="
                          << static_cast<int>(result.match_end.player_standings[2].ckind)
                          << " winner_ftkind="
                          << static_cast<int>(result.match_end.player_standings[2].ftkind)
                          << " initial_pool=" << initial_pool << std::flush;
                gm_Mode_Vs_States[4].on_exit(&gm_Mode_Vs_States[4]);
                std::cout << " source_pool=" << cm_804D645C
                          << " destination=" << melee_web_vs_mode_next_state()
                          << " tick=" << session.source_frames() << '\n' << std::flush;
                check_pool("mode OnExit");
                if (melee_web_vs_mode_next_state() != gmVsMode_State_Css)
                    throw std::runtime_error("Four-CPU Results mode did not request CSS");
                if (!melee_web_vs_mode_end())
                    throw std::runtime_error("Cannot release Results mode callback");
                gmVsMelee_VsExitInfo = saved_exit;
                gmVsMelee_ResultsEnterData = saved_enter;
                *gmVsMelee_GetVsData() = saved_vs;
            }
        }
        check_pool("before close");
        std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> final_input{};
        const auto final_seed = session.random_seed();
        if (host) melee_web_pad_state_capture(final_input.data());
        trace_camera("close-before");
        session.close();
        std::cout << "results-camera boundary=close-after source_pool="
                  << static_cast<const void*>(cm_804D645C)
                  << " allocation_generation="
                  << melee_web_camera_pool_allocation_generation_value()
                  << " last_subject_count="
                  << melee_web_camera_pool_last_subject_count_value()
                  << '\n' << std::flush;
        check_results_teardown();
        if (host && (!melee_web_menu_host_results_end(host, final_seed,
                          final_input.data(), error, sizeof(error)) ||
                     !melee_web_menu_host_destroy(host, error, sizeof(error))))
            throw std::runtime_error(error);
        if (!melee_web_gameplay_session_end(error, sizeof(error)))
            throw std::runtime_error(error);
        ended = true;
        std::cout << "ok; all four participant demo owners constructed and closed\n";
        return 0;
    } catch (...) {
        if (!ended) melee_web_gameplay_session_end(error, sizeof(error));
        throw;
    }
}

static void check_results_teardown()
{
    if (melee_web_gameplay_world_exists() || HSD_GObj_Entities ||
        efAsync_DatEntries[0].data || efAsync_DatEntries[1].data)
        throw std::runtime_error("Real Results session retained source ownership");
}

static void draw_history_match(melee_web::GameplayMatchSession& match)
{
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
    aurora_update();
    if (!aurora_begin_frame())
        throw std::runtime_error("Rendered match-history frame is unavailable");
    match.draw();
    aurora_end_frame();
    EM_ASM({ window.matchFrame = $0; }, match.source_frames());
    emscripten_sleep(16);
#else
    (void) match;
    throw std::runtime_error("Match-history drawing requires an initialized rendered target");
#endif
}

// Unlike run_real_lineup, this lane never manufactures standings. The human
// control steers slot 2 and losers with raw PAD; natural_cpu9 instead runs the
// source CPU simulation from a typed four-player payload. Both retain the
// default host CSS/save subset; neither is the rendered original-menu browser
// scenario or a retail/reference replay.
static int run_match_history(const melee_web::RuntimeFiles& files, bool draw,
                             bool natural_cpu9 = false)
{
    char error[256]{};
    const auto require = [&](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    require(melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                               error, sizeof(error)), error);
    auto* host = prepare_results_host(files);
    const auto arena = melee_web_gameplay_allocation();
    const auto check_arena = [&](const char* phase) {
        const auto current = melee_web_gameplay_allocation();
        require(arena.identity && current.identity == arena.identity &&
                current.generation == arena.generation && current.bytes == arena.bytes,
                phase);
    };
    MeleeWebMenuMatchSelection selection{};
    require(melee_web_menu_host_selection(host, &selection, error, sizeof(error)), error);
    const auto host_seed = selection.random_seed;
    std::cout << "Match-history B host_profile=default-CSS-subset"
              << " host_selected_players=" << selection.player_count
              << " host_hud_layout=" << selection.hud_layout
              << " host_seed=" << host_seed
              << " match_mode=" << (natural_cpu9 ? "four-CPU9" : "four-human-controlled")
              << " arena=" << arena.identity << " arena_generation=" << arena.generation
              << " draw_scope=" << (draw ? "rendered-GPU" : "unrun native-state-only")
              << '\n' << std::flush;
    constexpr std::array<int, 4> roster = {
        CKIND_SAMUS, CKIND_YOSHI, CKIND_ZELDA, CKIND_FALCO,
    };
    // Configure the typed pre-construction payload, never live Player/Fighter
    // fields. Preserve the actual host RNG and semantic initial PAD history.
    const auto human_template = selection.start.players[0];
    selection.player_count = 4;
    require(selection.hud_layout == 4 && selection.start.rules.x0_3 == 4,
            "Match-history host did not retain the original four-slot VS HUD");
    selection.start.rules.match_kind = MatchKind_Stock;
    selection.start.rules.is_stock = true;
    selection.start.rules.is_vs = true;
    selection.start.rules.xB = -1;
    for (unsigned slot = 0; slot < 4; ++slot) {
        auto& player = selection.start.players[slot];
        player = human_template;
        player.ckind = roster[slot];
        player.slot_type = natural_cpu9 ? Gm_PKind_Cpu : Gm_PKind_Human;
        player.slot = slot + 1;
        player.color = player.sub_color = 0;
        player.stocks = 4;
        player.rumble_enabled = 0;
        if (natural_cpu9) {
            player.cpu_kind = 4;
            player.cpu_level = 9;
        }
        selection.players[slot] = {slot, 4, 0, 0};
    }
    require(selection.start.rules.stkind == St_Kind_Last,
            "Match-history host did not supply Final Destination");
    // Let the private rendered pack use the actual production descriptors for
    // this selection, not a whole asset corpus or a hand-maintained file list.
    std::set<std::string> assets;
    for (const auto& names : {melee_web::menu_asset_names(),
                              melee_web::match_asset_names(selection),
                              melee_web::results_asset_names(selection)})
        assets.insert(names.begin(), names.end());
    for (const auto& name : assets)
        std::cout << "match-history asset=" << name << '\n';
    const auto* initial_input = melee_web_menu_host_input(host);
    require(initial_input != nullptr, "Match-history host did not retain source PAD");
    melee_web::GameplayMatchSession match(files, selection, *initial_input);
    check_arena("Match-history match construction replaced session arena");
    const auto match_generation = melee_web_gameplay_generation();
    unsigned match_ticks = 0, match_draws = 0, audio_phase = 0;
    float pcm[1068];
    PADStatus pads[4]{};
    if (natural_cpu9) {
        // Match the observed P1/P2-connected, CPU-slot-2/3-disconnected
        // Results port profile without supplying input to any CPU fighter.
        pads[2].err = PAD_ERR_NO_CONTROLLER;
        pads[3].err = PAD_ERR_NO_CONTROLLER;
    }
    std::array<int, 4> stocks{4, 4, 4, 4};
    std::array<unsigned, 4> losses{};
    const auto tick_match = [&] {
        require(match_ticks < (natural_cpu9 ? 60000U : 6000U),
                natural_cpu9 ? "Natural CPU9 Match-history exceeded 60000 raw PAD ticks" :
                               "Match-history exceeded 6000 raw PAD ticks");
        match.tick(pads); ++match_ticks;
        audio_phase += 32000;
        const unsigned samples = audio_phase / 60; audio_phase %= 60;
        require(melee_web_audio_render(match.audio(), pcm, samples, error, sizeof(error)), error);
        if (draw) { draw_history_match(match); ++match_draws; }
        for (unsigned slot = 0; slot < 4; ++slot) {
            const auto state = match.player_stats(slot);
            require(std::isfinite(state.position[0]) && std::isfinite(state.position[1]),
                    "Match-history produced a nonfinite source position");
            require(state.stocks >= 0 && state.stocks <= stocks[slot],
                    "Match-history stocks increased or became negative");
            if (state.stocks != stocks[slot]) {
                require(state.stocks == stocks[slot] - 1 &&
                        (natural_cpu9 || slot != 2),
                        "Match-history lost a winner stock or skipped a stock boundary");
                ++losses[slot]; stocks[slot] = state.stocks;
                std::cout << "match-history stock-loss slot=" << slot
                          << " stocks=" << state.stocks << " motion=" << state.motion_id
                          << " source_frame=" << match.source_frames() << '\n' << std::flush;
            }
        }
    };
    for (unsigned slot = 0; slot < 4; ++slot) {
        require(match.player_stats(slot).stocks == 4 &&
                match.fighter_kind(slot) == melee_web_fighter_content(roster[slot])->fighter_kind,
                "Match-history source initialization changed roster/stocks");
    }
    for (unsigned n = 0; n < 600 && !match.ready(); ++n) tick_match();
    require(match.ready(), "Match-history original Ready did not complete");
    if (natural_cpu9) {
        std::array<int, 4> observed_forms{};
        for (unsigned slot = 0; slot < 4; ++slot)
            observed_forms[slot] = match.fighter_kind(slot);
        for (unsigned n = 0; n < 60000 && !match.complete(); ++n) {
            tick_match();
            for (unsigned slot = 0; slot < 4; ++slot) {
                const int form = match.fighter_kind(slot);
                if (form != observed_forms[slot]) {
                    std::cout << "match-history CPU9 form-change slot=" << slot
                              << " from=" << observed_forms[slot] << " to=" << form
                              << " source_frame=" << match.source_frames() << '\n' << std::flush;
                    observed_forms[slot] = form;
                }
            }
            if (match_ticks % 600 == 0) {
                std::cout << "match-history CPU9 progress frame=" << match.source_frames()
                          << " stocks=";
                for (unsigned slot = 0; slot < 4; ++slot)
                    std::cout << (slot ? "," : "") << match.player_stats(slot).stocks;
                std::cout << '\n' << std::flush;
            }
        }
        require(match.complete(), "Natural four-CPU9 Match-history did not complete");
    } else {
        for (unsigned n = 0; n < 240 &&
             (match.player_stats(2).ground_or_air != 0 ||
              match.player_stats(2).motion_id != ftCo_MS_Wait); ++n) tick_match();
        require(match.player_stats(2).motion_id == ftCo_MS_Wait &&
                match.player_stats(2).ground_or_air == 0 && match.fighter_kind(2) == FTKIND_ZELDA,
                "Match-history P3 (source slot 2) did not settle as grounded Zelda");
        pads[2].stickY = -80; pads[2].button = PAD_BUTTON_B;
        tick_match(); pads[2] = {};
        bool down_b_observed = false;
        for (unsigned n = 0; n < 240; ++n) {
            const auto state = match.player_stats(2);
            down_b_observed |= state.motion_id == ftZd_MS_SpecialLw ||
                               state.motion_id == ftZd_MS_SpecialLw2;
            if (state.fighter_kind == FTKIND_SEAK) break;
            tick_match();
        }
        require(down_b_observed && match.fighter_kind(2) == FTKIND_SEAK,
                "Match-history ordinary P3 (source slot 2) down-B did not transform Zelda to Sheik");
        std::cout << "match-history P3 (source slot 2) down-B Zelda->Sheik source_frame="
                  << match.source_frames() << '\n' << std::flush;
        // Steer each surviving loser away from the idle winner. Rebirth and all
        // four stock losses are original source behavior, not placement/KO writes.
        for (unsigned n = 0; n < 4200 && (stocks[0] || stocks[1] || stocks[3]); ++n) {
            const auto winner = match.player_stats(2);
            for (const unsigned slot : {0U, 1U, 3U}) {
                pads[slot] = {};
                if (stocks[slot])
                    pads[slot].stickX = match.player_stats(slot).position[0] < winner.position[0] ? -80 : 80;
            }
            tick_match();
        }
        require(losses == std::array<unsigned, 4>{4, 4, 0, 4} && stocks[2] == 4,
                "Match-history raw walkoffs did not remove all twelve losing stocks");
        for (auto& pad : pads) pad = {};
        for (unsigned n = 0; n < 600 && !match.complete(); ++n) tick_match();
        require(match.complete() && match.fighter_kind(2) == FTKIND_SEAK,
                "Match-history did not reach source completion with P3 (source slot 2) Sheik active");
    }
    const auto match_seed = match.random_seed();
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> final_match_input{};
    melee_web_pad_state_capture(final_match_input.data()); // Before close restores external PAD.
    const auto source_frames = match.source_frames();
    match.close();
    require(!melee_web_gameplay_world_exists() && !HSD_GObj_Entities,
            "Match-history teardown retained its source world");
    check_arena("Match-history teardown replaced session arena");
    MatchExitInfo terminal{};
    require(melee_web_match_rules_terminal_data(&terminal),
            "Match-history source close did not publish MatchExitInfo");
    const auto& end = terminal.match_end;
    const unsigned winner_slot = end.n_winners == 1 ? end.winners[0] : 6U;
    require(end.outcome == OUTCOME_ELIMINATION && end.match_kind == MatchKind_Stock &&
            end.n_winners == 1 && winner_slot < 4,
            "Match-history source terminal was not a sole four-player Stock elimination");
    if (natural_cpu9) {
        for (unsigned slot = 0; slot < 4; ++slot)
            require(end.player_standings[slot].slot_type == Gm_PKind_Cpu,
                    "Natural CPU9 MatchExitInfo did not preserve a CPU slot type");
        require(end.player_standings[winner_slot].is_big_loser == 0,
                "Natural CPU9 terminal winner is marked as a loser");
    } else {
        require(winner_slot == 2 && end.player_standings[2].ftkind == FTKIND_SEAK &&
                end.player_standings[2].slot_type == Gm_PKind_Human &&
                end.player_standings[2].is_big_loser == 0,
                "Match-history source terminal was not sole P3 (source slot 2) Sheik Stock elimination");
    }
    std::cout << "match-history source-terminal winner=" << winner_slot << " winner_ckind="
              << int(end.player_standings[winner_slot].ckind) << " winner_ftkind="
              << int(end.player_standings[winner_slot].ftkind) << " winner_slot_type="
              << int(end.player_standings[winner_slot].slot_type)
              << " winner_is_big_loser=" << int(end.player_standings[winner_slot].is_big_loser)
              << " source_frame=" << source_frames
              << " raw_ticks=" << match_ticks << " match_draw_api_calls=" << match_draws
              << " match_exit_seed=" << match_seed
              << (natural_cpu9 ? " natural_cpu9=1" : " losses=4,4,0,4") << '\n' << std::flush;
    ResultsMatchInfo result{};
    require(melee_web_menu_host_results_begin(host, &terminal, match_seed, &result,
                                                error, sizeof(error)), error);
    std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)> results_input(
        melee_web_pad_state_decode(final_match_input.data(), final_match_input.size(),
                                   error, sizeof(error)), melee_web_pad_state_free);
    require(results_input != nullptr, error);
    melee_web::GameplayResultsSession results(files, result, match_seed, *results_input);
    check_arena("Match-history Results construction replaced session arena");
    require(melee_web_gameplay_generation() != match_generation,
            "Match-history Results reused the match world generation");
    check_results_fighter_leases(result);
    auto* winner = Player_GetEntity(winner_slot);
    require(winner && ftLib_GetKind(winner) ==
            end.player_standings[winner_slot].ftkind && ftLib_80086B74(winner),
            "Match-history Results lost the source terminal winner/subject");
    const bool target_sheik_winner = natural_cpu9 && winner_slot == 2 &&
        end.player_standings[2].ckind == CKIND_ZELDA &&
        end.player_standings[2].ftkind == FTKIND_SEAK;
    const auto pool = cm_804D645C;
    const auto check_pool = [&](const char* phase) {
        require(pool && cm_804D645C == pool, phase);
    };
    std::cout << "match-history Results winner_demo_motion=" << ftLib_GetMotionId(winner)
              << " source_pool=" << pool << " arena_reused=1\n" << std::flush;
    unsigned results_draws = 0;
    audio_phase = 0;
    P1StatisticsControl cpu_statistics(false);
    for (unsigned tick = 0; tick < 1200 && !results.requested(); ++tick) {
        for (auto& pad : pads) pad = {};
        if (natural_cpu9) {
            pads[2].err = pads[3].err = PAD_ERR_NO_CONTROLLER;
            cpu_statistics.prepare(tick, pads);
        } else if (tick >= 600 && tick % 90 == 0)
            for (auto& pad : pads) pad.button = PAD_BUTTON_START;
        results.tick(pads);
        if (natural_cpu9) cpu_statistics.observe(results.source_frames());
        check_pool("Match-history Results pool changed after tick");
        audio_phase += 32000;
        const unsigned samples = audio_phase / 60; audio_phase %= 60;
        require(melee_web_audio_render(results.audio(), pcm, samples, error, sizeof(error)), error);
        if (draw) { draw_results(results); ++results_draws; }
        check_pool("Match-history Results pool changed after draw/audio");
    }
    require(results.requested() && results.source_frames() > 600,
            "Match-history delayed ordinary Start did not complete Results");
    const auto results_frames = results.source_frames();
    if (natural_cpu9) cpu_statistics.finish(results_frames, results_draws, draw);
    require(results_draws == (draw ? results_frames : 0),
            "Match-history Results draw API count differs from declared scope");
    results.exit_scene();
    require(melee_web_menu_host_results_exit(host, error, sizeof(error)), error);
    check_pool("Match-history Results host OnExit/commit changed pool");
    const auto destination = melee_web_menu_host_results_destination(host);
    require(destination == gmVsMode_State_Css || destination == gmVsMode_State_Prize,
            "Match-history Results requested an unsupported source route");
    const auto result_seed = results.random_seed();
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> final_results_input{};
    melee_web_pad_state_capture(final_results_input.data());
    std::cout << "match-history host OnExit tick=" << results_frames
              << " initial_pool=" << pool << " source_pool=" << cm_804D645C
              << " results_draw_api_calls=" << results_draws
              << " destination=" << destination
              << " route_commit=" << (destination == gmVsMode_State_Css)
              << " continuation=" << (destination == gmVsMode_State_Prize ?
                                         "Prize-unrun" : "CSS-unrun")
              << '\n' << std::flush;
    results.close();
    check_results_teardown();
    check_arena("Match-history Results teardown replaced session arena");
    require(melee_web_menu_host_results_end(host, result_seed, final_results_input.data(),
                                              error, sizeof(error)), error);
    // This is a Results ownership discriminator. A fresh human-match profile
    // can legitimately request Prize; preserve it and dispose the host through
    // its supported unload path, without entering Prize or claiming CSS return.
    require(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    require(melee_web_gameplay_session_end(error, sizeof(error)), error);
    require(!melee_web_gameplay_allocation().identity,
            "Match-history final session retained its arena");
    std::cout << (natural_cpu9 ?
        "match-history natural four-CPU9 Match->Results host handoff and close passed;" :
        "match-history actual Match->Sheik Results host handoff and close passed;")
              << " default-CSS/profile subset, no rendered/browser/full-session equivalence"
              << (target_sheik_winner ? "; target slot-2 Zelda-origin Sheik winner observed" :
                  natural_cpu9 ? "; target slot-2 Zelda-origin Sheik winner not observed" : "")
              << '\n';
    return 0;
}

static void check_results_fighter_leases(const ResultsMatchInfo& result)
{
    std::vector<std::uint32_t> source_owners;
    for (unsigned slot = 0; slot < 4; ++slot) {
        if (result.match_end.player_standings[slot].slot_type == Gm_PKind_NA)
            continue;
        auto* entity = Player_GetEntity(slot);
        if (!entity)
            throw std::runtime_error("Results participant has no source demo Fighter");
        {
            MeleeWebSourceFighterAddress lease{};
            if (!melee_web_source_memory_fighter_read(entity->user_data, &lease) ||
                !lease.live || !lease.source_address ||
                !lease.allocation_generation ||
                lease.world_generation != melee_web_gameplay_generation())
                throw std::runtime_error("Results demo Fighter has no live source lease");
            for (const auto source : source_owners)
                if (source == lease.source_address)
                    throw std::runtime_error("Results demo Fighters share a source owner");
            source_owners.push_back(lease.source_address);
        }
    }
}

static std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES>
results_pad_snapshot(std::uint32_t copy_winner_buttons)
{
    std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES> bytes{};
    std::size_t offset = 0;
    auto u8 = [&](std::uint8_t value) { bytes[offset++] = value; };
    auto i8 = [&](std::int8_t value) { u8(static_cast<std::uint8_t>(value)); };
    auto u32 = [&](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            u8(static_cast<std::uint8_t>(value >> shift));
    };
    auto i32 = [&](std::int32_t value) { u32(static_cast<std::uint32_t>(value)); };
    auto f32 = [&](float value) { u32(std::bit_cast<std::uint32_t>(value)); };
    // This is the valid source pad-processing configuration installed by the
    // Results context before it applies the retained semantic state.
    i32(45); i32(8); i8(0); i8(30); f32(0.0f);
    u8(0); u8(1); i8(80); i8(0);
    u8(1); u8(140); u8(0); u8(1); u8(140); u8(0);
    i8(80); u8(140); u8(140); u8(0); u8(0); u8(0);
    if (offset != 30) throw std::runtime_error("PAD snapshot encoder drifted");
    for (unsigned bank = 0; bank < 3; ++bank) {
        for (unsigned slot = 0; slot < 4; ++slot) {
            // Semantic retained input, applied by the existing Results context
            // before OnEnter. Never write a Fighter/Results variant or RNG.
            u32(bank == 1 && slot == 2 ? copy_winner_buttons : 0);
            for (unsigned field = 0; field < 5; ++field) u32(0);
            for (unsigned field = 0; field < 8; ++field) u8(0);
            for (unsigned field = 0; field < 8; ++field) f32(0.0f);
            u8(0); i8(0);
        }
    }
    if (offset != bytes.size()) throw std::runtime_error("PAD history encoder drifted");
    return bytes;
}

static int run_real_roster(const melee_web::RuntimeFiles& files,
                           std::span<const int> opponents,
                           const char* roster_name, bool confirm)
{
    char error[256]{};
    if (!melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                          error, sizeof(error)))
        throw std::runtime_error(error);
    bool ended = false;
    try {
        auto input_bytes = results_pad_snapshot();
        std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)> input(
            melee_web_pad_state_decode(input_bytes.data(), input_bytes.size(),
                                       error, sizeof(error)),
            melee_web_pad_state_free);
        if (!input) throw std::runtime_error(error);
        PADStatus neutral[4]{};
        neutral[2].err = neutral[3].err = -1;
        unsigned completed = 0;
        for (const int opponent : opponents) {
            for (const bool opponent_wins : {true, false}) {
                const auto result = make_results_match(opponent, opponent_wins);
                std::cout << "Results " << roster_name << " "
                          << melee_web_fighter_content(opponent)->name
                          << (opponent_wins ? " wins" : " Mario wins") << "..."
                          << std::flush;
                /* A source match may leave the TyDatai scene alias populated
                 * until preloadState retires scene-heap aliases for Results. */
                if (_Toy_sbss_804D6ED0 != nullptr)
                    throw std::runtime_error("Trophy archive alias was not idle before the next Results scene");
                _Toy_sbss_804D6ED0 = reinterpret_cast<HSD_Archive*>(uintptr_t(1));
                melee_web::GameplayResultsSession session(files, result,
                                                           0x13579bdfU, *input);
                if (_Toy_sbss_804D6ED0 == reinterpret_cast<HSD_Archive*>(uintptr_t(1)))
                    throw std::runtime_error("Results preload did not retire the previous TyDatai alias");
                check_results_fighter_leases(result);
                if (session.source_frames() != 0)
                    throw std::runtime_error("Results scene advanced during construction");
                for (unsigned tick = 0; tick < 2; ++tick)
                    session.tick(neutral);
                if (session.source_frames() != 2 || session.requested())
                    throw std::runtime_error("Short Results tick changed source transition state");
                if (confirm) {
                    float pcm[1068];
                    unsigned audio_phase = 0;
                    for (unsigned tick = 0; tick < 900 && !session.requested(); ++tick) {
                        PADStatus pads[4]{};
                        pads[2].err = pads[3].err = -1;
                        if (tick >= 240 && tick % 90 == 0)
                            pads[0].button = pads[1].button = PAD_BUTTON_START;
                        session.tick(pads);
                        audio_phase += 32000;
                        const unsigned samples = audio_phase / 60;
                        audio_phase %= 60;
                        if (!melee_web_audio_render(session.audio(), pcm, samples,
                                                     error, sizeof(error)))
                            throw std::runtime_error(error);
                    }
                    if (!session.requested())
                        throw std::runtime_error("Original Results Start confirmation did not finish");
                    session.exit_scene();
                }
                session.close();
                check_results_teardown();
                ++completed;
                std::cout << "ok\n";
            }
        }
        if (!melee_web_gameplay_session_end(error, sizeof(error)))
            throw std::runtime_error(error);
        ended = true;
        std::cout << "Validated " << completed << " " << roster_name
                  << (confirm ? " Results confirmations and teardown\n" :
                                " Results constructions with short ticks\n");
        return 0;
    } catch (...) {
        if (!ended) melee_web_gameplay_session_end(error, sizeof(error));
        throw;
    }
}

static void check_source_entry(const melee_web::GameplayWorld& world)
{
    HSD_GObj** links=reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    const auto previous_common=efAsync_DatEntries[0].data;
    const auto previous_fighter=efAsync_DatEntries[1].data;
    void* item_common=it_804D6D28;
    void* item_bounce=it_804D6D40;
    void* item_colors=it_804D6D04;
    void* item_public=it_804D6D20;
    void* item_common_articles=it_804D6D24;
    void* item_pokemon_articles=it_804D6D30;
    void* item_character_articles=it_804D6D38;
    if(!item_public||!item_common||!item_bounce||!item_colors||
       !item_common_articles||!item_pokemon_articles||!item_character_articles)
        throw std::runtime_error("Deferred Results item globals were not attached");
    if(links[11]||links[12]||previous_common||previous_fighter)
        throw std::runtime_error("Deferred Results source owners were initialized too early");

    /* fn_8017AA78's authored boundary is Item_80266FA8 then Item_80266FCC
     * before efLib_Init;
     * the checked world has already attached the typed data these routines
     * consume, while the source tables remain unpublished until LoadSync. */
    const int saved_language=lbLang_GetLanguageSetting();
    lbLang_SetLanguageSetting(LANG_US);
    Item_80266FA8();
    lbLang_SetLanguageSetting(saved_language);
    Item_80266FCC();
    if(it_804D6D20!=item_public||it_804D6D24!=item_common_articles||
       it_804D6D30!=item_pokemon_articles||it_804D6D38!=item_character_articles||
       it_804D6D28!=item_common||it_804D6D40!=item_bounce||it_804D6D04!=item_colors)
        throw std::runtime_error("Original item initializer replaced checked typed globals");
    efLib_Init();
    if(!links[11]||!links[12]||!links[11]->proc||!links[12]->proc)
        throw std::runtime_error("Original effect initializer did not publish both GObj processes");
    efAsync_LoadSync(0);
    efAsync_LoadSync(1);
    world.verify_result_source_loads();
    if(!efAsync_DatEntries[0].data||!efAsync_DatEntries[1].data)
        throw std::runtime_error("Original Results source loads did not publish effect data");
}

int main(int argc,char** argv){try{
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
    AuroraConfig config{};
    config.appName = "Results-only ownership trace";
    config.cachePath = "/melee-results-cache";
    config.desiredBackend = BACKEND_WEBGPU;
    config.windowWidth = 640; config.windowHeight = 480;
    config.msaa = 1; config.vsync = true; config.logLevel = LOG_INFO;
    if (!SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas"))
        throw std::runtime_error("Cannot select Results trace canvas");
    aurora_initialize(argc, argv, &config);
    if (!aurora_pipeline_set_complete_draws(1))
        throw std::runtime_error("Cannot require complete Results draws");
    aurora_set_deferred_pipeline_cache_writes(true);
    alignas(32) static unsigned char fifo[64 * 1024];
    GXInit(fifo, sizeof(fifo));
#endif
    const std::string command = argc >= 2 ? argv[1] : "";
    const bool history_state = command == "--lineup-b-match-history-host-state";
    const bool history_draw = command == "--lineup-b-match-history-host-draw";
    const bool cpu9_history_state = command == "--lineup-b-cpu9-match-history-host-state";
    const bool match_history = history_state || history_draw || cpu9_history_state;
    const bool p1_statistics_state = command == "--lineup-b-zelda-sheik-stock-p1-statistics-host-state";
    const bool p1_statistics_draw = command == "--lineup-b-zelda-sheik-stock-p1-statistics-host-draw";
    const bool p1_statistics_browser_cadence =
        command == "--lineup-b-zelda-sheik-stock-p1-statistics-browser-cadence-host-state";
    const bool p1_statistics = p1_statistics_state || p1_statistics_draw ||
                               p1_statistics_browser_cadence;
    const bool real_mario = command == "--real-mario" ||
                            command == "--real-mario-confirm";
    const bool real_eight = command == "--real-eight" ||
                            command == "--real-eight-confirm";
    const bool real_enabled = command == "--real-enabled" ||
                              command == "--real-enabled-confirm";
    const bool confirm = command == "--real-mario-confirm" ||
                         command == "--real-eight-confirm" ||
                         command == "--real-enabled-confirm";
    const bool lineup_a = command == "--lineup-a";
    const bool pool_guard = command == "--lineup-b-camera-pool-guard";
    const bool handoff_guard = command == "--lineup-b-results-handoff-guard";
    const WinnerDemoControl* winner_demo = nullptr;
    bool variant_state_only = p1_statistics_state || p1_statistics_browser_cadence;
    for (const auto& control : winner_demo_controls) {
        const std::string prefix =
            std::string("--lineup-b-zelda-sheik-stock-delayed-demo-") + control.button_name;
        if (command == prefix + "-host-state" || command == prefix + "-host-draw") {
            winner_demo = &control;
            variant_state_only = command == prefix + "-host-state";
        }
    }
    // Keep the Zelda-origin transformed standing distinct from an external
    // Sheik standing: Results world assets are selected using external ckind.
    const bool zelda_sheik_host =
        command == "--lineup-b-zelda-sheik-stock-delayed-host-draw" || winner_demo || p1_statistics;
    const bool delayed_confirmation =
        command == "--lineup-b-sheik-stock-delayed-mode-exit" ||
        command == "--lineup-b-sheik-stock-delayed-host-draw" || zelda_sheik_host;
    const bool stock = command == "--lineup-b-sheik-stock-mode-exit" ||
                       command == "--lineup-b-sheik-stock-host-draw" || delayed_confirmation ||
                       handoff_guard;
    const bool host_draw = command == "--lineup-b-sheik-host-draw" ||
                            command == "--lineup-b-sheik-stock-host-draw" ||
                            command == "--lineup-b-sheik-stock-delayed-host-draw" ||
                            (zelda_sheik_host && !variant_state_only);
    const bool host_route = host_draw || handoff_guard || variant_state_only;
    const bool draw = command == "--lineup-b-sheik-draw" || host_draw || history_draw;
#if !defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
    if (draw)
        throw std::runtime_error("Draw diagnostics require MELEE_WEB_RESULTS_RENDERED_TRACE; Node does not submit GX frames");
#else
    if (variant_state_only || history_state)
        throw std::runtime_error("Native state-only control requires the Node target; use -host-draw for rendered GPU");
#endif
    const bool sheik_standing = !zelda_sheik_host && !handoff_guard &&
        (command == "--lineup-b-sheik-mode-exit" || stock || draw);
    const bool mode_exit = sheik_standing || zelda_sheik_host || handoff_guard ||
                           command == "--lineup-b-zelda-sheik-mode-exit";
    const bool sheik_confirm = command == "--lineup-b-sheik-confirm" || mode_exit;
    const bool lineup_b = command == "--lineup-b" || sheik_confirm || pool_guard;
    const bool real_roster = real_mario || real_eight || real_enabled ||
                             lineup_a || lineup_b || match_history;
    if ((!real_roster && argc != 3) || (real_roster && argc != 5))
        throw std::runtime_error(real_roster ?
            "Expected --real-mario/--real-eight/--real-enabled[-confirm]/--lineup-a/--lineup-b[-sheik-confirm/-zelda-sheik-mode-exit/-sheik-mode-exit/-sheik-draw/-sheik-host-draw/-camera-pool-guard/-results-handoff-guard/-zelda-sheik-stock-delayed-demo-{b,y,x}-host-{state,draw}/-match-history-host-{state,draw}/-cpu9-match-history-host-state] <common/fighter> <Results shared/music> <Results fighters>" :
            "Expected common/fighter and Results asset directories");
    melee_web::RuntimeFiles files;
    const int first_directory = real_roster ? 2 : 1;
    for (int directory = first_directory; directory < argc; ++directory)
        load_directory(files, argv[directory]);
    if (match_history) {
        const int status = run_match_history(files, history_draw, cpu9_history_state);
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
        EM_ASM({ window.resultsDone = $0; }, status);
#endif
        return status;
    }
    if (lineup_a || lineup_b) {
        constexpr std::array<int, 4> a = {
            CKIND_GAMEWATCH, CKIND_KIRBY, CKIND_POPONANA, CKIND_FOX,
        };
        constexpr std::array<int, 4> b = {
            CKIND_SAMUS, CKIND_YOSHI, CKIND_ZELDA, CKIND_FALCO,
        };
        const int status = run_real_lineup(files, lineup_a ? std::span<const int>(a) :
                                                 std::span<const int>(b),
                               lineup_a ? "A" : "B", sheik_confirm, mode_exit, draw,
                               sheik_standing, pool_guard, host_route, stock,
                               delayed_confirmation, handoff_guard, winner_demo, p1_statistics,
                               p1_statistics_browser_cadence);
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
        EM_ASM({ window.resultsDone = $0; }, status);
#endif
        return status;
    }
    if (real_roster) {
        constexpr std::array<int, 1> real_mario_opponents = {CKIND_MARIO};
        constexpr std::array<int, 8> real_eight_opponents = {
            CKIND_MARIO, CKIND_DRMARIO, CKIND_FOX, CKIND_FALCO,
            CKIND_MARS, CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK,
        };
        constexpr std::array<int, 26> real_enabled_opponents = {
            CKIND_MARIO, CKIND_FOX, CKIND_FALCO, CKIND_MARS,
            CKIND_DRMARIO, CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK,
            CKIND_CAPTAIN, CKIND_GANON, CKIND_LUIGI, CKIND_PIKACHU,
            CKIND_PICHU, CKIND_PURIN, CKIND_DONKEY, CKIND_KOOPA,
            CKIND_MEWTWO, CKIND_NESS, CKIND_PEACH, CKIND_GAMEWATCH,
            CKIND_KIRBY, CKIND_POPONANA, CKIND_SAMUS, CKIND_YOSHI,
            CKIND_ZELDA, CKIND_SEAK,
        };
        const auto opponents = real_mario
            ? std::span<const int>(real_mario_opponents)
            : real_enabled
            ? std::span<const int>(real_enabled_opponents)
            : std::span<const int>(real_eight_opponents);
        return run_real_roster(files, opponents,
                               real_mario ? "Mario" :
                               real_enabled ? "enabled-roster" : "real-eight",
                               confirm);
    }
    const melee_web::FighterCostume* mario=nullptr;
    for(const auto& identity:melee_web::fighter_costumes())
        if(identity.fighter_kind==0&&identity.costume_index==0)mario=&identity;
    if(!mario)throw std::runtime_error("Mario source identity unavailable");
    melee_web::GameplayWorldSelection selection;
    selection.purpose=melee_web::GameplayWorldPurpose::Results;
    for(unsigned cycle=0;cycle<2;++cycle){
        melee_web::GameplayWorld world(files,selection);
        melee_web::GameplayResultsAssets assets(files,*mario);
        world.install_result_demo(0,assets.result_motion_archive());
        check_source_entry(world);
        if(melee_web_gameplay_stats().ticks!=0||assets.demo_clips().size()!=9)
            throw std::runtime_error("Results preparation changed source time or omitted demo clips");
        assets.verify();world.verify_immutable_archives();
        world.close();assets.close();
        if(melee_web_gameplay_world_exists()||HSD_GObj_Entities||
           efAsync_DatEntries[0].data||efAsync_DatEntries[1].data)
            throw std::runtime_error("Results preparation retained its source world");
    }
    std::cout<<"Original Mario Results assets and prepared world constructed and closed twice; scene execution untested\n";
    return 0;
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';
#if defined(MELEE_WEB_RESULTS_RENDERED_TRACE)
    EM_ASM({ window.resultsDone = 1; });
#endif
    return 1;
}}
