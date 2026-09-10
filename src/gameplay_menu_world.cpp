#include "gameplay_compat.h"
#include "gameplay_menu_world.hpp"
#include "runtime_archive_cache.hpp"

#include "dat_archive.hpp"
#include "dat_menu_support.hpp"
#include "dat_native_menu.hpp"
#include "dat_sis.hpp"
#include "gameplay_archive_sections.h"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_bank_transport.h"
#include "gameplay_audio_residency.h"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_font_atlas.h"
#include "hsd_native_joint.h"

extern "C" {
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lbaudio_ax.h>
}

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace melee_web {
namespace {

constexpr std::size_t kWorldHeapBytes = 32U * 1024U * 1024U;

constexpr std::array<std::string_view, 21> kRequiredFiles = {
    "MnSlChr.usd", "MnSlMap.usd", "SdSlChr.usd", "MnExtAll.usd",
    "LbMcGame.usd", "NtMemAc.usd", "sislib_font.bin", "smash2.sem",
    "dsp_coef.bin", "menu01.hps", "main.ssm", "mario.ssm", "fox.ssm", "falco.ssm", "mars.ssm", "pupupu.ssm",
    "nr_select.ssm", "nr_title.ssm", "nr_name.ssm", "pokemon.ssm",
    "end.ssm",
};

constexpr std::array<std::string_view, 11> kBankFiles = {
    "main.ssm", "mario.ssm", "fox.ssm", "falco.ssm", "mars.ssm", "pupupu.ssm", "nr_select.ssm", "nr_title.ssm",
    "nr_name.ssm", "pokemon.ssm", "end.ssm",
};

[[noreturn]] void fail(const char* message)
{
    throw DatError(message ? message : "Native menu runtime operation failed");
}

void check(int ok, const char* error, const char* fallback)
{
    if (!ok) fail(error && *error ? error : fallback);
}

const std::vector<std::uint8_t>& require_file(const RuntimeFiles& files,
                                               std::string_view name)
{
    const auto it = files.find(name);
    if (it == files.end() || it->second.empty()) {
        throw DatError("Missing required native menu runtime file: " +
                       std::string(name));
    }
    return it->second;
}

} // namespace

struct GameplayMenuWorld::Storage {
    std::map<std::string, std::shared_ptr<const DatArchive>, std::less<>> archives;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> snapshots;
    RuntimeArchiveCache* archive_cache = nullptr;

    std::unique_ptr<DatNativeMenu> css;
    std::unique_ptr<DatNativeMenu> sss;
    std::unique_ptr<DatSis> sis;
    std::unique_ptr<DatMenuSupport> card_icons;
    std::unique_ptr<DatMenuSupport> card_scene;

    std::span<const std::uint8_t> sem;
    std::span<const std::uint8_t> coefficients;
    std::span<const std::uint8_t> hps;
    std::span<const std::uint8_t> font_bytes;
    std::array<std::span<const std::uint8_t>, kBankFiles.size()> bank_bytes;

    std::unique_ptr<GameplayAudioBank> audio_bank;
    std::unique_ptr<GameplayAudioStream> music;
    MeleeWebAudioResidency* residency = nullptr;
    MeleeWebArchiveSections* archive_scope = nullptr;
    MeleeWebFontAtlas* font = nullptr;

    char error[256]{};
    bool world_started = false;
    bool transport_started = false;
    bool fully_constructed = false;
    bool scene_rebuild_started = false;
    bool closed = false;

    std::shared_ptr<const DatArchive> archive(std::string_view name) const
    {
        const auto it = archives.find(name);
        if (it == archives.end()) {
            throw DatError("Native menu archive was not retained: " +
                           std::string(name));
        }
        return it->second;
    }

    void load_archives(const RuntimeFiles& files)
    {
        for (const auto name : kRequiredFiles) {
            if (name == "sislib_font.bin" || name == "smash2.sem" ||
                name == "dsp_coef.bin" || name == "menu01.hps" ||
                name.ends_with(".ssm")) {
                require_file(files, name);
                continue;
            }
            const auto& bytes = require_file(files, name);
            auto value = archive_cache ? archive_cache->archive(name) :
                                         std::make_shared<const DatArchive>(bytes);
            if (!archive_cache)
                snapshots.emplace(std::string(name),
                                  std::vector<std::uint8_t>(value->data().begin(),
                                                            value->data().end()));
            archives.emplace(std::string(name), std::move(value));
        }
    }

    void start_scene(GameplayMenuScene scene)
    {
        check(melee_web_gameplay_startup(kWorldHeapBytes, error, sizeof(error)),
              error, "Native menu SDK world startup failed");
        world_started = true;
        check(melee_web_native_world_enable(error, sizeof(error)), error,
              "Native menu HSD object lifetime setup failed");

        // These owners hydrate the exact source roots before publication. The
        // source files still own scene state, object creation, and animation.
        std::vector<MeleeWebArchiveSymbol> symbols;
        if (scene == GameplayMenuScene::Stages) {
            sss = std::make_unique<DatNativeMenu>(archive("MnSlMap.usd"),
                                                  NativeMenuKind::Stages);
            symbols.push_back(
                {"MnSlMap.usd", "MnSelectStageDataTable", sss->descriptor()});
        } else {
            css = std::make_unique<DatNativeMenu>(archive("MnSlChr.usd"),
                                                  NativeMenuKind::Characters);
            sis = std::make_unique<DatSis>(archive("SdSlChr.usd"),
                                           "SIS_SelCharData");
            card_icons = std::make_unique<DatMenuSupport>(
                archive("LbMcGame.usd"), DatMenuSupportKind::CardIcons);
            card_scene = std::make_unique<DatMenuSupport>(
                archive("NtMemAc.usd"), DatMenuSupportKind::CardScene);
            symbols = {
                {"MnSlChr.usd", "MnSelectChrDataTable", css->descriptor()},
                {"SdSlChr.usd", "SIS_SelCharData", sis->descriptor()},
                {"LbMcGame.usd", "MemCardIconData", card_icons->descriptor()},
                {"NtMemAc.usd", "ScNtcCommon_scene_data", card_scene->descriptor()},
            };
            const auto extra = archive("MnExtAll.usd");
            for (const auto& symbol : extra->public_symbols())
                symbols.push_back({"MnExtAll.usd", symbol.name.c_str(), nullptr});
        }
        archive_scope = melee_web_archive_sections_register_heap(
            symbols.data(), symbols.size(), error, sizeof(error));
        check(archive_scope != nullptr, error,
              "Native menu archive scope registration failed");

        font = melee_web_font_atlas_register(font_bytes.data(), font_bytes.size(),
                                             error, sizeof(error));
        check(font != nullptr, error, "Native menu font registration failed");
    }

    void start_audio()
    {
        if (archive_cache) {
            std::vector<std::shared_ptr<const DatAudioBank>> decoded;
            decoded.reserve(kBankFiles.size());
            for (const auto name : kBankFiles)
                decoded.push_back(archive_cache->audio_bank(name));
            audio_bank = std::make_unique<GameplayAudioBank>(
                sem, std::move(decoded), coefficients);
        } else {
            std::vector<std::span<const std::uint8_t>> bank_views;
            bank_views.reserve(bank_bytes.size());
            for (const auto& bytes : bank_bytes) bank_views.emplace_back(bytes);
            audio_bank = std::make_unique<GameplayAudioBank>(sem, bank_views,
                                                              coefficients);
        }

        residency = melee_web_audio_residency_create(error, sizeof(error));
        check(residency != nullptr, error,
              "Native menu audio residency creation failed");
        for (std::size_t i = 0; i < bank_bytes.size(); ++i) {
            const std::string path = "/audio/us/" + std::string(kBankFiles[i]);
            const MeleeWebAudioResidencyAsset asset = {
                path.c_str(), bank_bytes[i].data(), bank_bytes[i].size(),
                100 + static_cast<int>(i),
            };
            check(melee_web_audio_residency_register(residency, &asset, error,
                                                     sizeof(error)),
                  error, "Native menu SSM residency registration failed");
        }

        // The transport must own the original lbAudio scope before the HPS
        // stream is created. This ordering is required by the source DevCom
        // callbacks and makes close() able to drain both services coherently.
        check(melee_web_audio_bank_transport_begin(
                  audio_bank->get(), residency, "/audio/us/smash2.sem", error,
                  sizeof(error)),
              error, "Native menu source audio transport startup failed");
        transport_started = true;
        check(melee_web_audio_enable_effects(audio_bank->get(), error,
                                             sizeof(error)),
              error, "Native menu source audio effects setup failed");
        music = std::make_unique<GameplayAudioStream>(
            audio_bank->get(), "/audio/menu01.hps", hps);
    }

    void start(const RuntimeFiles& files, RuntimeArchiveCache* cache)
    {
        archive_cache = cache;
        load_archives(files);

        font_bytes = std::span<const std::uint8_t>{require_file(files, "sislib_font.bin")};
        sem = std::span<const std::uint8_t>{require_file(files, "smash2.sem")};
        coefficients = std::span<const std::uint8_t>{require_file(files, "dsp_coef.bin")};
        hps = std::span<const std::uint8_t>{require_file(files, "menu01.hps")};
        for (std::size_t i = 0; i < kBankFiles.size(); ++i)
            bank_bytes[i] = std::span<const std::uint8_t>{require_file(files, kBankFiles[i])};

        start_scene(GameplayMenuScene::Characters);
        start_audio();
        fully_constructed = true;
    }

    void verify() const
    {
        for (const auto& [name, source] : archives) {
            if (archive_cache) {
                archive_cache->verify(source);
                continue;
            }
            const auto expected = snapshots.find(name);
            if (expected == snapshots.end()) continue;
            const auto actual = source->data();
            if (!std::equal(actual.begin(), actual.end(), expected->second.begin(),
                            expected->second.end())) {
                throw DatError("Original source mutated immutable menu archive: " +
                               name);
            }
        }
    }

    void drain_source_audio()
    {
        if (!transport_started || !melee_web_audio_bank_transport_configured()) return;
        // The source stop/cancel transition must happen while menu01.hps still
        // owns the nested stream. Transport requests can legitimately span
        // multiple 64-transfer pumps, so continue until the original queue is
        // empty rather than treating the first partial pump as failure.
        lbAudioAx_80027DBC();
        for (unsigned attempt = 0;
             melee_web_audio_bank_transport_busy() && attempt < 4096;
             ++attempt) {
            melee_web_audio_bank_transport_pump();
        }
        if (melee_web_audio_bank_transport_busy())
            throw DatError("Native menu source audio transport did not drain");
    }

    void close_scene(bool discard_card_globals)
    {
        if (world_started) {
            check(melee_web_gameplay_shutdown(error, sizeof(error)), error,
                  "Native menu SDK world shutdown failed");
            world_started = false;
        }

        // These are the exact card globals released by the original menu
        // teardown. Constructor failure never ran source OnEnter, so its
        // rollback path intentionally skips them.
        if (discard_card_globals) {
            lb_8001D1F4();
            lb_8001C5A4();
        }

        // Source archive/font scopes close while their typed descriptors still
        // exist. This is required by the borrowed native_data contract.
        if (archive_scope) {
            check(melee_web_archive_sections_close(archive_scope, error,
                                                   sizeof(error)),
                  error, "Native menu archive scope close failed");
            archive_scope = nullptr;
        }
        if (font) {
            check(melee_web_font_atlas_close(font, error, sizeof(error)), error,
                  "Native menu font scope close failed");
            font = nullptr;
        }

        card_scene.reset();
        card_icons.reset();
        sis.reset();
        sss.reset();
        css.reset();
    }

    void begin_scene_rebuild()
    {
        if (closed || !fully_constructed || !audio_bank || !transport_started ||
            !music || scene_rebuild_started) {
            fail("Native menu scene rebuild requires a live complete audio scope");
        }
        verify();
        close_scene(true);
        scene_rebuild_started = true;
    }

    void finish_scene_rebuild(GameplayMenuScene scene)
    {
        if (closed || !scene_rebuild_started || world_started || archive_scope ||
            font) {
            fail("Native menu scene rebuild finish requires completed teardown");
        }
        start_scene(scene);
        scene_rebuild_started = false;
    }

    void rebuild_scene(GameplayMenuScene scene)
    {
        begin_scene_rebuild();
        finish_scene_rebuild(scene);
    }

    void close_impl(bool discard_card_globals)
    {
        if (closed) return;

        // On normal use the caller has already run source OnExit. Calling the
        // original stop path again is deliberate: it closes the source's
        // bank/voice transition before the host stream and transport owners.
        // A constructor rollback cannot have run source audio startup, so it
        // must only release the host transport. Normal close is explicitly
        // after the caller's OnExit and runs the original stop transition.
        if (discard_card_globals) drain_source_audio();
        music.reset();

        if (transport_started) {
            check(melee_web_audio_bank_transport_end(error, sizeof(error)),
                  error, "Native menu source audio transport close failed");
            transport_started = false;
        }
        if (residency) {
            check(melee_web_audio_residency_destroy(residency, error,
                                                    sizeof(error)),
                  error, "Native menu audio residency close failed");
            residency = nullptr;
        }
        audio_bank.reset();

        close_scene(discard_card_globals);
        closed = true;
    }

    ~Storage()
    {
        if (closed) return;
        try {
            // A partially constructed owner has not exposed a source scene;
            // only a fully constructed owner may release source card globals.
            close_impl(fully_constructed);
        } catch (const std::exception& exception) {
            std::fprintf(stderr, "Native menu teardown: %s\n", exception.what());
            std::abort();
        }
    }
};

GameplayMenuWorld::GameplayMenuWorld(const RuntimeFiles& files)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, nullptr);
}

GameplayMenuWorld::GameplayMenuWorld(const RuntimeFiles& files,
                                     RuntimeArchiveCache& cache)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, &cache);
}

GameplayMenuWorld::~GameplayMenuWorld() = default;

void GameplayMenuWorld::close()
{
    storage_->close_impl(true);
}

void GameplayMenuWorld::close_prepared()
{
    storage_->close_impl(false);
}

void GameplayMenuWorld::rebuild_scene(GameplayMenuScene scene)
{
    storage_->rebuild_scene(scene);
}

void GameplayMenuWorld::begin_scene_rebuild()
{
    storage_->begin_scene_rebuild();
}

void GameplayMenuWorld::finish_scene_rebuild(GameplayMenuScene scene)
{
    storage_->finish_scene_rebuild(scene);
}

MeleeWebAudio* GameplayMenuWorld::audio() const noexcept
{
    return storage_->audio_bank ? storage_->audio_bank->get() : nullptr;
}

void GameplayMenuWorld::verify_immutable_archives() const
{
    storage_->verify();
}

} // namespace melee_web
