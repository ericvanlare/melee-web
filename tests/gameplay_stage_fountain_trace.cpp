#include "dat_archive.hpp"
#include "dat_collision.hpp"
#include "dat_stage.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_stage_fountain.h"
#include "gameplay_world.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" int melee_web_test_stage_fountain_registry(
    uint32_t*, unsigned*, unsigned*, unsigned*, unsigned*);
extern "C" int melee_web_test_stage_fountain_platforms(
    int16_t[2], int16_t[2], int16_t[2], float[2], float[2], float[6], unsigned*);
extern "C" int melee_web_test_stage_fountain_dynamic_collision(
    float[12], int*, unsigned*);
extern "C" int melee_web_test_stage_fountain_markers(float[27]);
extern "C" int melee_web_test_stage_fountain_bounds(float[4], float[4]);
extern "C" int melee_web_test_stage_fountain_lights(uint32_t*, uint16_t[3],
                                                      uint8_t[12]);
extern "C" int melee_web_test_stage_fountain_light_positions(float[6]);
extern "C" int melee_web_test_stage_fountain_light_animation(void*);
extern "C" int melee_web_test_stage_fountain_global_light_animation(void*);
extern "C" int melee_web_test_stage_fountain_reflection(void);
extern "C" void* melee_web_test_stage_fountain_yakumono(void);
extern "C" int melee_web_test_stage_fountain_music(uint32_t, int*, int*);
extern "C" int melee_web_test_stage_fountain_empty(unsigned*, unsigned*);

using namespace melee_web;

struct FountainLightAnimationSnapshot {
    uint32_t source_light_lists;
    uint32_t source_animation_tables;
    uint32_t source_animation_records;
    uint32_t source_active_records;
    uint32_t source_channel_mask;
    uint32_t live_light_count;
    uint32_t live_lobj_animations;
    uint32_t live_position_animations;
    uint32_t live_interest_animations;
    uint32_t lobj_flags[3];
    uint32_t position_flags[3];
    uint32_t interest_flags[3];
    uint32_t lobj_channel_mask[3];
    uint32_t position_channel_mask[3];
    uint32_t interest_channel_mask[3];
    float lobj_frames[3];
    float position_frames[3];
    float interest_frames[3];
    float lobj_end_frames[3];
    float position_end_frames[3];
    float interest_end_frames[3];
    uint8_t colors[12];
    float positions[9];
    uint32_t spline_carriers;
    uint32_t spline_flags[3];
    float spline_scales[9];
};

