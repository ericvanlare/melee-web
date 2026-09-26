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
        if (argc != 12)
            throw std::runtime_error("expected Yoshi fighter, animation, effects, audio, results and six costumes");
        constexpr std::array<const char*, 6> models = {
            "PlyYoshi5K_Share_joint", "PlyYoshi5KRe_Share_joint",
            "PlyYoshi5KBu_Share_joint", "PlyYoshi5KYe_Share_joint",
            "PlyYoshi5KPi_Share_joint", "PlyYoshi5KAq_Share_joint"};
        const FighterCostume* identity = nullptr;
        for (const auto& costume : fighter_costumes())
            if (costume.fighter_kind == 14 && costume.costume_index == 0) identity = &costume;
        if (!identity || identity->motion_count != 314 || identity->fighter_symbol != "ftDataYoshi")
            throw std::runtime_error("generated Yoshi identity differs from GALE01r2 source metadata");

        auto archive = std::make_shared<const DatArchive>(read_file(argv[1]), DatExternalPolicy::ResolveNull);
        auto runtime = std::make_shared<const DatFighterRuntime>(archive, *identity);
        if (runtime->actions().size() != 314 || !runtime->yoshi_attributes())
            throw std::runtime_error("Yoshi actions or exact ftYoshiAttributes are incomplete");
        const auto fighter = root(*archive, "ftDataYoshi");
        const auto attributes = archive->pointer(fighter + 4, 0x138);
        if (!attributes) throw std::runtime_error("Yoshi's exact 0x138 source attribute root is absent");
        const auto guard = archive->pointer(fighter + 0x20, 4);
        if (!guard || archive->pointer(*guard, 4) || archive->f32(*guard + 4) != 0.0f)
            throw std::runtime_error("Yoshi source guard descriptor is not its null-joint/zero-scalar record");
        const auto articles = archive->pointer(fighter + 0x48, 16);
        if (!articles) throw std::runtime_error("Yoshi four-entry x48 Article/joint table is absent");
        constexpr std::array<uint32_t, 3> special_sizes = {8, 8, 0};
        constexpr std::array<uint32_t, 3> state_counts = {2, 1, 0};
        for (unsigned i = 0; i < 3; ++i) {
            const auto article = archive->pointer(*articles + i * 4, 24);
            if (!article) throw std::runtime_error("Yoshi registered Article root is null");
            if (special_sizes[i]) {
                const auto special = archive->pointer(*article + 4, special_sizes[i]);
                if (!special) throw std::runtime_error("Yoshi Article special attributes are absent");
                (void)archive->range(*special, special_sizes[i]);
            } else if (archive->pointer(*article + 4)) {
                throw std::runtime_error("Egg Lay unexpectedly has source special attributes");
            }
            const auto states = archive->pointer(*article + 12,
                state_counts[i] ? state_counts[i] * 16 : 1);
            if (state_counts[i]) {
                if (!states) throw std::runtime_error("Yoshi serialized ItemState rows are absent");
                (void)archive->range(*states, state_counts[i] * 16);
            } else if (states) {
                throw std::runtime_error("Egg Lay unexpectedly has serialized ItemState rows");
            }
        }
        const auto egg_lay = archive->pointer(*articles + 2 * 4, 24);
        const auto model_desc = archive->pointer(*egg_lay + 16, 16);
        const auto egg_lay_joint = archive->pointer(*model_desc, 64);
        const auto special_n_joint = archive->pointer(*articles + 3 * 4, 64);
        if (!egg_lay_joint || !special_n_joint || *egg_lay_joint != *special_n_joint)
            throw std::runtime_error("Yoshi x48[3] no longer aliases Egg Lay's source model joint");

        DatFighterAnimationStore actions(runtime, read_file(argv[2]));
        for (const auto motion : {0U, 44U, 295U, 297U, 299U, 302U, 306U, 309U, 313U}) {
            const auto selected = actions.select(motion);
            if (!selected.action.archive_bytes || !selected.animation)
                throw std::runtime_error("Yoshi source motion animation is missing: " + std::to_string(motion));
            if (selected.action.command_offset && !selected.commands)
                throw std::runtime_error("Yoshi source motion command stream is missing: " + std::to_string(motion));
            if (motion == 297 && !selected.action.command_offset)
                throw std::runtime_error("Yoshi source special-N2 motion 297 has no authored command root");
        }
        const DatArchive effects(read_file(argv[3]), DatExternalPolicy::ResolveNull);
        const auto effect_root = root(effects, "effYoshiDataTable");
        const auto effect_extent = effects.next_target_offset(effect_root) - effect_root;
        // The target interval includes alignment slack, so it bounds one
        // authored row without requiring its extent to end on a row edge.
        if (effect_extent < 8 + 20 || effect_extent >= 8 + 2 * 20)
            throw std::runtime_error("Yoshi effect table target interval does not bound exactly one row: root=" +
                std::to_string(effect_root) + " extent=" + std::to_string(effect_extent));
        constexpr unsigned effect_count = 1;
        if (read_file(argv[4]).empty()) throw std::runtime_error("Yoshi source audio bank is empty");
        if (read_file(argv[5]).empty()) throw std::runtime_error("Yoshi result-motion archive is empty");
        for (size_t i = 0; i < models.size(); ++i) {
            const FighterCostume* row = nullptr;
            for (const auto& costume : fighter_costumes())
                if (costume.fighter_kind == 14 && costume.costume_index == i) row = &costume;
            if (!row || row->model_symbol != models[i] || row->motion_count != 314)
                throw std::runtime_error("generated Yoshi costume identity differs from source metadata");
            const DatArchive model(read_file(argv[6 + i]), DatExternalPolicy::ResolveNull);
            (void)root(model, models[i]);
        }
        std::cout << "Yoshi GALE01r2 metadata, 314 actions, three Articles plus aliased joint, six costume roots, effect bank 9/count "
                  << effect_count << " and audio passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
