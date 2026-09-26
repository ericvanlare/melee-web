#include "gameplay_compat.h"
#include "gameplay_results_assets.hpp"
#include "gameplay_results_session.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_content.h"
#include "gameplay_pad_state.h"
#include "gameplay_source_memory_runtime.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <melee/gm/types.h>
#include <melee/ef/types.h>
#include <melee/ef/efasync.h>
#include <melee/ef/efdata.h>
#include <melee/ef/eflib.h>
#include <melee/lb/lbarchive.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/types.h>
#include <sysdolphin/baselib/gobj.h>
extern EF_DAT_Entry efAsync_DatEntries[51];
extern HSD_Archive* _Toy_sbss_804D6ED0;
HSD_GObj* Player_GetEntity(s32 slot);
extern void* it_804D6D28;
extern void* it_804D6D40;
extern void* it_804D6D04;
extern void* it_804D6D20;
extern void* it_804D6D24;
extern void* it_804D6D30;
extern void* it_804D6D38;
void Item_80266FA8(void);
void Item_80266FCC(void);
}

static void load_directory(melee_web::RuntimeFiles& files,
                           const std::filesystem::path& directory)
{
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read owned Results input");
        files[entry.path().filename().string()] =
            {std::istreambuf_iterator<char>(input), {}};
    }
}

