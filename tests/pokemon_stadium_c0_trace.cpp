#include "gameplay_compat.h"
#include "dat_archive.hpp"
#include "dat_audio.hpp"
#include "dat_audio_stream.hpp"
#include "dat_collision.hpp"
#include "dat_effect_banks.hpp"
#include "dat_lights.hpp"
#include "dat_native_joint.hpp"
#include "dat_scene.hpp"
#include "dat_sis.hpp"
#include "dat_stage.hpp"
#include "dat_texture.hpp"
#include "gameplay_effect_banks.h"
#include "gameplay_ground_data.h"
#include "native_dat.hpp"
#include "pokemon_stadium_ground_snapshot.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace melee_web;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    check(bool(file), "cannot open input archive");
    const auto size = file.tellg();
    check(size >= 0 && std::uint64_t(size) <= DatArchive::max_archive_bytes,
          "input exceeds the DAT reader budget");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    check(bool(file.read(reinterpret_cast<char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()))),
          "cannot read the complete input archive");
    return bytes;
}

std::uint32_t symbol_offset(const DatArchive& archive, const std::string& name)
{
    const auto found = std::find_if(archive.public_symbols().begin(),
        archive.public_symbols().end(), [&](const auto& symbol) {
            return symbol.name == name;
        });
    check(found != archive.public_symbols().end(), "required source root is absent: " + name);
    return found->data_offset;
}

std::uint32_t public_symbol_span(const DatArchive& archive, std::uint32_t root)
{
    std::uint32_t next = static_cast<std::uint32_t>(archive.data().size());
    for (const auto& symbol : archive.public_symbols())
        if (symbol.data_offset > root) next = std::min(next, symbol.data_offset);
    check(next >= root, "public symbol root exceeds the archive data section");
    return next - root;
}

std::uint32_t relocation_count(const DatArchive& archive)
{
    std::uint32_t result = 0;
    for (std::uint32_t offset = 0; offset + 4 <= archive.data().size(); offset += 4)
        result += archive.has_relocation(offset) ? 1U : 0U;
    return result;
}

std::vector<std::uint32_t> relocation_offsets(const DatArchive& archive)
{
    std::vector<std::uint32_t> result;
    for (std::uint32_t offset = 0; offset + 4 <= archive.data().size(); offset += 4)
        if (archive.has_relocation(offset)) result.push_back(offset);
    return result;
}

std::uint32_t pointer_list_count(const DatArchive& archive, std::uint32_t root)
{
    const auto end = archive.next_target_offset(root);
    for (std::uint32_t i = 0; i <= 64; ++i) {
        const auto slot = root + i * 4;
        check(slot <= end && end - slot >= 4,
              "pointer list has no null terminator within its referenced region");
        if (!archive.pointer(slot, 4)) return i;
        check(i < 64, "pointer list exceeds the source loader budget");
    }
    throw std::runtime_error("pointer list is unterminated");
}

std::uint32_t sis_string_length(const DatArchive& archive, std::uint32_t root,
                               std::uint32_t index)
{
    const auto target = archive.pointer(root + index * 4, 1);
    check(target.has_value(), "required SIS string is null");
    const auto end = archive.next_target_offset(*target);
    const auto bytes = archive.range(*target, end - *target);
    for (std::size_t position = 0; position < bytes.size();) {
        const auto opcode = bytes[position];
        if (opcode == 0) return static_cast<std::uint32_t>(position + 1);
        std::size_t size = opcode >= 0x20 ? 2 : 1;
        if (opcode < 0x20) {
            check(opcode <= 26, "SIS opcode exceeds the original instruction domain");
            if (opcode == 5) size = 3;
            if (opcode == 12) size = 4;
            if (opcode == 6 || opcode == 7 || opcode == 10 || opcode == 14) size = 5;
        }
        check(size <= bytes.size() - position,
              "SIS instruction crosses its bounded referenced region");
        position += size;
    }
    throw std::runtime_error("SIS string lacks a bounded terminator");
}

struct YakumonoValues {
    std::vector<std::uint32_t> fields;
    std::array<std::uint8_t, 3> rgb{};
};

YakumonoValues read_yakumono(const DatArchive& archive, std::uint32_t root,
                             const std::string& archive_name)
{
    const auto measured_span = archive.next_target_offset(root) - root;
    check(0x54 <= measured_span,
          "yakumono parameter in " + archive_name + " has bounded span " +
              std::to_string(measured_span) + "; source ABI struct occupies 0x54 bytes");
    (void)archive.range(root, 0x54);
    YakumonoValues values;
    values.fields.reserve(22);
    for (std::uint32_t offset = 0; offset < 28; offset += 4)
        values.fields.push_back(archive.be32(root + offset));
    for (std::uint32_t offset = 0x1c; offset <= 0x1e; ++offset)
        values.rgb[offset - 0x1c] = archive.range(root + offset, 1)[0];
    for (std::uint32_t offset = 0x20; offset < 0x48; offset += 4)
        values.fields.push_back(archive.be32(root + offset));
    for (std::uint32_t offset = 0x48; offset < 0x52; offset += 2)
        values.fields.push_back(archive.be16(root + offset));
    return values;
}

struct ArchiveRecord {
    std::string name;
    std::vector<std::uint8_t> raw;
    std::shared_ptr<const DatArchive> archive;
    std::unique_ptr<DatStage> stage;
    YakumonoValues yaku;
};

void print_bool(bool value)
{
    std::cout << (value ? "true" : "false");
}

void print_u32_array(const std::vector<std::uint32_t>& values)
{
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}

void print_hex_bytes(std::span<const std::uint8_t> bytes)
{
    std::cout << '"' << std::hex << std::setfill('0');
    for (const auto byte : bytes) std::cout << std::setw(2) << unsigned(byte);
    std::cout << std::dec << std::setfill(' ') << '"';
}

