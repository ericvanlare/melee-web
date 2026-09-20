#include "gameplay_world.hpp"
#include "gameplay_match_session.hpp"
#include "gameplay_menu.h"
#include "gameplay_content.h"
#include "gameplay_collision.h"
#include "gameplay_bootstrap.h"
#include "dat_archive.hpp"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" int melee_web_test_stage_temple_state(uint32_t*, unsigned*);
extern "C" int melee_web_test_stage_temple_bounds(float[4], float[4]);
extern "C" int melee_web_test_stage_temple_lights(uint32_t*, uint16_t[3], uint8_t[12]);
extern "C" int melee_web_test_stage_temple_ground_kind(void);
extern "C" int melee_web_test_stage_temple_light_positions(float[6]);
extern "C" void* melee_web_test_stage_temple_yakumono(void);
extern "C" int melee_web_test_stage_temple_music(uint32_t, int*, int*);
extern "C" int melee_web_test_stage_temple_overrides(uint8_t[3], unsigned*);
extern "C" int melee_web_test_stage_temple_empty(unsigned*);
extern "C" const char* melee_web_audio_music_path(int);

using namespace melee_web;

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static void load_files(RuntimeFiles& files, const std::filesystem::path& root)
{
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.file_size() > 64 * 1024 * 1024)
            continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open Temple runtime file");
        files[entry.path().filename().string()] = {
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
}

static void check_finite(const float* values, unsigned count, const char* message)
{
    for (unsigned i = 0; i < count; ++i)
        if (!std::isfinite(values[i])) throw std::runtime_error(message);
}

static void check_music_candidates(const RuntimeFiles& files)
{
    const auto shrine = files.find("shrine.hps");
    const auto akaneia = files.find("akaneia.hps");
    check(shrine != files.end() && shrine->second.size() == 3795904,
          "Temple primary BGM75 was not captured at its authored size");
    check(akaneia != files.end() && akaneia->second.size() == 4518752,
          "Temple alternate BGM1 was not captured at its authored size");

    /* Ground_801C24F8 is the source table consumer. Its four forced masks
     * exercise x4, x8, xC and x10 independently; no unlock or RNG branch is
     * taken by these masks. */
    struct Candidate { uint32_t mask; int music; bool alternate; const char* file; };
    const Candidate candidates[] = {
        {1, 75, false, "shrine.hps"},
        {2, 1, true, "akaneia.hps"},
        {0x11, 75, false, "shrine.hps"},
        {0x12, 1, true, "akaneia.hps"},
    };
    for (const auto& candidate : candidates) {
        int music = -1;
        int alternate = 0;
        check(melee_web_test_stage_temple_music(candidate.mask, &music, &alternate),
              "Temple source BGM candidate did not produce an ID");
        check(music == candidate.music && alternate == static_cast<int>(candidate.alternate),
              "Temple source BGM candidate selected the wrong authored ID");
        const char* path = melee_web_audio_music_path(music);
        check(path && std::string(path) == std::string("/audio/") + candidate.file,
              "Temple source BGM ID has the wrong disc path");
        check(files.contains(candidate.file), "Selected Temple BGM is not runtime-ready");
    }
}

