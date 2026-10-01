#include "dat_fighter_runtime.hpp"
#include "dat_material_animation.hpp"
#include "dat_native_joint.hpp"
#include "fighter_binding.hpp"

#include <array>
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

std::uint32_t symbol_offset(const DatArchive& archive, std::string_view name)
{
    const DatPublicSymbol* found = nullptr;
    for (const auto& symbol : archive.public_symbols()) {
        if (symbol.name != name) continue;
        if (found) throw std::runtime_error("duplicate Falco source symbol: " + std::string(name));
        found = &symbol;
    }
    if (!found) throw std::runtime_error("missing Falco source symbol: " + std::string(name));
    return found->data_offset;
}
}

int main(int argc, char** argv)
{
    if (argc != 9) {
        std::cerr << "usage: falco_real_asset_trace PlFc.dat PlFcAJ.dat EfFxData.dat falco.ssm "
                     "PlFcNr.dat PlFcRe.dat PlFcBu.dat PlFcGr.dat\n";
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
        constexpr std::array<std::string_view, 4> falco_model_files{
            "PlFcNr.dat", "PlFcRe.dat", "PlFcBu.dat", "PlFcGr.dat"};
        unsigned costume_count = 0;
        for (const auto& costume : fighter_costumes()) {
            if (costume.fighter_kind != 22) continue;
            if (costume.costume_index >= falco_model_files.size() ||
                costume.model_filename != falco_model_files[costume.costume_index])
                throw std::runtime_error("Falco costume registry contains an unexpected costume identity");
            auto model_archive = std::make_shared<const DatArchive>(
                read_file(argv[5 + costume.costume_index]));
            auto model = std::make_unique<DatNativeJoint>(model_archive,
                symbol_offset(*model_archive, costume.model_symbol));
            DatMaterialAnimation material(model_archive,
                symbol_offset(*model_archive, costume.material_animation_symbol), model->graph());
            std::cout << "Falco costume " << costume.costume_index
                      << " native graph joints=" << model->graph().joint_count
                      << " materials=" << model->graph().material_count
                      << " texture_animations=" << material.texture_animation_count()
                      << " images=" << material.image_count() << ": passed\n";
            ++costume_count;
        }
        if (costume_count != falco_model_files.size())
            throw std::runtime_error("Falco native material trace did not cover all four registered costumes");
        std::cout << "Falco real DAT metadata, 327 action rows, Fox-family attributes, special animations, effect bank 3/count 6, audio and four native costume/material graphs: passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
