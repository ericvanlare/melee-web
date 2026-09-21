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
void real_donkey(const char* path)
{
    const auto identity = resolve_fighter_costume("PlyDonkey5K_Share_joint");
    check(identity.fighter_kind == 3 && identity.motion_count == 337,
          "Donkey source identity or authored action count changed");
    auto archive = std::make_shared<const DatArchive>(read_real_archive(path));
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 337 && runtime->donkey_attributes(),
          "Donkey action count or exact extension is missing");
    check(!runtime->mario_attributes() && !runtime->luigi_attributes() &&
          !runtime->pikachu_attributes() && !runtime->purin_attributes() &&
          !runtime->captain_attributes(),
          "Donkey extension was aliased to another fighter schema");
    check(sizeof(MeleeWebDonkeyAttributes) == 0x74 &&
          archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() == 0x74,
          "Donkey extension does not retain its exact 0x74 source bound");
    const auto& attributes = *runtime->donkey_attributes();
    check(attributes.motion_state == 341 && attributes.x4_motion_state == 351 &&
          attributes.specialn_x2C_MAX_ARM_SWINGS == 10 &&
          attributes.specialn_x30_DAMAGE_PER_SWING == 2,
          "Donkey signed motion-state or Giant Punch counters changed");
    check(attributes.cargo_hold_x20_TURN_SPEED == 6.0f &&
          attributes.cargo_hold_x24_JUMP_STARTUP_LAG == 3.0f &&
          attributes.cargo_hold_x28_LANDING_LAG == 15.0f,
          "Donkey cargo hold floats changed");
    check(runtime->dynamics().active_bone_count == 1 && runtime->dynamics().bones.size() == 1 &&
          runtime->dynamics().spheres.size() == 1 && runtime->dynamics().animation_table_offset &&
          *runtime->dynamics().animation_table_offset == 0x2a88,
          "Donkey authored dynamics descriptor was omitted or fabricated");
    check(!archive->pointer(runtime->root_offset() + 0x48, 4),
          "Donkey x48 Article root is not authored");
    auto malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0x20, 0x7fc00000U);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    std::cout << "Donkey 0x74 attributes, signed counters, cargo floats, authored dynamics and null x48: passed\n";
}
void real_koopa(const char* path)
{
    const auto identity = resolve_fighter_costume("PlyKoopa5K_Share_joint");
    check(identity.fighter_kind == 5 && identity.motion_count == 316,
          "Koopa source identity or authored action count changed");
    auto bytes = read_real_archive(path);
    auto archive = std::make_shared<const DatArchive>(bytes);
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 316 && runtime->koopa_attributes(),
          "Koopa action count or exact extension is missing");
    check(!runtime->mario_attributes() && !runtime->donkey_attributes() &&
          !runtime->pikachu_attributes() && !runtime->purin_attributes(),
          "Koopa extension was aliased to another fighter schema");
    check(sizeof(MeleeWebKoopaAttributes) == 0xa0 &&
          archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() >= 0xa0,
          "Koopa extension does not retain its 0xa0 source bound");
    const auto& attributes = *runtime->koopa_attributes();
    check(attributes.x0 == 0.0f && attributes.x4 == 40 && attributes.x20 == 30 &&
          attributes.x2C == 3U && attributes.unk50 == 0U,
          "Koopa signed/unsigned source words changed");
    check(attributes.x8 == 0.7f && attributes.x10 == 360.0f &&
          attributes.x54 == 1.78f && attributes.x94 == -7.5f,
          "Koopa authored float values changed");
    check(runtime->dynamics().active_bone_count == 1 && runtime->dynamics().bones.size() == 1 &&
          runtime->dynamics().spheres.empty() && !runtime->dynamics().animation_table_offset,
          "Koopa dynamics descriptor was omitted or fabricated");
    // Drive the actual decoder with negative counters and an unsigned high
    // bit so these source categories cannot silently become float storage.
    auto typed = bytes;
    put32(typed, 0x20 + runtime->extension_offset() + 4, 0xffffffd8U);
    put32(typed, 0x20 + runtime->extension_offset() + 0x20, 0xffffffe2U);
    put32(typed, 0x20 + runtime->extension_offset() + 0x50, 0x80000001U);
    DatFighterRuntime typed_runtime(std::make_shared<const DatArchive>(typed), identity);
    const auto& typed_attributes = *typed_runtime.koopa_attributes();
    check(typed_attributes.x4 == -40 && typed_attributes.x20 == -30 &&
          typed_attributes.unk50 == 0x80000001U,
          "Koopa signed/unsigned decoder changed source bits");
    auto malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0x94, 0x7fc00000U);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->root_offset() + 4, runtime->root_offset());
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    std::cout << "Koopa 0xa0 attributes, signed/unsigned words, authored dynamics and open Flame Article boundary: passed\n";
}
void real_ness(const char* path)
{
    const auto identity = resolve_fighter_costume("PlyNess5K_Share_joint");
    check(identity.fighter_kind == 8 && identity.motion_count == 326,
          "Ness source identity or authored action count changed");
    auto bytes = read_real_archive(path);
    auto archive = std::make_shared<const DatArchive>(bytes);
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 326 && runtime->ness_attributes(),
          "Ness action count or exact extension is missing");
    check(!runtime->mario_attributes() && !runtime->donkey_attributes() &&
          !runtime->koopa_attributes() && !runtime->pikachu_attributes() &&
          !runtime->purin_attributes() && !runtime->luigi_attributes() &&
          !runtime->fox_attributes() && !runtime->mars_attributes() && !runtime->link_attributes(),
          "Ness extension was aliased to another fighter schema");
    check(runtime->dynamics().active_bone_count == 0 && runtime->dynamics().bones.empty() &&
              runtime->dynamics().spheres.empty() && !runtime->dynamics().animation_table_offset,
          "Ness authored dynamics are empty with a null mode table");
    check(runtime->wait_choices().empty(),
          "Ness authored null Wait table must decode as semantically empty");
    check(runtime->squat_wait_choices().size() == 2 &&
              runtime->squat_wait_choices()[0].motion_id == 31 &&
              runtime->squat_wait_choices()[0].weight == 50 &&
              runtime->squat_wait_choices()[1].motion_id == 32 &&
              runtime->squat_wait_choices()[1].weight == 50,
          "Ness authored Squat Wait choices changed");
    check(sizeof(MeleeWebNessAttributes) == 0xDC &&
          archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() == 0xDC,
          "Ness extension does not retain its exact 0xDC source bound");
    const auto& attributes = *runtime->ness_attributes();
    check(attributes.pkflash_timer1_loopframes == 30 && attributes.pkflash_timer2_loopframes == 25 &&
              attributes.pkflash_gravity_delay == 10 && attributes.pkflash_minchargeframes == 30 &&
              attributes.pkflash_fall_accel == 0.017f && attributes.pkflash_landing_lag == 30.0f,
          "Ness PK Flash counters or floats changed");
    check(attributes.pkfire_aerial_launch_trajectory == -0.663225114f &&
              attributes.pkfire_aerial_velocity == 2.5f &&
              attributes.pkfire_grounded_launch_trajectory == -0.062831849f &&
              attributes.pkfire_spawn_x == 3.2f && attributes.pkfire_spawn_y == 1.1f,
          "Ness PK Fire trajectories or spawn offsets changed");
    check(attributes.pkthunder_loop1 == 30 && attributes.pkthunder_loop2 == 24 &&
              attributes.pkthunder_gravity_delay == 30 &&
              attributes.pkthunder2_knockdown_angle == 65.0f &&
              attributes.pkthunder2_wallhug_angle == 60.0f,
          "Ness PK Thunder counters or self-hit angles changed");
    check(attributes.psimagnet_release_lag == 30.0f && attributes.psimagnet_heal_mul == 2.0f &&
              attributes.psimagnet_absorb_bone == 1 &&
              attributes.psimagnet_absorb_offset_y == 6.5f &&
              attributes.psimagnet_absorb_size == 8.5f,
          "Ness PSI Magnet record changed");
    check(attributes.yoyo_charge_duration == 60.0f && attributes.yoyo_damage_mul == 350.0f &&
              attributes.yoyo_rehit_rate == 30.0f,
          "Ness Yo-Yo scalars changed");
    check(attributes.bat_reflect_bone_id == 4 && attributes.bat_reflect_max_damage == 50 &&
              attributes.bat_reflect_size == 6.5f &&
              attributes.bat_reflect_damage_mul == 1.5f &&
              attributes.bat_reflect_speed_mul == 1.0f && attributes.bat_reflect_behavior == 0,
          "Ness baseball bat reflection record changed");
    // Drive the actual decoder with a negative PK Flash loop counter so this
    // source category cannot silently become float storage.
    auto typed = bytes;
    put32(typed, 0x20 + runtime->extension_offset(), 0xfffffff0U);
    DatFighterRuntime typed_runtime(std::make_shared<const DatArchive>(typed), identity);
    check(typed_runtime.ness_attributes()->pkflash_timer1_loopframes == -16,
          "Ness signed decoder changed source bits");
    auto malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0x10, 0x7fc00000U);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0x98, 0xfffffffeU);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    std::cout << "Ness 0xDC attributes, integer counters, absorb/reflection records, empty dynamics and null Wait: passed\n";
}
void real_peach(const char* path)
{
    const auto identity = resolve_fighter_costume("PlyPeach5K_Share_joint");
    check(identity.fighter_kind == 9 && identity.motion_count == 318,
          "Peach source identity or authored action count changed");
    auto bytes = read_real_archive(path);
    auto archive = std::make_shared<const DatArchive>(bytes);
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    check(runtime->actions().size() == 318 && runtime->peach_attributes(),
          "Peach action count or exact extension is missing");
    check(!runtime->mario_attributes() && !runtime->donkey_attributes() &&
              !runtime->koopa_attributes() && !runtime->pikachu_attributes() &&
              !runtime->purin_attributes() && !runtime->luigi_attributes() &&
              !runtime->fox_attributes() && !runtime->mars_attributes() &&
              !runtime->link_attributes() && !runtime->ness_attributes(),
          "Peach extension was aliased to another fighter schema");
    // Peach authors nine body chains, an empty auxiliary count whose +0xC
    // pointer is nonetheless relocated, and 86 dynamics mode rows.
    check(runtime->dynamics().active_bone_count == 9 &&
              runtime->dynamics().bones.size() == 9 &&
              runtime->dynamics().spheres.empty() &&
              runtime->dynamics().animation_table_offset.has_value(),
          "Peach authored dynamics shape changed");
    check(runtime->wait_choices().size() == 4 &&
              runtime->wait_choices()[0].motion_id == 2 &&
              runtime->wait_choices()[0].weight == 25 &&
              runtime->wait_choices()[3].motion_id == 5 &&
              runtime->wait_choices()[3].weight == 25,
          "Peach authored Wait choices changed");
    check(runtime->squat_wait_choices().empty(),
          "Peach authored null Squat Wait table must decode as semantically empty");
    check(sizeof(MeleeWebPeachAttributes) == 0xC0 &&
              archive->next_target_offset(runtime->extension_offset()) - runtime->extension_offset() == 0xC0,
          "Peach extension does not retain its exact 0xC0 source bound");
    const auto& attributes = *runtime->peach_attributes();
    check(attributes.floatfallf_anim_start == 0.0f && attributes.floatfallb_anim_start == 0.0f &&
              attributes.floatfall_anim_start_offset == 5.0f && attributes.xC == 150.0f,
          "Peach float-fall record changed");
    check(attributes.speciallw_item_table_count == 3 && attributes.x14 == 128 &&
              attributes.speciallw_item_0_randi_max == 2 && attributes.speciallw_item_0_kind == 6 &&
              attributes.speciallw_item_1_randi_max == 3 && attributes.speciallw_item_1_kind == 7 &&
              attributes.speciallw_item_2_randi_max == 1 && attributes.speciallw_item_2_kind == 12,
          "Peach Toad counter item table changed");
    check(attributes.x30 == 3 && attributes.x34 == 0.1f && attributes.x90 == 600 &&
              attributes.specialairn_vel_x_div == 2.0f && attributes.specialairn_vel_y == 0.7f,
          "Peach special scalars changed");
    check(attributes.absorb_bone == 3 && attributes.absorb_offset_x == 0.0f &&
              attributes.absorb_offset_y == 1.0f && attributes.absorb_offset_z == 3.5f &&
              attributes.absorb_size == 6.0f,
          "Peach Toad absorb record changed");
    // Drive the actual decoder with a negative Toad counter threshold so this
    // source category cannot silently become float storage.
    auto typed = bytes;
    put32(typed, 0x20 + runtime->extension_offset() + 0x14, 0xffffff80U);
    DatFighterRuntime typed_runtime(std::make_shared<const DatArchive>(typed), identity);
    check(typed_runtime.peach_attributes()->x14 == -128,
          "Peach signed decoder changed source bits");
    auto malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0x94, 0x7fc00000U);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    malformed = read_real_archive(path);
    put32(malformed, 0x20 + runtime->extension_offset() + 0xAC, 0xfffffffeU);
    rejects([&] {
        (void) DatFighterRuntime(std::make_shared<const DatArchive>(malformed), identity);
    });
    std::cout << "Peach 0xC0 attributes, Toad counter pairs, absorb record, nine authored dynamics chains and null Squat Wait: passed\n";
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
        if (argc == 3 && std::string_view(argv[1]) == "real_donkey") {
            real_donkey(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "real_koopa") {
            real_koopa(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "real_ness") {
            real_ness(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string_view(argv[1]) == "real_peach") {
            real_peach(argv[2]);
            return 0;
        }
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
