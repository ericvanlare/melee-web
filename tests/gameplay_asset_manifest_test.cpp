#include "gameplay_asset_manifest.hpp"

#include "dat_archive.hpp"
#include "fighter_binding.hpp"
#include "gameplay_content.h"
#include <melee/pl/forward.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace melee_web;

namespace {

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& operation)
{
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("Expected asset descriptor rejection");
}

bool has(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::string logical_model(std::string_view authored)
{
    std::string result(authored);
    if (!result.empty() && result.back() == '.') result += "usd";
    return result;
}

void no_duplicates(const std::vector<std::string>& names)
{
    auto sorted = names;
    std::sort(sorted.begin(), sorted.end());
    check(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(),
          "Asset descriptor contains duplicate logical names");
}

MeleeWebMenuMatchSelection selection(int stage, int first, int second,
                                     unsigned first_costume = 0,
                                     unsigned second_costume = 0)
{
    MeleeWebMenuMatchSelection value{};
    value.player_count = 2;
    value.hud_layout = 2;
    value.start.rules.match_kind = MatchKind_Stock;
    value.start.rules.is_stock = 1;
    value.start.rules.is_vs = 1;
    value.start.rules.xB = -1;
    value.start.rules.x20 = UINT64_MAX;
    value.start.rules.x0_3 = 2;
    value.start.rules.stkind = stage;
    for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i) {
        value.start.players[i].slot_type = Gm_PKind_NA;
        value.start.players[i].stocks = 0;
    }
    value.start.players[0].slot_type = Gm_PKind_Human;
    value.start.players[1].slot_type = Gm_PKind_Human;
    value.start.players[0].rumble_enabled = 1;
    value.start.players[1].rumble_enabled = 1;
    value.start.players[0].ckind = first;
    value.start.players[1].ckind = second;
    value.start.players[0].color = first_costume;
    value.start.players[1].color = second_costume;
    value.start.players[0].stocks = value.start.players[1].stocks = 4;
    value.players[0] = {0, 4, first_costume, 0};
    value.players[1] = {1, 4, second_costume, 0};
    return value;
}

void menu_contract()
{
    const auto names = menu_asset_names();
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
    check(names.size()==33, "Silent menu descriptor excludes only DSP coefficients");
    check(!has(names,"dsp_coef.bin"), "Public scope must not request DSP coefficients");
#else
    check(names.size()==34, "Menu descriptor must include each admitted CSS voice bank");
    check(has(names,"dsp_coef.bin"), "Development scope requires DSP coefficients");
#endif
    for(const auto name:{"MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd",
                         "LbMcGame.usd","NtMemAc.usd","sislib_font.bin","smash2.sem",
                         "menu01.hps"})
        check(std::find(names.begin(),names.end(),name)!=names.end(),"Missing menu resource");
    const auto banks=menu_audio_bank_names();
    no_duplicates(banks);
    for(int kind=0;kind<CKIND_PLAYABLE_COUNT;++kind)
        if(const auto* fighter=melee_web_fighter_content(kind))
            check(std::find(banks.begin(),banks.end(),fighter->audio_bank)!=banks.end(),
                  "CSS OnExit fighter voice bank is missing");
    for(const auto& name:banks)
        check(std::find(names.begin(),names.end(),name)!=names.end(),"Menu scope omits registered bank");
    no_duplicates(names);
}

void source_fighter_closure()
{
    const std::vector<int> characters = {
        CKIND_MARIO, CKIND_FOX, CKIND_FALCO, CKIND_MARS, CKIND_DRMARIO,
        CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK, CKIND_CAPTAIN, CKIND_GANON,
        CKIND_LUIGI, CKIND_PIKACHU, CKIND_PICHU, CKIND_PURIN, CKIND_DONKEY,
        CKIND_KOOPA, CKIND_MEWTWO,
    };
    for (const int character : characters) {
        const auto* content = melee_web_fighter_content(character);
        check(content != nullptr, "Admitted fighter content row is missing");
        auto value = selection(St_Kind_Last, character, CKIND_MARIO,
                               content->costumes - 1, 0);
        const auto names = match_asset_names(value);
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
        check(!has(names,"dsp_coef.bin"), "Public match must not request DSP coefficients");
#else
        check(has(names,"dsp_coef.bin"), "Development match requires DSP coefficients");
#endif
        const auto selected = std::find_if(fighter_costumes().begin(), fighter_costumes().end(),
            [&](const FighterCostume& row) {
                return row.fighter_kind == static_cast<std::uint32_t>(content->fighter_kind) &&
                       row.costume_index == content->costumes - 1;
            });
        check(selected != fighter_costumes().end(), "Selected costume is absent from registry");
        check(has(names, logical_model(selected->model_filename)),
              "Selected costume model is absent from descriptor");
        check(has(names, content->effect_archive) && has(names, content->audio_bank),
              "Selected fighter effect/audio closure is incomplete");
        no_duplicates(names);
    }
}

