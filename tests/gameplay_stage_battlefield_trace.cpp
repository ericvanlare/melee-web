#include "gameplay_world.hpp"
#include "gameplay_content.h"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include "gameplay_collision.h"
#include "dat_archive.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

extern "C" int melee_web_test_stage_battlefield_state(uint32_t*, unsigned*);
extern "C" int melee_web_test_stage_battlefield_background(int*, int*, int*, int*);
extern "C" int melee_web_test_stage_battlefield_bounds(float[4], float[4]);

using namespace melee_web;

static std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open stage runtime file");
    const auto size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > DatArchive::max_archive_bytes)
        throw std::runtime_error("Invalid stage runtime file size");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("Truncated stage runtime file");
    return bytes;
}

static void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

static bool near(float actual, float expected, float epsilon = 0.0005f)
{
    return std::isfinite(actual) && std::fabs(actual - expected) <= epsilon;
}

static void cleanup_cycle(GameplayWorld& world, MeleeWebMatchContext*& match,
                          MeleeWebRender*& camera)
{
    char cleanup_error[256] = {};
    try {
        /* The world owns the source stage manager, so release it before the
         * render and fighter contexts just as the production session does. */
        world.end_stage();
    } catch (const std::exception& error) {
        std::cerr << "Battlefield trace stage cleanup failed: " << error.what()
                  << '\n';
    }
    if (camera) {
        if (!melee_web_render_end(camera, cleanup_error, sizeof(cleanup_error)))
            std::cerr << "Battlefield trace render cleanup failed: "
                      << cleanup_error << '\n';
        camera = nullptr;
    }
    if (match) {
        if (!melee_web_match_end(match, cleanup_error, sizeof(cleanup_error)))
            std::cerr << "Battlefield trace match cleanup failed: "
                      << cleanup_error << '\n';
        match = nullptr;
    }
    try {
        world.close();
    } catch (const std::exception& error) {
        std::cerr << "Battlefield trace world cleanup failed: " << error.what()
                  << '\n';
    }
}

static std::string lifecycle_detail(unsigned tick, uint32_t mask, unsigned count,
                                    int probe_ok, int state, int current,
                                    int previous, int timer, const char* error)
{
    std::ostringstream detail;
    detail << "Battlefield lifecycle failure at tick=" << tick
           << " map_mask=0x" << std::hex << mask << std::dec
           << " objects=" << count << " background_probe=" << probe_ok
           << " state=" << state << " current=" << current
           << " previous=" << previous << " timer=" << timer;
    if (error && *error) detail << " error=" << error;
    return detail.str();
}

