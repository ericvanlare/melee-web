#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace melee_web;

static std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), {}};
}
static uint32_t root(const DatArchive& archive, const char* name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw std::runtime_error(std::string("missing public symbol: ") + name);
}

int main(int argc, char** argv)
{
    try {
        if (argc != 11)
            throw std::runtime_error("expected Samus fighter, animation, effects, audio, results and five costumes");
        const FighterCostume* identity = nullptr;
        constexpr std::array<const char*, 5> models = {
            "PlySamus5K_Share_joint", "PlySamus5KPi_Share_joint",
            "PlySamus5KBk_Share_joint", "PlySamus5KGr_Share_joint",
            "PlySamus5KLa_Share_joint"};
        for (const auto& costume : fighter_costumes())
            if (costume.fighter_kind == 13 && costume.costume_index == 0) identity = &costume;
        if (!identity || identity->motion_count != 313 || identity->fighter_symbol != "ftDataSamus")
            throw std::runtime_error("generated Samus identity differs from GALE01r2 source metadata");

        auto archive = std::make_shared<const DatArchive>(read_file(argv[1]), DatExternalPolicy::ResolveNull);
        auto runtime = std::make_shared<const DatFighterRuntime>(archive, *identity);
        if (runtime->actions().size() != 313 || !runtime->samus_attributes())
            throw std::runtime_error("Samus actions or exact ftSs_DatAttrs are incomplete");
        if (runtime->samus_attributes()->xD0 != 0xB4)
            throw std::runtime_error("Samus's unrelocated xD0 source word was not retained exactly");

        const auto fighter = root(*archive, "ftDataSamus");
        const auto attributes = archive->pointer(fighter + 4, 0xD4);
        if (!attributes || archive->has_relocation(*attributes + 0xD0))
            throw std::runtime_error("Samus source attribute root or xD0 relocation contract changed");
        const auto articles = archive->pointer(fighter + 0x48, 16);
        if (!articles) throw std::runtime_error("Samus four-entry x48 Article table is absent");
        constexpr std::array<uint32_t, 4> special_sizes = {0x1C, 0x20, 0x40, 0xB0};
        constexpr std::array<uint32_t, 4> state_counts = {2, 9, 4, 0};
        for (unsigned i = 0; i < 4; ++i) {
            const auto article = archive->pointer(*articles + i * 4, 24);
            if (!article) throw std::runtime_error("Samus x48 Article root is null");
            const auto special = archive->pointer(*article + 4, special_sizes[i]);
            if (!special) throw std::runtime_error("Samus Article special record is null");
            (void)archive->range(*special, special_sizes[i]);
            const auto states = archive->pointer(*article + 12, state_counts[i] ? state_counts[i] * 16 : 1);
            if (state_counts[i] != 0) {
                if (!states) throw std::runtime_error("Samus serialized ItemState rows are absent");
                (void)archive->range(*states, state_counts[i] * 16);
            } else if (states) {
                throw std::runtime_error("Grapple Beam has an unexpected ItemState table");
            }
        }

        DatFighterAnimationStore actions(runtime, read_file(argv[2]));
        for (const auto motion : {0U, 44U, 295U, 299U, 301U, 303U, 306U, 312U}) {
            const auto selected = actions.select(motion);
            if (!selected.action.archive_bytes || !selected.animation)
                throw std::runtime_error("Samus source motion animation is missing: " + std::to_string(motion));
            if (selected.action.command_offset && !selected.commands)
                throw std::runtime_error("Samus source motion command stream is missing: " + std::to_string(motion));
        }

        const DatArchive effects(read_file(argv[3]), DatExternalPolicy::ResolveNull);
        const auto effect_root = root(effects, "effSamusDataTable");
        const auto effect_extent = effects.next_target_offset(effect_root) - effect_root;
        if (effect_extent < 8 + 4 * 20 || effect_extent >= 8 + 5 * 20)
            throw std::runtime_error("Samus source effect table does not bound four entries");
        if (read_file(argv[4]).empty()) throw std::runtime_error("Samus source audio bank is empty");
        if (read_file(argv[5]).empty()) throw std::runtime_error("Samus result-motion archive is empty");
        for (size_t i = 0; i < models.size(); ++i) {
            const FighterCostume* row = nullptr;
            for (const auto& costume : fighter_costumes())
                if (costume.fighter_kind == 13 && costume.costume_index == i) row = &costume;
            if (!row || row->model_symbol != models[i] || row->motion_count != 313)
                throw std::runtime_error("generated Samus costume identity differs from source metadata");
            const DatArchive model(read_file(argv[6 + i]), DatExternalPolicy::ResolveNull);
            (void)root(model, models[i]);
        }
        std::cout << "Samus GALE01r2 metadata, 313 actions, four Article schemas, five costume roots, effect bank 2/count 4 and audio passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
