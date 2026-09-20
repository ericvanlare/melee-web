#include "dat_archive.hpp"
#include "dat_collision.hpp"
#include "dat_lights.hpp"
#include "dat_stage.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_stage_old_yoshi.h"
#include "gameplay_world.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

extern "C" int melee_web_test_stage_old_yoshi_registry(
    uint32_t*, unsigned*, int[6]);
extern "C" int melee_web_test_stage_old_yoshi_clouds(
    int16_t[3], uint8_t[3], uint8_t[3], float[3], float[3], uint8_t[3],
    uint8_t[3]);
extern "C" int melee_web_test_stage_old_yoshi_cloud_callbacks(uint8_t[3]);
extern "C" int melee_web_test_stage_old_yoshi_trigger_contact(unsigned);
extern "C" int melee_web_test_stage_old_yoshi_guest(
    int16_t*, int16_t*, uint8_t*, uint8_t*);
extern "C" void* melee_web_test_stage_old_yoshi_yakumono(void);
extern "C" int melee_web_test_stage_old_yoshi_empty(unsigned*);

using namespace melee_web;

static void check(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}

static void load_files(RuntimeFiles& files, const std::filesystem::path& root)
{
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.file_size() > 64 * 1024 * 1024)
            continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) throw std::runtime_error("Cannot open Old Yoshi runtime file");
        files[entry.path().filename().string()] = {
            std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
}

static uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& item : archive.public_symbols())
        if (item.name == name) return item.data_offset;
    throw std::runtime_error(std::string("Missing symbol ") + name);
}

static bool near(float actual, float expected, float epsilon = 0.0001f)
{
    return std::isfinite(actual) && std::fabs(actual - expected) <= epsilon;
}

static void check_archive_contract(const RuntimeFiles& files)
{
    const auto archive_it = files.find("GrOy.dat");
    check(archive_it != files.end() && archive_it->second.size() == 247034,
          "Old Yoshi GrOy.dat is missing or changed");
    const auto music_it = files.find("old_ys.hps");
    check(music_it != files.end() && music_it->second.size() == 2164672,
          "Old Yoshi BGM59 payload is missing or changed");

    DatArchive archive(archive_it->second);
    DatStage stage(archive);
    check(stage.entries.size() == 6, "Old Yoshi map entry count changed");
    const bool joint_animation[] = {false, false, true, true, true, true};
    const bool material_animation[] = {false, false, true, true, true, false};
    for (unsigned i = 0; i < 6; ++i) {
        check(stage.entries[i].joint_offset.has_value() &&
                  stage.entries[i].joint_animation_table.has_value() == joint_animation[i] &&
                  stage.entries[i].material_animation_table.has_value() == material_animation[i] &&
                  !stage.entries[i].shape_animation_table,
              "Old Yoshi map animation table topology changed");
    }
    check(stage.joint_reference_table.count == 1 && stage.spline_table.count == 0 &&
              stage.light_override_table.count == 28 && stage.shadow_table.count == 0 &&
              stage.flagged_object_table.count == 11,
          "Old Yoshi source service table counts changed");

    DatCollision collision(archive);
    check(near(read_dat_stage_scale(archive), 1.0f) &&
              collision.vertices.size() == 22 && collision.lines.size() == 16 &&
              collision.joints.size() == 4 && collision.line_ranges[4].count == 0,
          "Old Yoshi source scale or collision table changed");
    DatLights lights(archive);
    check(lights.lights.size() == 2,
          "Old Yoshi global map_plit light identity changed");

    const uint32_t yaku_root = symbol(archive, "yakumono_param");
    check(archive.next_target_offset(yaku_root) - yaku_root == 0x1c,
          "Old Yoshi yakumono source extent changed");
    check(archive.be16(yaku_root) == 120 && archive.be16(yaku_root + 2) == 180 &&
              near(archive.f32(yaku_root + 4), 0.15f) &&
              near(archive.f32(yaku_root + 8), 0.15f) &&
              near(archive.f32(yaku_root + 0x0c), 6.0f) &&
              archive.be16(yaku_root + 0x10) == 30 &&
              archive.be16(yaku_root + 0x12) == 30 &&
              archive.be16(yaku_root + 0x14) == 3000 &&
              archive.be16(yaku_root + 0x16) == 4000 &&
              archive.be16(yaku_root + 0x18) == 30,
          "Old Yoshi authored yakumono words changed");
}

