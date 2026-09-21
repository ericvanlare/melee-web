#include "gameplay_asset_manifest.hpp"

#include "dat_archive.hpp"
#include "gameplay_content.h"
#include "gameplay_result_motion_table.hpp"
#include "fighter_binding.hpp"
#include <melee/pl/forward.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace melee_web {
namespace {

constexpr auto kMenuFiles = std::to_array<std::string_view>({
    "MnSlChr.usd", "MnSlMap.usd", "SdSlChr.usd", "MnExtAll.usd",
    "LbMcGame.usd", "NtMemAc.usd", "sislib_font.bin", "smash2.sem",
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
    "dsp_coef.bin",
#endif
    "menu01.hps",
});

constexpr auto kMatchCommonAudio = std::to_array<std::string_view>({
    "main.ssm", "nr_select.ssm", "nr_title.ssm", "nr_name.ssm",
    "pokemon.ssm", "end.ssm", "smash2.sem",
#if !defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
    "dsp_coef.bin",
#endif
});

[[noreturn]] void reject(const char* message)
{
    throw DatError(message);
}

void add_unique(std::vector<std::string>& result, std::string_view name)
{
    if (name.empty()) reject("Asset descriptor contains an empty logical name");
    if (std::find(result.begin(), result.end(), name) == result.end())
        result.emplace_back(name);
}

// lbFileGetFullName resolves a source trailing-dot basename through the
// selected language. The native RuntimeFiles boundary is English, so the
// authored PlCaRe. identity is transferred as the exact FST name PlCaRe.usd.
std::string runtime_name(std::string_view authored)
{
    if (!authored.empty() && authored.back() == '.') {
        std::string result(authored);
        result += "usd";
        return result;
    }
    return std::string(authored);
}

const FighterCostume* source_costume(std::uint32_t fighter_kind,
                                     std::uint32_t costume_index)
{
    const auto costumes = fighter_costumes();
    const auto found = std::find_if(costumes.begin(), costumes.end(),
        [&](const FighterCostume& value) {
            return value.fighter_kind == fighter_kind &&
                   value.costume_index == costume_index;
        });
    return found == costumes.end() ? nullptr : &*found;
}

unsigned active_player_count(const MeleeWebMenuMatchSelection& selection)
{
    unsigned active = 0;
    while (active < MELEE_WEB_MENU_MAX_PLAYERS &&
           selection.start.players[active].slot_type != Gm_PKind_NA)
        ++active;
    return active;
}

void check_selection(const MeleeWebMenuMatchSelection& selection)
{
    const auto& rules = selection.start.rules;
    const unsigned active = active_player_count(selection);
    const unsigned player_count = selection.player_count != 0
                                      ? selection.player_count : active;
    if (player_count < MELEE_WEB_MENU_MIN_PLAYERS ||
        player_count > MELEE_WEB_MENU_MAX_PLAYERS || active != player_count)
        reject("Match selection requires two through four active players");
    // These are the same compatibility checks performed by
    // GameplayMatchSession::Storage::begin. The descriptor is requested after
    // the source menu has already checked full VS rules, so it does not rerun
    // rule normalization or consume any source state here.
    if (selection.hud_layout != rules.x0_3)
        reject("Match selection has an unsupported HUD layout");
    if (!melee_web_stage_content(rules.stkind))
        reject("Match selection stage has no admitted source content");

    for (unsigned i = active; i < GM_MAX_PLAYERS; ++i) {
        if (selection.start.players[i].slot_type != Gm_PKind_NA)
            reject("Match selection has an inactive source slot with data");
    }

    for (unsigned i = 0; i < active; ++i) {
        const auto& source = selection.start.players[i];
        const auto& compatibility = selection.players[i];
        const auto* content = melee_web_fighter_content(source.ckind);
        if (!content || source.stocks < 1 || source.stocks > 5 ||
            (source.slot != 0 && source.slot - 1 != i) ||
            source.color >= content->costumes || source.sub_color > 4 ||
            compatibility.controller != (source.slot ? source.slot - 1u : i) ||
            compatibility.controller != i ||
            compatibility.stocks != static_cast<std::uint32_t>(source.stocks) ||
            compatibility.costume != source.color ||
            compatibility.sub_color != source.sub_color)
            reject("Match selection contains an unsupported fighter or port");
        if (!source_costume(content->fighter_kind, 0) ||
            !source_costume(content->fighter_kind, source.color))
            reject("Match selection costume is absent from the source registry");
    }
}

struct StageMusicWords {
    int stage_kind;
    std::array<int, 4> words; // StageParam x4, x8, xC, x10
};

// Values are the authored GALE01r2 Gr* grGroundParam rows consumed by
// Ground_801C24F8 (the source x4/x8/xC/x10 words; see the pinned
// .deps/melee/src/melee/gr/ground.c:1342-1485). The filename lookup below
// uses the pinned lbAudioAx hps_files IDs from
// .deps/melee/src/melee/lb/lbaudio_ax.static.h:250-275. Keeping the words
// here makes the descriptor independent of fn_8016E5C0: it enumerates every
// possible source candidate without selecting music, touching save unlocks,
// or consuming RNG.
constexpr std::array<StageMusicWords, 7> kStageMusicWords = {{
    {St_Kind_Last,      {78, 39, 78, 39}},
    {St_Kind_Battle,    {81, 38, 81, 38}},
    {St_Kind_Story,     {96, -1, 96, -1}},
    {St_Kind_OldPupupu, {58, -1, 58, -1}},
    {St_Kind_Shrine,    {75, 1, 75, 1}},
    {St_Kind_Izumi,     {49, -1, 49, -1}},
    {St_Kind_OldYoshi,  {59, -1, 59, -1}},
}};

std::string_view music_file(int id)
{
    // Only IDs reachable from the admitted StageParam rows are listed. Their
    // names are the exact entries at those indices in lbAudioAx's source
    // hps_files[] table; no alternate path is inferred from the stage title.
    switch (id) {
    case 1:  return "akaneia.hps";
    case 38: return "hyaku.hps";
    case 39: return "hyaku2.hps";
    case 49: return "izumi.hps";
    case 58: return "old_kb.hps";
    case 59: return "old_ys.hps";
    case 75: return "shrine.hps";
    case 78: return "sp_end.hps";
    case 81: return "sp_zako.hps";
    case 96: return "ystory.hps";
    default: return {};
    }
}

void add_stage_music(std::vector<std::string>& result, int stage_kind)
{
    const auto row = std::find_if(kStageMusicWords.begin(), kStageMusicWords.end(),
        [&](const StageMusicWords& value) { return value.stage_kind == stage_kind; });
    if (row == kStageMusicWords.end())
        reject("Stage music candidates are missing for the admitted source stage");
    for (const int id : row->words) {
        if (id < 0) continue;
        const auto name = music_file(id);
        if (name.empty()) reject("Source stage music ID has no HPS path");
        add_unique(result, name);
    }
}

void add_selection_fighter_assets(std::vector<std::string>& result,
                                  const MeleeWebMenuMatchSelection& selection)
{
    // Both the match and the Results scene that follows it resolve the same
    // source fighter identities, so the descriptor keeps one loop here.
    const unsigned active = active_player_count(selection);
    std::vector<unsigned> fighter_kinds;
    for (unsigned i = 0; i < active; ++i) {
        const auto* content = melee_web_fighter_content(selection.start.players[i].ckind);
        if (std::find(fighter_kinds.begin(), fighter_kinds.end(),
                      content->fighter_kind) == fighter_kinds.end())
            fighter_kinds.push_back(content->fighter_kind);
    }
    for (const auto fighter_kind : fighter_kinds) {
        const auto* content = melee_web_fighter_content_by_kind(fighter_kind);
        const auto* neutral = source_costume(fighter_kind, 0);
        if (!content || !neutral) reject("Source fighter identity is incomplete");
        add_unique(result, neutral->fighter_filename);
        add_unique(result, neutral->animation_filename);
        add_unique(result, runtime_name(neutral->model_filename));
        add_unique(result, content->effect_archive);
        add_unique(result, content->audio_bank);
        for (unsigned i = 0; i < active; ++i) {
            if (content->fighter_kind !=
                melee_web_fighter_content(selection.start.players[i].ckind)->fighter_kind)
                continue;
            const auto* costume = source_costume(fighter_kind,
                                                  selection.start.players[i].color);
            if (!costume) reject("Selected source costume is incomplete");
            add_unique(result, runtime_name(costume->model_filename));
        }
    }
}

} // namespace

