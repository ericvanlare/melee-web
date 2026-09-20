#include "fighter_runtime_fixture.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <algorithm>
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
Bytes read_real_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "cannot open Luigi real fighter archive");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes),
          "Luigi real fighter archive exceeds bounds");
    Bytes bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)),
          "Luigi real fighter archive is truncated");
    return bytes;
}
void real_luigi(const char* path, const char* effect_path)
{
    const auto identity = resolve_fighter_costume("PlyLuigi5K_Share_joint");
    check(identity.fighter_kind == 17 && identity.motion_count == 312,
          "Luigi source identity changed");
    auto bytes = read_real_archive(path);
    auto archive = std::make_shared<const DatArchive>(bytes);
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 312, "Luigi source action count changed");
    check(!runtime->mario_attributes() && runtime->luigi_attributes(),
          "Luigi extension was not kept distinct from Mario");
    check(sizeof(MeleeWebLuigiAttributes) == 0x98 &&
          archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() == 0x98,
          "Luigi extension does not retain its exact 0x98 source bound");
    const auto fighter_root = runtime->root_offset();
    const auto item_table = archive->pointer(fighter_root + 0x48, 16);
    check(item_table && archive->pointer(*item_table, 24),
          "Luigi source Article table is not a single authored slot");
    const auto article = *archive->pointer(*item_table, 24);
    const auto special = archive->pointer(article + 4, 16);
    const auto states = archive->pointer(article + 12, 16);
    const auto model = archive->pointer(article + 16, 16);
    check(special && archive->next_target_offset(*special) - *special == 16 &&
          states && archive->next_target_offset(*states) - *states == 16 && model,
          "Luigi fire Article does not retain its 16-byte special and one-state bounds");
    const auto guard_desc = archive->pointer(fighter_root + 0x20, 4);
    check(guard_desc && archive->pointer(*guard_desc, 64),
          "Luigi source guard descriptor is not a required non-null model graph");
    check(*guard_desc == 9736 && *archive->pointer(*guard_desc, 64) == 36608,
          "Luigi source guard graph target changed");
    for (std::uint32_t motion = 295; motion <= 311; ++motion) {
        const auto command = runtime->commands(motion);
        check(command && !command->bytes().empty(),
              "Luigi authored self-motion command boundary is missing");
    }
    const DatArchive effects(read_real_archive(effect_path));
    const auto effect_root = std::find_if(effects.public_symbols().begin(), effects.public_symbols().end(),
        [](const auto& symbol) { return symbol.name == "effLuigiDataTable"; });
    check(effect_root != effects.public_symbols().end() && effect_root->data_offset == 0 &&
          effects.next_target_offset(effect_root->data_offset) - effect_root->data_offset == 64,
          "Luigi effect bank does not retain its authored two-entry bound");
    const auto& attributes = *runtime->luigi_attributes();
    check(attributes.greenmissile_misfire_chance == 8.0f &&
          attributes.greenmissile_smash == 3.0f &&
          attributes.greenmissile_charge_rate == 20.0f &&
          attributes.cyclone_unk == 3 && attributes.cyclone_landing_lag == 0,
          "Luigi authored float/integer attributes changed");
}
void real_pikachu_family(const char* path, bool pichu)
{
    const char* const prefix = pichu ? "PlyPichu5K" : "PlyPikachu5K";
    const auto& first = resolve_fighter_costume(std::string(prefix) + "_Share_joint");
    check(first.fighter_kind == (pichu ? 23U : 12U) && first.motion_count == 320,
          "Pikachu-family source identity changed");
    auto archive = std::make_shared<const DatArchive>(read_real_archive(path));
    for (unsigned costume = 0; costume < 4; ++costume) {
        const char* suffixes[] = {"_Share_joint", "Re_Share_joint", "Bu_Share_joint", "Gr_Share_joint"};
        const auto identity = resolve_fighter_costume(std::string(prefix) + suffixes[costume]);
        const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
        check(runtime->actions().size() == 320 && runtime->pikachu_attributes(),
              "Pikachu-family action count or shared extension is missing");
        check(!runtime->mario_attributes() && !runtime->luigi_attributes(),
              "Pikachu-family extension was aliased to a Mario-family schema");
        check(archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() ==
                  MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES,
              "Pikachu-family extension does not retain its exact 0xf8 source bound");
        const auto& attributes = *runtime->pikachu_attributes();
        if (pichu) {
            check(attributes.specialn_itkind == 0x5b && attributes.specialairn_itkind == 0x5c &&
                      attributes.xDC == 0x52 && attributes.x60 == 8,
                  "Pichu authored item or integer attributes changed");
        } else {
            check(attributes.specialn_itkind == 0x59 && attributes.specialairn_itkind == 0x5a &&
                      attributes.xDC == 0x51 && attributes.x60 == 5,
                  "Pikachu authored item or integer attributes changed");
        }
        check(attributes.x5C == 10 && attributes.xD4 == 4 && attributes.xD8 == 8 &&
                  runtime->dynamics().bones.empty() && runtime->dynamics().spheres.empty() &&
                  !runtime->dynamics().animation_table_offset,
              "Pikachu-family integer or authored zero-dynamics fields changed");
    }
}
void real_purin(const char* path)
{
    const auto identity = resolve_fighter_costume("PlyPurin5K_Share_joint");
    check(identity.fighter_kind == 15 && identity.motion_count == 327,
          "Purin source identity changed");
    auto archive = std::make_shared<const DatArchive>(read_real_archive(path));
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 327 && runtime->purin_attributes(),
          "Purin action count or exact extension is missing");
    check(!runtime->mario_attributes() && !runtime->luigi_attributes() &&
          !runtime->pikachu_attributes(),
          "Purin extension was aliased to another fighter schema");
    check(sizeof(MeleeWebPurinAttributes) == 0x100 &&
          archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() == 0x100,
          "Purin extension does not retain its exact 0x100 source bound");
    const auto& attributes = *runtime->purin_attributes();
    check(attributes.x2C == 341 && attributes.x30 == -1 && attributes.x34 == 90 &&
          attributes.x38 == 20 && attributes.x70 == 8 && attributes.x9C == 32 &&
          attributes.specialn_vel.x == -0.13f && attributes.specialn_vel.y == 1.6f &&
          attributes.xE8 == 0x3e800000U && attributes.xEC == 0x3e4ccccdU,
          "Purin signed, Vec2 and opaque attribute values changed");
    check(archive->be32(runtime->extension_offset()+0x48) == 0x3d4ccccdU &&
          archive->be32(runtime->extension_offset()+0x60) == 0x3f800000U &&
          archive->be32(runtime->extension_offset()+0xb0) == 0x41a00000U,
          "Purin authored padding words changed");
    check(attributes._48[0] == 0x3d && attributes._48[1] == 0x4c &&
          attributes._48[2] == 0xcc && attributes._48[3] == 0xcd &&
          attributes._60[0] == 0x3f && attributes._60[1] == 0x80 &&
          attributes._60[4] == 0x40 && attributes._B0[0] == 0x41 &&
          attributes._B0[1] == 0xa0 && attributes._F8[0] == 0 &&
          attributes._F8[7] == 0,
          "Purin portable padding bytes changed");
    {
        static const std::uint32_t ids[5] = {7, 3, 7, 3, 9};
        static const std::size_t parameter_counts[5] = {3, 3, 3, 5, 5};
        static const float z[5] = {0.00001f, 0.145f, 0.145f, 0.145f, 0.145f};
        const auto& bones = runtime->dynamics().bones;
        check(runtime->dynamics().active_bone_count == 1 && bones.size() == 5 &&
                  runtime->dynamics().spheres.empty() &&
                  !runtime->dynamics().animation_table_offset,
              "Purin authored five-row zero-sphere dynamics table changed");
        for (std::size_t i = 0; i < 5; ++i)
            check(bones[i].bone_index == ids[i] && bones[i].parameters.size() == parameter_counts[i] &&
                      bones[i].position[0] == 1.0f && bones[i].position[1] == 1.0f &&
                      bones[i].position[2] == z[i],
                  "Purin authored dynamics descriptor row changed");
    }
    check(runtime->squat_wait_choices().size() == 2 &&
          runtime->squat_wait_choices()[0].motion_id == 31 &&
          runtime->squat_wait_choices()[0].weight == 80 &&
          runtime->squat_wait_choices()[1].motion_id == 32 &&
          runtime->squat_wait_choices()[1].weight == 20,
          "Purin authored crouch Wait choices changed");
    constexpr std::size_t dat_header = 0x20, squat_choices = 0x6e40;
    auto malformed = read_real_archive(path);
    put32(malformed, dat_header + squat_choices + 4, 0xffffffffU);
    rejects([&] { (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity); });
    malformed = read_real_archive(path);
    put32(malformed, dat_header + squat_choices + 12, 19U);
    rejects([&] { (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity); });
    malformed = read_real_archive(path);
    put32(malformed, dat_header + squat_choices, identity.motion_count);
    rejects([&] { (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity); });
    const auto dynamics = archive->pointer(runtime->root_offset() + 0x2c, 20);
    check(dynamics.has_value(), "Purin dynamics root is missing");
    for (const auto active_count : {0U, 2U}) {
        malformed = read_real_archive(path);
        put32(malformed, dat_header + *dynamics, active_count);
        rejects([&] { (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity); });
    }
}
}
int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases{
        {"decoded_values", decoded_values}, {"malformed_attributes", malformed_attributes},
        {"malformed_actions", malformed_actions}, {"owned_command_boundary", owned_command_boundary},
        {"selected_motion_identity", selected_motion_identity}, {"hurtbox_dynamics", hurtbox_dynamics}};
    try {
        if (argc == 4 && std::string_view(argv[1]) == "real_luigi") {
            real_luigi(argv[2], argv[3]);
            return 0;
        }
        if (argc == 3 && (std::string_view(argv[1]) == "real_pikachu" ||
                          std::string_view(argv[1]) == "real_pichu")) {
            real_pikachu_family(argv[2], std::string_view(argv[1]) == "real_pichu");
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "real_purin") {
            real_purin(argv[2]);
            return 0;
        }
        check(argc == 2, "expected case");
        cases.at(argv[1])();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