static void check_yakumono()
{
    const auto* value = static_cast<const MeleeWebOldYoshiYakumono*>(
        melee_web_test_stage_old_yoshi_yakumono());
    check(value != nullptr && value->x0 == 120 && value->x2 == 180 &&
              near(value->x4, 0.15f) && near(value->x8, 0.15f) &&
              near(value->xC, 6.0f) && value->x10 == 30 && value->x12 == 30 &&
              value->x14 == 3000 && value->x16 == 4000 && value->x18 == 30 &&
              value->_final_padding == 0,
          "Old Yoshi live yakumono values changed");
}

static void run_lifetime(const RuntimeFiles& files, unsigned cycle)
{
    GameplayWorldSelection selection{};
    selection.player_count = 2;
    selection.fighter_kinds = {FTKIND_MARIO, FTKIND_MARIO, 0, 0};
    selection.ground_kind = Gr_Kind_OldYoshi;
    GameplayWorld world(files, selection);
    void* previous_yakumono = melee_web_test_stage_old_yoshi_yakumono();
    world.enable_full_stage();

    void* live_yakumono = melee_web_test_stage_old_yoshi_yakumono();
    check(live_yakumono != nullptr && live_yakumono != previous_yakumono,
          "Old Yoshi source yakumono root was not exchanged");
    check_yakumono();

    uint32_t map_mask = 0;
    unsigned map_count = 0;
    int creation_order[6] = {};
    check(melee_web_test_stage_old_yoshi_registry(
              &map_mask, &map_count, creation_order),
          "Old Yoshi source registry or callback identities differ");
    check(map_mask == 0x3f && map_count == 6,
          "Old Yoshi source map registry does not contain six objects");
    const int expected_order[6] = {0, 1, 4, 5, 2, 3};
    for (unsigned i = 0; i < 6; ++i)
        check(creation_order[i] == expected_order[i],
              "Old Yoshi source map initialization order changed");

    uint8_t callbacks[3] = {};
    check(melee_web_test_stage_old_yoshi_cloud_callbacks(callbacks),
          "Old Yoshi cloud collision callbacks were not installed");
    for (unsigned i = 0; i < 3; ++i)
        check(callbacks[i] == 1, "Old Yoshi cloud callback owner identity changed");

    int16_t cloud_state[3] = {};
    uint8_t cloud_timer[3] = {};
    uint8_t cloud_contact[3] = {}, cloud_hidden[3] = {}, cloud_enabled[3] = {};
    float cloud_displacement[3] = {}, cloud_velocity[3] = {};
    check(melee_web_test_stage_old_yoshi_clouds(
              cloud_state, cloud_contact, cloud_timer,
              cloud_displacement, cloud_velocity, cloud_hidden, cloud_enabled),
          "Old Yoshi initial cloud state was unavailable");
    for (unsigned i = 0; i < 3; ++i) {
        check(cloud_state[i] == 0 && cloud_contact[i] == 0 &&
                  cloud_timer[i] == 0 && near(cloud_displacement[i], 0.0f) &&
                  near(cloud_velocity[i], 0.0f) && cloud_enabled[i] == 1,
              "Old Yoshi initial cloud state changed");
    }

    int16_t guest_timer = 0, guest_child = 0;
    uint8_t guest_hidden = 0, guest_child_visible = 0;
    check(melee_web_test_stage_old_yoshi_guest(
              &guest_timer, &guest_child, &guest_hidden, &guest_child_visible),
          "Old Yoshi guest scheduler state was unavailable");
    check(guest_timer >= 3000 && guest_timer < 4000 && guest_child == -1 &&
              guest_hidden == 1 && guest_child_visible == 0,
          "Old Yoshi authored guest scheduler initialization changed");

    bool saw_cloud_state_1 = false;
    bool saw_cloud_state_2 = false;
    bool saw_cloud_state_3 = false;
    bool saw_cloud_disabled = false;
    bool saw_cloud_reenabled = false;
    bool saw_guest_child = false;
    bool saw_guest_visible = false;
    bool contact_phase_done = false;
    int16_t previous_guest_timer = guest_timer;
    char error[256] = {};
    for (unsigned tick = 0; tick < 5000; ++tick) {
        check(melee_web_test_stage_old_yoshi_clouds(
                  cloud_state, cloud_contact, cloud_timer,
                  cloud_displacement, cloud_velocity, cloud_hidden, cloud_enabled),
              "Old Yoshi cloud state disappeared before scheduler tick");
        /* grOldYoshi_8020EC10 clears xC4_4 at the end of every source tick.
         * Re-submit the original collision callback once per frame while the
         * first cloud remains in state 0, then let the source state machine
         * own the complete collapse/reappearance cycle. */
        if (!contact_phase_done && cloud_state[0] == 0) {
            check(melee_web_test_stage_old_yoshi_trigger_contact(0),
                  "Old Yoshi source cloud collision callback did not accept contact stimulus");
        }
        check(melee_web_gameplay_step(error, sizeof(error)), error);
        check(melee_web_test_stage_old_yoshi_clouds(
                  cloud_state, cloud_contact, cloud_timer,
                  cloud_displacement, cloud_velocity, cloud_hidden, cloud_enabled),
              "Old Yoshi cloud state disappeared during scheduler ticks");
        saw_cloud_state_1 |= cloud_state[0] == 1;
        saw_cloud_state_2 |= cloud_state[0] == 2;
        saw_cloud_state_3 |= cloud_state[0] == 3;
        contact_phase_done |= cloud_state[0] == 1;
        saw_cloud_disabled |= cloud_enabled[0] == 0;
        saw_cloud_reenabled |= saw_cloud_disabled && cloud_enabled[0] == 1 &&
                               cloud_state[0] == 0;
        check(std::isfinite(cloud_displacement[0]) &&
                  std::isfinite(cloud_velocity[0]) && cloud_state[0] >= 0 &&
                  cloud_state[0] <= 3,
              "Old Yoshi cloud source state became invalid");

        check(melee_web_test_stage_old_yoshi_guest(
                  &guest_timer, &guest_child, &guest_hidden,
                  &guest_child_visible),
              "Old Yoshi guest scheduler state disappeared");
        if (!saw_guest_child && guest_child == -1)
            check(guest_timer <= previous_guest_timer,
                  "Old Yoshi guest timer did not follow source countdown");
        previous_guest_timer = guest_timer;
        if (guest_child >= 1 && guest_child <= 5)
            saw_guest_child = true;
        if (guest_child_visible)
            saw_guest_visible = true;
    }
    check(saw_cloud_state_1 && saw_cloud_state_2 && saw_cloud_state_3 &&
              saw_cloud_disabled && saw_cloud_reenabled,
          "Old Yoshi source cloud collapse/reappear lifecycle did not complete");
    check(saw_guest_child && saw_guest_visible,
          "Old Yoshi guest source scheduler did not select and reveal a child");

    world.verify_immutable_archives();
    world.end_stage();
    unsigned objects = 0;
    check(melee_web_test_stage_old_yoshi_empty(&objects) && objects == 0,
          "Old Yoshi teardown left source map objects live");
    check(melee_web_test_stage_old_yoshi_yakumono() == previous_yakumono,
          "Old Yoshi teardown did not restore the prior yakumono root");
    world.close();
    check(melee_web_gameplay_stats().objects == 0,
          "Old Yoshi world close left SDK objects live");
    std::cout << "Old Yoshi cycle=" << cycle
              << " maps=" << map_count << " cloud_states=1,2,3 guest=selected/revealed\n";
}

int main(int argc, char** argv)
{
    try {
        if (argc != 3)
            throw std::runtime_error(
                "Expected Old Yoshi stage and common runtime asset directories");
        RuntimeFiles files;
        load_files(files, argv[1]);
        load_files(files, argv[2]);
        for (const char* name : {"GrOy.dat", "old_ys.hps"})
            check(files.contains(name), std::string("Missing Old Yoshi asset: ") + name);
        for (const char* name : {"PlCo.dat", "PlMr.dat", "PlMrAJ.dat",
                                 "EfMrData.dat", "ItCo.usd", "EfCoData.dat",
                                 "PdPm.dat", "LbRb.dat", "sislib_font.bin"})
            check(files.contains(name), std::string("Missing common Old Yoshi asset: ") + name);
        check_archive_contract(files);
        run_lifetime(files, 0);
        run_lifetime(files, 1);
        std::cout << "Original Old Yoshi source maps, cloud collision collapse/reappear, "
                     "guest scheduler and repeated teardown passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
