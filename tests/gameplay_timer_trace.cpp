#include "gameplay_match_rules.h"
#include "gameplay_match_session.hpp"

extern "C" {
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/forward.h>
#include <melee/mn/types.h>
#include <melee/pl/forward.h>
}

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

namespace {

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

RuntimeFiles load_files(const std::filesystem::path& first,
                        const std::filesystem::path& second)
{
    RuntimeFiles files;
    for (const auto* root : {&first, &second}) {
        for (const auto& entry : std::filesystem::directory_iterator(*root)) {
            if (!entry.is_regular_file() || entry.file_size() > 64 * 1024 * 1024) continue;
            std::ifstream stream(entry.path(), std::ios::binary);
            check(bool(stream), "Open timer-test asset: " + entry.path().string());
            files[entry.path().filename().string()] =
                {std::istreambuf_iterator<char>(stream), {}};
        }
    }
    return files;
}

MeleeWebMenuMatchSelection timed_selection()
{
    VsModeData defaults{};
    gm_InitVsMode(&defaults);
    MeleeWebMenuMatchSelection selection{};
    selection.start = defaults.start;
    selection.start.rules.match_kind = MatchKind_Stock;
    selection.start.rules.is_stock = true;
    selection.start.rules.is_vs = true;
    selection.start.rules.is_teams = false;
    selection.start.rules.timer_enabled = true;
    selection.start.rules.timer_counts_up = false;
    selection.start.rules.timer_shows_hours = false;
    /* One minute keeps this test bounded while exercising the same whole-minute
     * Rule Plus -> seconds conversion as the eight-minute cohort. */
    selection.start.rules.time_limit = 60;
    selection.start.rules.x14 = 0;
    selection.start.rules.xB = -1;
    selection.start.rules.x20 = UINT64_MAX;
    selection.start.rules.stkind = 0x20; /* St_Kind_Last / Final Destination. */
    selection.hud_layout = 4;
    selection.random_seed = 1;
    for (unsigned i = 0; i < 2; ++i) {
        PlayerInitData& player = selection.start.players[i];
        player.ckind = CKIND_MARIO;
        player.slot_type = Gm_PKind_Human;
        player.stocks = 4;
        player.color = 0;
        player.slot = 0;
        player.sub_color = 0;
        player.rumble_enabled = true;
        player.nametag = 120;
        selection.players[i] = {i, 4, 0, 0};
    }
    check(melee_web_match_timer_supported(&selection.start.rules),
          "ordinary one-minute countdown was rejected");
    return selection;
}

void validate_timer_shape()
{
    check(!melee_web_match_timer_supported(nullptr),
          "null timer rules were accepted");
    StartMeleeRules rules{};
    check(melee_web_match_timer_supported(&rules),
          "disabled timer no longer preserves the untimed contract");
    rules.timer_enabled = true;
    rules.timer_counts_up = false;
    rules.time_limit = 60;
    check(melee_web_match_timer_supported(&rules), "one-minute timer rejected");
    rules.time_limit = 99 * 60;
    check(melee_web_match_timer_supported(&rules), "99-minute timer rejected");
    rules.time_limit = 30;
    check(!melee_web_match_timer_supported(&rules), "sub-minute timer admitted");
    rules.time_limit = 61;
    check(!melee_web_match_timer_supported(&rules), "non-minute timer admitted");
    rules.time_limit = 100 * 60;
    check(!melee_web_match_timer_supported(&rules), "overlong menu timer admitted");
    rules.time_limit = 60;
    rules.timer_counts_up = true;
    check(!melee_web_match_timer_supported(&rules), "count-up timer admitted");
    rules.timer_counts_up = false;
    rules.timer_shows_hours = true;
    check(!melee_web_match_timer_supported(&rules), "hour-display timer admitted");
    rules.timer_shows_hours = false;
    rules.x14 = 1;
    check(!melee_web_match_timer_supported(&rules), "custom initial subframe admitted");
}

void run_timer_lifecycle(const RuntimeFiles& files)
{
    auto selection = timed_selection();
    auto match = std::make_unique<GameplayMatchSession>(files, selection);
    std::array<PADStatus, 4> neutral{};
    neutral[0].err = PAD_ERR_NONE;
    neutral[1].err = PAD_ERR_NONE;
    neutral[2].err = PAD_ERR_NO_CONTROLLER;
    neutral[3].err = PAD_ERR_NO_CONTROLLER;
    std::array<PADStatus, 4> start = neutral;
    start[0].button = PAD_BUTTON_START;
    try {
        for (unsigned i = 0; i < 360 && !match->ready(); ++i) {
            match->tick(neutral.data());
        }
        check(match->ready(), "timed match HUD never reached ready state");

        int displayed = -1;
        check(GetMatchTimer(&displayed), "source timer was not enabled");
        check(displayed > 0 && displayed <= 60,
              "source timer did not initialize to the one-minute countdown");

        match->tick(start.data());
        check(match->paused(), "START did not enter the source pause state");
        /* The input tick which enters pause may advance the subframe before
         * the source pause gate takes effect.  Snapshot only after the
         * transition, then require every subsequent paused tick to stay put. */
        const u32 paused_seconds = gm_8016AEEC();
        const u16 paused_subframe = gm_8016AEFC();
        for (unsigned i = 0; i < 20; ++i) {
            match->tick(neutral.data());
            check(gm_8016AEEC() == paused_seconds &&
                      gm_8016AEFC() == paused_subframe,
                  "source timer advanced while paused");
        }
        /* The source pause gate waits ten frames before accepting unpause. */
        for (unsigned i = 0; i < 10; ++i) match->tick(neutral.data());
        match->tick(start.data());
        check(!match->paused(), "START did not resume the source match");

        bool countdown_seen = false;
        bool timeout_seen = false;
        for (unsigned i = 0; i < 4000 && !timeout_seen; ++i) {
            match->tick(neutral.data());
            int winner = -1;
            const int outcome = match->outcome(winner);
            check(outcome == OUTCOME_NONE || outcome == OUTCOME_TIMEOUT,
                  "timed neutral match produced an unexpected outcome");
            int current = -1;
            check(GetMatchTimer(&current), "source timer disappeared during match");
            if (current < displayed) countdown_seen = true;
            displayed = current;
            if (outcome == OUTCOME_TIMEOUT) {
                check(winner == -1, "timeout exposed an invented winner");
                timeout_seen = true;
            }
        }
        check(countdown_seen, "source countdown never advanced after resume");
        check(timeout_seen, "one-minute source timer did not produce timeout");
        /* The source timer reaches zero at the end of fn_8016CD98, while
         * gm_Scene_Vs_OnFrame latches match_result and enters end state 1 at
         * the start of the following source frame.  The public outcome query
         * therefore observes the timeout one source tick before ending(). */
        if (!match->ending()) match->tick(neutral.data());
        check(match->ending(), "timeout did not enter the source ending state");

        for (unsigned i = 0; i < 300 && !match->complete(); ++i) {
            match->tick(neutral.data());
        }
        check(match->complete(), "timeout ending did not complete its source transition");
        int final_winner = -1;
        check(match->outcome(final_winner) == OUTCOME_TIMEOUT && final_winner == -1,
              "completed source match lost its timeout result");
        match->close();
    } catch (...) {
        try { match->close(); } catch (...) {}
        throw;
    }
}

} // namespace

int main(int argc, char** argv)
{
    try {
        check(argc == 3, "Expected owned menu and game asset directories");
        validate_timer_shape();
        RuntimeFiles files = load_files(argv[1], argv[2]);
        run_timer_lifecycle(files);
        run_timer_lifecycle(files); /* repeat ownership/lifetime teardown */
        std::cout << "Original stock timer countdown, pause/resume, timeout and repeated lifetime passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