static ResultsMatchInfo make_results_match(int opponent_ckind,
                                           bool opponent_wins)
{
    ResultsMatchInfo result{};
    result.match_end.outcome = OUTCOME_ELIMINATION;
    result.match_end.match_kind = 0;
    result.match_end.is_teams = 0;
    result.match_end.n_winners = 1;
    const auto* opponent = melee_web_fighter_content(opponent_ckind);
    const auto* mario = melee_web_fighter_content(CKIND_MARIO);
    if (!opponent || !mario)
        throw std::runtime_error("Results fighter content is unavailable");
    for (auto& player : result.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    auto fill = [](auto& player, const MeleeWebFighterContent& fighter,
                   bool winner) {
        player.slot_type = Gm_PKind_Human;
        player.ckind = static_cast<s8>(fighter.character_kind);
        player.ftkind = static_cast<s8>(fighter.fighter_kind);
        player.x3 = 0;
        player.x4 = 0;
        player.is_big_loser = winner ? 0 : 1;
        player.is_small_loser = winner ? 0 : 1;
        player.team = 0;
        player.stocks = 0;
    };
    fill(result.match_end.player_standings[0], *opponent, opponent_wins);
    fill(result.match_end.player_standings[1], *mario, !opponent_wins);
    result.match_end.winners[0] = opponent_wins ? 0 : 1;
    return result;
}

static void check_results_teardown()
{
    if (melee_web_gameplay_world_exists() || HSD_GObj_Entities ||
        efAsync_DatEntries[0].data || efAsync_DatEntries[1].data)
        throw std::runtime_error("Real Results session retained source ownership");
}

static void check_results_fighter_leases(const ResultsMatchInfo& result)
{
    std::vector<std::uint32_t> source_owners;
    for (unsigned slot = 0; slot < 4; ++slot) {
        if (result.match_end.player_standings[slot].slot_type == Gm_PKind_NA)
            continue;
        auto* entity = Player_GetEntity(slot);
        if (!entity)
            throw std::runtime_error("Results participant has no source demo Fighter");
        {
            MeleeWebSourceFighterAddress lease{};
            if (!melee_web_source_memory_fighter_read(entity->user_data, &lease) ||
                !lease.live || !lease.source_address ||
                !lease.allocation_generation ||
                lease.world_generation != melee_web_gameplay_generation())
                throw std::runtime_error("Results demo Fighter has no live source lease");
            for (const auto source : source_owners)
                if (source == lease.source_address)
                    throw std::runtime_error("Results demo Fighters share a source owner");
            source_owners.push_back(lease.source_address);
        }
    }
}

static std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES> neutral_pad_snapshot()
{
    std::array<std::uint8_t, MELEE_WEB_PAD_STATE_BYTES> bytes{};
    std::size_t offset = 0;
    auto u8 = [&](std::uint8_t value) { bytes[offset++] = value; };
    auto i8 = [&](std::int8_t value) { u8(static_cast<std::uint8_t>(value)); };
    auto u32 = [&](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
            u8(static_cast<std::uint8_t>(value >> shift));
    };
    auto i32 = [&](std::int32_t value) { u32(static_cast<std::uint32_t>(value)); };
    auto f32 = [&](float value) { u32(std::bit_cast<std::uint32_t>(value)); };
    // This is the valid source pad-processing configuration installed by the
    // Results context before it applies the retained semantic state.
    i32(45); i32(8); i8(0); i8(30); f32(0.0f);
    u8(0); u8(1); i8(80); i8(0);
    u8(1); u8(140); u8(0); u8(1); u8(140); u8(0);
    i8(80); u8(140); u8(140); u8(0); u8(0); u8(0);
    if (offset != 30) throw std::runtime_error("PAD snapshot encoder drifted");
    return bytes;
}

static int run_real_roster(const melee_web::RuntimeFiles& files,
                           std::span<const int> opponents,
                           const char* roster_name, bool confirm)
{
    char error[256]{};
    if (!melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                          error, sizeof(error)))
        throw std::runtime_error(error);
    bool ended = false;
    try {
        auto input_bytes = neutral_pad_snapshot();
        std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)> input(
            melee_web_pad_state_decode(input_bytes.data(), input_bytes.size(),
                                       error, sizeof(error)),
            melee_web_pad_state_free);
        if (!input) throw std::runtime_error(error);
        PADStatus neutral[4]{};
        neutral[2].err = neutral[3].err = -1;
        unsigned completed = 0;
        for (const int opponent : opponents) {
            for (const bool opponent_wins : {true, false}) {
                const auto result = make_results_match(opponent, opponent_wins);
                std::cout << "Results " << roster_name << " "
                          << melee_web_fighter_content(opponent)->name
                          << (opponent_wins ? " wins" : " Mario wins") << "..."
                          << std::flush;
                /* A source match may leave the TyDatai scene alias populated
                 * until preloadState retires scene-heap aliases for Results. */
                if (_Toy_sbss_804D6ED0 != nullptr)
                    throw std::runtime_error("Trophy archive alias was not idle before the next Results scene");
                _Toy_sbss_804D6ED0 = reinterpret_cast<HSD_Archive*>(uintptr_t(1));
                melee_web::GameplayResultsSession session(files, result,
                                                           0x13579bdfU, *input);
                if (_Toy_sbss_804D6ED0 == reinterpret_cast<HSD_Archive*>(uintptr_t(1)))
                    throw std::runtime_error("Results preload did not retire the previous TyDatai alias");
                check_results_fighter_leases(result);
                if (session.source_frames() != 0)
                    throw std::runtime_error("Results scene advanced during construction");
                for (unsigned tick = 0; tick < 2; ++tick)
                    session.tick(neutral);
                if (session.source_frames() != 2 || session.requested())
                    throw std::runtime_error("Short Results tick changed source transition state");
                if (confirm) {
                    float pcm[1068];
                    unsigned audio_phase = 0;
                    for (unsigned tick = 0; tick < 900 && !session.requested(); ++tick) {
                        PADStatus pads[4]{};
                        pads[2].err = pads[3].err = -1;
                        if (tick >= 240 && tick % 90 == 0)
                            pads[0].button = pads[1].button = PAD_BUTTON_START;
                        session.tick(pads);
                        audio_phase += 32000;
                        const unsigned samples = audio_phase / 60;
                        audio_phase %= 60;
                        if (!melee_web_audio_render(session.audio(), pcm, samples,
                                                     error, sizeof(error)))
                            throw std::runtime_error(error);
                    }
                    if (!session.requested())
                        throw std::runtime_error("Original Results Start confirmation did not finish");
                    session.exit_scene();
                }
                session.close();
                check_results_teardown();
                ++completed;
                std::cout << "ok\n";
            }
        }
        if (!melee_web_gameplay_session_end(error, sizeof(error)))
            throw std::runtime_error(error);
        ended = true;
        std::cout << "Validated " << completed << " " << roster_name
                  << (confirm ? " Results confirmations and teardown\n" :
                                " Results constructions with short ticks\n");
        return 0;
    } catch (...) {
        if (!ended) melee_web_gameplay_session_end(error, sizeof(error));
        throw;
    }
}

