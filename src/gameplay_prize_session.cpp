#include "gameplay_prize_session.hpp"
#include "gameplay_prize_context.h"
#include "gameplay_prize_assets.hpp"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include <array>
#include "gameplay_bootstrap.h"
#include "gameplay_font_atlas.h"
#include "hsd_native_joint.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace melee_web {
namespace {
void check(bool value, const char* error)
{ if (!value) throw std::runtime_error(error); }
}
struct GameplayPrizeSession::Storage {
    bool world = false;
    std::unique_ptr<GameplayPrizeAssets> assets;
    std::unique_ptr<GameplayAudioBank> bank;
    std::unique_ptr<GameplayAudioStream> music;
    MeleeWebFontAtlas* font = nullptr;
    MeleeWebPrizeContext* context = nullptr;
    void start(const RuntimeFiles& files, MeleeWebMenuHost* host,
               uint32_t seed, const MeleeWebPadState& input)
    {
        char error[256]{};
        check(melee_web_gameplay_startup(32U * 1024U * 1024U, error, sizeof(error)), error);
        world = true;
        check(melee_web_native_world_enable(error, sizeof(error)), error);
        assets = std::make_unique<GameplayPrizeAssets>(files);
        const auto& font_bytes = files.at("sislib_font.bin");
        font = melee_web_font_atlas_register(font_bytes.data(), font_bytes.size(), error, sizeof(error));
        check(font, error);
        std::vector<std::span<const uint8_t>> banks;
        for (const auto name : {"main.ssm", "nr_select.ssm", "nr_title.ssm",
                                "nr_name.ssm", "pokemon.ssm", "end.ssm"})
            banks.emplace_back(files.at(name));
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
        bank = std::make_unique<GameplayAudioBank>(files.at("smash2.sem"), banks,
                                                   std::span<const uint8_t>{});
#else
        bank = std::make_unique<GameplayAudioBank>(files.at("smash2.sem"), banks,
                                                   files.at("dsp_coef.bin"));
#endif
        check(melee_web_audio_enable_effects(bank->get(), error, sizeof(error)), error);
        const std::array<GameplayAudioStreamFile, 3> music_files{{
            {"/audio/s_info1.hps", files.at("s_info1.hps")},
            {"/audio/s_info2.hps", files.at("s_info2.hps")},
            {"/audio/s_info3.hps", files.at("s_info3.hps")},
        }};
        music = std::make_unique<GameplayAudioStream>(bank->get(), music_files);
        context = melee_web_prize_context_begin(host, seed, &input, bank->get(), error, sizeof(error));
        check(context, error);
    }
    void close()
    {
        char error[256]{};
        if (context) {
            check(melee_web_prize_context_end(context, error, sizeof(error)), error);
            context = nullptr;
        }
        music.reset();
        if (assets) assets->verify_immutable_archives();
        if (world) {
            check(melee_web_gameplay_shutdown(error, sizeof(error)), error);
            world = false;
        }
        if (font) {
            check(melee_web_font_atlas_close(font, error, sizeof(error)), error);
            font = nullptr;
        }
        if (assets) { assets->close(); assets.reset(); }
        bank.reset();
    }
    ~Storage()
    {
        try { close(); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "Prize session teardown: %s\n", e.what()); std::abort();
        }
    }
};
GameplayPrizeSession::GameplayPrizeSession(const RuntimeFiles& files, MeleeWebMenuHost* host,
    uint32_t seed, const MeleeWebPadState& input) : storage_(std::make_unique<Storage>())
{ storage_->start(files, host, seed, input); }
GameplayPrizeSession::~GameplayPrizeSession() = default;
void GameplayPrizeSession::tick(const PADStatus raw[4])
{ char e[256]{}; check(melee_web_prize_context_tick(storage_->context, raw, e, sizeof(e)), e); }
void GameplayPrizeSession::draw()
{ char e[256]{}; check(melee_web_prize_context_draw(storage_->context, e, sizeof(e)), e); }
void GameplayPrizeSession::exit_scene()
{ char e[256]{}; check(melee_web_prize_context_exit(storage_->context, e, sizeof(e)), e); }
int GameplayPrizeSession::requested() const
{ return melee_web_prize_context_requested(storage_->context); }
uint32_t GameplayPrizeSession::random_seed() const
{ return melee_web_prize_context_random_seed(storage_->context); }
uint32_t GameplayPrizeSession::source_frames() const
{ return melee_web_prize_context_ticks(storage_->context); }
MeleeWebAudio* GameplayPrizeSession::audio() const
{ return storage_->bank ? storage_->bank->get() : nullptr; }
void GameplayPrizeSession::close() { storage_->close(); }
}
