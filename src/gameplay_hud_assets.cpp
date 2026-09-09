#include "gameplay_hud_assets.hpp"
#include "dat_archive.hpp"
#include "dat_scene.hpp"
#include "dat_sis.hpp"
#include "gameplay_archive_sections.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace melee_web {
struct GameplayHudAssets::Storage {
    std::map<std::string, std::shared_ptr<const DatArchive>, std::less<>> archives;
    std::map<std::string, std::vector<uint8_t>, std::less<>> snapshots;
    std::vector<std::unique_ptr<DatScene>> scenes;
    std::unique_ptr<DatSis> intro;
    MeleeWebArchiveSections* scope = nullptr;

    void start(const RuntimeFiles& files)
    {
        for (const char* name : {"IfAll.usd", "IfCoGet.dat", "SdIntro.dat"}) {
            const auto found = files.find(name);
            if (found == files.end() || found->second.empty())
                throw DatError("Missing original HUD archive: " + std::string(name));
            std::shared_ptr<const DatArchive> archive;
            try { archive = std::make_shared<const DatArchive>(found->second); }
            catch (const DatError& error) {
                throw DatError(std::string("HUD archive ") + name + ": " + error.what());
            }
            snapshots[name] = {archive->data().begin(), archive->data().end()};
            archives.emplace(name, std::move(archive));
        }
        std::vector<MeleeWebArchiveSymbol> symbols;
        auto add_scene = [&](const char* filename, const char* symbol, DatSceneRootKind kind) {
            auto scene = std::make_unique<DatScene>(archives.at(filename), symbol, kind);
            void* descriptor = kind == DatSceneRootKind::SceneDesc ?
                static_cast<void*>(scene->descriptor()) : static_cast<void*>(scene->model_table());
            if (!descriptor) throw DatError("Original HUD descriptor is absent");
            symbols.push_back({filename, symbol, descriptor});
            scenes.push_back(std::move(scene));
        };
        add_scene("IfAll.usd", "ScInfDmg_scene_data", DatSceneRootKind::SceneDesc);
        for (const char* symbol : {
            "ScInfCnt_scene_models", "ScInfStc_scene_models", "ScInfTim_scene_models",
            "DmgNum_scene_models", "DmgMrk_scene_models", "Stc_scemdls",
            "Stc_rarwmdls", "ScInfPnm_scene_models", "lupe", "tdsce"}) {
            add_scene("IfAll.usd", symbol, DatSceneRootKind::DynamicModelTable);
        }
        add_scene("IfCoGet.dat", "ScInfCgt_scene_data", DatSceneRootKind::SceneDesc);
        intro = std::make_unique<DatSis>(archives.at("SdIntro.dat"), "SIS_IntroData");
        symbols.push_back({"SdIntro.dat", "SIS_IntroData", intro->descriptor()});
        char error[256]{};
        scope = melee_web_archive_sections_register_heap(symbols.data(), symbols.size(), error, sizeof(error));
        if (!scope) throw DatError(error);
    }
    void verify() const
    {
        for (const auto& [name, archive] : archives) {
            const auto bytes = archive->data();
            const auto& expected = snapshots.at(name);
            if (!std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end()))
                throw DatError("Source mutated immutable HUD archive: " + name);
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
        intro.reset();
        scenes.clear();
    }
    ~Storage()
    {
        try { close(); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Original HUD asset teardown: %s\n", error.what());
            std::abort();
        }
    }
};
GameplayHudAssets::GameplayHudAssets(const RuntimeFiles& files)
    : storage_(std::make_unique<Storage>()) { storage_->start(files); }
GameplayHudAssets::~GameplayHudAssets() = default;
void GameplayHudAssets::verify() const { storage_->verify(); }
void GameplayHudAssets::close() { storage_->close(); }
}
