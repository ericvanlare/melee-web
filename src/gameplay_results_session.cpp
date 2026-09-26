#include "gameplay_results_session.hpp"
#include "gameplay_results_context.h"
#include "gameplay_results_assets.hpp"
#include "gameplay_hud_assets.hpp"
#include "gameplay_audio_bank.hpp"
#include "gameplay_audio_stream_asset.hpp"
#include "gameplay_content.h"
extern "C" {
#include <melee/gm/gm_1601.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/types.h>
}
#include <algorithm>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace melee_web {
namespace {

void check(bool value, const char* error)
{
    if (!value) throw std::runtime_error(error);
}

struct VictoryMusic {
    std::uint32_t source_id;
    const char* path;
};

const VictoryMusic* victory_music(int ckind)
{
    // Authored ckind_victory_themes in gm_1601.c, followed by the hps table
    // in lbaudio_ax.static.h. The IDs are retained as source evidence even
    // though GameplayAudioStream receives the corresponding file bytes.
    switch (ckind) {
    case CKIND_MARIO:
    case CKIND_DRMARIO: {
        static constexpr VictoryMusic music{0x16, "/audio/ff_mario.hps"};
        return &music;
    }
    case CKIND_GAMEWATCH: {
        static constexpr VictoryMusic music{0x0f, "/audio/ff_flat.hps"};
        return &music;
    }
    case CKIND_KIRBY: {
        static constexpr VictoryMusic music{0x14, "/audio/ff_kirby.hps"};
        return &music;
    }
    case CKIND_POPONANA: {
        static constexpr VictoryMusic music{0x13, "/audio/ff_ice.hps"};
        return &music;
    }
    case CKIND_SAMUS: {
        static constexpr VictoryMusic music{0x19, "/audio/ff_samus.hps"};
        return &music;
    }
    case CKIND_YOSHI: {
        static constexpr VictoryMusic music{0x1d, "/audio/ff_yoshi.hps"};
        return &music;
    }
    case CKIND_FOX:
    case CKIND_FALCO: {
        static constexpr VictoryMusic music{0x10, "/audio/ff_fox.hps"};
        return &music;
    }
    case CKIND_MARS:
    case CKIND_EMBLEM: {
        static constexpr VictoryMusic music{0x0e, "/audio/ff_emb.hps"};
        return &music;
    }
    case CKIND_LINK:
    case CKIND_CLINK:
    case CKIND_GANON:
    case CKIND_ZELDA:
    case CKIND_SEAK: {
        static constexpr VictoryMusic music{0x15, "/audio/ff_link.hps"};
        return &music;
    }
    case CKIND_CAPTAIN: {
        static constexpr VictoryMusic music{0x11, "/audio/ff_fzero.hps"};
        return &music;
    }
    case CKIND_DONKEY: {
        static constexpr VictoryMusic music{0x0d, "/audio/ff_dk.hps"};
        return &music;
    }
    case CKIND_KOOPA:
    case CKIND_LUIGI:
    case CKIND_PEACH: {
        static constexpr VictoryMusic music{0x16, "/audio/ff_mario.hps"};
        return &music;
    }
    case CKIND_NESS: {
        static constexpr VictoryMusic music{0x17, "/audio/ff_nes.hps"};
        return &music;
    }
    case CKIND_MEWTWO:
    case CKIND_PIKACHU:
    case CKIND_PICHU:
    case CKIND_PURIN: {
        static constexpr VictoryMusic music{0x18, "/audio/ff_poke.hps"};
        return &music;
    }
    default: return nullptr;
    }
}

int source_result_player(const MatchEnd& match_end)
{
    // gmResult calls fn_80165418 from fn_801771C0 for the non-team Results
    // path. Keep that source routine as the winner selector instead of
    // reproducing its standings policy here.
    return fn_80165418(const_cast<MatchEnd*>(&match_end));
}

} // namespace

struct GameplayResultsSession::Storage {
    std::unique_ptr<GameplayWorld> world;
    std::unique_ptr<GameplayResultsAssets> assets;
    std::unique_ptr<GameplayHudAssets> hud_assets;
    std::unique_ptr<GameplayAudioBank> bank;
    std::unique_ptr<GameplayAudioStream> music;
    MeleeWebResultsContext* context = nullptr;

