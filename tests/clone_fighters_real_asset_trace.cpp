#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

namespace {

std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t public_root(const DatArchive& archive, const char* name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw std::runtime_error(std::string("missing public symbol: ") + name);
}

const FighterCostume& costume(std::uint32_t kind, std::uint32_t index)
{
    for (const auto& entry : fighter_costumes())
        if (entry.fighter_kind == kind && entry.costume_index == index) return entry;
    throw std::runtime_error("source costume is absent from the generated registry");
}

void check_effect_table(const char* path, const char* symbol, std::uint32_t bank)
{
    const DatArchive effects(read_file(path));
    const auto effect_root = public_root(effects, symbol);
    const auto extent = effects.next_target_offset(effect_root) - effect_root;
    require(extent >= 8 + 2 * 20 && extent < 8 + 3 * 20,
            std::string("effect bank ") + std::to_string(bank) + " does not expose exactly two entries");
    // Entries are 20-byte scalar records; their first words are not DAT
    // pointers. The referenced-target extent above is the native count gate.
}

void check_animation_actions(const std::shared_ptr<const DatFighterRuntime>& runtime,
                             const char* path, const std::vector<std::uint32_t>& motions,
                             const char* label)
{
    DatFighterAnimationStore store(runtime, read_file(path));
    for (const auto motion : motions) {
        const auto selected = store.select(motion);
        require(selected.action.archive_bytes && selected.animation,
                std::string(label) + " action has no decoded animation: " + std::to_string(motion));
        if (selected.action.command_offset)
            require(selected.commands.has_value(), std::string(label) +
                " action command stream is missing: " + std::to_string(motion));
    }
}

void check_model_archives(int first_argument,
                          int argc,
                          const std::array<const char*, 5>& models,
                          const std::array<const char*, 5>& materials,
                          const char* label,
                          char** argv)
{
    for (std::size_t index = 0; index < models.size(); ++index) {
        require(first_argument + static_cast<int>(index) < argc,
                std::string(label) + " costume argument is missing");
        const DatArchive model(read_file(argv[first_argument + static_cast<int>(index)]));
        (void)public_root(model, models[index]);
        (void)public_root(model, materials[index]);
    }
}

void check_dr_mario(const std::shared_ptr<const DatArchive>& archive,
                    const char* animation_path, const char* effect_path,
                    const char* audio_path, int model_first, int argc, char** argv)
{
    const auto& identity = costume(21, 0);
    require(identity.motion_count == 303 && identity.fighter_symbol == "ftDataDrmario" &&
                identity.fighter_filename == "PlDr.dat" && identity.animation_filename == "PlDrAJ.dat",
            "Dr. Mario source identity differs from the pinned registry");
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    require(runtime->actions().size() == 303 && runtime->mario_attributes().has_value(),
            "Dr. Mario action count or Mario extension is incomplete");
    require(runtime->mario_attributes()->specials_cape_kind == 0x54,
            "Dr. Mario source Sheet article kind is not 0x54");

    const auto fighter_root = public_root(*archive, "ftDataDrmario");
    const auto item_table = archive->pointer(fighter_root + 0x48, 16);
    require(item_table.has_value(), "Dr. Mario source Article table is absent");
    require(!archive->pointer(*item_table, 24) && archive->pointer(*item_table + 4, 24) &&
                !archive->pointer(*item_table + 8, 24) && archive->pointer(*item_table + 12, 24),
            "Dr. Mario source Article slots are not exactly vitamin and Sheet");

    const auto vitamin = *archive->pointer(*item_table + 4, 24);
    const auto vitamin_special = *archive->pointer(vitamin + 4, 20);
    const std::array<std::uint32_t, 5> vitamin_words{
        0x3fb33333, 0xbf32b8c2, 0x42960000, 0x3f59999a, 0x3f666666};
    for (std::size_t index = 0; index < vitamin_words.size(); ++index)
        require(archive->be32(vitamin_special + static_cast<std::uint32_t>(index * 4)) == vitamin_words[index],
                "Dr. Mario vitamin native attributes changed");
    const auto vitamin_states = *archive->pointer(vitamin + 12, 6 * 16);
    require(archive->range(vitamin_states, 6 * 16).size() == 6 * 16,
            "Dr. Mario vitamin article state table is not six DAT rows");
    const std::array<std::array<std::uint32_t, 4>, 6> vitamin_state_words{{
        {{0, 0x18fd4, 0, 0x3be8}}, {{0, 0x18fd4, 0, 0}},
        {{0x20188, 0, 0, 0}}, {{0x24b20, 0, 0, 0}},
        {{0x23028, 0, 0, 0}}, {{0, 0, 0, 0}}}};
    for (std::size_t row = 0; row < vitamin_state_words.size(); ++row)
        for (std::size_t word = 0; word < vitamin_state_words[row].size(); ++word)
            require(archive->be32(vitamin_states + static_cast<std::uint32_t>(row * 16 + word * 4)) ==
                        vitamin_state_words[row][word],
                    "Dr. Mario vitamin article state row changed");

    const auto sheet = *archive->pointer(*item_table + 12, 24);
    const auto sheet_special = *archive->pointer(sheet + 4, 4);
    (void)sheet_special;
    const auto sheet_states = *archive->pointer(sheet + 12, 2 * 16);
    require(archive->range(sheet_states, 2 * 16).size() == 2 * 16,
            "Dr. Mario Sheet article state table is not two DAT rows");

    check_animation_actions(runtime, animation_path,
        {2, 46, 295, 296, 297, 298, 299, 300, 301, 302}, "Dr. Mario");
    check_effect_table(effect_path, "effMarioDataTable", 1);
    require(!read_file(audio_path).empty(), "Dr. Mario source audio bank is empty");
    check_model_archives(model_first, argc,
        {"PlyDrmario5K_Share_joint", "PlyDrmario5KRe_Share_joint",
         "PlyDrmario5KBu_Share_joint", "PlyDrmario5KGr_Share_joint",
         "PlyDrmario5KBk_Share_joint"},
        {"PlyDrmario5K_Share_matanim_joint", "PlyDrmario5KRe_Share_matanim_joint",
         "PlyDrmario5KBu_Share_matanim_joint", "PlyDrmario5KGr_Share_matanim_joint",
         "PlyDrmario5KBk_Share_matanim_joint"}, "Dr. Mario", argv);
}

void check_roy(const std::shared_ptr<const DatArchive>& archive,
               const char* animation_path, const char* effect_path,
               const char* audio_path, int model_first, int argc, char** argv)
{
    const auto& identity = costume(26, 0);
    require(identity.motion_count == 327 && identity.fighter_symbol == "ftDataEmblem" &&
                identity.fighter_filename == "PlFe.dat" && identity.animation_filename == "PlFeAJ.dat",
            "Roy source identity differs from the pinned registry");
    const auto runtime = std::make_shared<const DatFighterRuntime>(archive, identity);
    require(runtime->actions().size() == 327 && runtime->mars_attributes().has_value(),
            "Roy action count or Mars extension is incomplete");
    const auto& mars = *runtime->mars_attributes();
    require(mars.absorb_bone >= 0 && mars.absorb_size > 0 && mars.sword_x14 >= 0,
            "Roy source Mars attributes are invalid");

    const auto fighter_root = public_root(*archive, "ftDataEmblem");
    require(!archive->pointer(fighter_root + 0x48, 1),
            "Roy source Article table must be null");
    require(runtime->dynamics().bones.size() == 3 && runtime->dynamics().spheres.empty() &&
                runtime->dynamics().animation_table_offset.has_value(),
            "Roy source dynamics descriptor is incomplete");
    const auto dynamics = runtime->dynamics().descriptor_offset;
    const auto mode_table = *runtime->dynamics().animation_table_offset;
    // The source consumer walks five mode pointers. The following referenced
    // DAT target is adjacent padding/metadata, not a sixth native mode.
    require(archive->range(mode_table, 5 * 4).size() == 5 * 4,
            "Roy dynamics mode table is not five pointer rows");
    const std::array<std::array<std::uint32_t, 3>, 5> modes{{
        {{2, 2, 2}}, {{0, 0, 2}}, {{2, 0, 0}},
        {{1, 1, 1}}, {{2, 1, 1}}}};
    for (std::size_t row = 0; row < modes.size(); ++row) {
        const auto target = archive->pointer(mode_table + static_cast<std::uint32_t>(row * 4), 12);
        require(target.has_value(), "Roy dynamics mode row pointer is missing");
        for (std::size_t field = 0; field < modes[row].size(); ++field)
            require(archive->be32(*target + static_cast<std::uint32_t>(field * 4)) == modes[row][field],
                    "Roy dynamics mode row changed");
    }
    require(dynamics != 0, "Roy dynamics descriptor offset was not retained");

    check_animation_actions(runtime, animation_path,
        {2, 46, 295, 296, 297, 298, 299, 300, 301, 302,
         303, 304, 305, 306, 307, 308, 309, 310, 311,
         312, 313, 314, 315, 316, 317, 318, 319, 320,
         321, 322, 323, 324, 325, 326}, "Roy");
    check_effect_table(effect_path, "effEmblemDataTable", 49);
    require(!read_file(audio_path).empty(), "Roy source audio bank is empty");
    check_model_archives(model_first, argc,
        {"PlyEmblem5K_Share_joint", "PlyEmblem5KRe_Share_joint",
         "PlyEmblem5KBu_Share_joint", "PlyEmblem5KGr_Share_joint",
         "PlyEmblem5KYe_Share_joint"},
        {"PlyEmblem5K_Share_matanim_joint", "PlyEmblem5KRe_Share_matanim_joint",
         "PlyEmblem5KBu_Share_matanim_joint", "PlyEmblem5KGr_Share_matanim_joint",
         "PlyEmblem5KYe_Share_matanim_joint"}, "Roy", argv);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        // 1 + 9 Dr. Mario assets + 9 Roy assets. Five model archives per clone
        // make every source costume identity part of this native check.
        if (argc != 19) throw std::runtime_error(
            "usage: clone_fighters_real_asset_trace <Dr assets x9> <Roy assets x9>");
        const auto dr = std::make_shared<const DatArchive>(read_file(argv[1]));
        try {
            check_dr_mario(dr, argv[2], argv[3], argv[4], 5, argc, argv);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("Dr. Mario: ") + error.what());
        }
        const auto roy = std::make_shared<const DatArchive>(read_file(argv[10]));
        try {
            check_roy(roy, argv[11], argv[12], argv[13], 14, argc, argv);
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string("Roy: ") + error.what());
        }
        std::cout << "Dr. Mario kind 21/303 actions, vitamin+Sheet articles, five costumes, effect bank 1/count 2; "
                     "Roy kind 26/327 actions, Mars attributes, three dynamics chains/five mode rows, five costumes, "
                     "effect bank 49/count 2 and audio passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