static void run_lifetime(const RuntimeFiles& files, unsigned cycle)
{
    GameplayWorldSelection selection{};
    selection.player_count = 2;
    selection.fighter_kinds = {FTKIND_MARIO, FTKIND_MARIO, 0, 0};
    selection.ground_kind = Gr_Kind_Shrine;
    GameplayWorld world(files, selection);
    void* previous_yakumono = melee_web_test_stage_temple_yakumono();

    world.enable_full_stage();
    void* live_yakumono = melee_web_test_stage_temple_yakumono();
    check(live_yakumono != nullptr && live_yakumono != previous_yakumono,
          "Temple source yakumono public address was not retained as an opaque owner");

    uint32_t map_mask = 0;
    unsigned map_count = 0;
    check(melee_web_test_stage_temple_state(&map_mask, &map_count),
          "Temple source OnInit did not install map IDs 0, 1 and 2");
    check(map_mask == 0x7 && map_count == 3, "Temple source map registry differs");

    float camera[4] = {}, blast[4] = {};
    check(melee_web_test_stage_temple_bounds(camera, blast),
          "Temple source GroundParam scale or camera/blast bounds are invalid");
    check_finite(camera, 4, "Temple camera bounds are nonfinite");
    check_finite(blast, 4, "Temple blast bounds are nonfinite");
    const float expected_camera[4] = {-279.0f, -201.6f, 279.0f, 180.0f};
    const float expected_blast[4] = {-315.0f, -257.4f, 315.0f, 207.0f};
    for (unsigned i = 0; i < 4; ++i) {
        check(std::fabs(camera[i] - expected_camera[i]) <= 0.0005f,
              "Temple authored camera range was not scaled by GroundParam.y");
        check(std::fabs(blast[i] - expected_blast[i]) <= 0.0005f,
              "Temple authored blast range was not scaled by GroundParam.y");
    }

    uint32_t light_count = 0;
    uint16_t flags[3] = {};
    uint8_t rgba[12] = {};
    const bool lights_published =
        melee_web_test_stage_temple_lights(&light_count, flags, rgba);
    if (!lights_published || light_count != 3) {
        throw std::runtime_error(
            "Temple light publication kind=" +
            std::to_string(melee_web_test_stage_temple_ground_kind()) +
            " count=" + std::to_string(light_count));
    }
    if (!(flags[0] == 4 && flags[1] == 0x401 && flags[2] == 13)) {
        throw std::runtime_error(
            "Temple source light flags differ: " + std::to_string(flags[0]) +
            "," + std::to_string(flags[1]) + "," + std::to_string(flags[2]));
    }
    const uint8_t expected_rgba[12] = {
        204, 204, 255, 255, 255, 255, 255, 255, 153, 153, 179, 255,
    };
    bool colors_match = true;
    for (unsigned i = 0; i < 12; ++i) colors_match &= rgba[i] == expected_rgba[i];
    if (!colors_match) {
        std::string observed = "";
        for (unsigned i = 0; i < 12; ++i) {
            if (i) observed += ",";
            observed += std::to_string(rgba[i]);
        }
        throw std::runtime_error("Temple source light colors differ: " + observed);
    }
    float light_positions[6] = {};
    check(melee_web_test_stage_temple_light_positions(light_positions),
          "Temple source infinite-light positions are missing");
    // HSD_WObjDesc begins with class_name; Vec3 starts at byte4. The
    // authored triples are (-9,7.5,8) and (-7,2,6.972552776), then source
    // Ground_801C2374 applies the 0.9 scale to the live objects.
    const float expected_lights[6] = {-8.1f, 6.75f, 7.2f, -6.3f, 1.8f, 6.2752975f};
    std::cout << "Temple live light positions=";
    for (float value : light_positions) std::cout << value << ',';
    std::cout << std::endl;
    for (unsigned i = 0; i < 6; ++i)
        check(std::isfinite(light_positions[i]) &&
                  std::fabs(light_positions[i] - expected_lights[i]) <= 0.0005f,
              "Temple source light position was not scaled by GroundParam.y");

    const float expected_spawns[4][3] = {
        {-92.7f, 21.6f, 0.0f}, {91.8f, 4.5f, 0.0f},
        {-27.0f, 74.7f, 0.0f}, {27.0f, -10.8f, 0.0f},
    };
    for (unsigned slot = 0; slot < 4; ++slot) {
        const auto spawn = world.player_spawn(slot);
        for (unsigned axis = 0; axis < 3; ++axis) {
            check(std::isfinite(spawn[axis]) &&
                      std::fabs(spawn[axis] - expected_spawns[slot][axis]) <= 0.0005f,
                  "Temple authored player marker was not scaled by GroundParam.y");
        }
    }

    uint8_t override_flags[3] = {};
    unsigned override_count = 0;
    check(melee_web_test_stage_temple_overrides(override_flags, &override_count) &&
              override_count == 3,
          "Temple source light override identities were not retained");
    bool saw_zero = false, saw_32 = false, saw_192 = false;
    for (unsigned i = 0; i < override_count; ++i) {
        saw_zero |= override_flags[i] == 0;
        saw_32 |= override_flags[i] == 32;
        saw_192 |= override_flags[i] == 192;
    }
    check(saw_zero && saw_32 && saw_192,
          "Temple source light override flags differ");

    char error[256] = {};
    MeleeWebCollisionLineResult line{};
    check(melee_web_collision_line(world.collision(), 0, &line, error, sizeof(error)), error);
    check(line.kind == 1 && std::isfinite(line.v0[0]) && std::isfinite(line.v0[1]) &&
              std::isfinite(line.v1[0]) && std::isfinite(line.v1[1]) &&
              std::fabs(line.v0[0] - (-224.8200f)) <= 0.0005f &&
              std::fabs(line.v0[1] - 5.1840f) <= 0.0005f &&
              std::fabs(line.v1[0] - (-214.4157f)) <= 0.0005f &&
              std::fabs(line.v1[1] - 5.1840f) <= 0.0005f,
          "Temple authored floor line was not scaled by GroundParam.y");
    // This query follows one selected floor chain, not every disconnected
    // Temple island. Query the checked line at its midpoint.
    MeleeWebCollisionFloorResult floor_result{};
    const float floor_x = (line.v0[0] + line.v1[0]) * 0.5f;
    check(melee_web_collision_floor(world.collision(), 0, floor_x, 20.0f,
                                    &floor_result, error, sizeof(error)), error);
    const float floor = 20.0f + floor_result.displacement_y;
    check(floor_result.line == 0 && std::isfinite(floor) &&
              std::fabs(floor - 5.1840f) <= 0.0005f,
          "Temple source floor-chain query differs from its authored line");

    check_music_candidates(files);
    for (unsigned tick = 0; tick < 120; ++tick)
        check(melee_web_gameplay_step(error, sizeof(error)), error);
    check(melee_web_test_stage_temple_state(&map_mask, &map_count),
          "Temple source map callbacks did not survive bounded scheduler ticks");
    world.verify_immutable_archives();
    world.end_stage();
    unsigned objects = 0;
    check(melee_web_test_stage_temple_empty(&objects) && objects == 0,
          "Temple teardown left source map objects live");
    check(melee_web_test_stage_temple_yakumono() == previous_yakumono,
          "Temple teardown did not restore the prior opaque yakumono pointer");
    world.close();
    check(melee_web_gameplay_stats().objects == 0,
          "Temple world close left SDK objects live");
    std::cout << "Temple cycle=" << cycle << " maps=" << map_count
              << " lights=" << light_count << " floor=" << floor << '\n';
}