void print_json_string(const std::string& value)
{
    std::cout << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') std::cout << '\\' << static_cast<char>(c);
        else if (c < 0x20) std::cout << "\\u00" << std::hex
                                     << std::setw(2) << std::setfill('0')
                                     << static_cast<unsigned>(c) << std::dec
                                     << std::setfill(' ');
        else std::cout << static_cast<char>(c);
    }
    std::cout << '"';
}

void print_external_metadata(const DatArchive& archive)
{
    std::cout << "\"external_policy\":\"resolve_null\",\"externals\":[";
    for (std::size_t i = 0; i < archive.external_symbols().size(); ++i) {
        if (i) std::cout << ',';
        const auto& external = archive.external_symbols()[i];
        std::cout << "{\"name\":";
        print_json_string(external.name);
        std::cout << ",\"slots\":";
        print_u32_array(external.slots);
        std::cout << '}';
    }
    std::cout << ']';
}

void print_entry_animations(const ArchiveRecord& record)
{
    const auto& archive = *record.archive;
    const auto& entries = record.stage->entries;
    std::cout << '[';
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (i) std::cout << ',';
        const auto& entry = entries[i];
        const auto count = [&](const std::optional<std::uint32_t>& table) {
            return table ? pointer_list_count(archive, *table) : 0U;
        };
        std::cout << "{\"joint\":";
        print_bool(entry.joint_offset.has_value());
        std::cout << ",\"camera\":";
        print_bool(entry.camera_offset.has_value());
        std::cout << ",\"light\":";
        print_bool(entry.light_table_offset.has_value());
        std::cout << ",\"fog\":";
        print_bool(entry.fog_offset.has_value());
        std::cout << ",\"animation_flags\":";
        print_bool(entry.animation_flags_offset.has_value());
        std::cout << ",\"joint_anim\":" << count(entry.joint_animation_table)
                  << ",\"material_anim\":" << count(entry.material_animation_table)
                  << ",\"shape_anim\":" << count(entry.shape_animation_table)
                  << ",\"unknown_14\":";
        print_bool(entry.unknown_14_offset.has_value());
        std::cout << ",\"collision_bindings\":" << entry.collision_bindings.count
                  << ",\"joint_indices\":" << entry.joint_indices.count << '}';
    }
    std::cout << ']';
}

ArchiveRecord load_archive(const std::filesystem::path& directory,
                           const std::string& name)
{
    ArchiveRecord result;
    result.name = name;
    result.raw = read_file(directory / name);
    result.archive = std::make_shared<const DatArchive>(
        result.raw, DatExternalPolicy::ResolveNull);
    result.stage = std::make_unique<DatStage>(*result.archive);
    result.yaku = read_yakumono(*result.archive,
        symbol_offset(*result.archive, "yakumono_param"), name);
    return result;
}

struct MarkerBinding {
    std::uint32_t joint_index, marker_id, joint_offset;
    std::array<float, 3> translation;
};

std::vector<MarkerBinding> marker_bindings(const ArchiveRecord& record,
                                           std::uint32_t& root_joint)
{
    const auto& archive = *record.archive;
    const auto& stage = *record.stage;
    check(stage.joint_reference_table.count == 1 &&
              stage.joint_reference_table.data_offset.has_value(),
          "source marker reference table is not a single row");
    const auto row = *stage.joint_reference_table.data_offset;
    const auto marker_root = archive.pointer(row, 64);
    check(marker_root.has_value(), "marker row has no root joint");
    root_joint = *marker_root;
    const auto count = archive.be32(row + 8);
    const auto pairs = archive.pointer(row + 4, std::size_t(count) * 4);
    check(count == 20 && pairs.has_value(), "source marker pair table is not 20 bounded rows");

    // The source pair table intentionally contains duplicate marker 0x87.
    // Use the checked joint graph's source preorder for local transforms, while
    // preserving every authored pair and its original order here.
    const DatNativeJoint joint_owner(record.archive, root_joint);
    const auto& graph = joint_owner.graph();
    check(graph.joint_count > 0 && graph.root == 0 &&
              graph.joints[0].source_offset == root_joint,
          "marker root does not match the checked source joint graph");

    std::array<unsigned, 261> id_counts{};
    std::vector<MarkerBinding> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto joint_index = archive.be16(*pairs + i * 4);
        const auto marker_id = archive.be16(*pairs + i * 4 + 2);
        check(joint_index < graph.joint_count && marker_id < id_counts.size(),
              "marker binding exceeds decoded source joint/id domain");
        ++id_counts[marker_id];
        const auto& joint = graph.joints[joint_index];
        result.push_back({joint_index, marker_id, joint.source_offset,
                          {joint.translation[0], joint.translation[1],
                           joint.translation[2]}});
    }
    for (unsigned id = 0; id < id_counts.size(); ++id) {
        const unsigned expected =
            (id <= 4 || (id >= 0x7f && id <= 0x86) ||
             (id >= 0x95 && id <= 0x98)) ? 1U :
            id == 0x87 ? 2U : id == 0x88 ? 1U : 0U;
        check(id_counts[id] == expected,
              "source marker inventory differs at ID " + std::to_string(id));
    }
    check(id_counts[0x94] == 0, "source unexpectedly binds absent camera marker 0x94");
    const auto find_marker = [&](std::uint32_t id) -> const MarkerBinding& {
        const auto found = std::find_if(result.begin(), result.end(),
            [id](const MarkerBinding& binding) { return binding.marker_id == id; });
        check(found != result.end(), "required marker is absent");
        return *found;
    };
    const auto& left = find_marker(0x97).translation;
    const auto& right = find_marker(0x98).translation;
    check(left[0] == -230.0f && left[1] == 180.0f &&
              right[0] == 230.0f && right[1] == -111.0f,
          "source blast marker local translations differ from the data contract");
    return result;
}

