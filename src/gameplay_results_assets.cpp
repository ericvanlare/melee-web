#include "gameplay_results_assets.hpp"

#include "dat_archive.hpp"
#include "dat_trophy_data.hpp"
#include "gameplay_result_motion_table.hpp"
#include "runtime_archive_cache.hpp"
#include "gameplay_archive_sections.h"
#include "gameplay_compat.h"
#include "dat_menu_support.hpp"
#include <melee/ft/forward.h>
#include <melee/ty/types.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>

namespace melee_web {
namespace {

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

std::shared_ptr<const DatArchive> read_archive(const RuntimeFiles& files,
                                                RuntimeArchiveCache* cache,
                                                std::string_view name,
                                                DatExternalPolicy policy)
{
    const auto found = files.find(name);
    if (found == files.end() || found->second.empty())
        throw DatError("Missing original Results archive: " + std::string(name));
    try {
        return cache ? cache->archive(name, policy) :
            std::make_shared<const DatArchive>(found->second, policy);
    } catch (const DatError& error) {
        throw DatError("Results archive " + std::string(name) + ": " + error.what());
    }
}

const DatPublicSymbol& public_symbol(const DatArchive& archive,
                                     std::string_view name)
{
    const auto found = std::find_if(archive.public_symbols().begin(),
                                    archive.public_symbols().end(),
                                    [&](const auto& symbol) {
                                        return symbol.name == name;
                                    });
    if (found == archive.public_symbols().end())
        throw DatError("Results archive public symbol is missing: " + std::string(name));
    return *found;
}

using ResultArchiveSpec = ResultMotionArchiveSpec;

ResultArchiveSpec result_archive_spec(std::uint32_t fighter_kind)
{
    const auto spec = result_motion_archive_spec(fighter_kind);
    if (spec.archive.empty())
        throw DatError("Results fighter has no authored gm_1601 result archive");
    return spec;
}

bool is_result_action(std::string_view symbol)
{
    // The ftData + 0x14 table also contains Intro/Ending/Stage demo rows.
    // gmResult consumes only the authored Win/Selected/Lose result domain.
    return symbol.find("_ACTION_Win") != std::string_view::npos ||
           symbol.find("_ACTION_Selected") != std::string_view::npos ||
           symbol.find("_ACTION_Lose") != std::string_view::npos;
}

std::shared_ptr<const DatArchive> nested_result_archive(
    const std::shared_ptr<const DatArchive>& outer,
    const DatFighterAction& action)
{
    const auto bytes = outer->data();
    require(action.container_offset <= bytes.size() &&
                action.archive_bytes <= bytes.size() - action.container_offset,
            "Result demo archive range exceeds GmRstM data");
    const auto selected = bytes.subspan(action.container_offset,
                                        action.archive_bytes);
    return std::make_shared<const DatArchive>(selected);
}

} // namespace

struct GameplayResultsAssets::Storage {
    struct FighterResult {
        std::string archive_name;
        std::string root_symbol;
        std::shared_ptr<const DatArchive> archive;
        std::vector<GameplayResultDemoClip> clips;
    };

    std::map<std::string, std::shared_ptr<const DatArchive>, std::less<>> archives;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> snapshots;
    std::map<std::uint32_t, FighterResult> fighters;
    RuntimeArchiveCache* archive_cache = nullptr;
    std::unique_ptr<DatScene> panel;
    std::unique_ptr<DatScene> film;
    std::unique_ptr<DatSis> text;
    std::unique_ptr<DatTrophyData> trophy_data;
    std::unique_ptr<DatMenuSupport> card_icons;
    std::unique_ptr<DatMenuSupport> card_scene;
    std::vector<TrophyData> trophy_models, trophy_models_d;
    std::vector<ToyNameData> trophy_names;
    std::vector<TyDspEntry> trophy_display, trophy_display_us;
    std::uint32_t primary_kind = 0;
    MeleeWebArchiveSections* scope = nullptr;

    void load_archive(const RuntimeFiles& files, std::string_view name,
                      DatExternalPolicy policy = DatExternalPolicy::Reject)
    {
        if (archives.contains(name)) return;
        auto archive = read_archive(files, archive_cache, name, policy);
        if (!archive_cache)
            snapshots[std::string(name)] = {archive->data().begin(), archive->data().end()};
        archives.emplace(name, std::move(archive));
    }

