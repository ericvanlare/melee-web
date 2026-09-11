#include "gameplay_compat.h"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include "gameplay_world.hpp"
#include "dat_archive.hpp"

#include <melee/ft/kinds/ftCommon/forward.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

namespace {

void check(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<uint8_t> bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Open owned combat-test asset: " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}

void step(MeleeWebMatchContext* match, const PADStatus& p1, const PADStatus& p2,
          char* error)
{
    PADStatus pads[4] = {{0}};
    pads[0] = p1;
    pads[1] = p2;
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
        state.position[0], state.position[1], state.position[2], state.animation_frame,
        state.source_stick[0], state.source_stick[1], state.source_triggers,
        state.damage_percent, state.shield_health,
        state.eyes[0].animation_frame, state.eyes[0].animation_rate,
        state.eyes[1].animation_frame, state.eyes[1].animation_rate,
    };
    for (float value : values) {
        if (!std::isfinite(value)) {
            throw std::runtime_error(std::string(label) + " produced non-finite state");
        }
    }
}

void settle(MeleeWebMatchContext* match, char* error)
{
    const PADStatus neutral = {0};
    for (unsigned tick = 0; tick < 120; ++tick) {
        MeleeWebMatchStats p1 = stats(match, 0, error);
        MeleeWebMatchStats p2 = stats(match, 1, error);
        check_finite(p1, "P1 settle");
        check_finite(p2, "P2 settle");
        if (p1.motion_id == ftCo_MS_Wait && p2.motion_id == ftCo_MS_Wait &&
            p1.ground_or_air == GA_Ground && p2.ground_or_air == GA_Ground) {
            return;
        }
        step(match, neutral, neutral, error);
    }
    throw std::runtime_error("Two source fighters did not settle into grounded Wait");
}

MeleeWebMatchContext* begin_match(GameplayWorld& world, MeleeWebRender*& camera, char* error)
{
    const float left_floor = world.floor_height(-5.0f);
    const float right_floor = world.floor_height(5.0f);
    MeleeWebPlayerSettings players[2] = {
        {0, 0, 4, {-5.0f, left_floor + 1.0f, 0.0f}, 1},
        {1, 1, 4, {5.0f, right_floor + 1.0f, 0.0f}, -1},
    };
    MeleeWebMatchContext* match = melee_web_match_begin_players(
        players, 2, 2, 1, world.collision(), error, 256);
    check(match != nullptr, error);
    try {
        check(melee_web_match_create_fighters(match, error, 256), error);
        MeleeWebRenderSettings rendering{640,480,{0,35,190},{0,5,0},45,1,2000,
            (UINT64_C(1)<<3)|(UINT64_C(1)<<5)};
        camera=melee_web_render_begin_match(&rendering,error,256);
        check(camera!=nullptr,error);
        world.enable_full_stage();
        settle(match, error);
        return match;
    } catch (...) {
        char cleanup_error[256];
        world.end_stage();
        if(camera){melee_web_render_end(camera,cleanup_error,sizeof(cleanup_error));camera=nullptr;}
        if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error))) {
            std::cerr << "Combat trace setup cleanup failed: " << cleanup_error << '\n';
        }
        throw;
    }
}

void end_match(GameplayWorld& world, MeleeWebMatchContext*& match, MeleeWebRender*& camera, char* error)
{
    if (match == nullptr) {
        return;
    }
    world.end_stage();
    if(camera){check(melee_web_render_end(camera,error,256),error);camera=nullptr;}
    check(melee_web_match_end(match, error, 256), error);
    match = nullptr;
}