void validate_archive_contract(const ArchiveRecord& record, std::size_t ordinal)
{
    static constexpr std::array<std::size_t, 5> public_counts{97, 31, 25, 39, 28};
    static constexpr std::array<std::uint32_t, 5> relocation_counts{
        1762, 929, 530, 1080, 1063};
    static constexpr std::array<std::size_t, 5> external_counts{75, 91, 88, 71, 91};
    static constexpr std::array<std::uint32_t, 12> expected_map_head{
        0x50, 1, 0x88, 10, 0, 0, 0x290, 0x30, 0, 0, 0x350, 44};
    check(ordinal < public_counts.size(), "archive assertion index exceeds contract");
    const auto& archive = *record.archive;
    const auto& stage = *record.stage;
    check(archive.public_symbols().size() == public_counts[ordinal],
          record.name + " public symbol count differs from the pinned DAT inventory");
    check(relocation_count(archive) == relocation_counts[ordinal],
          record.name + " relocation count differs from the pinned DAT inventory");
    check(archive.external_symbols().size() == external_counts[ordinal],
          record.name + " extern count differs from the pinned DAT inventory");
    for (const auto& external : archive.external_symbols()) {
        check(!external.name.empty() && !external.slots.empty(),
              record.name + " contains an extern without an identity or slot");
        for (const auto slot : external.slots) {
            check(slot < 0x400,
                  record.name + " extern slot is outside the authored map-head table region");
        }
    }
    check(stage.root_offset == 0x400 && stage.entries.size() == 10,
          record.name + " map_head root or map entry count differs from contract");
    check(stage.joint_reference_table.count == 1 && stage.spline_table.count == 0 &&
              stage.light_override_table.count == 0x30 && stage.shadow_table.count == 0 &&
              stage.flagged_object_table.count == 44,
          record.name + " map_head service counts differ from contract");
    for (std::size_t i = 0; i < expected_map_head.size(); ++i)
        check(archive.be32(stage.root_offset + static_cast<std::uint32_t>(i * 4)) ==
                  expected_map_head[i],
              record.name + " map_head word differs at byte offset " +
                  std::to_string(i * 4));
    check(stage.light_override_table.data_offset.has_value(),
          record.name + " map_head light-override pointer is absent");
    const auto light_start = *stage.light_override_table.data_offset;
    const auto light_end = archive.next_target_offset(light_start);
    check(light_end >= light_start && light_end - light_start == 24 * 8,
          record.name + " bounded light identity interval is not exactly 24 rows");

    const std::array<bool, 10> expected_joint{false, false, false, false, false,
                                               false, true, true, true, false};
    const std::array<bool, 10> expected_material{false, true, true, true, true,
                                                  true, false, true, true, true};
    for (std::size_t i = 0; i < stage.entries.size(); ++i) {
        const auto& entry = stage.entries[i];
        check(bool(entry.joint_animation_table) == expected_joint[i] &&
                  bool(entry.material_animation_table) == expected_material[i] &&
                  !entry.shape_animation_table,
              record.name + " authored animation root presence differs at map " +
                  std::to_string(i));
        if (entry.joint_animation_table)
            check(pointer_list_count(archive, *entry.joint_animation_table) == 1,
                  record.name + " joint animation table length differs at map " +
                      std::to_string(i));
        if (entry.material_animation_table)
            check(pointer_list_count(archive, *entry.material_animation_table) == 1,
                  record.name + " material animation table length differs at map " +
                      std::to_string(i));
    }
}

void print_marker_rows(const std::vector<MarkerBinding>& markers)
{
    std::cout << '[';
    for (std::size_t i = 0; i < markers.size(); ++i) {
        if (i) std::cout << ',';
        const auto& binding = markers[i];
        std::cout << "{\"pair_index\":" << i << ",\"joint_index\":"
                  << binding.joint_index << ",\"marker_id\":" << binding.marker_id
                  << ",\"joint_offset\":" << binding.joint_offset
                  << ",\"local_translation\":[" << std::setprecision(9)
                  << binding.translation[0] << ',' << binding.translation[1] << ','
                  << binding.translation[2] << "]}";
    }
    std::cout << ']';
}

void print_collision_bindings(const std::vector<DatCollisionBinding>& bindings)
{
    std::cout << '[';
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        if (i) std::cout << ',';
        const auto& binding = bindings[i];
        std::cout << "{\"entry\":" << binding.stage_entry
                  << ",\"source_offset\":" << binding.descriptor_offset
                  << ",\"collision_joint\":" << binding.collision_joint
                  << ",\"source_stage_entry\":" << binding.source_stage_entry
                  << ",\"render_joint\":" << binding.render_joint << '}';
    }
    std::cout << ']';
}