std::vector<std::string> menu_asset_names()
{
    std::vector<std::string> result;
    result.reserve(kMenuFiles.size());
    for (const auto name : kMenuFiles) add_unique(result, name);
    for (const auto& name : menu_audio_bank_names()) add_unique(result, name);
    return result;
}

std::vector<std::string> menu_audio_bank_names()
{
    std::vector<std::string> result;
    for (const auto name : {"main.ssm", "nr_select.ssm", "nr_title.ssm",
                            "nr_name.ssm", "pokemon.ssm", "end.ssm"})
        add_unique(result, name);
    // mnCharSel_Scene_OnExit uses lbAudioAx_80026E84 for each current source
    // selection, then starts the original bank preload. Unload can reach that
    // path too; retaining only the previous match's voices is insufficient.
    for (int kind=0;kind<CKIND_PLAYABLE_COUNT;++kind)
        if (const auto* fighter=melee_web_fighter_content(kind))
            add_unique(result, fighter->audio_bank);
    for (int kind=St_Kind_Izumi;kind<=St_Kind_Last;++kind)
        if (const auto* stage=melee_web_stage_content(kind);stage&&stage->audio_bank)
            add_unique(result, stage->audio_bank);
    return result;
}

std::vector<std::string>
match_asset_names(const MeleeWebMenuMatchSelection& selection)
{
    check_selection(selection);

    std::vector<std::string> result;
    result.reserve(64);
    for (const auto name : std::array<std::string_view, 11>{
             "PlCo.dat", "ItCo.usd", "EfCoData.dat", "PdPm.dat", "LbRb.dat",
             "sislib_font.bin", "IfAll.usd", "IfCoGet.dat", "SdIntro.dat",
             "GmPause.usd", "LbBf.dat"})
        add_unique(result, name);
    for (const auto name : kMatchCommonAudio) add_unique(result, name);

    add_selection_fighter_assets(result, selection);

    const auto* stage = melee_web_stage_content(selection.start.rules.stkind);
    add_unique(result, stage->archive);
    if (stage->audio_bank) add_unique(result, stage->audio_bank);
    add_stage_music(result, stage->stage_kind);
    return result;
}