void source_stage_music()
{
    const std::vector<int> stages = {St_Kind_Last, St_Kind_Battle, St_Kind_Story,
                                     St_Kind_OldPupupu, St_Kind_Shrine,
                                     St_Kind_Izumi, St_Kind_OldYoshi};
    const std::map<int, std::vector<std::string>> expected = {
        {St_Kind_Last, {"sp_end.hps", "hyaku2.hps"}},
        {St_Kind_Battle, {"sp_zako.hps", "hyaku.hps"}},
        {St_Kind_Story, {"ystory.hps"}},
        {St_Kind_OldPupupu, {"old_kb.hps"}},
        {St_Kind_Shrine, {"shrine.hps", "akaneia.hps"}},
        {St_Kind_Izumi, {"izumi.hps"}},
        {St_Kind_OldYoshi, {"old_ys.hps"}},
    };
    for (const int stage : stages) {
        const auto names = match_asset_names(selection(stage, CKIND_MARIO, CKIND_MARIO));
        for (const auto& music : expected.at(stage))
            check(has(names, music), "Source stage music candidate is absent");
    }
}

void rejects_invalid_without_mutation()
{
    auto value = selection(St_Kind_Last, CKIND_MARIO, CKIND_MARIO);
    const auto before = value;
    check(match_asset_names(value) == match_asset_names(value),
          "Repeated descriptor calls are not deterministic");
    check(std::memcmp(&value, &before, sizeof(value)) == 0,
          "Descriptor changed the selection payload");
    value.player_count = 0;
    check(match_asset_names(value) == match_asset_names(selection(
              St_Kind_Last, CKIND_MARIO, CKIND_MARIO)),
          "Zero player count did not use the source active-slot fallback");
    value = selection(St_Kind_Last, CKIND_MARIO, CKIND_MARIO);
    value.player_count = 1;
    rejects([&] { (void)match_asset_names(value); });
    value = selection(St_Kind_Last, CKIND_KIRBY, CKIND_MARIO);
    rejects([&] { (void)match_asset_names(value); });
    value = selection(St_Kind_Last, CKIND_MARIO, CKIND_MARIO, 5, 0);
    rejects([&] { (void)match_asset_names(value); });
    value = selection(St_Kind_Kongo, CKIND_MARIO, CKIND_MARIO);
    rejects([&] { (void)match_asset_names(value); });
    value = selection(St_Kind_Last, CKIND_MARIO, CKIND_MARIO);
    value.players[0].costume = 1;
    rejects([&] { (void)match_asset_names(value); });
}

std::vector<std::uint8_t> read_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open stage archive");
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

void print_music_rows(const char* path)
{
    const auto bytes = read_archive(path);
    const DatArchive archive(bytes, DatExternalPolicy::ResolveNull);
    const auto symbol = std::find_if(archive.public_symbols().begin(),
                                     archive.public_symbols().end(),
        [](const DatPublicSymbol& value) { return value.name == "grGroundParam"; });
    if (symbol == archive.public_symbols().end())
        throw std::runtime_error("Stage archive has no grGroundParam public symbol");
    const auto root = symbol->data_offset;
    const auto count = archive.be32(root + 0xb4);
    if (count == 0 || count > 256)
        throw std::runtime_error("Stage archive has an invalid StageParam count");
    const auto rows = archive.pointer(root + 0xb0, count * 0x64);
    if (!rows) throw std::runtime_error("Stage archive StageParam pointer is absent");
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto row = *rows + i * 0x64;
        const auto signed_word = [&](std::uint32_t offset) {
            return static_cast<std::int32_t>(archive.be32(offset));
        };
        std::cout << signed_word(row) << ' ' << signed_word(row + 4) << ' '
                  << signed_word(row + 8) << ' '
                  << signed_word(row + 0xc) << ' '
                  << signed_word(row + 0x10) << '\n';
    }
}

void print_descriptor_stage(int stage)
{
    const auto names = match_asset_names(selection(stage, CKIND_MARIO, CKIND_MARIO));
    for (const auto& name : names) std::cout << name << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 3 && std::string(argv[1]) == "--music-rows") {
            print_music_rows(argv[2]);
            return 0;
        }
        if (argc == 3 && std::string(argv[1]) == "--descriptor-stage") {
            print_descriptor_stage(std::stoi(argv[2]));
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--menu-names") {
            for(const auto& name:menu_asset_names())std::cout << name << '\n';
            return 0;
        }
        menu_contract();
        source_fighter_closure();
        source_stage_music();
        rejects_invalid_without_mutation();
        std::cout << "Source menu/match asset descriptors, costume closure, authored music candidates, and rejection boundaries: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