void report_archive(const ArchiveRecord& record)
{
    const auto& archive = *record.archive;
    const auto& stage = *record.stage;
    validate_archive_contract(record, 0);
    const auto scale = read_dat_stage_scale(archive);
    const DatCollision collision(archive);
    const DatCollisionRanges expected_collision_ranges{{
        {0, 71}, {71, 5}, {76, 15}, {91, 21}, {112, 24}}};
    bool ranges_match = true;
    for (std::size_t i = 0; i < expected_collision_ranges.size(); ++i)
        ranges_match = ranges_match &&
            collision.line_ranges[i].start == expected_collision_ranges[i].start &&
            collision.line_ranges[i].count == expected_collision_ranges[i].count;
    check(collision.vertices.size() == 155 && collision.lines.size() == 136 &&
              collision.joints.size() == 8 && ranges_match &&
              collision.source_reserved_2c == 0,
          "GrPs.usd collision inventory differs from the checked source contract");
    const auto collision_bindings = read_dat_collision_bindings(archive, stage, collision);

    std::uint32_t marker_root = 0;
    const auto markers = marker_bindings(record, marker_root);
    const DatLights public_light(archive, "map_plit");
    std::vector<std::uint8_t> public_light_flags;
    std::vector<std::uint32_t> public_light_offsets;
    for (const auto& light : public_light.lights) {
        const auto flags = read_dat_light_override(archive, light.source_offset);
        check(flags.has_value(), "map_plit light lacks a matched bounded override identity");
        public_light_flags.push_back(*flags);
        public_light_offsets.push_back(light.source_offset);
    }
    check(public_light.lights.size() == 3,
          "GrPs.usd map_plit list does not contain three source lights");

    std::vector<std::uint8_t> map2_light_flags;
    std::vector<std::uint32_t> map2_light_offsets;
    const auto& map2 = stage.entries.at(2);
    check(map2.light_table_offset.has_value(), "GrPs.usd map 2 has no source light list");
    const auto table = *map2.light_table_offset;
    const auto table_end = archive.next_target_offset(table);
    bool terminated = false;
    for (std::uint32_t i = 0; i <= 64; ++i) {
        const auto slot = table + i * 4;
        check(slot <= table_end && table_end - slot >= 4,
              "map 2 light table has no bounded terminator");
        const auto list = archive.pointer(slot, 8);
        if (!list) { terminated = true; break; }
        check(i < 64, "map 2 light table exceeds the descriptor budget");
        const auto descriptor = archive.pointer(*list, 28);
        check(descriptor.has_value(), "map 2 light list has a null descriptor");
        const auto flags = read_dat_light_override(archive, *descriptor);
        check(flags.has_value(), "map 2 light lacks a matched bounded override identity");
        map2_light_flags.push_back(*flags);
        map2_light_offsets.push_back(*descriptor);
    }
    check(terminated && !map2_light_flags.empty(),
          "map 2 source light table is empty or unterminated");

    const auto ground_root = symbol_offset(archive, "grGroundParam");
    NativeDatArena ground_arena(record.archive);
    void* decoded_ground = melee_web_ground_data_decode(ground_arena.reader(), ground_root);
    StadiumGroundSnapshot ground{};
    check(stadium_ground_snapshot(decoded_ground, &ground) &&
              ground.stage_param_count == 18,
          "checked GroundParam decode did not produce 18 StageParam rows");
    check(ground.y == scale && ground.row0_stkind == 3 && ground.row0_x14 == 6,
          "GrPs.usd GroundParam/row-zero Stadium source facts differ");
    const auto stage_rows = archive.pointer(ground_root + 0xb0,
                                             std::size_t(ground.stage_param_count) * 0x64);
    check(stage_rows && *stage_rows == 0x3d380,
          "GrPs.usd StageParam source row location differs from the contract");
    std::vector<std::uint32_t> stage_ids;
    for (int i = 0; i < ground.stage_param_count; ++i)
        stage_ids.push_back(ground.stage_ids[i]);

    const auto itemdata = symbol_offset(archive, "itemdata");
    const auto itemdata_null = !archive.pointer(itemdata, 4) && !archive.has_relocation(itemdata);
    check(itemdata_null, "GrPs.usd itemdata is not the authored null word");

    DatEffectBanks effect_owner(record.archive, "map_ptcl", "map_texg", 64);
    MeleeWebEffectBankStats effect_stats{};
    char effect_error[256]{};
    check(melee_web_effect_bank_stats(effect_owner.bank(), &effect_stats,
                                      effect_error, sizeof(effect_error)),
          std::string("effect bank statistics failed: ") + effect_error);
    check(effect_owner.command_root() && effect_owner.texture_root() &&
              effect_stats.bank == 64 && effect_stats.first_command == 30000 &&
              effect_stats.command_count == 30 && effect_stats.texture_groups == 10 &&
              !effect_stats.particle_bank_ready && !effect_stats.effect_entries_ready,
          "GrPs.usd checked effect roots/stats differ or were prematurely published");

    DatScene quake(record.archive, "quake_model_set", DatSceneRootKind::DynamicModel);
    auto* quake_model = quake.single_model();
    check(quake_model && quake.model_count() == 1 && quake_model->joint &&
              quake_model->anims,
          "quake_model_set is not a single typed model with joint and animations");
    for (unsigned i = 0; i < 4; ++i)
        check(quake_model->anims[i], "quake_model_set is missing an authored animation");
    check(quake_model->anims[4] == nullptr,
          "quake_model_set animation list is not terminated after four records");

    const auto dummy_image = symbol_offset(archive,
        "GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc");
    const auto image = read_dat_texture_image(archive, dummy_image);
    std::uint32_t dummy_refs = 0;
    for (const auto slot : relocation_offsets(archive))
        if (archive.pointer(slot, 1) == dummy_image) ++dummy_refs;
    check(image.width == 16 && image.height == 16 && image.format == 0 &&
              dummy_refs == 1,
          "dummy image is not the one-reference 16x16 I4 source descriptor");

    const auto yaku_root = symbol_offset(archive, "yakumono_param");
    const auto yaku_public_interval = public_symbol_span(archive, yaku_root);
    const auto yaku_target_interval = archive.next_target_offset(yaku_root) - yaku_root;
    check(yaku_public_interval == 0x70 && yaku_target_interval == 0x54,
          "GrPs.usd yakumono public/next-target intervals differ from raw source bounds");
    std::uint32_t yaku_relocations = 0;
    for (std::uint32_t offset = 0; offset < 0x54; offset += 4)
        yaku_relocations += archive.has_relocation(yaku_root + offset) ? 1U : 0U;
    check(yaku_relocations == 0,
          "GrPs.usd yakumono ABI span unexpectedly contains a relocation");
    std::vector<std::uint32_t> refs_to_yaku_target;
    for (const auto slot : relocation_offsets(archive))
        if (archive.pointer(slot, 1) == yaku_root + 0x54)
            refs_to_yaku_target.push_back(slot);
    const auto ald_yaku = symbol_offset(archive, "ALDYakuAll");
    check(refs_to_yaku_target.size() == 1 && refs_to_yaku_target[0] == ald_yaku + 4,
          "the next referenced object after yakumono is not the ALDYakuAll + 4 target");

    const auto expected_yaku = std::vector<std::uint32_t>{
        3600, 3800, 1200, 1800, 300, 120, 60,
        600, 240, 600, 300, 600, 1200, 600, 1200, 600, 800,
        5, 2, 2, 0, 7};
    check(record.yaku.fields == expected_yaku &&
              record.yaku.rgb == std::array<std::uint8_t, 3>{150, 180, 160},
          "GrPs.usd yakumono fields or RGB identity differs from the source contract");

    const auto root = stage.root_offset;
    const auto words = relocation_offsets(archive);
    std::uint32_t external_slots = 0;
    for (const auto& external : archive.external_symbols())
        external_slots += static_cast<std::uint32_t>(external.slots.size());

    std::cout << "{\"name\":";
    print_json_string(record.name);
    std::cout << ",\"scope\":\"english-source-base\",";
    print_external_metadata(archive);
    std::cout << ",\"file_bytes\":" << record.raw.size()
              << ",\"data_bytes\":" << archive.data().size()
              << ",\"public_symbols\":" << archive.public_symbols().size()
              << ",\"relocation_count\":" << words.size()
              << ",\"relocation_slots\":";
    print_u32_array(words);
    std::cout << ",\"external_symbols\":" << archive.external_symbols().size()
              << ",\"external_slots\":" << external_slots
              << ",\"map_head_offset\":" << root << ",\"map_head_words\":[";
    for (std::size_t i = 0; i < 12; ++i) {
        if (i) std::cout << ',';
        std::cout << archive.be32(root + static_cast<std::uint32_t>(i * 4));
    }
    std::cout << "],\"entry_count\":" << stage.entries.size()
              << ",\"joint_references\":" << stage.joint_reference_table.count
              << ",\"splines\":" << stage.spline_table.count
              << ",\"light_override_declared_count\":" << stage.light_override_table.count
              << ",\"light_override_bounded_rows\":24"
              << ",\"shadows\":" << stage.shadow_table.count
              << ",\"flagged_objects\":" << stage.flagged_object_table.count
              << ",\"entry_animations\":";
    print_entry_animations(record);
    std::cout << ",\"stage_scale\":" << std::setprecision(9) << scale
              << ",\"stage_param_count\":" << ground.stage_param_count
              << ",\"stage_param_rows_offset\":" << *stage_rows
              << ",\"stage_param_ids\":";
    print_u32_array(stage_ids);
    std::cout << ",\"stadium_stage_param\":{\"stkind\":"
              << ground.row0_stkind
              << ",\"x14\":" << ground.row0_x14 << "}"
              << ",\"itemdata_is_null\":true"
              << ",\"collision\":{\"root\":" << collision.root_offset
              << ",\"vertices\":" << collision.vertices.size()
              << ",\"lines\":" << collision.lines.size()
              << ",\"joints\":" << collision.joints.size()
              << ",\"ranges\":[";
    for (std::size_t i = 0; i < collision.line_ranges.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[' << collision.line_ranges[i].start << ','
                  << collision.line_ranges[i].count << ']';
    }
    std::cout << "],\"reserved_2c\":" << collision.source_reserved_2c << "}"
              << ",\"collision_bindings\":";
    print_collision_bindings(collision_bindings);
    std::cout << ",\"markers\":{\"root_joint\":" << marker_root
              << ",\"pair_count\":" << markers.size()
              << ",\"rows_in_authored_order\":";
    print_marker_rows(markers);
    std::cout << ",\"absent_0x94\":true,\"blast_local_xy\":[[-230,180],[230,-111]]}"
              << ",\"lights\":{\"map_plit\":" << public_light.lights.size()
              << ",\"map_plit_source_offsets\":";
    print_u32_array(public_light_offsets);
    std::cout << ",\"map_plit_override_flags\":[";
    for (std::size_t i = 0; i < public_light_flags.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << static_cast<unsigned>(public_light_flags[i]);
    }
    std::cout << "],\"map2_light_count\":" << map2_light_flags.size()
              << ",\"map2_source_offsets\":";
    print_u32_array(map2_light_offsets);
    std::cout << ",\"map2_override_flags\":[";
    for (std::size_t i = 0; i < map2_light_flags.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << static_cast<unsigned>(map2_light_flags[i]);
    }
    std::cout << "]},\"particle_bank\":{\"bank\":" << effect_stats.bank
              << ",\"first_command\":" << effect_stats.first_command
              << ",\"command_count\":" << effect_stats.command_count
              << ",\"texture_groups\":" << effect_stats.texture_groups
              << ",\"command_root_valid\":true,\"texture_root_valid\":true"
              << ",\"published\":false,\"effect_entries_ready\":false}"
              << ",\"quake_model\":{\"models\":" << quake.model_count()
              << ",\"animations\":4,\"terminated_at_4\":true}"
              << ",\"dummy_image\":{\"offset\":" << dummy_image
              << ",\"width\":" << image.width << ",\"height\":" << image.height
              << ",\"format\":" << image.format << ",\"reference_count\":" << dummy_refs
              << "},\"yakumono_fields\":";
    print_u32_array(record.yaku.fields);
    std::cout << ",\"yakumono_rgb\":[" << unsigned(record.yaku.rgb[0]) << ','
              << unsigned(record.yaku.rgb[1]) << ',' << unsigned(record.yaku.rgb[2])
              << "],\"yakumono_consumed_prefix_bytes\":82"
              << ",\"yakumono_abi_size_bytes\":84"
              << ",\"yakumono_public_symbol_interval_bytes\":" << yaku_public_interval
              << ",\"yakumono_next_referenced_target_interval_bytes\":" << yaku_target_interval
              << ",\"yakumono_abi_padding_hex\":";
    print_hex_bytes(archive.range(yaku_root + 0x52, 2));
    std::cout << ",\"bytes_after_abi_before_next_public_symbol_hex\":";
    print_hex_bytes(archive.range(yaku_root + 0x54, yaku_public_interval - 0x54));
    std::cout << ",\"next_target_reference_slots\":";
    print_u32_array(refs_to_yaku_target);
    std::cout << ",\"yakumono_abi_relocations\":" << yaku_relocations << '}';
}

void report_transform_archive(const ArchiveRecord& record, std::size_t ordinal)
{
    const auto& archive = *record.archive;
    const auto& stage = *record.stage;
    validate_archive_contract(record, ordinal);
    const DatCollision collision(archive);
    const auto collision_bindings = read_dat_collision_bindings(archive, stage, collision);
    const auto root = stage.root_offset;
    std::uint32_t ext_slots = 0, out_of_header_ext_slots = 0;
    for (const auto& external : archive.external_symbols()) {
        ext_slots += static_cast<std::uint32_t>(external.slots.size());
        for (const auto slot : external.slots)
            out_of_header_ext_slots += slot >= 0x400 ? 1U : 0U;
    }
    const auto yaku_root = symbol_offset(archive, "yakumono_param");
    const auto yaku_public_interval = public_symbol_span(archive, yaku_root);
    const auto yaku_target_interval = archive.next_target_offset(yaku_root) - yaku_root;
    std::uint32_t yaku_relocations = 0;
    check(yaku_public_interval >= 0x54, "yakumono public interval is shorter than ABI size");
    for (std::uint32_t offset = 0; offset < 0x54; offset += 4)
        yaku_relocations += archive.has_relocation(yaku_root + offset) ? 1U : 0U;
    check(yaku_relocations == 0 && record.yaku.fields ==
              std::vector<std::uint32_t>{3600, 3800, 1200, 1800, 300, 120, 60,
                  600, 240, 600, 300, 600, 1200, 600, 1200, 600, 800,
                  5, 2, 2, 0, 7} &&
              record.yaku.rgb == std::array<std::uint8_t, 3>{150, 180, 160},
          record.name + " yakumono data differs from the source contract");
    std::vector<std::uint32_t> next_target_refs;
    for (const auto slot : relocation_offsets(archive))
        if (archive.pointer(slot, 1) == yaku_root + yaku_target_interval)
            next_target_refs.push_back(slot);

    std::cout << "{\"name\":";
    print_json_string(record.name);
    std::cout << ",\"scope\":\"transformation-map-head-only\",";
    print_external_metadata(archive);
    std::cout << ",\"file_bytes\":" << record.raw.size()
              << ",\"data_bytes\":" << archive.data().size()
              << ",\"public_symbols\":" << archive.public_symbols().size()
              << ",\"relocation_count\":" << relocation_count(archive)
              << ",\"relocation_slots\":";
    print_u32_array(relocation_offsets(archive));
    std::cout << ",\"collision_bindings\":";
    print_collision_bindings(collision_bindings);
    std::cout << ",\"collision_binding_count\":" << collision_bindings.size()
              << ",\"external_symbols\":" << archive.external_symbols().size()
              << ",\"external_slots\":" << ext_slots
              << ",\"external_slots_outside_0_3ff\":" << out_of_header_ext_slots
              << ",\"map_head_offset\":" << root << ",\"map_head_words\":[";
    for (std::uint32_t i = 0; i < 12; ++i) {
        if (i) std::cout << ',';
        std::cout << archive.be32(root + i * 4);
    }
    std::cout << "],\"entry_count\":" << stage.entries.size()
              << ",\"joint_references\":" << stage.joint_reference_table.count
              << ",\"splines\":" << stage.spline_table.count
              << ",\"light_override_declared_count\":" << stage.light_override_table.count
              << ",\"light_override_bounded_rows\":24"
              << ",\"shadows\":" << stage.shadow_table.count
              << ",\"flagged_objects\":" << stage.flagged_object_table.count
              << ",\"entry_animations\":";
    print_entry_animations(record);
    std::cout << ",\"yakumono_fields\":";
    print_u32_array(record.yaku.fields);
    std::cout << ",\"yakumono_rgb\":[" << unsigned(record.yaku.rgb[0]) << ','
              << unsigned(record.yaku.rgb[1]) << ',' << unsigned(record.yaku.rgb[2]) << ']';
    std::cout << ",\"yakumono_consumed_prefix_bytes\":82,\"yakumono_abi_size_bytes\":84"
              << ",\"yakumono_public_symbol_interval_bytes\":" << yaku_public_interval
              << ",\"yakumono_next_referenced_target_interval_bytes\":" << yaku_target_interval
              << ",\"yakumono_abi_padding_hex\":";
    print_hex_bytes(archive.range(yaku_root + 0x52, 2));
    std::cout << ",\"bytes_after_abi_before_next_public_symbol_hex\":";
    print_hex_bytes(archive.range(yaku_root + 0x54, yaku_public_interval - 0x54));
    std::cout << ",\"next_target_reference_slots\":";
    print_u32_array(next_target_refs);
    std::cout << ",\"yakumono_abi_relocations\":" << yaku_relocations << '}';
}

void report_sis(const ArchiveRecord& english, const ArchiveRecord& japanese)
{
    const auto& a = *english.archive;
    const auto& jp = *japanese.archive;
    const auto root = symbol_offset(a, "SIS_GrPStadiumData");
    const auto jp_root = symbol_offset(jp, "SIS_GrPStadiumData");
    const DatSis owner(english.archive, "SIS_GrPStadiumData");
    const DatSis jp_owner(japanese.archive, "SIS_GrPStadiumData");
    std::vector<std::uint32_t> lengths;
    for (std::uint32_t i = 2; i < owner.entry_count(); ++i) {
        const auto target = a.pointer(root + i * 4, 1);
        lengths.push_back(target ? sis_string_length(a, root, i) : 0U);
    }
    std::vector<std::uint32_t> jp_lengths;
    for (std::uint32_t i = 2; i < jp_owner.entry_count(); ++i) {
        const auto target = jp.pointer(jp_root + i * 4, 1);
        jp_lengths.push_back(target ? sis_string_length(jp, jp_root, i) : 0U);
    }
    std::uint32_t en_composed = 0, jp_composed = 0;
    for (std::uint32_t type = 8; type <= 12; ++type) {
        en_composed = std::max(en_composed,
            sis_string_length(a, root, 6) + sis_string_length(a, root, type) +
            sis_string_length(a, root, 7) - 2);
        jp_composed = std::max(jp_composed,
            sis_string_length(jp, jp_root, 6) + sis_string_length(jp, jp_root, type) +
            sis_string_length(jp, jp_root, 7) - 2);
    }
    const auto table_bytes = a.next_target_offset(root) - root;
    const auto slot5_bytes = sis_string_length(a, root, 5);
    const bool localized_prefix_equal =
        std::equal(a.data().begin(), a.data().begin() + root,
                   jp.data().begin(), jp.data().begin() + jp_root);
    const auto symbols_equal = a.public_symbols().size() == jp.public_symbols().size() &&
        std::equal(a.public_symbols().begin(), a.public_symbols().end(),
            jp.public_symbols().begin(), [](const auto& left, const auto& right) {
                return left.name == right.name && left.data_offset == right.data_offset;
            });
    const auto external_equal = a.external_symbols().size() == jp.external_symbols().size() &&
        std::equal(a.external_symbols().begin(), a.external_symbols().end(),
            jp.external_symbols().begin(), [](const auto& left, const auto& right) {
                return left.name == right.name && left.slots == right.slots;
            });
    const bool metadata_equal = symbols_equal && external_equal &&
        relocation_offsets(a) == relocation_offsets(jp);
    check(owner.entry_count() == 22 && jp_owner.entry_count() == 22 &&
              table_bytes == 22 * 4 && slot5_bytes == 1 &&
              en_composed == 112 && jp_composed == 66 &&
              localized_prefix_equal && metadata_equal,
          "Stadium English/Japanese SIS identity, length or prefix contract failed");
    std::cout << "\"sis\":{\"offset\":" << root << ",\"entry_count\":"
              << owner.entry_count() << ",\"table_bytes\":" << table_bytes
              << ",\"empty_authored_slot5_bytes\":" << slot5_bytes
              << ",\"english_composed_max\":" << en_composed
              << ",\"japanese_composed_max\":" << jp_composed
              << ",\"english_string_lengths\":";
    print_u32_array(lengths);
    std::cout << ",\"japanese_string_lengths\":";
    print_u32_array(jp_lengths);
    std::cout << ",\"localized_prefix_equal\":true,\"metadata_equal\":true";
    std::cout << "}";
}

int inspect_yakumono_target(const std::filesystem::path& path)
{
    const auto bytes = read_file(path);
    const DatArchive archive(bytes, DatExternalPolicy::ResolveNull);
    const auto root = symbol_offset(archive, "yakumono_param");
    const auto target = archive.next_target_offset(root);
    const auto public_interval = public_symbol_span(archive, root);
    check(target >= root, "next referenced target precedes yakumono root");
    std::cout << "{\"archive\":";
    print_json_string(path.filename().string());
    std::cout << ",\"yakumono_root\":" << root
              << ",\"abi_size_bytes\":84,\"consumed_prefix_bytes\":82"
              << ",\"next_referenced_target\":" << target
              << ",\"next_referenced_target_delta\":" << target - root
              << ",\"next_public_symbol_interval\":" << public_interval
              << ",\"interval_bytes_0x52_to_next_public_symbol_hex\":";
    print_hex_bytes(archive.range(root + 0x52, public_interval - 0x52));
    std::cout << ",\"references_to_next_target\":[";
    bool first = true;
    for (std::uint32_t slot = 0; slot + 4 <= archive.data().size(); slot += 4) {
        if (!archive.has_relocation(slot) || archive.pointer(slot, 1) != target) continue;
        if (!first) std::cout << ',';
        first = false;
        const DatPublicSymbol* owner = nullptr;
        for (const auto& symbol : archive.public_symbols())
            if (symbol.data_offset <= slot &&
                (!owner || symbol.data_offset > owner->data_offset)) owner = &symbol;
        std::cout << "{\"slot\":" << slot << ",\"relative_to_yakumono\":"
                  << (slot >= root ? static_cast<std::int64_t>(slot - root)
                                   : -static_cast<std::int64_t>(root - slot))
                  << ",\"nearest_preceding_public_symbol\":";
        if (owner) print_json_string(owner->name);
        else std::cout << "null";
        std::cout << ",\"relative_to_symbol\":";
        if (owner) std::cout << slot - owner->data_offset;
        else std::cout << "null";
        std::cout << '}';
    }
    std::cout << "]}" << '\n';
    return 0;
}

int run_probe(std::string_view probe, const std::filesystem::path& path)
{
    const auto bytes = read_file(path);
    const auto archive = std::make_shared<const DatArchive>(
        bytes, DatExternalPolicy::ResolveNull);
    if (probe == "effects") {
        DatEffectBanks owner(archive, "map_ptcl", "map_texg", 64);
        MeleeWebEffectBankStats stats{};
        char error[256]{};
        check(melee_web_effect_bank_stats(owner.bank(), &stats, error, sizeof(error)),
              std::string("effect bank statistics failed: ") + error);
        check(owner.command_root() && owner.texture_root() && stats.bank == 64 &&
                  stats.first_command == 30000 && stats.command_count == 30 &&
                  stats.texture_groups == 10 && !stats.particle_bank_ready &&
                  !stats.effect_entries_ready,
              "Stadium effect source probe failed before any bank publication");
        std::cout << "{\"probe\":\"effects\",\"scope\":\"structural ResolveNull inspection; not attached or started\",\"bank\":"
                  << stats.bank << ",\"first_command\":" << stats.first_command
                  << ",\"command_count\":" << stats.command_count
                  << ",\"texture_groups\":" << stats.texture_groups
                  << ",\"valid_roots\":true,\"published\":false}\n";
        return 0;
    }
    if (probe == "ground") {
        const auto root = symbol_offset(*archive, "grGroundParam");
        NativeDatArena arena(archive);
        void* decoded_ground = melee_web_ground_data_decode(arena.reader(), root);
        StadiumGroundSnapshot ground{};
        check(stadium_ground_snapshot(decoded_ground, &ground) &&
                  ground.stage_param_count == 18 && ground.row0_stkind == 3 &&
                  ground.row0_x14 == 6,
              "Stadium checked GroundParam source probe failed");
        std::cout << "{\"probe\":\"ground\",\"scope\":\"typed GroundParam/StageParam structural decode\",\"rows\":"
                  << ground.stage_param_count << ",\"row0_stkind\":"
                  << ground.row0_stkind
                  << ",\"row0_x14\":" << ground.row0_x14 << "}\n";
        return 0;
    }
    if (probe == "quake") {
        DatScene quake(archive, "quake_model_set", DatSceneRootKind::DynamicModel);
        auto* model = quake.single_model();
        check(model && quake.model_count() == 1 && model->joint && model->anims,
              "Stadium quake_model_set has no typed model/joint/animation root");
        for (unsigned i = 0; i < 4; ++i)
            check(model->anims[i], "Stadium quake_model_set animation is absent");
        check(!model->anims[4], "Stadium quake_model_set animation list lacks terminator");
        std::cout << "{\"probe\":\"quake\",\"scope\":\"typed DynamicModel structural decode\",\"models\":1,\"animations\":4,\"terminated\":true}\n";
        return 0;
    }
    throw std::runtime_error("probe must be one of effects, ground, or quake");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--inspect-yakumono-target")
            return inspect_yakumono_target(argv[2]);
        if (argc == 4 && std::string_view(argv[1]) == "--probe")
            return run_probe(argv[2], argv[3]);
        check(argc == 2, "expected one local asset-directory argument");
        const std::filesystem::path directory(argv[1]);
        std::vector<ArchiveRecord> records;
        for (const auto* name : {"GrPs.usd", "GrPs1.dat", "GrPs2.dat",
                                 "GrPs3.dat", "GrPs4.dat"})
            records.push_back(load_archive(directory, name));
        const auto japanese_bytes = read_file(directory / "GrPs.dat");
        const auto japanese = std::make_shared<const DatArchive>(
            japanese_bytes, DatExternalPolicy::ResolveNull);
        ArchiveRecord japanese_record;
        japanese_record.name = "GrPs.dat";
        japanese_record.raw = japanese_bytes;
        japanese_record.archive = japanese;
        japanese_record.yaku = read_yakumono(*japanese,
            symbol_offset(*japanese, "yakumono_param"), "GrPs.dat");

        const auto expected_yaku = std::vector<std::uint32_t>{
            3600, 3800, 1200, 1800, 300, 120, 60,
            600, 240, 600, 300, 600, 1200, 600, 1200, 600, 800,
            5, 2, 2, 0, 7};
        const std::array<std::uint8_t, 3> expected_rgb{150, 180, 160};
        check(japanese_record.yaku.fields == expected_yaku &&
                  japanese_record.yaku.rgb == expected_rgb,
              "Japanese-base yakumono data differs from the source contract");
        for (const auto& record : records)
            check(record.yaku.fields == expected_yaku && record.yaku.rgb == expected_rgb,
                  record.name + " yakumono fields differ from the common six-archive contract");

        std::cout << "{\"archives\":[";
        for (std::size_t i = 0; i < records.size(); ++i) {
            if (i) std::cout << ',';
            if (i == 0) report_archive(records[i]);
            else report_transform_archive(records[i], i);
        }
        std::cout << "],";
        report_sis(records.front(), japanese_record);

        const auto& pstadium_bytes = read_file(directory / "audio/pstadium.hps");
        const auto& pokesta_bytes = read_file(directory / "audio/pokesta.hps");
        const DatAudioStream pstadium(pstadium_bytes, false);
        const DatAudioStream pokesta(pokesta_bytes, false);
        const auto& ssm_bytes = read_file(directory / "audio/us/pstadium.ssm");
        const DatAudioBank bank(ssm_bytes);
        check(bank.base_id == 1453 && bank.samples.size() == 2 &&
                  bank.samples.front().id == 1453 && bank.samples.back().id == 1454,
              "English pstadium.ssm does not contain authored sample IDs 1453-1454");
        check(pstadium.sample_rate > 0 && !pstadium.blocks.empty() &&
                  pokesta.sample_rate > 0 && !pokesta.blocks.empty(),
              "one of the two source Stadium HPS streams lacks a valid structural block chain");
        std::cout << ",\"audio_bank\":{\"base_id\":" << bank.base_id
                  << ",\"sample_count\":" << bank.samples.size()
                  << ",\"first_sample_id\":" << bank.samples.front().id
                  << ",\"last_sample_id\":" << bank.samples.back().id << "}"
                  << ",\"audio_streams\":[{\"path\":\"audio/pstadium.hps\",\"sample_rate\":"
                  << pstadium.sample_rate << ",\"channels\":" << pstadium.channel_headers.size()
                  << ",\"blocks\":" << pstadium.blocks.size() << "},{\"path\":\"audio/pokesta.hps\",\"sample_rate\":"
                  << pokesta.sample_rate << ",\"channels\":" << pokesta.channel_headers.size()
                  << ",\"blocks\":" << pokesta.blocks.size() << "}]";

        std::cout << ",\"transform_yaku_identical\":";
        print_bool(std::all_of(records.begin() + 1, records.end(),
            [&](const auto& record) { return record.yaku.fields == records.front().yaku.fields &&
                record.yaku.rgb == records.front().yaku.rgb; }));
        std::cout << ",\"six_archive_yaku_identical\":";
        print_bool(japanese_record.yaku.fields == records.front().yaku.fields &&
                   japanese_record.yaku.rgb == records.front().yaku.rgb);
        std::cout << ",\"japanese_base_parse_scope\":\"localization metadata, yakumono contract, and SIS comparison only\"}"
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Stadium C0 trace failed: " << error.what() << '\n';
        return 1;
    }
}
