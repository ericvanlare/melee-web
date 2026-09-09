#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
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

const FighterCostume& falco_costume()
{
    for (const auto& costume : fighter_costumes())
        if (costume.fighter_kind == 22 && costume.costume_index == 0) return costume;
    throw std::runtime_error("FTKIND_FALCO costume 0 is absent from the generated registry");
}
}

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: falco_real_asset_trace PlFc.dat PlFcAJ.dat EfFxData.dat falco.ssm\n";
        return 2;
    }
    try {
        auto fighter = std::make_shared<const DatArchive>(read_file(argv[1]));
        const auto& identity = falco_costume();
        if (identity.motion_count != 327 || identity.fighter_symbol != "ftDataFalco")
            throw std::runtime_error("generated Falco identity does not match source metadata");
        auto runtime = std::make_shared<const DatFighterRuntime>(fighter, identity);
        if (runtime->actions().size() != 327 || !runtime->fox_attributes())
            throw std::runtime_error("Falco action or Fox-family extension table is incomplete");
        const auto& fox = *runtime->fox_attributes();
        if (fox.blaster_shot_item_kind != 0x37 || fox.blaster_gun_item_kind != 0x4b ||
            fox.reflector_bone_id != 1)
            throw std::runtime_error("Falco source article or reflector fields changed");
        auto animation = read_file(argv[2]);
        DatFighterAnimationStore store(runtime, animation);
        for (const auto motion : {2U, 20U, 295U, 296U, 302U, 303U, 310U, 326U}) {
            const auto selected = store.select(motion);
            if (!selected.action.archive_bytes || !selected.animation)
                throw std::runtime_error("Falco source motion has no decoded animation: " + std::to_string(motion));
            if (!selected.commands && selected.action.command_offset)
                throw std::runtime_error("Falco source command pointer was not retained: " + std::to_string(motion));
        }
        auto effects = std::make_shared<const DatArchive>(read_file(argv[3]));
        std::uint32_t effect_root = 0;
        bool found_effect_root = false;
        for (const auto& symbol : effects->public_symbols()) {
            if (symbol.name == "effFoxDataTable") { effect_root = symbol.data_offset; found_effect_root = true; break; }
        }
        if (!found_effect_root || effects->next_target_offset(effect_root) - effect_root < 8 + 6 * 20 ||
            effects->next_target_offset(effect_root) - effect_root >= 8 + 7 * 20)
            throw std::runtime_error("Falco effect table does not expose the six source entries");
        if (read_file(argv[4]).empty()) throw std::runtime_error("Falco source audio bank is empty");
        std::cout << "Falco real DAT metadata, 327 action rows, Fox-family attributes, special animations, effect bank 3/count 6 and audio: passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