void jab_trace(GameplayWorld& world, char* error)
{
    MeleeWebRender* camera=nullptr;
    MeleeWebMatchContext* match = begin_match(world, camera, error);
    try {
        const PADStatus neutral = {0};
        PADStatus jab = {0};
        jab.button = PAD_BUTTON_A;
        const MeleeWebMatchStats initial = stats(match, 1, error);
        bool saw_jab = false;
        bool saw_damage = false;
        bool saw_damage_motion = false;
        for (unsigned tick = 0; tick < 120; ++tick) {
            if (tick == 0) {
                step(match, jab, neutral, error);
            } else {
                step(match, neutral, neutral, error);
            }
            MeleeWebMatchStats p1 = stats(match, 0, error);
            MeleeWebMatchStats p2 = stats(match, 1, error);
            check_finite(p1, "jab P1");
            check_finite(p2, "jab P2");
            saw_jab |= p1.motion_id == ftCo_MS_Attack11;
            saw_damage |= p2.damage_percent > initial.damage_percent + 0.001f;
            saw_damage_motion |=
                p2.motion_id >= ftCo_MS_DamageHi1 && p2.motion_id <= ftCo_MS_DamageFlyRoll;
            if (tick == 0) {
                check((p1.held_buttons & PAD_BUTTON_A) != 0 && p2.held_buttons == 0,
                      "Raw jab input did not reach only P1");
            }
        }
        check(saw_jab, "P1 raw A did not enter source jab motion");
        check(saw_damage, "P1 jab did not damage close-range P2");
        check(saw_damage_motion, "P2 did not enter a source damage motion after the jab");
        std::cerr << "jab: P2 damage " << initial.damage_percent << " -> "
                  << stats(match, 1, error).damage_percent << '\n';
        end_match(world, match, camera, error);
    } catch (...) {
        if (match != nullptr) {
            char cleanup_error[256];
            world.end_stage();
            if(camera){melee_web_render_end(camera,cleanup_error,sizeof(cleanup_error));camera=nullptr;}
            if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error))) {
                std::cerr << "Jab trace cleanup failed: " << cleanup_error << '\n';
            }
            match = nullptr;
        }
        throw;
    }
}

void shield_trace(GameplayWorld& world, char* error)
{
    MeleeWebRender* camera=nullptr;
    MeleeWebMatchContext* match = begin_match(world, camera, error);
    try {
        const PADStatus neutral = {0};
        PADStatus jab = {0};
        jab.button = PAD_BUTTON_A;
        PADStatus shield = {0};
        shield.button = PAD_TRIGGER_R;
        shield.triggerRight = 140;
        const MeleeWebMatchStats initial = stats(match, 1, error);
        bool saw_jab = false;
        bool saw_guard = false;
        float minimum_shield = initial.shield_health;
        float maximum_damage = initial.damage_percent;
        for (unsigned tick = 0; tick < 80; ++tick) {
            if (tick == 0) {
                step(match, jab, shield, error);
            } else if (tick < 40) {
                step(match, neutral, shield, error);
            } else {
                step(match, neutral, neutral, error);
            }
            MeleeWebMatchStats p1 = stats(match, 0, error);
            MeleeWebMatchStats p2 = stats(match, 1, error);
            check_finite(p1, "shield P1");
            check_finite(p2, "shield P2");
            saw_jab |= p1.motion_id == ftCo_MS_Attack11;
            saw_guard |= p2.motion_id == ftCo_MS_GuardOn || p2.motion_id == ftCo_MS_Guard;
            minimum_shield = std::min(minimum_shield, p2.shield_health);
            maximum_damage = std::max(maximum_damage, p2.damage_percent);
            if (tick == 0) {
                check((p1.held_buttons & PAD_BUTTON_A) != 0 &&
                          (p2.held_buttons & PAD_TRIGGER_R) != 0,
                      "Raw jab/shield input did not reach the selected fighters");
            }
        }
        check(saw_jab, "Shield test P1 did not enter source jab motion");
        check(saw_guard, "Raw R did not enter P2 source shield motion");
        check(minimum_shield < initial.shield_health - 0.001f,
              "P1 jab did not reduce P2 source shield health");
        check(maximum_damage <= initial.damage_percent + 0.001f,
              "Shielded P2 received damage from the jab");
        std::cerr << "shield: P2 shield " << initial.shield_health << " -> "
                  << minimum_shield << ", damage " << initial.damage_percent << " -> "
                  << maximum_damage << '\n';
        end_match(world, match, camera, error);
    } catch (...) {
        if (match != nullptr) {
            char cleanup_error[256];
            world.end_stage();
            if(camera){melee_web_render_end(camera,cleanup_error,sizeof(cleanup_error));camera=nullptr;}
            if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error))) {
                std::cerr << "Shield trace cleanup failed: " << cleanup_error << '\n';
            }
            match = nullptr;
        }
        throw;
    }
}

struct ThrowCase {
    const char* name;
    int stick_x;
    int stick_y;
    int throw_motion;
    int thrown_motion;
};

