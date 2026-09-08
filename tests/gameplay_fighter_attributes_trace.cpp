#include "fighter_runtime_fixture.hpp"
#include "gameplay_fighter_attributes.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <limits>
using namespace fighter_runtime_test;
namespace {
void verify_copy(const MeleeWebFighterBaseAttributes& input)
{
    MeleeWebFighterBaseAttributes result{};
    char error[160];
    check(melee_web_fighter_copy_base_attributes(&input, &result, error, sizeof(error)), error);
#define CHECK_CO(at, type, name, original) \
    check(std::memcmp(&input.co.name, &result.co.name, sizeof(input.co.name)) == 0, "Original co copy differs: " #name);
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(CHECK_CO)
#define CHECK_PICKUP(at, type, name, original) \
    check(std::memcmp(&input.pickup.name, &result.pickup.name, sizeof(input.pickup.name)) == 0, "Original pickup copy differs: " #name);
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(CHECK_PICKUP)
#undef CHECK_CO
#undef CHECK_PICKUP
    check(std::memcmp(&input.x2c4_x, &result.x2c4_x, sizeof(float)) == 0 &&
          std::memcmp(&input.x2c4_y, &result.x2c4_y, sizeof(float)) == 0, "Original x2C4 copy differs");
}
Bytes read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "cannot open local fighter input");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes), "local fighter input exceeds bounds");
    Bytes bytes(static_cast<std::size_t>(size)); input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)), "local fighter input is truncated");
    return bytes;
}
}
int main(int argc, char** argv)
{
    try {
        auto fixture = FighterFixture().read();
        auto input = fixture->base_attributes();
        input.co.gravity = -0.0f; input.co.unused_0 = std::numeric_limits<std::int32_t>::min();
        input.co.max_jumps = std::numeric_limits<std::int32_t>::max();
        verify_copy(input);
        MeleeWebFighterBaseAttributes unchanged{}; unchanged.co.gravity = 123;
        char error[160];
        check(!melee_web_fighter_copy_base_attributes(nullptr, &unchanged, error, sizeof(error)) &&
              unchanged.co.gravity == 123, "null input rejects without writing output");
        input.pickup.air_light_offset_w = std::numeric_limits<float>::infinity();
        check(!melee_web_fighter_copy_base_attributes(&input, &unchanged, error, sizeof(error)) &&
              unchanged.co.gravity == 123, "invalid attribute rejects before exposing partial copy");
        std::cout << "Original ftCo_800D0FA0 typed attribute consumer: passed\n";
        if (argc > 1) {
            auto fighter = std::make_shared<const DatFighterRuntime>(std::make_shared<const DatArchive>(read_file(argv[1])), mario());
            verify_copy(fighter->base_attributes());
            check(fighter->mario_attributes().has_value(), "local Mario extension is not typed");
            std::cout << "Local Mario attributes consumed by original source: passed; motions=" << fighter->actions().size()
                      << "; active_clips=" << fighter->archive_actions().actions.size()
                      << "; wait_choices=" << fighter->wait_choices().size()
                      << "; hurtboxes=" << fighter->hurtboxes().size()
                      << "; dynamics=" << fighter->dynamics().bones.size()
                      << "; weight=" << fighter->base_attributes().co.weight
                      << "; gravity=" << fighter->base_attributes().co.gravity << '\n';
            if (argc > 2) {
                DatFighterAnimationStore store(fighter, read_file(argv[2]));
                const auto wait = store.select(2), alias = store.select(6);
                check(wait.animation && wait.action.motion_id == 2 && wait.commands,
                      "exact source Wait1 action did not retain clip/command identity");
                fighter->validate_part_indices(wait.animation->node_counts.size());
                check(alias.action.motion_id == 6 && alias.animation == wait.animation && alias.commands &&
                      alias.commands->offset() != wait.commands->offset(),
                      "real Mario motions2/6 must share a clip and retain different commands");
                std::cout << "Local exact motion2 owned clip: passed; nodes=" << wait.animation->node_counts.size()
                          << "; tracks=" << wait.animation->tracks.size()
                          << "; end_frame=" << wait.animation->end_frame
                          << "; command_offset=" << wait.commands->offset()
                          << "; command_execution_ready=" << DatPackedCommands::native_execution_ready << '\n';
            }
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
