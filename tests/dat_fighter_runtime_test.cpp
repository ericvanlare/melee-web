#include "fighter_runtime_fixture.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <limits>

using namespace fighter_runtime_test;
namespace {
void decoded_values()
{
    const FighterFixture fixture;
    const auto data = fixture.read();
    const auto& co = data->base_attributes().co;
    check(co.gravity == 92.25f && co.max_jumps == -312 && co.weight_independent_throws_mask == 0xa5,
          "float, signed int and BE tail byte retain their distinct source types");
    check(co.xBC_x10_z == 212.25f && co.x170_y == 372.25f &&
          data->base_attributes().pickup.air_light_offset_w == 44.25f &&
          data->base_attributes().x2c4_x == -3.5f, "nested vectors and pickup fields decode exact scalar offsets");
    check(data->mario_attributes()->specials_cape_kind == -380 &&
          data->mario_attributes()->cape_reflection_x0_bone_id == 0x81234560 &&
          data->mario_attributes()->cape_reflection_x20_behavior == 0xa5,
          "Mario extension preserves ItemKind, reflector integer and behavior byte");
    check(data->actions().size() == mario().motion_count && data->action(0).archive_bytes == 0 &&
          data->action(6).motion_flags == 0x80001234 && data->action(6).blend_dynamics[0] == 6,
          "all original rows and independent per-motion metadata are retained");
    check(data->wait_choices().size() == 2 && data->wait_choices()[0].weight == 70 &&
          data->wait_choices()[1].motion_id == 6, "source weighted Wait choices terminate at -1");
    auto foreign = FighterFixture(resolve_fighter_costume("PlyFox5K_Share_joint")).read();
    check(!foreign->mario_attributes(), "other fighter extension schemas remain explicitly undecoded");
}
void malformed_attributes()
{
    for (auto field : {FighterFixture::co + 0x5c, FighterFixture::ext + 0x70,
                       FighterFixture::pickup + 4, FighterFixture::vector}) {
        FighterFixture fixture; put32(fixture.data, field, 0x7f800000);
        rejects([&] { (void) fixture.read(); });
    }
    for (auto field : {0U, 4U, 64U, 80U, 16U}) {
        FighterFixture fixture; fixture.unlink(field); rejects([&] { (void) fixture.read(); });
    }
    FighterFixture fixture; fixture.link(FighterFixture::co + 0x58, 0);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link(8, FighterFixture::co + 4);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link(0, FighterFixture::co + 1);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); auto forged = mario(); forged.material_animation_symbol = "wrong";
    rejects([&] { (void) DatFighterRuntime(std::make_shared<const DatArchive>(fixture.file()), forged); });
    rejects([] { (void) DatFighterRuntime({}, mario()); });
}
void malformed_actions()
{
    for (auto value : {0xffffffffU, mario().motion_count}) {
        FighterFixture fixture; put32(fixture.data, fixture.waits + 8, value);
        // A premature terminator leaves only weight70, below source random range.
        rejects([&] { (void) fixture.read(); });
    }
    for (auto value : {0xffffffffU, 0x7fffffffU}) {
        FighterFixture fixture; put32(fixture.data, fixture.waits + 12, value);
        rejects([&] { (void) fixture.read(); });
    }
    FighterFixture fixture; put32(fixture.data, fixture.waits + 16, 2);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link(fixture.row(6) + 16, 0);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link(fixture.row(6) + 12, fixture.command_a + 1);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link(fixture.blends, 0);
    rejects([&] { (void) fixture.read(); });
    fixture = FighterFixture(); fixture.link((fixture.blends + mario().motion_count * 2 - 1) & ~3U, 0);
    rejects([&] { (void) fixture.read(); });
    const auto data = FighterFixture().read();
    rejects([&] { (void) data->action(mario().motion_count); });
    rejects([&] { (void) data->commands(std::numeric_limits<std::uint32_t>::max()); });
}
void hurtbox_dynamics()
{
    FighterFixture fixture;
    put32(fixture.data, FighterFixture::ext + 0x60, 1);
    auto data = fixture.read();
    check(data->hurtboxes().size() == 1 && data->hurtboxes()[0].a_offset[0] == -2 &&
          data->hurtboxes()[0].b_offset[0] == 3 && data->hurtboxes()[0].scale == 1.5f &&
          data->dynamics().bones.empty() && data->dynamics().spheres.empty() &&
          !data->dynamics().animation_table_offset,
          "hurt capsules are typed and zero-count dynamics still retain a real descriptor");
    data->validate_part_indices(2);
    rejects([&] { data->validate_part_indices(1); });
    for (auto count : {16U, 0xffffffffU}) {
        FighterFixture bad; put32(bad.data, bad.hurt_desc, count);
        rejects([&] { (void) bad.read(); });
    }
    for (auto field : {0U, 8U}) {
        FighterFixture bad; put32(bad.data, bad.dynamics_desc + field, field ? 12 : 10);
        rejects([&] { (void) bad.read(); });
    }
    for (auto slot : {0x2cU, 0x30U}) {
        FighterFixture bad; bad.unlink(slot); rejects([&] { (void) bad.read(); });
    }
    FighterFixture bad; put32(bad.data, bad.hurt_rows + 4, 3);
    rejects([&] { (void) bad.read(); });
    bad = FighterFixture(); put32(bad.data, bad.hurt_rows + 36, 0x7fc00000);
    rejects([&] { (void) bad.read(); });
    bad = FighterFixture(); bad.unlink(bad.hurt_desc + 4);
    rejects([&] { (void) bad.read(); });
    const auto bones = std::uint32_t(fixture.data.size()), parameters = bones + 24, spheres = parameters + 60;
    fixture.data.resize(spheres + 20);
    put32(fixture.data, fixture.dynamics_desc, 1); fixture.link(fixture.dynamics_desc + 4, bones);
    put32(fixture.data, fixture.dynamics_desc + 8, 1); fixture.link(fixture.dynamics_desc + 12, spheres);
    put32(fixture.data, bones, 1); fixture.link(bones + 4, parameters); put32(fixture.data, bones + 8, 1);
    put32(fixture.data, parameters + 56, std::bit_cast<std::uint32_t>(0.75f));
    put32(fixture.data, spheres, 1); put32(fixture.data, spheres + 16, std::bit_cast<std::uint32_t>(3.0f));
    data = fixture.read(); data->validate_part_indices(2);
    check(data->dynamics().bones[0].parameters[0][14] == 0.75f && data->dynamics().spheres[0].size == 3,
          "source dynamics parameter and sphere records decode without fabricating native nodes");
    put32(fixture.data, bones + 8, 0); rejects([&] { (void) fixture.read(); });
    put32(fixture.data, bones + 8, 2); rejects([&] { (void) fixture.read(); });
    put32(fixture.data, bones + 8, 1); fixture.link(parameters, 0);
    rejects([&] { (void) fixture.read(); });
}
void owned_command_boundary()
{
    auto fixture = std::make_unique<FighterFixture>();
    auto data = fixture->read();
    auto commands = data->commands(2);
    const auto begin = fixture->command_a;
    std::fill(fixture->data.begin(), fixture->data.end(), 0);
    fixture.reset(); data.reset();
    check(commands && commands->offset() == begin && commands->word(0) == 0xa0000123 &&
          commands->word(1) == 0 && commands->relocated_target(1) == 0,
          "owned BE command storage survives inputs and distinguishes relocated target zero");
    check(!commands->relocated_target(0) && !DatPackedCommands::native_execution_ready,
          "scalar word is not interpreted as native pointer and execution remains unavailable");
    rejects([&] { (void) commands->word(2); });
    rejects([&] { (void) commands->word(std::numeric_limits<std::size_t>::max()); });
    check(!FighterFixture().read()->commands(0), "null command is distinct from relocated offset zero");
}
void selected_motion_identity()
{
    FighterFixture fixture;
    const auto data = fixture.read();
    DatFighterAnimationStore store(data, fixture.container);
    std::fill(fixture.container.begin(), fixture.container.end(), 0);
    const auto first = store.select(2), alias = store.select(6);
    check(first.animation && first.animation == alias.animation && first.action.motion_id == 2 &&
          alias.action.motion_id == 6 && first.action.motion_flags != alias.action.motion_flags &&
          first.commands->offset() != alias.commands->offset() &&
          first.action.blend_dynamics != alias.action.blend_dynamics,
          "aliases share immutable clip storage while preserving every selected row field");
    const auto second = store.select(7);
    check(second.animation != first.animation, "different source storage offsets remain different cache identities");
    (void) store.select(8);
    check(first.animation->end_frame == 10 && first.commands->word(0) == 0xa0000123,
          "evicted cache entries remain valid while selections retain ownership");
    check(!store.select(0).animation, "empty source clip stays empty without a fabricated animation");
    rejects([&] { (void) store.select(0xffffffff); });
    rejects([&] { (void) DatFighterAnimationStore(data, std::span(fixture.container).first(100)); });
    rejects([&] { (void) DatFighterAnimationStore({}, fixture.container); });
    DatFighterAnimationStore malformed(data, fixture.container);
    rejects([&] { (void) malformed.select(2); });
}
}
int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases{
        {"decoded_values", decoded_values}, {"malformed_attributes", malformed_attributes},
        {"malformed_actions", malformed_actions}, {"owned_command_boundary", owned_command_boundary},
        {"selected_motion_identity", selected_motion_identity}, {"hurtbox_dynamics", hurtbox_dynamics}};
    try { check(argc == 2, "expected case"); cases.at(argv[1])(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
