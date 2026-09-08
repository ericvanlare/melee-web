#include "gameplay_compat.h"
#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "gameplay_article_item_state.h"
#include "dat_archive.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace melee_web;

namespace {

constexpr int kWait = 14;
constexpr int kSpecialS = 345;
constexpr int kSpecialAirS = 346;
constexpr int kSpecialHi = 347;
constexpr int kSpecialAirHi = 348;
constexpr int kSpecialLw = 349;
constexpr int kSpecialAirLw = 350;

void check(bool ok, const char* why)
{
    if (!ok) {
        std::cerr << why << '\n';
        throw DatError(why);
    }
}

void fail(const std::string& why)
{
    std::cerr << why << '\n';
    throw DatError(why);
}

std::vector<uint8_t> bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Open owned specials-test asset");
    return {std::istreambuf_iterator<char>(file), {}};
}

void step(MeleeWebMatchContext* match, const PADStatus& pad, char* error)
{
    PADStatus pads[4] = {{0}};
    pads[0] = pad;
    check(melee_web_match_step_raw(match, pads, error, 256), error);
}

MeleeWebMatchStats stats(MeleeWebMatchContext* match, unsigned player, char* error)
{
    MeleeWebMatchStats result{};
    check(melee_web_match_player_stats(match, player, &result, error, 256), error);
    return result;
}

void check_finite(const MeleeWebMatchStats& state, const char* label)
{
    const float values[] = {
        state.position[0], state.position[1], state.position[2],
        state.animation_frame, state.source_stick[0], state.source_stick[1],
        state.source_triggers, state.damage_percent, state.shield_health,
        state.eyes[0].animation_frame, state.eyes[0].animation_rate,
        state.eyes[1].animation_frame, state.eyes[1].animation_rate,
    };
    for (float value : values) {
        if (!std::isfinite(value)) {
            fail(std::string(label) + " produced non-finite fighter state");
        }
    }
}

MeleeWebArticleItemSnapshot item_snapshot()
{
    MeleeWebArticleItemSnapshot result{};
    check(melee_web_article_item_snapshot(&result),
          "Original item list was not available while specials match was live");
    return result;
}

void settle_wait(MeleeWebMatchContext* match, char* error, const char* label)
{
    PADStatus neutral = {0};
    for (unsigned tick = 0; tick < 360; ++tick) {
        MeleeWebMatchStats state = stats(match, 0, error);
        check_finite(state, label);
        if (state.motion_id == kWait && state.ground_or_air == 0) {
            return;
        }
        step(match, neutral, error);
    }
    fail(std::string(label) + " did not return to grounded Wait");
}

void ground_special(MeleeWebMatchContext* match, char* error,
                    const char* label, int stick_x, int stick_y, int expected,
                    int expected_followup = -1)
{
    settle_wait(match, error, label);
    PADStatus input = {0};
    input.button = PAD_BUTTON_B;
    input.stickX = stick_x;
    input.stickY = stick_y;
    step(match, input, error);

    bool saw_expected = false;
    bool saw_followup = expected_followup < 0;
    for (unsigned tick = 0; tick < 12; ++tick) {
        MeleeWebMatchStats state = stats(match, 0, error);
        check_finite(state, label);
        if (state.motion_id == expected) {
            saw_expected = true;
        }
        if (state.motion_id == expected_followup) {
            saw_followup = true;
        }
        if (tick == 0) {
            std::cerr << label << " motion " << state.motion_id
                      << " ground_or_air " << state.ground_or_air << '\n';
        }
        PADStatus neutral = {0};
        step(match, neutral, error);
    }
    if (!saw_expected) {
        fail(std::string(label) + " did not enter expected source motion " +
             std::to_string(expected));
    }
    if (!saw_followup) {
        fail(std::string(label) + " did not reach expected follow-up motion " +
             std::to_string(expected_followup));
    }
    settle_wait(match, error, label);
}

