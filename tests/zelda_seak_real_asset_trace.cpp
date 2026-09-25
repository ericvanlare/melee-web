#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
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
static const FighterCostume& identity(uint32_t kind, uint32_t costume)
{
    for (const auto& row : fighter_costumes())
        if (row.fighter_kind == kind && row.costume_index == costume) return row;
    throw std::runtime_error("missing generated Zelda/Sheik identity");
}
static void check_form(const char* fighter_path, const char* animation_path,
                       const char* fighter_symbol, uint32_t kind,
                       uint32_t motions, uint32_t article_count,
                       const std::array<uint32_t, 4>& article_special_bytes,
                       const std::array<uint32_t, 4>& article_state_rows,
                       const std::array<const char*, 5>& models,
                       const std::array<const char*, 5>& costume_paths)
{
    auto archive = std::make_shared<const DatArchive>(read_file(fighter_path),
                                                       DatExternalPolicy::ResolveNull);
    const auto& base = identity(kind, 0);
    if (base.motion_count != motions || base.fighter_symbol != fighter_symbol)
        throw std::runtime_error("generated form identity differs from GALE01r2: " +
            std::string(base.fighter_symbol) + "/" + std::to_string(base.motion_count));
    auto runtime = std::make_shared<const DatFighterRuntime>(archive, base);
    if (runtime->actions().size() != motions)
        throw std::runtime_error("source action table extent differs from generated identity");
    const auto fighter = root(*archive, fighter_symbol);
    const auto dynamics = archive->pointer(fighter + 0x2c, 20);
    if (!dynamics) throw std::runtime_error("source fighter dynamics descriptor is absent");
    std::set<unsigned> selectors;
    for (const auto& action : runtime->actions()) selectors.insert(action.blend_dynamics[1]);
    const auto mode_table = archive->pointer(*dynamics + 16);
    if (kind == 19) {
        const std::set<unsigned> expected_selectors={
            0,2,3,4,5,7,8,10,11,15,16,17,18,19,20,21,22,23,26,28,30,31,32,33,34,35};
        if (archive->be32(*dynamics) != 9 || archive->be32(*dynamics + 8) != 0 ||
            !mode_table || archive->next_target_offset(*mode_table) - *mode_table < 36 * 4 ||
            selectors != expected_selectors)
            throw std::runtime_error("Zelda authored nine-chain/36-mode dynamics identity differs");
    } else if (archive->be32(*dynamics) != 0 || archive->be32(*dynamics + 8) != 0 ||
               mode_table || selectors.size() != 1 || *selectors.begin() != 0) {
        throw std::runtime_error("Sheik source must retain its null dynamics mode table");
    }
    const auto attributes = archive->pointer(fighter + 4, 4);
    if (!attributes) throw std::runtime_error("source special attribute root is absent");
    const uint32_t attribute_bytes = kind == 19 ? 0xA8 : 0x74;
    if (archive->next_target_offset(*attributes) - *attributes < attribute_bytes)
        throw std::runtime_error("source special attribute record is shorter than its ABI");
    const auto article_table = archive->pointer(fighter + 0x48, article_count * 4);
    if (!article_table) throw std::runtime_error("source OnLoad Article table is absent");
    for (uint32_t index = 0; index < article_count; ++index) {
        const auto article = archive->pointer(*article_table + index * 4, 24);
        if (!article)
            throw std::runtime_error("source OnLoad Article entry is absent: " + std::to_string(index));
        const auto special = archive->pointer(*article + 4);
        const auto states = archive->pointer(*article + 12);
        if (bool(special) != (article_special_bytes[index] != 0) ||
            (special && archive->next_target_offset(*special) - *special != article_special_bytes[index]))
            throw std::runtime_error("source Article special record extent differs at slot " +
                                     std::to_string(index));
        const auto expected_state_bytes = article_state_rows[index] * 16;
        if (bool(states) != (expected_state_bytes != 0) ||
            (states && archive->next_target_offset(*states) - *states != expected_state_bytes))
            throw std::runtime_error("source Article state-row extent differs at slot " +
                                     std::to_string(index));
    }
    DatFighterAnimationStore action_store(runtime, read_file(animation_path));
    for (const uint32_t motion : {0U, 44U, 295U, motions - 1}) {
        const auto selected = action_store.select(motion);
        if (!selected.action.archive_bytes || !selected.animation)
            throw std::runtime_error("source animation is absent: " + std::to_string(motion));
    }
    for (size_t index = 0; index < models.size(); ++index) {
        const auto& costume = identity(kind, static_cast<uint32_t>(index));
        if (costume.model_symbol != models[index] || costume.motion_count != motions)
            throw std::runtime_error("generated costume row differs from source initializer");
        const DatArchive model(read_file(costume_paths[index]),
                               DatExternalPolicy::ResolveNull);
        (void)root(model, models[index]);
    }
}

int main(int argc, char** argv)
{
    try {
        if (argc != 20)
            throw std::runtime_error("expected both fighter/action archives, effects, audio, results and ten costumes");
        constexpr std::array<const char*, 5> zelda_models = {
            "PlyZelda5K_Share_joint", "PlyZelda5KRe_Share_joint",
            "PlyZelda5KBu_Share_joint", "PlyZelda5KGr_Share_joint",
            "PlyZelda5KWh_Share_joint"};
        constexpr std::array<const char*, 5> sheik_models = {
            "PlySeak5K_Share_joint", "PlySeak5KRe_Share_joint",
            "PlySeak5KBu_Share_joint", "PlySeak5KGr_Share_joint",
            "PlySeak5KWh_Share_joint"};
        check_form(argv[1], argv[2], "ftDataZelda", 19, 311, 2,
                   {48, 20, 0, 0}, {2, 1, 0, 0}, zelda_models,
                   {argv[10], argv[11], argv[12], argv[13], argv[14]});
        check_form(argv[3], argv[4], "ftDataSeak", 7, 317, 4,
                   {12, 4, 0, 108}, {5, 1, 1, 0}, sheik_models,
                   {argv[15], argv[16], argv[17], argv[18], argv[19]});
        const DatArchive samus_effects(read_file(argv[5]), DatExternalPolicy::ResolveNull);
        const DatArchive zelda_effects(read_file(argv[6]), DatExternalPolicy::ResolveNull);
        const auto samus_effect_root = root(samus_effects, "effSamusDataTable");
        const auto zelda_effect_root = root(zelda_effects, "effZeldaDataTable");
        if (samus_effects.next_target_offset(samus_effect_root) - samus_effect_root != 96 ||
            zelda_effects.next_target_offset(zelda_effect_root) - zelda_effect_root != 160)
            throw std::runtime_error("source Samus/Zelda effect bank extents differ");
        if (read_file(argv[7]).empty() || read_file(argv[8]).empty() || read_file(argv[9]).empty())
            throw std::runtime_error("shared voice or form-specific result archive is empty");
        (void)root(DatArchive(read_file(argv[8]), DatExternalPolicy::ResolveNull),
                   "ftDemoResultMotionFileZelda");
        (void)root(DatArchive(read_file(argv[9]), DatExternalPolicy::ResolveNull),
                   "ftDemoResultMotionFileSeak");
        std::cout << "Zelda FTKind 19/311 and Sheik FTKind 7/317 source rows, Articles, five costumes per form, effects, shared audio and separate result archives passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
