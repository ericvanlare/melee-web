#include "gameplay_replay_session.hpp"

#include "gameplay_content.h"
#include "gameplay_menu.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace melee_web {
namespace {

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

MeleeWebMenuMatchSelection selection_for(
    const GameplayReplayTransport& replay) {
    check(replay.frames.size() && replay.players.size() == 2,
          "Replay requires a nonempty two-player transport");
    check(replay.physical_input() && replay.derived_setup(),
          "Replay transport is not a derived physical-input workload");
    check(replay.players[0].source_port == 1 &&
              replay.players[1].source_port == 2,
          "Replay runtime only supports source ports 1 and 2");

    char error[256]{};
    MeleeWebMenuRuntime services{
        nullptr,
        [](void*, MeleeWebMenuScene, char*, size_t) { return 1; },
        [](void*, char*, size_t) { return 1; },
        [](void*, MeleeWebMenuScene, int*, char*, size_t) { return 1; },
    };
    auto* menu = melee_web_menu_session_create(
        &services, nullptr, error, sizeof(error));
    check(menu != nullptr, error);
    MeleeWebMenuMatchSelection selection{};
    selection.start = melee_web_menu_css(menu)->vs.start;
    check(melee_web_menu_session_destroy(menu, error, sizeof(error)), error);

    // This setup is intentionally derived from the workload profile. Source
    // Game Info and post-frame observations stay identity/provenance data and
    // are never copied into the mutable match initializer.
    selection.start.rules.match_kind = MatchKind_Stock;
    selection.start.rules.is_stock = true;
    selection.start.rules.is_vs = true;
    selection.start.rules.is_teams = false;
    selection.start.rules.timer_enabled = false;
    selection.start.rules.xB = -1; // items disabled
    selection.start.rules.x20 = std::numeric_limits<uint64_t>::max();
    selection.start.rules.stkind = replay.stage;
    selection.start.rules.x0_3 = 2;
    for (unsigned index = 0; index < 2; ++index) {
        const auto& player = replay.players[index];
        check(player.stocks == 4,
              "Replay transport does not contain the derived four-stock setup");
        check(melee_web_fighter_content(player.character) != nullptr,
              "Replay workload character has no admitted source content");
        selection.start.players[index].ckind =
            static_cast<CharacterKind>(player.character);
        selection.start.players[index].color = player.costume;
        selection.start.players[index].stocks = 4;
        selection.start.players[index].rumble_enabled = true;
        selection.players[index] = {index, 4, player.costume, 0};
    }
    selection.random_seed = replay.initial_seed;
    selection.hud_layout = 2;
    return selection;
}

} // namespace

GameplayReplaySession::GameplayReplaySession(
    const RuntimeFiles& files, const GameplayReplayTransport& replay)
    : replay_(replay), match_(files, selection_for(replay)) {}

void GameplayReplaySession::fail(std::string message) {
    if (!failure_)
        failure_ = std::move(message);
}

bool GameplayReplaySession::step() {
    if (failure_ || cursor_ >= replay_.frames.size())
        return false;

    if (match_.paused()) {
        fail("source match paused before all replay inputs were consumed");
        return false;
    }
    if (match_.ending() || match_.complete()) {
        fail("source match ended before all replay inputs were consumed");
        return false;
    }

    const auto& frame = replay_.frames[cursor_];
    // The match counter is held during original Ready/Go. Count scheduler
    // ticks here so intro input is consumed without requiring gameplay to run.
    const uint64_t before = match_.player_stats(0).ticks;
    PADStatus pads[4]{};
    pads[0] = frame.pads[0];
    pads[1] = frame.pads[1];
    pads[2].err = pads[3].err = PAD_ERR_NO_CONTROLLER;
    try {
        match_.tick(pads);
    } catch (const std::exception& error) {
        fail(std::string("source match tick failed: ") + error.what());
        return false;
    }
    ++cursor_;

    const uint64_t after = match_.player_stats(0).ticks;
    if (after != before + 1) {
        fail("source tick did not progress while consuming replay input");
        return false;
    }
    if (cursor_ < replay_.frames.size() && match_.paused()) {
        fail("source match paused before all replay inputs were consumed");
        return false;
    }
    if (cursor_ < replay_.frames.size() &&
        (match_.ending() || match_.complete())) {
        fail("source match ended before all replay inputs were consumed");
        return false;
    }
    return true;
}

bool GameplayReplaySession::complete() const noexcept {
    return !failure_ && cursor_ == replay_.frames.size();
}

} // namespace melee_web