void throw_trace(GameplayWorld& world, const ThrowCase& test, char* error)
{
    MeleeWebRender* camera=nullptr;
    MeleeWebMatchContext* match = begin_match(world, camera, error);
    try {
        const PADStatus neutral = {0};
        PADStatus grab = {0};
        grab.button = PAD_BUTTON_A | PAD_TRIGGER_R;
        bool saw_catch = false;
        bool saw_catch_wait = false;
        bool saw_capture = false;
        bool saw_capture_wait = false;
        for (unsigned tick = 0; tick < 100; ++tick) {
            if (tick == 0) {
                step(match, grab, neutral, error);
            } else {
                step(match, neutral, neutral, error);
            }
            MeleeWebMatchStats p1 = stats(match, 0, error);
            MeleeWebMatchStats p2 = stats(match, 1, error);
            check_finite(p1, "grab P1");
            check_finite(p2, "grab P2");
            saw_catch |= p1.motion_id == ftCo_MS_Catch ||
                         p1.motion_id == ftCo_MS_CatchPull ||
                         p1.motion_id == ftCo_MS_CatchDash ||
                         p1.motion_id == ftCo_MS_CatchDashPull;
            saw_catch_wait |= p1.motion_id == ftCo_MS_CatchWait;
            saw_capture |= p2.motion_id == ftCo_MS_CapturePulledLw ||
                           p2.motion_id == ftCo_MS_CapturePulledHi;
            saw_capture_wait |= p2.motion_id == ftCo_MS_CaptureWaitLw ||
                                p2.motion_id == ftCo_MS_CaptureWaitHi;
            if (saw_catch_wait && saw_capture_wait) {
                break;
            }
        }
        check(saw_catch, "Raw A+R did not enter a source Mario catch motion");
        check(saw_capture, "Close-range raw grab did not enter a source capture motion");
        check(saw_catch_wait && saw_capture_wait,
              "Close-range raw grab did not reach source catch wait/capture wait");
        PADStatus direction = {0};
        direction.stickX = static_cast<int8_t>(test.stick_x);
        direction.stickY = static_cast<int8_t>(test.stick_y);
        bool saw_throw = false;
        bool saw_thrown = false;
        for (unsigned tick = 0; tick < 100; ++tick) {
            if (tick == 0) {
                step(match, direction, neutral, error);
            } else {
                step(match, neutral, neutral, error);
            }
            MeleeWebMatchStats p1 = stats(match, 0, error);
            MeleeWebMatchStats p2 = stats(match, 1, error);
            check_finite(p1, test.name);
            check_finite(p2, test.name);
            saw_throw |= p1.motion_id == test.throw_motion;
            saw_thrown |= p2.motion_id == test.thrown_motion;
            if (saw_throw && saw_thrown) {
                break;
            }
        }
        if (!saw_throw || !saw_thrown) {
            MeleeWebMatchStats p1 = stats(match, 0, error);
            MeleeWebMatchStats p2 = stats(match, 1, error);
            std::ostringstream detail;
            detail << test.name << " raw throw did not reach source motions (P1 " << p1.motion_id
                   << ", P2 " << p2.motion_id << ", P1 stick " << p1.source_stick[0]
                   << ", P2 position " << p2.position[0] << "," << p2.position[1] << ")";
            throw std::runtime_error(detail.str());
        }
        std::cerr << test.name << " entered source throw " << test.throw_motion
                  << " / victim " << test.thrown_motion << '\n';
        end_match(world, match, camera, error);
    } catch (...) {
        if (match != nullptr) {
            char cleanup_error[256];
            world.end_stage();
            if(camera){melee_web_render_end(camera,cleanup_error,sizeof(cleanup_error));camera=nullptr;}
            if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error))) {
            std::cerr << test.name << " cleanup failed: " << cleanup_error << '\n';
            }
            match = nullptr;
        }
        throw;
    }
}

void grab_throw_trace(RuntimeFiles& files, char* error)
{
    const ThrowCase cases[] = {
        {"forward", 80, 0, ftCo_MS_ThrowF, ftCo_MS_ThrownF},
        {"back", -80, 0, ftCo_MS_ThrowB, ftCo_MS_ThrownB},
        {"up", 0, 80, ftCo_MS_ThrowHi, ftCo_MS_ThrownHi},
        {"down", 0, -80, ftCo_MS_ThrowLw, ftCo_MS_ThrownLw},
    };
    for (const auto& test : cases) {GameplayWorld world(files);throw_trace(world, test, error);world.close();}
}

enum class MovePreparation { None, Air, Dash, Shield };
struct MoveCase {
    const char* name;
    MovePreparation preparation;
    int motion;
    unsigned buttons;
    int stick_x, stick_y, cstick_x, cstick_y;
};