    void build_fighter_result(const FighterCostume& identity,
                              const ResultArchiveSpec spec)
    {
        const auto kind = identity.fighter_kind;
        auto& result = fighters.at(kind);
        const auto& motion_root = public_symbol(*result.archive, spec.root);
        require(motion_root.data_offset == 0,
                "Result motion root is not the authored archive base");
        const DatFighterActions source_actions(
            *archives.at(std::string(identity.fighter_filename)), identity,
            0x14, fighter_demo_motion_count(kind));
        std::set<std::string> found;
        for (const auto& action : source_actions.actions) {
            if (!is_result_action(action.symbol)) continue;
            auto nested = nested_result_archive(result.archive, action);
            const auto& root = public_symbol(*nested, action.symbol);
            auto animation = std::make_shared<const DatAnimation>(
                *nested, root.data_offset, DatAnimationPolicy::NativeFighterAction);
            require(found.insert(action.symbol).second,
                    "Result demo symbol has duplicate source ownership");
            result.clips.push_back({action.motion_id, action.symbol,
                                    std::move(nested), std::move(animation)});
        }
        require(!result.clips.empty(), "Result fighter has no authored result clips");
        std::sort(result.clips.begin(), result.clips.end(),
                  [](const auto& a, const auto& b) {
                      return a.motion_id < b.motion_id;
                  });
        for (std::size_t i = 1; i < result.clips.size(); ++i)
            require(result.clips[i - 1].motion_id != result.clips[i].motion_id,
                    "Result demo rows alias a duplicate source motion id");
    }

    void start(const RuntimeFiles& files, RuntimeArchiveCache* cache,
               std::span<const FighterCostume> identities)
    {
        require(!identities.empty(), "Results requires at least one fighter identity");
        archive_cache = cache;
        primary_kind = identities.front().fighter_kind;
        for (const auto* name : {"GmRst.usd", "SdRst.usd", "TyDatai.usd",
                                 "LbMcGame.usd", "NtMemAc.usd"})
            load_archive(files, name);

        std::map<std::uint32_t, const FighterCostume*> selected;
        for (const auto& identity : identities) {
            const auto [it, inserted] = selected.emplace(identity.fighter_kind, &identity);
            if (!inserted) continue;
            const auto spec = result_archive_spec(identity.fighter_kind);
            load_archive(files, spec.archive);
            // The fighter root follows lbArchive_InitializeDAT, just as it
            // does in GameplayWorld. That source loader validates and clears
            // external chains before publishing the demo/action tables.
            load_archive(files, identity.fighter_filename,
                         DatExternalPolicy::ResolveNull);
            fighters.emplace(identity.fighter_kind, FighterResult{
                std::string(spec.archive), std::string(spec.root),
                archives.at(std::string(spec.archive)), {}});
        }
        require(selected.contains(primary_kind),
                "Primary Results fighter identity was not retained");

        panel = std::make_unique<DatScene>(archives.at("GmRst.usd"), "pnlsce");
        film = std::make_unique<DatScene>(archives.at("GmRst.usd"), "flmsce");
        text = std::make_unique<DatSis>(archives.at("SdRst.usd"), "SIS_ResultData");
        card_icons = std::make_unique<DatMenuSupport>(
            archives.at("LbMcGame.usd"), DatMenuSupportKind::CardIcons);
        card_scene = std::make_unique<DatMenuSupport>(
            archives.at("NtMemAc.usd"), DatMenuSupportKind::CardScene);
        trophy_data = std::make_unique<DatTrophyData>(archives.at("TyDatai.usd"));
        auto models = [](auto entries) {
            std::vector<TrophyData> result;
            for (const auto& row : entries)
                result.push_back({row.id, row.x04, row.x08, row.x0c, row.x10,
                                  row.x14, row.x18, row.x1c, row.x20, row.x21,
                                  row.x22, row.x23});
            return result;
        };
        trophy_models = models(trophy_data->init_model_table());
        trophy_models_d = models(trophy_data->init_model_d_table());
        for (const auto& row : trophy_data->model_sort_table())
            trophy_names.push_back({row.x0, row.x2, row.x4, row.x6, row.x8, row.xa});
        auto displays = [](auto entries) {
            std::vector<TyDspEntry> result;
            for (const auto& row : entries)
                result.push_back({row.x00, row.x04, row.x05, {row.pad06, row.pad07},
                                  row.x08, row.x0c});
            return result;
        };
        trophy_display = displays(trophy_data->display_model_table());
        trophy_display_us = displays(trophy_data->display_model_us_table());

        for (const auto& [kind, identity] : selected)
            build_fighter_result(*identity, result_archive_spec(kind));

        std::vector<MeleeWebArchiveSymbol> symbols;
        symbols.reserve(12 + fighters.size());
        symbols.push_back({"GmRst.usd", "pnlsce", panel->descriptor()});
        symbols.push_back({"GmRst.usd", "flmsce", film->descriptor()});
        symbols.push_back({"SdRst.usd", "SIS_ResultData", text->descriptor()});
        symbols.push_back({"LbMcGame.usd", "MemCardIconData", card_icons->descriptor()});
        symbols.push_back({"NtMemAc.usd", "ScNtcCommon_scene_data", card_scene->descriptor()});
        for (const auto& [kind, result] : fighters) {
            (void) kind;
            symbols.push_back({result.archive_name.c_str(), result.root_symbol.c_str(),
                               const_cast<std::uint8_t*>(result.archive->data().data())});
        }
        symbols.push_back({"TyDatai.usd", "tyInitModelTbl", trophy_models.data()});
        symbols.push_back({"TyDatai.usd", "tyInitModelDTbl", trophy_models_d.data()});
        symbols.push_back({"TyDatai.usd", "tyModelSortTbl", trophy_names.data()});
        symbols.push_back({"TyDatai.usd", "tyExpDifferentTbl",
                           const_cast<std::int16_t*>(trophy_data->exp_different_table().data())});
        symbols.push_back({"TyDatai.usd", "tyNoGetUsTbl",
                           const_cast<std::int16_t*>(trophy_data->no_get_us_table().data())});
        symbols.push_back({"TyDatai.usd", "tyDisplayModelTbl", trophy_display.data()});
        symbols.push_back({"TyDatai.usd", "tyDisplayModelUsTbl", trophy_display_us.data()});
        char error[256]{};
        scope = melee_web_archive_sections_register_heap(
            symbols.data(), symbols.size(), error, sizeof(error));
        if (!scope) throw DatError(error);
    }