int main(int argc, char** argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected local runtime asset directory");
        const std::filesystem::path root(argv[1]);
        RuntimeFiles files;
        for (const char* name : {"PlCo.dat", "PlMr.dat", "PlMrNr.dat", "PlMrAJ.dat",
                                 "GrNBa.dat", "ItCo.usd", "EfMrData.dat", "EfCoData.dat",
                                 "PdPm.dat", "LbRb.dat", "sislib_font.bin"})
            files.emplace(name, read_file(root / name));

        GameplayWorldSelection selection{};
        selection.fighter_kinds = {FTKIND_MARIO, FTKIND_MARIO};
        selection.ground_kind = Gr_Kind_Battle;
        for (unsigned cycle = 0; cycle < 2; ++cycle) {
        GameplayWorld world(files, selection);
            MeleeWebMatchContext* match = nullptr;
            MeleeWebRender* camera = nullptr;
            bool cycle_complete = false;
            try {
            const auto first = world.player_spawn(0);
            const auto second = world.player_spawn(1);
            for (float value : first) check(std::isfinite(value), "Battlefield spawn is nonfinite");
            for (float value : second) check(std::isfinite(value), "Battlefield spawn is nonfinite");
            /* The regular match path starts the original stage manager. This
             * is required for grBattle's delayed background transitions. */
            MeleeWebMatchSettings match_settings{};
            match_settings.player = {0, 0, 4,
                                     {0, world.floor_height(0) + 1, 0}, 1};
            match_settings.camera_subjects = 70;
            match_settings.random_seed = 0x13579bdfu + cycle;
            char error[256];
            match = melee_web_match_begin(&match_settings, world.collision(),
                                          error, sizeof(error));
            check(match != nullptr, error);
            check(melee_web_match_create_fighter(match, error, sizeof(error)), error);
            MeleeWebRenderSettings render_settings{
                640, 480, {0, 35, 190}, {0, 5, 0}, 45, 1, 2000,
                (UINT64_C(1) << 3) | (UINT64_C(1) << 5)};
            camera = melee_web_render_begin_match(&render_settings,
                                                  error, sizeof(error));
            check(camera != nullptr, error);
            world.enable_full_stage();

            const float expected_first[3] = {0, 8, 0};
            const float expected_second[3] = {0, 62.4f, 0};
            for (unsigned axis = 0; axis < 3; ++axis) {
                check(near(first[axis], expected_first[axis]),
                      "Battlefield first spawn did not preserve source 0.8 scale");
                check(near(second[axis], expected_second[axis]),
                      "Battlefield second spawn did not preserve source 0.8 scale");
            }
            const auto third = world.player_spawn(2);
            const auto fourth = world.player_spawn(3);
            check(near(third[0], -38.8f) && near(third[1], 35.2f) && near(third[2], 0),
                  "Battlefield third spawn did not preserve source 0.8 scale");
            check(near(fourth[0], 38.8f) && near(fourth[1], 35.2f) && near(fourth[2], 0),
                  "Battlefield fourth spawn did not preserve source 0.8 scale");

            float camera_bounds[4], blast_bounds[4];
            check(melee_web_test_stage_battlefield_bounds(camera_bounds, blast_bounds),
                  "Battlefield source scale/bounds probe failed");
            const float expected_camera[4] = {-160, -82.4f, 160, 100.8f};
            const float expected_blast[4] = {-224, -144, 224, 164.8f};
            for (unsigned i = 0; i < 4; ++i) {
                check(near(camera_bounds[i], expected_camera[i]),
                      "Battlefield camera bounds are not source-scaled");
                check(near(blast_bounds[i], expected_blast[i]),
                      "Battlefield blast bounds are not source-scaled");
            }

            /* The source floor is line 1, and its three raised platform
             * segments are lines 2..4. Query the post-mpLib vertices so the
             * test covers collision scaling as well as marker scaling. */
            const float platform_endpoints[3][4] = {
                {-57.6f, 27.2f, -20, 27.2f},
                {-18.8f, 54.4f, 18.8f, 54.4f},
                {20, 27.2f, 57.6f, 27.2f},
            };
            for (int line_index = 2; line_index <= 4; ++line_index) {
                MeleeWebCollisionLineResult line{};
                check(melee_web_collision_line(world.collision(), line_index, &line,
                                                error, sizeof(error)), error);
                const auto& expected = platform_endpoints[line_index - 2];
                check(line.kind == 1 && near(line.v0[0], expected[0]) &&
                          near(line.v0[1], expected[1]) && near(line.v1[0], expected[2]) &&
                          near(line.v1[1], expected[3]),
                      "Battlefield source platform coordinates were not scaled");
            }

            uint32_t mask = 0;
            unsigned count = 0;
            check(melee_web_test_stage_battlefield_state(&mask, &count),
                  "Original Battlefield OnInit did not install required map objects");
            check((mask & UINT32_C(0x4b)) == UINT32_C(0x4b),
                  "Original Battlefield map object IDs differ");
            check(count >= 4 && count <= 7, "Unexpected Battlefield map object count");

            int previous_state = -1;
            unsigned transitions = 0;
            unsigned states_seen = 0;
            unsigned completed_transitions = 0;
            for (unsigned tick = 0; tick < 7200; ++tick) {
                error[0] = 0;
                if (!melee_web_match_step(match, 1, error, sizeof(error))) {
                    int state = -1, current = -1, previous = -1, timer = 0;
                    const int probe_ok = melee_web_test_stage_battlefield_background(
                        &state, &current, &previous, &timer);
                    throw std::runtime_error(lifecycle_detail(
                        tick, mask, count, probe_ok, state, current, previous,
                        timer, error));
                }
                int state = -1, current = -1, previous = -1, timer = 0;
                const int probe_ok = melee_web_test_stage_battlefield_background(
                    &state, &current, &previous, &timer);
                if (!melee_web_test_stage_battlefield_state(&mask, &count) ||
                    !probe_ok) {
                    throw std::runtime_error(lifecycle_detail(
                        tick, mask, count, probe_ok, state, current, previous,
                        timer, error));
                }
                states_seen |= 1u << static_cast<unsigned>(state);
                if (state != previous_state) {
                    ++transitions;
                    std::cout << "BF cycle=" << cycle << " tick=" << tick
                              << " state=" << state << " current=" << current
                              << " previous=" << previous << " timer=" << timer
                              << " objects=" << count << '\n';
                    previous_state = state;
                }
                if (state == 2) ++completed_transitions;
                check(current >= -1 && current <= 4 && previous >= -1 && previous <= 4,
                      "Battlefield background selected an invalid source map ID");
            }
            check((states_seen & 0x7u) == 0x7u && transitions >= 3 &&
                      completed_transitions >= 1,
                  "Battlefield background scheduler did not complete a source transition");
            world.verify_immutable_archives();
            world.end_stage();
            check(melee_web_render_end(camera, error, sizeof(error)), error);
            camera = nullptr;
            check(melee_web_match_end(match, error, sizeof(error)), error);
            match = nullptr;
            world.close();
            cycle_complete = true;
            } catch (...) {
                if (!cycle_complete) cleanup_cycle(world, match, camera);
                throw;
            }
        }
        std::cout << "Original Battlefield map descriptors, scaled markers, OnInit and two-cycle teardown passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
