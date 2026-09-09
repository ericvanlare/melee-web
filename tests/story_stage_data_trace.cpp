#include "dat_archive.hpp"
#include "dat_collision.hpp"
#include "dat_lights.hpp"
#include "dat_stage.hpp"
#include "gameplay_stage_story.h"
#include "native_dat.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

static void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& item : archive.public_symbols())
        if (item.name == name) return item.data_offset;
    throw std::runtime_error(std::string("Missing symbol ") + name);
}

static bool near(float actual, float expected)
{
    return std::isfinite(actual) && std::fabs(actual - expected) < 0.0001f;
}

int main(int argc, char** argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected owned GrSt.dat");
        std::ifstream file(std::filesystem::path(argv[1]), std::ios::binary);
        check(bool(file), "Cannot open GrSt.dat");
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        const auto original = bytes;
        auto archive = std::make_shared<DatArchive>(bytes,
                                                    DatExternalPolicy::ResolveNull);
        check(bytes == original, "DAT constructor changed caller-owned bytes");
        check(archive->external_symbols().size() == 1,
              "GrSt external symbol count changed");
        const auto& external = archive->external_symbols().front();
        check(external.name == "GrdStoryHeiho_TopN_shapeanim_joint" &&
                  external.slots == std::vector<uint32_t>{0x32d58, 0x32d78},
              "GrSt null external identity or chain changed");
        check(!archive->pointer(0x32d58) && !archive->pointer(0x32d78),
              "Original null external resolution was not applied");

        DatStage stage(*archive);
        check(stage.entries.size() == 4, "Yoshi's Story map entry count changed");
        const uint8_t animation_counts[] = {1, 1, 1, 2};
        check(!stage.entries[0].joint_animation_table &&
                  stage.entries[1].joint_animation_table &&
                  stage.entries[2].joint_animation_table &&
                  stage.entries[2].material_animation_table &&
                  stage.entries[3].joint_animation_table &&
                  !stage.entries[0].shape_animation_table &&
                  !stage.entries[1].shape_animation_table &&
                  !stage.entries[2].shape_animation_table &&
                  !stage.entries[3].shape_animation_table,
              "Yoshi's Story map animation services changed");
        check(animation_counts[3] == 2,
              "Source grStory map-3 callbacks require animation slots 0 and 1");
        check(stage.entries[2].collision_bindings.count == 1,
              "Randall collision binding count changed");
        check(stage.joint_reference_table.count == 2 &&
                  stage.spline_table.count == 1 &&
                  stage.light_override_table.count == 20 &&
                  stage.shadow_table.count == 0 &&
                  stage.flagged_object_table.count == 7,
              "Yoshi's Story map service table counts changed");

        DatCollision collision(*archive);
        const auto bindings = read_dat_collision_bindings(*archive, stage, collision);
        check(near(read_dat_stage_scale(*archive), 0.7f),
              "Yoshi's Story source scale changed");
        check(bindings.size() == 1 && bindings[0].stage_entry == 2,
              "Yoshi's Story moving collision binding changed");
        check(!collision.vertices.empty() && !collision.lines.empty() &&
                  !collision.joints.empty(),
              "Yoshi's Story collision arrays are empty");

        DatLights lights(*archive);
        check(!lights.lights.empty(), "Yoshi's Story light list is empty");
        for (const auto& light : lights.lights)
            (void) read_dat_light_override(*archive, light.source_offset);

        const uint32_t ground = symbol(*archive, "grGroundParam");
        check(near(archive->f32(ground), 0.7f) &&
                  archive->be32(ground + 0xb4) == 8,
              "Yoshi's Story GroundParam changed");
        const uint32_t versus = *archive->pointer(ground + 0xb0, 0x64);
        check(archive->be32(versus) == 8 && archive->be32(versus + 4) == 96 &&
                  int32_t(archive->be32(versus + 8)) == -1 &&
                  archive->be32(versus + 12) == 96 &&
                  int32_t(archive->be32(versus + 16)) == -1 &&
                  archive->be16(versus + 20) == 0 &&
                  archive->be16(versus + 22) == 1 &&
                  archive->be16(versus + 24) == 100,
              "Yoshi's Story versus BGM/selection row changed");

        NativeDatArena arena(archive);
        auto* yaku = static_cast<MeleeWebStoryYakumono*>(
            melee_web_story_yakumono_decode(
                arena.reader(), symbol(*archive, "yakumono_param")));
        check(yaku && near(yaku->timer_min, 600) &&
                  near(yaku->timer_rand, 1800) &&
                  near(yaku->spawnmany_rarity, 8),
              "Yoshi's Story timer/RNG parameters changed");
        const float expected_vpos[] = {30, 45, 60, 75, 90, 0};
        for (unsigned i = 0; i < 6; ++i)
            check(near(yaku->vpos[i], expected_vpos[i]),
                  "Yoshi's Story Shy Guy vertical positions changed");

        std::cout << "Yoshi's Story exact null extern, map, collision, light, "
                     "GroundParam and yakumono data passed: entries=4 "
                  << "vertices=" << collision.vertices.size()
                  << " lines=" << collision.lines.size()
                  << " joints=" << collision.joints.size()
                  << " lights=" << lights.lights.size() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