    void verify() const
    {
        for (const auto& [name, archive] : archives) {
            if (archive_cache) {
                archive_cache->verify(archive);
                continue;
            }
            const auto expected = snapshots.find(name);
            require(expected != snapshots.end(), "Results archive snapshot is missing");
            const auto bytes = archive->data();
            if (!std::equal(bytes.begin(), bytes.end(), expected->second.begin(),
                            expected->second.end()))
                throw DatError("Source mutated immutable Results archive: " + name);
        }
    }

    void close()
    {
        if (scope) {
            char error[256]{};
            if (!melee_web_archive_sections_close(scope, error, sizeof(error)))
                throw DatError(error);
            scope = nullptr;
        }
        verify();
        fighters.clear();
        trophy_data.reset();
        card_scene.reset();
        card_icons.reset();
        text.reset();
        film.reset();
        panel.reset();
        trophy_models.clear();
        trophy_models_d.clear();
        trophy_names.clear();
        trophy_display.clear();
        trophy_display_us.clear();
        archives.clear();
    }

    ~Storage()
    {
        try { close(); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Original Results asset teardown: %s\n", error.what());
            std::abort();
        }
    }
};

GameplayResultsAssets::GameplayResultsAssets(const RuntimeFiles& files,
                                             const FighterCostume& identity)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, nullptr, std::span<const FighterCostume>(&identity, 1));
}

GameplayResultsAssets::GameplayResultsAssets(const RuntimeFiles& files,
                                             RuntimeArchiveCache& cache,
                                             const FighterCostume& identity)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, &cache, std::span<const FighterCostume>(&identity, 1));
}

GameplayResultsAssets::GameplayResultsAssets(const RuntimeFiles& files,
                                             std::span<const FighterCostume> identities)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, nullptr, identities);
}

GameplayResultsAssets::GameplayResultsAssets(const RuntimeFiles& files,
                                             RuntimeArchiveCache& cache,
                                             std::span<const FighterCostume> identities)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, &cache, identities);
}

GameplayResultsAssets::~GameplayResultsAssets() = default;

const DatScene& GameplayResultsAssets::panel_scene() const { return *storage_->panel; }
const DatScene& GameplayResultsAssets::film_scene() const { return *storage_->film; }
const DatSis& GameplayResultsAssets::sis() const { return *storage_->text; }

std::shared_ptr<const DatArchive>
GameplayResultsAssets::result_motion_archive() const noexcept
{
    const auto found = storage_->fighters.find(storage_->primary_kind);
    return found == storage_->fighters.end() ? nullptr : found->second.archive;
}

std::span<const GameplayResultDemoClip>
GameplayResultsAssets::demo_clips() const noexcept
{
    const auto found = storage_->fighters.find(storage_->primary_kind);
    return found == storage_->fighters.end() ? std::span<const GameplayResultDemoClip>{} :
                                               found->second.clips;
}

const GameplayResultDemoClip&
GameplayResultsAssets::demo_clip(std::uint32_t motion_id) const
{
    return demo_clip(storage_->primary_kind, motion_id);
}

std::shared_ptr<const DatArchive>
GameplayResultsAssets::result_motion_archive(std::uint32_t fighter_kind) const
{
    const auto found = storage_->fighters.find(fighter_kind);
    if (found == storage_->fighters.end())
        throw DatError("Requested Results fighter archive was not constructed");
    return found->second.archive;
}

std::span<const GameplayResultDemoClip>
GameplayResultsAssets::demo_clips(std::uint32_t fighter_kind) const
{
    const auto found = storage_->fighters.find(fighter_kind);
    if (found == storage_->fighters.end())
        throw DatError("Requested Results fighter clips were not constructed");
    return found->second.clips;
}

const GameplayResultDemoClip&
GameplayResultsAssets::demo_clip(std::uint32_t fighter_kind,
                                 std::uint32_t motion_id) const
{
    const auto clips = demo_clips(fighter_kind);
    const auto found = std::find_if(clips.begin(), clips.end(),
                                    [&](const auto& clip) {
                                        return clip.motion_id == motion_id;
                                    });
    if (found == clips.end())
        throw DatError("Requested Results demo motion is not authored");
    return *found;
}

void GameplayResultsAssets::close() { storage_->close(); }
void GameplayResultsAssets::verify() const { storage_->verify(); }

} // namespace melee_web