static void check(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

static bool near(float actual, float expected, float epsilon = 0.0005f)
{
    return std::isfinite(actual) && std::fabs(actual - expected) <= epsilon;
}

static void load_files(RuntimeFiles& files, const std::filesystem::path& root)
{
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.file_size() > 64 * 1024 * 1024)
            continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open Fountain runtime file");
        files[entry.path().filename().string()] = {
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
}

static void check_archive_contract(const RuntimeFiles& files)
{
    const auto archive_it = files.find("GrIz.dat");
    check(archive_it != files.end(), "Fountain GrIz.dat is missing");
    check(archive_it->second.size() == 1118546,
          "Fountain GrIz.dat size changed from the owned source receipt");
    const auto music_it = files.find("izumi.hps");
    check(music_it != files.end() && music_it->second.size() == 5944864,
          "Fountain BGM49 payload is missing or changed");

    DatArchive archive(archive_it->second, DatExternalPolicy::ResolveNull);
    DatStage stage(archive);
    check(stage.entries.size() == 5, "Fountain map entry count changed");
    const bool joint_animation[] = {false, true, false, true, true};
    const bool material_animation[] = {false, true, false, true, true};
    const bool shape_animation[] = {false, false, false, false, true};
    for (unsigned i = 0; i < 5; ++i) {
        check(stage.entries[i].joint_offset.has_value() &&
                  stage.entries[i].joint_animation_table.has_value() == joint_animation[i] &&
                  stage.entries[i].material_animation_table.has_value() == material_animation[i] &&
                  stage.entries[i].shape_animation_table.has_value() == shape_animation[i],
              "Fountain map animation consumer table changed");
    }
    DatCollision collision(archive);
    check(std::fabs(read_dat_stage_scale(archive) - 0.75f) <= 0.00001f &&
              collision.vertices.size() == 38 && collision.lines.size() == 34 &&
              collision.joints.size() == 4 && collision.line_ranges[4].count == 0,
          "Fountain source scale or collision table changed");
    for (unsigned i = 0; i < 3; ++i)
        check(collision.joints[i].line_ranges[0].start == static_cast<int16_t>(i) &&
                  collision.joints[i].line_ranges[0].count == 1,
              "Fountain source platform collision joint ranges changed");
}

static void check_markers_and_bounds()
{
    float markers[27] = {};
    check(melee_web_test_stage_fountain_markers(markers),
          "Fountain source marker identities are unavailable");
    const float expected_markers[27] = {
        0.0f, 14.775f, 0.0f,
        0.0f, 135.225f, 0.0f,
        -45.0f, 135.000075f, 0.0f,
        45.0f, 135.000075f, 0.0f,
        0.0f, 30.0f, 0.0f,
        -123.75f, 112.5f, 0.0f,
        123.75f, -84.75f, 0.0f,
        -198.75f, 202.5f, 0.0f,
        198.75f, -146.25f, 0.0f,
    };
    for (unsigned i = 0; i < 27; ++i)
        check(near(markers[i], expected_markers[i]),
              "Fountain authored marker did not follow source scale 0.75");

    float camera[4] = {}, blast[4] = {};
    check(melee_web_test_stage_fountain_bounds(camera, blast),
          "Fountain source camera/blast bounds are unavailable");
    const float expected_camera[4] = {-123.75f, -114.75f, 123.75f, 82.5f};
    const float expected_blast[4] = {-198.75f, -176.25f, 198.75f, 172.5f};
    for (unsigned i = 0; i < 4; ++i) {
        check(near(camera[i], expected_camera[i]),
              "Fountain authored camera bounds did not follow source scale 0.75");
        check(near(blast[i], expected_blast[i]),
              "Fountain authored blast bounds did not follow source scale 0.75");
    }
}

static FountainLightAnimationSnapshot check_lights()
{
    uint32_t count = 0;
    uint16_t flags[3] = {};
    uint8_t colors[12] = {};
    check(melee_web_test_stage_fountain_lights(&count, flags, colors) && count == 3,
          "Fountain source light list was not published");
    const uint16_t expected_flags[3] = {4, 5, 1037};
    const uint8_t expected_colors[12] = {
        204, 204, 204, 255, 145, 164, 255, 255, 51, 0, 102, 255,
    };
    for (unsigned i = 0; i < 3; ++i)
        check(flags[i] == expected_flags[i],
              "Fountain source light override mutation changed");
    for (unsigned i = 0; i < 12; ++i)
        check(colors[i] == expected_colors[i],
              "Fountain source light color identity changed");

    float positions[6] = {};
    check(melee_web_test_stage_fountain_light_positions(positions),
          "Fountain live source light chain was not published");
    const float expected_positions[6] = {
        -1.5f, -2.25f, 6.75f,
        -2.7651352f, 11.022683f, 0.9194678f,
    };
    for (unsigned i = 0; i < 6; ++i)
        check(near(positions[i], expected_positions[i]),
              "Fountain live HSD_WObjDesc light position did not follow scale 0.75");

    FountainLightAnimationSnapshot animation{};
    check(melee_web_test_stage_fountain_light_animation(&animation),
          "Fountain selected Ground light animation owner was not published");
    std::cerr << "Fountain initial live light animations: lights="
              << animation.live_light_count << " lobj="
              << animation.live_lobj_animations << " position="
              << animation.live_position_animations << " interest="
              << animation.live_interest_animations << " spline="
              << animation.spline_carriers << '\n';
    for (unsigned i = 0; i < 3; ++i)
        std::cerr << "Fountain light " << i << " channels="
                  << animation.lobj_channel_mask[i] << ','
                  << animation.position_channel_mask[i] << ','
                  << animation.interest_channel_mask[i] << " flags="
                  << animation.lobj_flags[i] << ',' << animation.position_flags[i]
                  << " frames=" << animation.lobj_frames[i] << ','
                  << animation.position_frames[i] << '\n';
    check(animation.source_light_lists == 3 &&
              animation.source_animation_tables == 3 &&
              animation.source_animation_records == 3 &&
              animation.source_active_records == 1 &&
              animation.source_channel_mask ==
                  ((1u << 9) | (1u << 10) | (1u << 11)),
          "Fountain map_plit animation tables or RGB FObj channels changed");
    check(animation.live_light_count == 3 &&
              animation.live_lobj_animations == 1 &&
              animation.live_position_animations == 1 &&
              animation.live_interest_animations == 0,
          "Fountain selected light animation owner count changed");
    check(animation.lobj_channel_mask[1] ==
                  ((1u << 9) | (1u << 10) | (1u << 11)) &&
              animation.position_channel_mask[1] == (1u << 4) &&
              animation.interest_channel_mask[1] == 0,
          "Fountain live LObj/WObj FObj channels changed");
    check((animation.lobj_flags[1] & (1u << 29)) != 0 &&
              (animation.position_flags[1] & (1u << 29)) != 0 &&
              near(animation.lobj_frames[1], 0.0f) &&
              near(animation.position_frames[1], 0.0f) &&
              near(animation.lobj_end_frames[1], 899.0f) &&
              near(animation.position_end_frames[1], 899.0f),
          "Fountain source OnLoad did not preserve looping light animations");
    std::cerr << "Fountain source spline carrier: flags=0x" << std::hex
              << animation.spline_flags[0] << std::dec << " scale="
              << animation.spline_scales[0] << ',' << animation.spline_scales[1]
              << ',' << animation.spline_scales[2] << '\n';
    /* HSD_JObjInit starts every live object dirty (JOBJ_MTX_DIRTY, 0x40),
     * then HSD_JObjLoadJoint ORs in the authored 0x4018 spline flags. This
     * carrier is an animation target rather than a drawn model, so the
     * constructor dirtiness is still present at this boundary. Compare the
     * source-owned bits separately from that legitimate live-constructor bit. */
    check(animation.spline_carriers == 1 &&
              (animation.spline_flags[0] & ~0x40u) == 0x4018 &&
              (animation.spline_flags[0] & 0x40u) != 0 &&
              near(animation.spline_scales[0], 1.0f) &&
              near(animation.spline_scales[1], 1.0f) &&
              near(animation.spline_scales[2], 1.0f),
          "Fountain source spline carrier flags or scale changed");
    for (unsigned i = 0; i < 9; ++i)
        check(std::isfinite(animation.positions[i]),
              "Fountain live source light animation position is nonfinite");

    /* Ground_801C466C owns a second p_link-3 light chain. The earlier
     * classifier-0xC owner is the global map_plit chain: Stage_OnLoad applies
     * AOBJ_LOOP to its authored RGB animation, while the Ground-selected owner
     * carries the separate WObj path and spline. */
    FountainLightAnimationSnapshot global{};
    check(melee_web_test_stage_fountain_global_light_animation(&global),
          "Fountain global map_plit light animation owner was not published");
    check(global.live_light_count == 3 &&
              global.live_lobj_animations == 1 &&
              global.live_position_animations == 0 &&
              global.live_interest_animations == 0 &&
              global.spline_carriers == 0,
          "Fountain global light owner unexpectedly acquired Ground animation");
    check(global.lobj_channel_mask[2] ==
                  ((1u << 9) | (1u << 10) | (1u << 11)) &&
              (global.lobj_flags[2] & 0x08000000u) != 0 &&
              near(global.lobj_frames[2], 0.0f) &&
              (global.lobj_flags[2] & (1u << 29)) != 0,
          "Fountain global RGB light OnLoad loop or channels changed");
    return animation;
}

static bool changed(const float* before, const float* after, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if (std::fabs(before[i] - after[i]) > 0.0005f)
            return true;
    return false;
}

static void run_lifetime(const RuntimeFiles& files, unsigned cycle)
{
    GameplayWorldSelection selection{};
    selection.player_count = 2;
    selection.fighter_kinds = {FTKIND_MARIO, FTKIND_MARIO, 0, 0};
    selection.ground_kind = Gr_Kind_Izumi;
    GameplayWorld world(files, selection);
    void* previous_yakumono = melee_web_test_stage_fountain_yakumono();
    world.enable_full_stage();

    void* live_yakumono = melee_web_test_stage_fountain_yakumono();
    check(live_yakumono != nullptr && live_yakumono != previous_yakumono,
          "Fountain source yakumono root was not exchanged");
    const auto* yaku = static_cast<const MeleeWebFountainYakumono*>(live_yakumono);
    check(yaku->x0 == 20.0f && yaku->x4 == -1082130432 && yaku->x8 == 28.0f &&
              yaku->xC == 25.0f && yaku->x10 == 25.0f && yaku->x14 == 25.0f &&
              yaku->x18 == 5.0f && yaku->x1C == 10.0f &&
              yaku->x20 == 35.0f && yaku->x24 == 15.0f && yaku->x28 == 0.15f &&
              yaku->x2C == 0.1f && yaku->x30 == 0.25f && yaku->x34 == 0.25f &&
              yaku->x38 == 600.0f && yaku->x3C == 1080.0f && yaku->x40 == 4.0f &&
              yaku->x44 == 10.0f && yaku->x48 == 8.0f && yaku->x4C == 480.0f &&
              yaku->x50 == 1680.0f,
          "Fountain source yakumono scalar values changed");

    uint32_t registry_mask = 0;
    unsigned registry_count = 0, platform_count = 0, stage_gobj_count = 0;
    unsigned star_count = 0;
    check(melee_web_test_stage_fountain_registry(
              &registry_mask, &registry_count, &platform_count,
              &stage_gobj_count, &star_count),
          "Fountain OnInit/source object registry differs");
    check(registry_mask == 0x1f && registry_count == 5 && platform_count == 2 &&
              stage_gobj_count == 7 && star_count == 1,
          "Fountain descriptor IDs and actual GObj multiplicity differ");
    check(melee_web_test_stage_fountain_reflection(),
          "Fountain reflection camera/TObj source owners are missing");

    check_markers_and_bounds();
    const FountainLightAnimationSnapshot initial_light_animation = check_lights();
    /* Exercise each source StageParam field: x4/x8, then xC/x10 through
     * both the 0x10 and 0x20 selection branches. Fountain's x8/x10 are -1,
     * so the original Ground_801C24F8 fallback must retain x4/xC and report
     * primary music without bypassing its save/RNG gate. */
    for (uint32_t mask : {1u, 2u, 0x11u, 0x12u, 0x21u, 0x22u}) {
        int music = -1, alternate = -1;
        check(melee_web_test_stage_fountain_music(mask, &music, &alternate),
              "Fountain source BGM selection returned no ID");
        check(music == 49 && alternate == 0,
              "Fountain x4/x8/xC/x10 source BGM selection changed");
    }

    int16_t state[2] = {}, timer[2] = {}, index[2] = {};
    float current[2] = {}, target[2] = {}, positions[6] = {};
    unsigned platform_snapshot_count = 0;
    check(melee_web_test_stage_fountain_platforms(
              state, timer, index, current, target, positions,
              &platform_snapshot_count) && platform_snapshot_count == 2,
          "Fountain source platform objects were not both observable");
    check(index[0] != index[1] && current[0] == target[0] && current[1] == target[1],
          "Fountain source platform initialization lost authored targets");
    for (unsigned i = 0; i < 2; ++i)
        check(state[i] == 1 && timer[i] >= 600 && timer[i] <= 1080 &&
                  near(current[i], i == 0 ? 20.0f : 28.0f),
              "Fountain source platform initial scheduler state changed");

    float initial_collision[12] = {};
    int initial_joints[3] = {};
    unsigned collision_count = 0;
    check(melee_web_test_stage_fountain_dynamic_collision(
              initial_collision, initial_joints, &collision_count) &&
              collision_count == 3 && initial_joints[0] == 0 &&
              initial_joints[1] == 1 && initial_joints[2] == 2,
          "Fountain source collision joints 0, 1 and 2 were not bound");
    for (float value : initial_collision)
        check(std::isfinite(value), "Fountain initial dynamic collision is nonfinite");

    bool platform_moved = false;
    bool collision_moved = false;
    bool saw_motion_state = false;
    bool sampled_light_animation = false;
    FountainLightAnimationSnapshot sampled_light{};
    char error[256] = {};
    for (unsigned tick = 0; tick < 7200; ++tick) {
        check(melee_web_gameplay_step(error, sizeof(error)), error);
        check(melee_web_test_stage_fountain_platforms(
                  state, timer, index, current, target, positions,
                  &platform_snapshot_count) && platform_snapshot_count == 2,
              "Fountain platform source objects disappeared during scheduler ticks");
        for (unsigned i = 0; i < 2; ++i) {
            check(index[i] == 0 || index[i] == 1,
                  "Fountain platform source index changed");
            check(state[i] >= 0 && state[i] <= 4 && std::isfinite(current[i]) &&
                      std::isfinite(target[i]) && std::isfinite(positions[i * 3]) &&
                      std::isfinite(positions[i * 3 + 1]),
                  "Fountain source platform state became invalid");
            if (state[i] == 2 || state[i] == 3 || state[i] == 4)
                saw_motion_state = true;
        }
        platform_moved |= std::fabs(current[0] - 20.0f) > 0.0005f ||
                          std::fabs(current[1] - 28.0f) > 0.0005f;
        float collision[12] = {};
        int joints[3] = {};
        unsigned count = 0;
        check(melee_web_test_stage_fountain_dynamic_collision(
                  collision, joints, &count) && count == 3,
              "Fountain source collision bindings disappeared during scheduler ticks");
        collision_moved |= changed(initial_collision, collision, 12);
        if (tick == 120) {
            check(melee_web_test_stage_fountain_light_animation(&sampled_light),
                  "Fountain source light animation disappeared during scheduler ticks");
            sampled_light_animation = true;
        }
    }
    check(saw_motion_state && platform_moved,
          "Fountain source platform scheduler did not reach a motion state");
    check(collision_moved,
          "Fountain source collision joints did not follow platform motion");

    FountainLightAnimationSnapshot final_light{};
    check(melee_web_test_stage_fountain_light_animation(&final_light),
          "Fountain source light animation owner disappeared after scheduler ticks");
    check(sampled_light_animation &&
              sampled_light.live_light_count == 3 &&
              final_light.live_light_count == 3,
          "Fountain source light animation snapshots were incomplete");
    check(std::isfinite(sampled_light.lobj_frames[1]) &&
              std::isfinite(sampled_light.position_frames[1]) &&
              std::isfinite(final_light.lobj_frames[1]) &&
              std::isfinite(final_light.position_frames[1]) &&
              std::fabs(sampled_light.lobj_frames[1] -
                        initial_light_animation.lobj_frames[1]) > 0.0005f &&
              std::fabs(sampled_light.position_frames[1] -
                        initial_light_animation.position_frames[1]) > 0.0005f,
          "Fountain source AObj frames did not advance through the scheduler");
    check(changed(initial_light_animation.positions + 3,
                  sampled_light.positions + 3, 3) &&
              changed(initial_light_animation.positions + 3,
                      final_light.positions + 3, 3),
          "Fountain source WObj position animation did not advance");
    bool color_changed = false;
    for (unsigned i = 4; i < 7; ++i)
        color_changed |= initial_light_animation.colors[i] != sampled_light.colors[i] ||
                         initial_light_animation.colors[i] != final_light.colors[i];
    check(color_changed,
          "Fountain source LObj RGB FObj channels did not update live color");
    check((final_light.lobj_flags[1] & (1u << 29)) != 0 &&
              (final_light.position_flags[1] & (1u << 29)) != 0 &&
              (final_light.spline_flags[0] & ~0x40u) == 0x4018 &&
              (final_light.spline_flags[0] & 0x40u) != 0 &&
              near(final_light.spline_scales[0], 1.0f) &&
              near(final_light.spline_scales[1], 1.0f) &&
              near(final_light.spline_scales[2], 1.0f),
          "Fountain source light OnLoad flags or spline scale changed during ticks");
    std::cout << " Fountain light animation frame=" << initial_light_animation.position_frames[1]
              << "->" << sampled_light.position_frames[1] << "->"
              << final_light.position_frames[1] << " color="
              << unsigned(initial_light_animation.colors[4]) << "->"
              << unsigned(final_light.colors[4]) << " position="
              << initial_light_animation.positions[3] << "->"
              << final_light.positions[3] << '\n';

    world.verify_immutable_archives();
    world.end_stage();
    unsigned empty_registry = 0, empty_stage = 0;
    check(melee_web_test_stage_fountain_empty(&empty_registry, &empty_stage) &&
              empty_registry == 0 && empty_stage == 0,
          "Fountain teardown left source map GObjs live");
    check(melee_web_test_stage_fountain_yakumono() == previous_yakumono,
          "Fountain teardown did not restore the previous yakumono root");
    world.close();
    check(melee_web_gameplay_stats().objects == 0,
          "Fountain world close left SDK objects live");
    std::cout << "Fountain cycle=" << cycle
              << " source maps=5 actual_stage_gobjs=7 platform_motion/collision=passed\n";
}

int main(int argc, char** argv)
{
    try {
        if (argc != 3)
            throw std::runtime_error("Expected Fountain and common runtime asset directories");
        RuntimeFiles files;
        load_files(files, argv[1]);
        load_files(files, argv[2]);
        for (const char* name : {"GrIz.dat", "izumi.hps"})
            check(files.contains(name), std::string("Missing Fountain asset: ") + name);
        for (const char* name : {"PlCo.dat", "PlMr.dat", "PlMrAJ.dat",
                                 "EfMrData.dat", "ItCo.usd", "EfCoData.dat",
                                 "PdPm.dat", "LbRb.dat", "sislib_font.bin"})
            check(files.contains(name), std::string("Missing common Fountain asset: ") + name);
        check_archive_contract(files);
        run_lifetime(files, 0);
        run_lifetime(files, 1);
        std::cout << "Original Fountain source maps, dynamic platform collision, scaled markers/lights, "
                     "reflection and two-lifetime teardown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