std::vector<std::string>
results_asset_names(const MeleeWebMenuMatchSelection& selection)
{
    check_selection(selection);

    std::vector<std::string> result;
    result.reserve(64);
    // gmResult runs over the same source world as the match it reports on, so
    // the shared item/effect/HUD compatibility files are requested again.
    for (const auto name : std::array<std::string_view, 11>{
             "PlCo.dat", "ItCo.usd", "EfCoData.dat", "PdPm.dat", "LbRb.dat",
             "sislib_font.bin", "IfAll.usd", "IfCoGet.dat", "SdIntro.dat",
             "GmPause.usd", "LbBf.dat"})
        add_unique(result, name);
    for (const auto name : kMatchCommonAudio) add_unique(result, name);
    add_selection_fighter_assets(result, selection);
    // The source scene loader owns these roots; no stage archive or stage
    // music is requested because Results has no stage.
    for (const auto* name : {"GmRst.usd", "SdRst.usd", "TyDatai.usd",
                             "LbMcGame.usd", "NtMemAc.usd"})
        add_unique(result, name);
    {
        const unsigned active = active_player_count(selection);
        for (unsigned i = 0; i < active; ++i) {
            const auto* content =
                melee_web_fighter_content(selection.start.players[i].ckind);
            const auto spec = result_motion_archive_spec(content->fighter_kind);
            if (spec.archive.empty())
                reject("Results fighter has no authored gm_1601 result archive");
            add_unique(result, spec.archive);
        }
    }
    // Authored ckind victory themes in gm_1601; the winner is chosen by the
    // source Results callbacks after the scene runs.
    for (const auto* name : {"ff_mario.hps", "ff_fox.hps", "ff_emb.hps",
                             "ff_link.hps"})
        add_unique(result, name);
    return result;
}

std::vector<std::string> prize_asset_names()
{
    std::vector<std::string> result;
    result.reserve(24);
    // Prize runs its own source world over the authored IfPrize/SdPrize
    // scene data, the trophy tables and the card/name-entry archives.
    for (const auto* name : {"IfPrize.usd", "SdPrize.usd", "TyDatai.usd",
                             "LbMcGame.usd", "NtMemAc.usd", "sislib_font.bin"})
        add_unique(result, name);
    for (const auto name : kMatchCommonAudio) add_unique(result, name);
    // Toy_803124BC plays the three authored s_info voice streams.
    for (const auto* name : {"s_info1.hps", "s_info2.hps", "s_info3.hps"})
        add_unique(result, name);
    return result;
}

} // namespace melee_web