    void start(const RuntimeFiles& files, const ResultsMatchInfo& result,
               uint32_t seed, const MeleeWebPadState& input)
    {
        GameplayWorldSelection selection;
        selection.purpose = GameplayWorldPurpose::Results;
        selection.player_count = 0;
        std::vector<FighterCostume> identities;
        for (unsigned i = 0; i < 4; ++i) {
            const auto& player = result.match_end.player_standings[i];
            if (player.slot_type == Gm_PKind_NA) continue;
            check(i == selection.player_count,
                  "Results participants are not contiguous in source standings");
            const auto* fighter = melee_web_fighter_content(player.ckind);
            check(fighter && player.x3 < fighter->costumes,
                  "Results costume is unavailable");
            selection.fighter_kinds[i] = fighter->fighter_kind;
            selection.costume_indices[i] = player.x3;
            ++selection.player_count;
            for(unsigned identity_index=0;
                identity_index<melee_web_fighter_kind_count(player.ckind);++identity_index){
                const auto kind=static_cast<unsigned>(melee_web_fighter_kind_at(player.ckind,identity_index));
                const auto identity = std::find_if(
                    fighter_costumes().begin(), fighter_costumes().end(),
                    [&](const auto& costume) {
                        return costume.fighter_kind == kind && costume.costume_index == 0;
                    });
                check(identity != fighter_costumes().end(),
                      "Results source fighter identity is unavailable");
                if (std::none_of(identities.begin(), identities.end(),
                                 [&](const auto& value) {
                                     return value.fighter_kind == identity->fighter_kind;
                                 }))
                    identities.push_back(*identity);
            }
        }
        check(selection.player_count >= 2,
              "Results requires at least two participants");
        check(result.match_end.is_teams == 0,
              "Results session currently requires the source non-team path");

        world = std::make_unique<GameplayWorld>(files, selection);
        assets = std::make_unique<GameplayResultsAssets>(
            files, std::span<const FighterCostume>(identities));
        hud_assets = std::make_unique<GameplayHudAssets>(files);
        for (const auto& identity : identities)
            world->install_result_demo(identity.fighter_kind,
                                        assets->result_motion_archive(identity.fighter_kind));

        std::vector<std::span<const uint8_t>> banks;
        std::set<std::string_view> bank_names;
        auto add_bank = [&](std::string_view name) {
            if (!bank_names.insert(name).second) return;
            const auto found = files.find(name);
            check(found != files.end() && !found->second.empty(),
                  "Results audio bank is unavailable");
            banks.emplace_back(found->second);
        };
        for (const auto name : {"main.ssm", "nr_select.ssm", "nr_title.ssm",
                                "nr_name.ssm", "pokemon.ssm", "end.ssm"})
            add_bank(name);
        for (const auto& player : result.match_end.player_standings) {
            if (player.slot_type == Gm_PKind_NA) continue;
            const auto* fighter = melee_web_fighter_content(player.ckind);
            check(fighter != nullptr, "Results fighter audio mapping is unavailable");
            for(unsigned identity=0;identity<melee_web_fighter_kind_count(player.ckind);++identity){
                const auto* owner=melee_web_fighter_content_by_kind(
                    melee_web_fighter_kind_at(player.ckind,identity));
                add_bank(owner->audio_bank);
            }
        }
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
        bank = std::make_unique<GameplayAudioBank>(files.at("smash2.sem"), banks,
                                                    std::span<const uint8_t>{});
#else
        bank = std::make_unique<GameplayAudioBank>(files.at("smash2.sem"), banks,
                                                    files.at("dsp_coef.bin"));
#endif
        char error[256]{};
        check(melee_web_audio_enable_effects(bank->get(), error, sizeof(error)), error);

        const bool canceled = gm_WasMatchCanceled(result.match_end.outcome);
        if (!canceled) {
            const int player = source_result_player(result.match_end);
            check(player >= 0 && player < 4,
                  "Results source winner player is unavailable");
            const auto* theme = victory_music(result.match_end.player_standings[player].ckind);
            check(theme != nullptr, "Results source victory music is unavailable");
            const auto slash = std::string_view(theme->path).find_last_of('/');
            check(slash != std::string_view::npos,
                  "Results victory music path is malformed");
            const auto filename = std::string_view(theme->path).substr(slash + 1);
            const auto found = files.find(filename);
            check(found != files.end() && !found->second.empty(),
                  "Results victory music asset is unavailable");
            music = std::make_unique<GameplayAudioStream>(bank->get(), theme->path,
                                                           found->second);
        }

        context = melee_web_results_context_begin(&result, seed, &input, bank->get(),
                                                   error, sizeof(error));
        check(context, error);
        world->verify_result_source_loads();
    }

    void close()
    {
        char error[256]{};
        if (context) {
            check(melee_web_results_context_end(context, error, sizeof(error)), error);
            context = nullptr;
        }
        music.reset();
        if (world) {
            world->verify_immutable_archives();
            world->close();
            world.reset();
        }
        if (assets) {
            assets->close();
            assets.reset();
        }
        if (hud_assets) {
            hud_assets->close();
            hud_assets.reset();
        }
        bank.reset();
    }

    ~Storage()
    {
        try { close(); }
        catch (const std::exception& error) {
            std::fprintf(stderr, "Results session teardown: %s\n", error.what());
            std::abort();
        }
    }
};

GameplayResultsSession::GameplayResultsSession(const RuntimeFiles& files,
                                               const ResultsMatchInfo& result,
                                               uint32_t seed,
                                               const MeleeWebPadState& input)
    : storage_(std::make_unique<Storage>())
{
    storage_->start(files, result, seed, input);
}

GameplayResultsSession::~GameplayResultsSession() = default;

void GameplayResultsSession::tick(const PADStatus raw[4])
{
    char error[256]{};
    check(melee_web_results_context_tick(storage_->context, raw, error, sizeof(error)), error);
}

void GameplayResultsSession::draw()
{
    char error[256]{};
    check(melee_web_results_context_draw(storage_->context, error, sizeof(error)), error);
}

void GameplayResultsSession::exit_scene()
{
    char error[256]{};
    check(melee_web_results_context_exit(storage_->context, error, sizeof(error)), error);
}

int GameplayResultsSession::requested() const
{
    return melee_web_results_context_requested(storage_->context);
}

uint32_t GameplayResultsSession::random_seed() const
{
    return melee_web_results_context_random_seed(storage_->context);
}

uint32_t GameplayResultsSession::source_frames() const
{
    return melee_web_results_context_ticks(storage_->context);
}

MeleeWebAudio* GameplayResultsSession::audio() const
{
    return storage_->bank ? storage_->bank->get() : nullptr;
}

void GameplayResultsSession::close() { storage_->close(); }

} // namespace melee_web