void move_trace(RuntimeFiles& files, char* error)
{
    using P=MovePreparation;
    const MoveCase cases[]={
        {"forward tilt",P::None,ftCo_MS_AttackS3S,PAD_BUTTON_A,40,0,0,0},
        {"up tilt",P::None,ftCo_MS_AttackHi3,PAD_BUTTON_A,0,40,0,0},
        {"down tilt",P::None,ftCo_MS_AttackLw3,PAD_BUTTON_A,0,-40,0,0},
        {"forward smash",P::None,ftCo_MS_AttackS4S,0,0,0,80,0},
        {"up smash",P::None,ftCo_MS_AttackHi4,0,0,0,0,80},
        {"down smash",P::None,ftCo_MS_AttackLw4,0,0,0,0,-80},
        {"dash attack",P::Dash,ftCo_MS_AttackDash,PAD_BUTTON_A,80,0,0,0},
        {"neutral aerial",P::Air,ftCo_MS_AttackAirN,PAD_BUTTON_A,0,0,0,0},
        {"forward aerial",P::Air,ftCo_MS_AttackAirF,0,0,0,80,0},
        {"back aerial",P::Air,ftCo_MS_AttackAirB,0,0,0,-80,0},
        {"up aerial",P::Air,ftCo_MS_AttackAirHi,0,0,0,0,80},
        {"down aerial",P::Air,ftCo_MS_AttackAirLw,0,0,0,0,-80},
        {"air dodge",P::Air,ftCo_MS_EscapeAir,PAD_TRIGGER_L,0,0,0,0},
        {"forward roll",P::Shield,ftCo_MS_EscapeF,PAD_TRIGGER_L,80,0,0,0},
        {"back roll",P::Shield,ftCo_MS_EscapeB,PAD_TRIGGER_L,-80,0,0,0},
        {"spot dodge",P::Shield,ftCo_MS_EscapeN,PAD_TRIGGER_L,0,-80,0,0},
    };
    const PADStatus neutral={0};
    for(const auto& test:cases){
        GameplayWorld world(files);
        MeleeWebRender* camera=nullptr;
        auto* match=begin_match(world,camera,error);
        try {
            PADStatus preparation={0};
            if(test.preparation==P::Air)preparation.button=PAD_BUTTON_X;
            if(test.preparation==P::Dash)preparation.stickX=80;
            if(test.preparation==P::Shield){preparation.button=PAD_TRIGGER_L;preparation.triggerLeft=255;}
            if(test.preparation!=P::None)
                for(unsigned tick=0;tick<12;++tick)step(match,preparation,neutral,error);
            PADStatus input={0};input.button=test.buttons;
            input.stickX=test.stick_x;input.stickY=test.stick_y;
            input.substickX=test.cstick_x;input.substickY=test.cstick_y;
            if(input.button&PAD_TRIGGER_L)input.triggerLeft=255;
            bool entered=false;
            for(unsigned tick=0;tick<240;++tick){
                step(match,tick<2?input:neutral,neutral,error);
                auto p1=stats(match,0,error);auto p2=stats(match,1,error);
                check_finite(p1,test.name);check_finite(p2,test.name);
                entered|=p1.motion_id==test.motion;
            }
            auto final=stats(match,0,error);
            check(entered,std::string(test.name)+" did not enter expected source motion "+std::to_string(test.motion));
            check(final.motion_id==ftCo_MS_Wait&&final.ground_or_air==GA_Ground,
                  std::string(test.name)+" did not complete and return to grounded Wait");
            std::cerr<<test.name<<" source motion "<<test.motion<<" completed\n";
            end_match(world,match,camera,error);
            world.close();
        }catch(...){
            if(match){char cleanup[256];world.end_stage();if(camera)melee_web_render_end(camera,cleanup,sizeof(cleanup));if(!melee_web_match_end(match,cleanup,sizeof(cleanup)))std::cerr<<cleanup<<'\n';}
            throw;
        }
    }
}

void run_cycle(RuntimeFiles& files, unsigned cycle)
{
    char error[256];
    {GameplayWorld world(files);jab_trace(world,error);world.close();}
    {GameplayWorld world(files);shield_trace(world,error);world.close();}
    grab_throw_trace(files, error);
    move_trace(files, error);
    std::cout << "combat cycle " << cycle << " passed\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        check(argc == 2, "Expected local asset directory");
        RuntimeFiles files;
        for (const char* name : {"PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat",
                                 "GrNLa.dat", "ItCo.usd", "EfMrData.dat", "EfCoData.dat",
                                 "PdPm.dat", "LbRb.dat", "sislib_font.bin"}) {
            files[name] = bytes(std::filesystem::path(argv[1]) / name);
        }
        run_cycle(files, 0);
        run_cycle(files, 1);
        std::cout << "Original Mario close-range jab, shield, grab and directional throw and move paths passed in two complete passes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