static void run_match_lifetimes(const RuntimeFiles& files)
{
    // This is an explicit source-start fixture. The separate browser check
    // supplies real CSS/SSS input; these menu services are never entered.
    char error[256] = {};
    MeleeWebMenuRuntime services{nullptr,
        [](void*, MeleeWebMenuScene, char*, size_t) { return 1; },
        [](void*, char*, size_t) { return 1; },
        [](void*, MeleeWebMenuScene, int*, char*, size_t) { return 1; }};
    auto* menu = melee_web_menu_session_create(&services, nullptr, error, sizeof(error));
    check(menu, error);
    MeleeWebMenuMatchSelection selection{};
    selection.start = melee_web_menu_css(menu)->vs.start;
    check(melee_web_menu_session_destroy(menu, error, sizeof(error)), error);
    selection.start.rules.match_kind = MatchKind_Stock;
    selection.start.rules.is_stock = true;
    selection.start.rules.is_vs = true;
    selection.start.rules.xB = -1;
    selection.start.rules.x0_3 = 2;
    selection.start.rules.stkind = St_Kind_Shrine;
    selection.hud_layout = 2;
    selection.random_seed = 0x13579bdf;
    for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i) {
        selection.start.players[i].stocks = 4;
        selection.start.players[i].rumble_enabled = i < 2;
    }
    for (unsigned i = 0; i < 2; ++i) {
        selection.start.players[i].ckind = CKIND_MARIO;
        selection.start.players[i].color = i;
        selection.players[i] = {i, 4, i, 0};
    }
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        GameplayMatchSession match(files, selection);
        PADStatus raw[4]{};
        raw[2].err = raw[3].err = PAD_ERR_NO_CONTROLLER;
        float pcm[1068];
        unsigned audio_phase = 0;
        auto tick = [&]() {
            match.tick(raw);
            audio_phase += 32000;
            const unsigned samples = audio_phase / 60;
            audio_phase %= 60;
            check(melee_web_audio_render(match.audio(), pcm, samples,
                                         error, sizeof(error)), error);
            for (unsigned i = 0; i < 2; ++i) {
                const auto state = match.player_stats(i);
                check(std::isfinite(state.position[0]) && std::isfinite(state.position[1]),
                      "Temple match produced nonfinite fighter coordinates");
            }
        };
        for (unsigned n = 0; n < 600 && !match.ready(); ++n) tick();
        check(match.ready(), "Temple match did not finish original Ready");
        check(match.fighter_kind(0) == FTKIND_MARIO &&
                  match.fighter_kind(1) == FTKIND_MARIO,
              "Temple match lost the selected fighter identities");
        const auto first_frame = match.source_frames();
        for (unsigned n = 0; n < 120; ++n) tick();
        check(match.source_frames() >= first_frame + 120,
              "Temple match source frames did not advance");
        check(match.player_stats(0).stocks == 4 && match.player_stats(1).stocks == 4,
              "Temple neutral spawn window unexpectedly lost a stock");
        raw[0].button = PAD_BUTTON_START;
        tick();
        raw[0].button = 0;
        for (unsigned n = 0; n < 30 && !match.paused(); ++n) tick();
        check(match.paused(), "Temple original pause did not engage");
        for (unsigned n = 0; n < 15; ++n) tick();
        raw[0].button = PAD_TRIGGER_L | PAD_TRIGGER_R | PAD_BUTTON_A | PAD_BUTTON_START;
        tick();
        raw[0].button = 0;
        for (unsigned n = 0; n < 500 && !match.complete(); ++n) tick();
        check(match.complete(), "Temple original No Contest did not complete");
        match.close();
        match.close();
        check(melee_web_gameplay_stats().objects == 0,
              "Temple match teardown left SDK objects live");
        std::cout << "Temple source match cycle=" << cycle
                  << " Ready/120 ticks/pause/No Contest/teardown passed\n";
    }
}

int main(int argc, char** argv)
{
    try {
        if (argc < 2 || argc > 3)
            throw std::runtime_error("Expected Temple runtime asset directory and optional common directory");
        RuntimeFiles files;
        load_files(files, argv[1]);
        if (argc == 3) load_files(files, argv[2]);
        check(files.contains("GrSh.dat"), "Temple GrSh.dat is missing");
        check(files.contains("shrine.hps") && files.contains("akaneia.hps"),
              "Temple BGM candidates are missing");
        run_lifetime(files, 0);
        run_lifetime(files, 1);
        run_match_lifetimes(files);
        std::cout << "Original Hyrule Temple source maps, scaled collision, lights, BGM1/BGM75 readiness and two-lifetime teardown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