void air_special(MeleeWebMatchContext* match, char* error,
                 const char* label, int stick_x, int stick_y, int expected)
{
    settle_wait(match, error, label);
    PADStatus jump = {0};
    jump.button = PAD_BUTTON_X;
    step(match, jump, error);

    PADStatus neutral = {0};
    bool airborne = false;
    for (unsigned tick = 0; tick < 24; ++tick) {
        MeleeWebMatchStats state = stats(match, 0, error);
        check_finite(state, label);
        if (state.ground_or_air != 0) {
            airborne = true;
            break;
        }
        step(match, neutral, error);
    }
    if (!airborne) {
        fail(std::string(label) + " jump did not enter source airborne state");
    }

    PADStatus input = {0};
    input.button = PAD_BUTTON_B;
    input.stickX = stick_x;
    input.stickY = stick_y;
    step(match, input, error);

    bool saw_expected = false;
    for (unsigned tick = 0; tick < 12; ++tick) {
        MeleeWebMatchStats state = stats(match, 0, error);
        check_finite(state, label);
        if (state.motion_id == expected) {
            saw_expected = true;
        }
        if (tick == 0) {
            std::cerr << label << " motion " << state.motion_id
                      << " ground_or_air " << state.ground_or_air << '\n';
        }
        step(match, neutral, error);
    }
    if (!saw_expected) {
        fail(std::string(label) + " did not enter expected source motion " +
             std::to_string(expected));
    }
    settle_wait(match, error, label);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        check(argc == 2, "Expected local asset directory");
        RuntimeFiles files;
        for (const char* name : {"PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat",
                                 "GrNLa.dat", "ItCo.usd", "EfMrData.dat", "EfCoData.dat",
                                 "PdPm.dat", "sislib_font.bin"}) {
            files[name] = bytes(std::filesystem::path(argv[1]) / name);
        }

        char error[256];
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
            GameplayWorld world(files);
            MeleeWebMatchContext* match = nullptr;
            try {
                MeleeWebPlayerSettings players[2] = {
                    {0, 0, 4, {-20, world.floor_height(-20) + 1, 0}, 1},
                    {1, 1, 4, {20, world.floor_height(20) + 1, 0}, -1},
                };
                match = melee_web_match_begin_players(
                    players, 2, 2, 1, world.collision(), error, sizeof(error));
                check(match != nullptr, error);
                check(melee_web_match_create_fighters(match, error, sizeof(error)), error);

                PADStatus neutral = {0};
                for (unsigned tick = 0; tick < 30; ++tick) {
                    step(match, neutral, error);
                    MeleeWebMatchStats mario = stats(match, 0, error);
                    MeleeWebMatchStats p2 = stats(match, 1, error);
                    check_finite(mario, "initial Mario state");
                    check_finite(p2, "initial P2 state");
                }
                MeleeWebMatchStats settled = stats(match, 0, error);
                check(settled.motion_id == kWait && settled.ground_or_air == 0,
                      "Mario did not settle onto source ground before specials");
                check(item_snapshot().item_count == 0,
                      "A Mario article survived into the specials match");

                ground_special(match, error, "ground side-B", 80, 0, kSpecialS);
                ground_special(match, error, "ground up-B", 0, 80, kSpecialHi);
                /* Mario enters Tornado through the source air motion, then changes to
                 * ftMr_MS_SpecialLw on the first grounded collision. */
                ground_special(match, error, "ground down-B/tornado", 0, -80,
                               kSpecialAirLw, kSpecialLw);
                air_special(match, error, "air side-B", 80, 0, kSpecialAirS);
                air_special(match, error, "air up-B", 0, 80, kSpecialAirHi);
                air_special(match, error, "air down-B", 0, -80, kSpecialAirLw);

                check(melee_web_match_end(match, error, sizeof(error)), error);
                match = nullptr;
                check(item_snapshot().item_count == 0,
                      "Original item teardown left a specials article alive after match end");
                world.close();
            } catch (...) {
                if (match != nullptr) {
                    char cleanup_error[256];
                    if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error))) {
                        std::cerr << "Specials trace cleanup failed: " << cleanup_error << '\n';
                    }
                    match = nullptr;
                }
                world.close();
                throw;
            }
        }
        std::cout << "Original Mario grounded and aerial side/up/down B paths passed in two worlds\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
