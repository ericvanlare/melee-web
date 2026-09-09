#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <array>
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

const FighterCostume& fox_costume()
{
    for (const auto& costume : fighter_costumes())
        if (costume.fighter_kind == 1 && costume.costume_index == 0) return costume;
    throw std::runtime_error("FTKIND_FOX costume 0 is absent from the generated registry");
}

std::uint32_t public_root(const DatArchive& archive, const char* name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw std::runtime_error(std::string("missing public symbol: ") + name);
}
}

int main(int argc, char** argv)
{
    if (argc != 9) {
        std::cerr << "usage: fox_real_asset_trace PlFx.dat PlFxAJ.dat EfFxData.dat fox.ssm PlFxNr.dat PlFxOr.dat PlFxLa.dat PlFxGr.dat\n";
        return 2;
    }
    try {
        auto fighter = std::make_shared<const DatArchive>(read_file(argv[1]));
        const auto& identity = fox_costume();
        if (identity.motion_count != 327 || identity.fighter_symbol != "ftDataFox")
            throw std::runtime_error("generated Fox identity does not match source metadata");
        auto runtime = std::make_shared<const DatFighterRuntime>(fighter, identity);
        if (runtime->actions().size() != 327 || !runtime->fox_attributes())
            throw std::runtime_error("Fox action or Fox-family extension table is incomplete");
        const auto& fox = *runtime->fox_attributes();
        if (fox.blaster_shot_item_kind != 0x36 || fox.blaster_gun_item_kind != 0x4a ||
            fox.reflector_bone_id != 1)
            throw std::runtime_error("Fox source article or reflector fields changed");
        const auto fighter_root = public_root(*fighter, "ftDataFox");
        const auto item_table = fighter->pointer(fighter_root + 0x48, 16);
        if (!item_table || !fighter->pointer(*item_table, 24) ||
            !fighter->pointer(*item_table + 4, 24) || !fighter->pointer(*item_table + 8, 24) ||
            fighter->pointer(*item_table + 12, 1))
            throw std::runtime_error("Fox source Article slots are not exactly 0, 1 and 2");
        const auto part_table = fighter->pointer(fighter_root + 0x1c, 20);
        if (!part_table || fighter->next_target_offset(*part_table) - *part_table != 20)
            throw std::runtime_error("Fox source part-animation table does not contain five groups");
        constexpr std::array<std::uint16_t, 5> starts{27, 57, 41, 41, 41};
        constexpr std::array<std::uint16_t, 5> counts{12, 13, 1, 4, 4};
        constexpr std::array<std::uint32_t, 5> variants{4, 4, 3, 4, 4};
        constexpr std::array<std::uint8_t, 34> parts{
            27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,
            57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
            51, 42, 43, 44, 45, 46, 47, 48, 49};
        std::size_t part_cursor = 0;
        for (std::uint32_t group = 0; group < starts.size(); ++group) {
            const auto descriptor = fighter->pointer(*part_table + group * 4, 12);
            if (!descriptor || fighter->be16(*descriptor) != starts[group] ||
                fighter->be16(*descriptor + 2) != counts[group])
                throw std::runtime_error("Fox source part-animation group range changed");
            const auto indices = fighter->pointer(*descriptor + 4, counts[group]);
            const auto animations = fighter->pointer(*descriptor + 8, variants[group] * 4);
            if (!indices || !animations ||
                fighter->next_target_offset(*animations) - *animations != variants[group] * 4)
                throw std::runtime_error("Fox source part-animation group ownership changed");
            const auto bytes = fighter->range(*indices, counts[group]);
            for (const auto value : bytes)
                if (value != parts.at(part_cursor++))
                    throw std::runtime_error("Fox source part-animation index changed");
        }
        auto animation = read_file(argv[2]);
        DatFighterAnimationStore store(runtime, animation);
        for (const auto motion : {2U, 20U, 295U, 296U, 302U, 303U, 310U, 326U}) {
            const auto selected = store.select(motion);
            if (!selected.action.archive_bytes || !selected.animation)
                throw std::runtime_error("Fox source motion has no decoded animation: " + std::to_string(motion));
            if (!selected.commands && selected.action.command_offset)
                throw std::runtime_error("Fox source command pointer was not retained: " + std::to_string(motion));
        }
        auto effects = std::make_shared<const DatArchive>(read_file(argv[3]));
        std::uint32_t effect_root = 0;
        bool found_effect_root = false;
        for (const auto& symbol : effects->public_symbols()) {
            if (symbol.name == "effFoxDataTable") { effect_root = symbol.data_offset; found_effect_root = true; break; }
        }
        if (!found_effect_root || effects->next_target_offset(effect_root) - effect_root < 8 + 6 * 20 ||
            effects->next_target_offset(effect_root) - effect_root >= 8 + 7 * 20)
            throw std::runtime_error("Fox effect table does not expose the six source entries");
        if (read_file(argv[4]).empty()) throw std::runtime_error("Fox source audio bank is empty");
        constexpr std::array<const char*, 4> model_symbols{
            "PlyFox5K_Share_joint", "PlyFox5KOr_Share_joint",
            "PlyFox5KLa_Share_joint", "PlyFox5KGr_Share_joint"};
        constexpr std::array<const char*, 4> material_symbols{
            "PlyFox5K_Share_matanim_joint", "PlyFox5KOr_Share_matanim_joint",
            "PlyFox5KLa_Share_matanim_joint", "PlyFox5KGr_Share_matanim_joint"};
        for (std::size_t costume = 0; costume < model_symbols.size(); ++costume) {
            const DatArchive model(read_file(argv[5 + costume]));
            (void) public_root(model, model_symbols[costume]);
            (void) public_root(model, material_symbols[costume]);
        }
        std::cout << "Fox real DAT metadata, 327 action rows, distinct Fox attributes, special animations, effect bank 3/count 6 and audio: passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
