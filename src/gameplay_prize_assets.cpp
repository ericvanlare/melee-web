#include "gameplay_prize_assets.hpp"

#include "gameplay_archive_sections.h"
#include "gameplay_compat.h"
#include "runtime_archive_cache.hpp"

#include <melee/ft/forward.h>
#include <melee/ty/types.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace melee_web {
namespace {

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

std::shared_ptr<const DatArchive> read_archive(const RuntimeFiles& files,
                                                RuntimeArchiveCache* cache,
                                                std::string_view name)
{
    const auto found = files.find(name);
    if (found == files.end() || found->second.empty())
        throw DatError("Missing original Prize archive: " + std::string(name));
    try {
        return cache ? cache->archive(name) :
            std::make_shared<const DatArchive>(found->second);
    } catch (const DatError& error) {
        throw DatError("Prize archive " + std::string(name) + ": " + error.what());
    }
}

} // namespace

struct GameplayPrizeAssets::Storage {
    std::map<std::string, std::shared_ptr<const DatArchive>, std::less<>> archives;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> snapshots;
    RuntimeArchiveCache* archive_cache = nullptr;

    std::unique_ptr<DatScene> prize_scene;
    std::unique_ptr<DatSis> prize_text;
    std::unique_ptr<DatTrophyData> trophy_data;
    std::unique_ptr<DatMenuSupport> card_icons;
    std::unique_ptr<DatMenuSupport> card_scene;

    // These copies have the ABI/layout expected by Toy_803124BC's original
    // lbArchive_LoadSymbols call.  DatTrophyData remains the checked parser;
    // the copies only bridge its portable rows to source native pointers.
    std::vector<TrophyData> trophy_models, trophy_models_d;
    std::vector<ToyNameData> trophy_names;
    std::vector<TyDspEntry> trophy_display, trophy_display_us;

    MeleeWebArchiveSections* scope = nullptr;

    void load_archive(const RuntimeFiles& files, std::string_view name)
    {
        if (archives.contains(name)) return;
        auto archive = read_archive(files, archive_cache, name);
        if (!archive_cache)
            snapshots[std::string(name)] =
                {archive->data().begin(), archive->data().end()};
        archives.emplace(name, std::move(archive));
    }

    void start(const RuntimeFiles& files, RuntimeArchiveCache* cache)
    {
        archive_cache = cache;
        // The source resolves all five basenames below to these US files when
        // LANG_US is active.  No .dat fallback is allowed in this owner.
        for (const auto* name : {"IfPrize.usd", "SdPrize.usd", "TyDatai.usd",
                                 "LbMcGame.usd", "NtMemAc.usd"})
            load_archive(files, name);

        prize_scene = std::make_unique<DatScene>(
            archives.at("IfPrize.usd"), "ScInfPrize_scene_data");
        prize_text = std::make_unique<DatSis>(
            archives.at("SdPrize.usd"), "SIS_PrizeData");
        trophy_data = std::make_unique<DatTrophyData>(archives.at("TyDatai.usd"));
        card_icons = std::make_unique<DatMenuSupport>(
            archives.at("LbMcGame.usd"), DatMenuSupportKind::CardIcons);
        card_scene = std::make_unique<DatMenuSupport>(
            archives.at("NtMemAc.usd"), DatMenuSupportKind::CardScene);

        auto models = [](auto entries) {
            std::vector<TrophyData> result;
            result.reserve(entries.size());
            for (const auto& row : entries)
                result.push_back({row.id, row.x04, row.x08, row.x0c, row.x10,
                                  row.x14, row.x18, row.x1c, row.x20, row.x21,
                                  row.x22, row.x23});
            return result;
        };
        trophy_models = models(trophy_data->init_model_table());
        trophy_models_d = models(trophy_data->init_model_d_table());
        trophy_names.reserve(trophy_data->model_sort_table().size());
        for (const auto& row : trophy_data->model_sort_table())
            trophy_names.push_back({row.x0, row.x2, row.x4, row.x6, row.x8, row.xa});
        auto displays = [](auto entries) {
            std::vector<TyDspEntry> result;
            result.reserve(entries.size());
            for (const auto& row : entries)
                result.push_back({row.x00, row.x04, row.x05,
                                  {row.pad06, row.pad07}, row.x08, row.x0c});
            return result;
        };
        trophy_display = displays(trophy_data->display_model_table());
        trophy_display_us = displays(trophy_data->display_model_us_table());

        // Register every original public root before any source callback can
        // call lbArchive_LoadSymbols/lbArchive_80016DBC.  Native graph owners
        // and their source archive bytes remain alive until this scope closes.
        const MeleeWebArchiveSymbol symbols[] = {
            {"IfPrize.usd", "ScInfPrize_scene_data", prize_scene->descriptor()},
            {"SdPrize.usd", "SIS_PrizeData", prize_text->descriptor()},
            {"LbMcGame.usd", "MemCardIconData", card_icons->descriptor()},
            {"NtMemAc.usd", "ScNtcCommon_scene_data", card_scene->descriptor()},
            {"TyDatai.usd", "tyInitModelTbl", trophy_models.data()},
            {"TyDatai.usd", "tyInitModelDTbl", trophy_models_d.data()},
            {"TyDatai.usd", "tyModelSortTbl", trophy_names.data()},
            {"TyDatai.usd", "tyExpDifferentTbl",
             const_cast<std::int16_t*>(trophy_data->exp_different_table().data())},
            {"TyDatai.usd", "tyNoGetUsTbl",
             const_cast<std::int16_t*>(trophy_data->no_get_us_table().data())},
            {"TyDatai.usd", "tyDisplayModelTbl", trophy_display.data()},
            {"TyDatai.usd", "tyDisplayModelUsTbl", trophy_display_us.data()},
        };
        char error[256]{};
        scope = melee_web_archive_sections_register_heap(
            symbols, std::size(symbols), error, sizeof(error));
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
            require(expected != snapshots.end(), "Prize archive snapshot is missing");
            const auto bytes = archive->data();
            if (!std::equal(bytes.begin(), bytes.end(), expected->second.begin(),
                            expected->second.end()))
                throw DatError("Source mutated immutable Prize archive: " + name);
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
        card_scene.reset();
        card_icons.reset();
        prize_text.reset();
        prize_scene.reset();
        trophy_data.reset();
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
            std::fprintf(stderr, "Original Prize asset teardown: %s\n", error.what());
            std::abort();
        }
    }
};

GameplayPrizeAssets::GameplayPrizeAssets(const RuntimeFiles& files)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, nullptr);
}

GameplayPrizeAssets::GameplayPrizeAssets(const RuntimeFiles& files,
                                         RuntimeArchiveCache& cache)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, &cache);
}

GameplayPrizeAssets::~GameplayPrizeAssets() = default;

const DatScene& GameplayPrizeAssets::scene() const { return *storage_->prize_scene; }
const DatSis& GameplayPrizeAssets::sis() const { return *storage_->prize_text; }
const DatTrophyData& GameplayPrizeAssets::trophy_data() const
{
    return *storage_->trophy_data;
}
const DatMenuSupport& GameplayPrizeAssets::card_icons() const
{
    return *storage_->card_icons;
}
const DatMenuSupport& GameplayPrizeAssets::card_scene() const
{
    return *storage_->card_scene;
}
void GameplayPrizeAssets::close() { storage_->close(); }
void GameplayPrizeAssets::verify() const { storage_->verify(); }
void GameplayPrizeAssets::verify_immutable_archives() const { storage_->verify(); }

} // namespace melee_web