static void check_source_entry(const melee_web::GameplayWorld& world)
{
    HSD_GObj** links=reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    const auto previous_common=efAsync_DatEntries[0].data;
    const auto previous_fighter=efAsync_DatEntries[1].data;
    void* item_common=it_804D6D28;
    void* item_bounce=it_804D6D40;
    void* item_colors=it_804D6D04;
    void* item_public=it_804D6D20;
    void* item_common_articles=it_804D6D24;
    void* item_pokemon_articles=it_804D6D30;
    void* item_character_articles=it_804D6D38;
    if(!item_public||!item_common||!item_bounce||!item_colors||
       !item_common_articles||!item_pokemon_articles||!item_character_articles)
        throw std::runtime_error("Deferred Results item globals were not attached");
    if(links[11]||links[12]||previous_common||previous_fighter)
        throw std::runtime_error("Deferred Results source owners were initialized too early");

    /* fn_8017AA78's authored boundary is Item_80266FA8 then Item_80266FCC
     * before efLib_Init;
     * the checked world has already attached the typed data these routines
     * consume, while the source tables remain unpublished until LoadSync. */
    const int saved_language=lbLang_GetLanguageSetting();
    lbLang_SetLanguageSetting(LANG_US);
    Item_80266FA8();
    lbLang_SetLanguageSetting(saved_language);
    Item_80266FCC();
    if(it_804D6D20!=item_public||it_804D6D24!=item_common_articles||
       it_804D6D30!=item_pokemon_articles||it_804D6D38!=item_character_articles||
       it_804D6D28!=item_common||it_804D6D40!=item_bounce||it_804D6D04!=item_colors)
        throw std::runtime_error("Original item initializer replaced checked typed globals");
    efLib_Init();
    if(!links[11]||!links[12]||!links[11]->proc||!links[12]->proc)
        throw std::runtime_error("Original effect initializer did not publish both GObj processes");
    efAsync_LoadSync(0);
    efAsync_LoadSync(1);
    world.verify_result_source_loads();
    if(!efAsync_DatEntries[0].data||!efAsync_DatEntries[1].data)
        throw std::runtime_error("Original Results source loads did not publish effect data");
}

int main(int argc,char** argv){try{
    const std::string command = argc >= 2 ? argv[1] : "";
    const bool real_mario = command == "--real-mario" ||
                            command == "--real-mario-confirm";
    const bool real_eight = command == "--real-eight" ||
                            command == "--real-eight-confirm";
    const bool real_enabled = command == "--real-enabled" ||
                              command == "--real-enabled-confirm";
    const bool confirm = command == "--real-mario-confirm" ||
                         command == "--real-eight-confirm" ||
                         command == "--real-enabled-confirm";
    const bool real_roster = real_mario || real_eight || real_enabled;
    if ((!real_roster && argc != 3) || (real_roster && argc != 5))
        throw std::runtime_error(real_roster ?
            "Expected --real-mario/--real-eight/--real-enabled[-confirm] <common/fighter> <Results shared/music> <Results fighters>" :
            "Expected common/fighter and Results asset directories");
    melee_web::RuntimeFiles files;
    const int first_directory = real_roster ? 2 : 1;
    for (int directory = first_directory; directory < argc; ++directory)
        load_directory(files, argv[directory]);
    if (real_roster) {
        constexpr std::array<int, 1> real_mario_opponents = {CKIND_MARIO};
        constexpr std::array<int, 8> real_eight_opponents = {
            CKIND_MARIO, CKIND_DRMARIO, CKIND_FOX, CKIND_FALCO,
            CKIND_MARS, CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK,
        };
        constexpr std::array<int, 19> real_enabled_opponents = {
            CKIND_MARIO, CKIND_FOX, CKIND_FALCO, CKIND_MARS,
            CKIND_DRMARIO, CKIND_EMBLEM, CKIND_LINK, CKIND_CLINK,
            CKIND_CAPTAIN, CKIND_GANON, CKIND_LUIGI, CKIND_PIKACHU,
            CKIND_PICHU, CKIND_PURIN, CKIND_DONKEY, CKIND_KOOPA,
            CKIND_MEWTWO, CKIND_NESS, CKIND_PEACH,
        };
        const auto opponents = real_mario
            ? std::span<const int>(real_mario_opponents)
            : real_enabled
            ? std::span<const int>(real_enabled_opponents)
            : std::span<const int>(real_eight_opponents);
        return run_real_roster(files, opponents,
                               real_mario ? "Mario" :
                               real_enabled ? "enabled-roster" : "real-eight",
                               confirm);
    }
    const melee_web::FighterCostume* mario=nullptr;
    for(const auto& identity:melee_web::fighter_costumes())
        if(identity.fighter_kind==0&&identity.costume_index==0)mario=&identity;
    if(!mario)throw std::runtime_error("Mario source identity unavailable");
    melee_web::GameplayWorldSelection selection;
    selection.purpose=melee_web::GameplayWorldPurpose::Results;
    for(unsigned cycle=0;cycle<2;++cycle){
        melee_web::GameplayWorld world(files,selection);
        melee_web::GameplayResultsAssets assets(files,*mario);
        world.install_result_demo(0,assets.result_motion_archive());
        check_source_entry(world);
        if(melee_web_gameplay_stats().ticks!=0||assets.demo_clips().size()!=9)
            throw std::runtime_error("Results preparation changed source time or omitted demo clips");
        assets.verify();world.verify_immutable_archives();
        world.close();assets.close();
        if(melee_web_gameplay_world_exists()||HSD_GObj_Entities||
           efAsync_DatEntries[0].data||efAsync_DatEntries[1].data)
            throw std::runtime_error("Results preparation retained its source world");
    }
    std::cout<<"Original Mario Results assets and prepared world constructed and closed twice; scene execution untested\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
