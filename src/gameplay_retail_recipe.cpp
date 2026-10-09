#include "gameplay_retail_recipe.hpp"
#include "gameplay_cpu_observation.h"
#include "gameplay_menu.h"
#include <melee/pl/forward.h>
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

extern "C" int melee_web_retail_setup(const uint8_t*, uint32_t,
    MeleeWebMenuMatchSelection*, char*, size_t);
extern "C" void melee_web_retail_state(void);
extern "C" void melee_web_retail_entities_index(uint32_t);
extern "C" void melee_web_retail_entities_checked(void);
extern "C" uint32_t melee_web_retail_rng(void);
extern "C" uint32_t gm_GetFrameCount(void);
extern "C" uint32_t gm_8016AEEC(void);
extern "C" uint16_t gm_8016AEFC(void);
extern "C" int melee_web_match_source_result(void);
extern "C" int melee_web_match_end_state(void);
extern "C" void melee_web_retail_entities_reset(void);

namespace melee_web {
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t cursor = 0;
    uint8_t u8() {
        check(cursor < bytes.size(), "Reference input is truncated");
        return bytes[cursor++];
    }
    uint16_t u16() { const auto hi = u8(); const auto lo = u8(); return uint16_t(hi) << 8 | lo; }
    uint32_t u32() { const auto hi = u16(); const auto lo = u16(); return uint32_t(hi) << 16 | lo; }
    uint64_t u64() { const auto hi = u32(); const auto lo = u32(); return uint64_t(hi) << 32 | lo; }
};
void hex(std::span<const uint8_t> bytes, std::ostream& out = std::cout) {
    static constexpr char digits[] = "0123456789abcdef";
    for (auto byte : bytes) out << digits[byte >> 4] << digits[byte & 15];
}
std::string fixed_hex(uint64_t value, unsigned digits) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string result(digits, '0');
    for (unsigned index = 0; index < digits; ++index) {
        result[digits - index - 1] = alphabet[value & 0xf];
        value >>= 4;
    }
    return result;
}
void declared_setup_json(const StartMeleeData& setup) {
    const auto& rules = setup.rules;
    std::cout << "\"declared_setup\":{\"players\":[";
    bool first = true;
    for (unsigned index = 0; index < 4; ++index) {
        const auto& player = setup.players[index];
        if (player.slot_type != 0 && player.slot_type != 1)
            continue;
        if (!first)
            std::cout << ",";
        first = false;
        std::cout << "{\"port\":" << index + 1
                  << ",\"character_kind\":" << static_cast<int>(player.ckind)
                  << ",\"costume\":" << static_cast<unsigned>(player.color)
                  << ",\"stocks\":" << static_cast<int>(player.stocks)
                  << ",\"player_type\":" << static_cast<unsigned>(player.slot_type)
                  << ",\"rumble_enabled\":"
                  << (player.rumble_enabled ? "true" : "false");
        if (player.slot_type == 1)
            std::cout << ",\"cpu_kind\":" << static_cast<unsigned>(player.cpu_kind)
                      << ",\"cpu_level\":" << static_cast<unsigned>(player.cpu_level);
        std::cout << "}";
    }
    std::cout << "],\"stage\":" << rules.stkind
              << ",\"match_kind\":" << rules.match_kind
              << ",\"timer_enabled\":" << (rules.timer_enabled ? "true" : "false")
              << ",\"timer_counts_up\":" << (rules.timer_counts_up ? "true" : "false")
              << ",\"time_limit_seconds\":" << rules.time_limit
              << ",\"is_stock\":" << (rules.is_stock ? "true" : "false")
              << ",\"disable_pausing\":" << (rules.disable_pausing ? "true" : "false")
              << ",\"is_teams\":" << (rules.is_teams ? "true" : "false")
              << ",\"item_frequency\":" << static_cast<int>(rules.xB)
              << ",\"item_mask_hex\":\"" << fixed_hex(rules.x20, 16)
              << "\",\"damage_ratio_bits\":\""
              << fixed_hex(std::bit_cast<uint32_t>(rules.x30), 8)
              << "\",\"game_speed_bits\":\""
              << fixed_hex(std::bit_cast<uint32_t>(rules.game_speed), 8)
              << "\"}";
}
bool timer_audit_active = false;
bool whole_session_cpu_observation_requested = false;
bool whole_session_cpu_observation_started = false;
bool whole_session_cpu_observation_finished = false;
uint32_t whole_session_match_index = 0;
bool whole_session_primary_identity_active = false;
bool whole_session_setup_table_active = false;
bool whole_session_entity_prefix_active = false;

constexpr std::array<uint8_t, 0x60> kMilestoneRules = {
    0x30,0x00,0x86,0x4c,0xc3,0x00,0x00,0x00,0x00,0x00,0x00,0xff,
    0xff,0x6e,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x3f,0x80,0x00,0x00,
    0x3f,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
constexpr uint8_t kMilestoneRoster[3][4] = {
    {8, 2, 20, 9}, {22, 23, 6, 21}, {0, 25, 7, 13},
};
constexpr uint8_t kFighterCoverageRoster[3][4] = {
    {1, 5, 11, 12}, {15, 10, 24, 18}, {4, 14, 16, 17},
};

bool same_float(float left, float right) {
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

bool same_player(const PlayerInitData& left, const PlayerInitData& right) {
    return left.ckind == right.ckind && left.slot_type == right.slot_type &&
        left.stocks == right.stocks && left.color == right.color && left.slot == right.slot &&
        left.x5 == right.x5 && left.spawn_dir == right.spawn_dir &&
        left.sub_color == right.sub_color && left.handicap == right.handicap &&
        left.team == right.team && left.nametag == right.nametag && left.xB == right.xB &&
        left.rumble_enabled == right.rumble_enabled && left.xC_b1 == right.xC_b1 &&
        left.xC_b2 == right.xC_b2 && left.xC_b3 == right.xC_b3 &&
        left.vs_invisible == right.vs_invisible && left.xC_b5 == right.xC_b5 &&
        left.xC_b6 == right.xC_b6 && left.xC_b7 == right.xC_b7 &&
        left.xD_b0 == right.xD_b0 && left.xD_b1 == right.xD_b1 &&
        left.xD_b2 == right.xD_b2 && left.xD_b3 == right.xD_b3 &&
        left.xD_b4 == right.xD_b4 && left.xD_b5 == right.xD_b5 &&
        left.xD_b6 == right.xD_b6 && left.xD_b7 == right.xD_b7 &&
        left.cpu_kind == right.cpu_kind && left.cpu_level == right.cpu_level &&
        left.x10 == right.x10 && left.x12 == right.x12 && left.hp == right.hp &&
        same_float(left.attack_ratio, right.attack_ratio) &&
        same_float(left.defense_ratio, right.defense_ratio) &&
        same_float(left.model_scale, right.model_scale);
}

bool same_rules(const StartMeleeRules& a, const StartMeleeRules& b) {
    return a.match_kind == b.match_kind && a.x0_3 == b.x0_3 &&
        a.timer_enabled == b.timer_enabled && a.timer_counts_up == b.timer_counts_up &&
        a.x1_0 == b.x1_0 && a.x1_1 == b.x1_1 && a.x1_2 == b.x1_2 &&
        a.x1_3 == b.x1_3 && a.x1_4 == b.x1_4 && a.x1_5 == b.x1_5 &&
        a.timer_shows_hours == b.timer_shows_hours && a.friendly_fire == b.friendly_fire &&
        a.is_stock == b.is_stock && a.x2_1 == b.x2_1 && a.x2_2 == b.x2_2 &&
        a.single_button == b.single_button && a.disable_pausing == b.disable_pausing &&
        a.x2_5 == b.x2_5 && a.x2_6 == b.x2_6 && a.x2_7 == b.x2_7 &&
        a.x3_0 == b.x3_0 && a.x3_1 == b.x3_1 && a.x3_2 == b.x3_2 &&
        a.x3_3 == b.x3_3 && a.x3_4 == b.x3_4 && a.x3_5 == b.x3_5 &&
        a.x3_6 == b.x3_6 && a.x3_7 == b.x3_7 && a.x4_0 == b.x4_0 &&
        a.is_vs == b.is_vs && a.x4_2 == b.x4_2 && a.x4_3 == b.x4_3 &&
        a.x4_4 == b.x4_4 && a.x4_5 == b.x4_5 && a.x4_6 == b.x4_6 &&
        a.x4_7 == b.x4_7 && a.x5_0 == b.x5_0 && a.x5_1 == b.x5_1 &&
        a.x5_2 == b.x5_2 && a.x5_3 == b.x5_3 && a.x5_4 == b.x5_4 &&
        a.x5_5 == b.x5_5 && a.x5_6 == b.x5_6 && a.x5_7 == b.x5_7 &&
        a.x6 == b.x6 && a.x7 == b.x7 && a.is_teams == b.is_teams &&
        a.x9 == b.x9 && a.xA == b.xA && a.xB == b.xB && a.xC == b.xC &&
        a.xD == b.xD && a.stkind == b.stkind && a.time_limit == b.time_limit &&
        a.x14 == b.x14 && a.x18 == b.x18 &&
        std::equal(std::begin(a.x1C_pad), std::end(a.x1C_pad), std::begin(b.x1C_pad)) &&
        a.x20 == b.x20 && a.x28 == b.x28 && same_float(a.x2C, b.x2C) &&
        same_float(a.x30, b.x30) && same_float(a.game_speed, b.game_speed) &&
        a.on_unpause_override == b.on_unpause_override &&
        a.on_pause_override == b.on_pause_override &&
        a.check_for_pauser_override == b.check_for_pauser_override &&
        a.on_match_start == b.on_match_start && a.on_frame_start == b.on_frame_start &&
        a.on_frame_end == b.on_frame_end && a.on_match_end == b.on_match_end &&
        a.x54 == b.x54 && a.x58 == b.x58 &&
        std::equal(std::begin(a.pad_x5C), std::end(a.pad_x5C), std::begin(b.pad_x5C));
}

bool same_setup(const StartMeleeData& actual, const StartMeleeData& expected) {
    if (!same_rules(actual.rules, expected.rules)) return false;
    for (unsigned index = 0; index < GM_MAX_PLAYERS; ++index)
        if (!same_player(actual.players[index], expected.players[index])) return false;
    return true;
}

void report_setup_difference(unsigned match_index, const StartMeleeData& actual,
                             const StartMeleeData& expected) {
    std::cout << "MWRC v9 setup mismatch match=" << match_index;
    if (!same_rules(actual.rules, expected.rules)) {
        std::cout << " rules_actual=";
        hex(std::span(reinterpret_cast<const uint8_t*>(&actual.rules), sizeof(actual.rules)));
        std::cout << " rules_expected=";
        hex(std::span(reinterpret_cast<const uint8_t*>(&expected.rules), sizeof(expected.rules)));
    }
    for (unsigned slot = 0; slot < GM_MAX_PLAYERS; ++slot) {
        if (same_player(actual.players[slot], expected.players[slot])) continue;
        std::cout << " player" << slot << "_actual=";
        hex(std::span(reinterpret_cast<const uint8_t*>(&actual.players[slot]),
                       sizeof(actual.players[slot])));
        std::cout << " player" << slot << "_expected=";
        hex(std::span(reinterpret_cast<const uint8_t*>(&expected.players[slot]),
                       sizeof(expected.players[slot])));
    }
    std::cout << '\n' << std::flush;
}

void validate_milestone_setup(std::span<const uint8_t> raw,
                              const MeleeWebMenuMatchSelection& selection,
                              size_t match_index) {
    check(match_index < std::size(kMilestoneRoster),
          "MWRC v9 setup index is outside the three-match milestone");
    check(raw.size() == 0x138 &&
          std::equal(kMilestoneRules.begin(), kMilestoneRules.end(), raw.begin()),
          "MWRC v9 rules differ from the accepted four-Mario stock-match profile");
    check(selection.player_count == 4 && selection.start.rules.stkind == 0x20,
          "MWRC v9 setup requires four players on Final Destination");
    for (unsigned slot = 0; slot < 4; ++slot) {
        const auto& player = selection.start.players[slot];
        check(player.slot_type == Gm_PKind_Cpu && player.cpu_kind == 4 &&
              player.cpu_level == 9 && player.stocks == 4 && player.color == slot &&
              !player.rumble_enabled &&
              static_cast<uint8_t>(player.ckind) == kMilestoneRoster[match_index][slot],
              "MWRC v9 setup differs from its declared four-stock CPU9 roster");
    }
    for (unsigned slot = 4; slot < GM_MAX_PLAYERS; ++slot)
        check(selection.start.players[slot].slot_type == Gm_PKind_NA,
              "MWRC v9 setup has an active player outside its four-player roster");
}
void validate_fighter_v10_setup(std::span<const uint8_t> raw,
                                const MeleeWebMenuMatchSelection& selection,
                                size_t match_index) {
    check(match_index < std::size(kFighterCoverageRoster),
          "MWRC v10 setup index is outside its three-match fighter profile");
    check(raw.size() == 0x138 &&
          std::equal(kMilestoneRules.begin(), kMilestoneRules.end(), raw.begin()),
          "MWRC v10 rules differ from the accepted four-stock CPU9 profile");
    check(selection.player_count == 4 && selection.start.rules.stkind == 0x20,
          "MWRC v10 setup requires four players on Final Destination");
    for (unsigned slot = 0; slot < 4; ++slot) {
        const auto& player = selection.start.players[slot];
        check(player.slot_type == Gm_PKind_Cpu && player.cpu_kind == 4 &&
              player.cpu_level == 9 && player.stocks == 4 && player.color == slot &&
              !player.rumble_enabled &&
              static_cast<uint8_t>(player.ckind) == kFighterCoverageRoster[match_index][slot],
              "MWRC v10 setup differs from its distinct twelve-character CPU9 roster");
    }
    for (unsigned slot = 4; slot < GM_MAX_PLAYERS; ++slot)
        check(selection.start.players[slot].slot_type == Gm_PKind_NA,
              "MWRC v10 setup has an active player outside its four-player roster");
}
void timer_state(const char* record, size_t index = 0) {
    if (!timer_audit_active) return;
    // Separate diagnostic stream: never add fields to an older state schema,
    // and never execute this observer in a performance replay.
    std::cerr << "TIMER_AUDIT {\"record\":\"" << record << "\"";
    if (std::string_view(record) == "frame") std::cerr << ",\"index\":" << index;
    std::cerr << ",\"match_frame\":" << gm_GetFrameCount()
        << ",\"seconds\":" << gm_8016AEEC()
        << ",\"subframe\":" << gm_8016AEFC()
        << ",\"outcome\":" << melee_web_match_source_result()
        << ",\"end_state\":" << melee_web_match_end_state() << "}\n";
}
void history(const RetailReplayRecipe& recipe) {
    if (recipe.version < 2) return;
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> bytes{};
    melee_web_pad_state_capture(bytes.data());
    std::cout << ",\"pad_state_hex\":\""; hex(bytes); std::cout << "\"";
}
}

RetailReplayRecipe read_retail_replay(std::span<const uint8_t> bytes) {
    check(bytes.size() >= 16 + 0x138 && bytes.size() <= kRetailReplayMaxBytes,
          "Reference input size is outside its bounds");
    Reader input{bytes};
    check(input.u32() == 0x4d575243, "Unsupported reference input format");
    RetailReplayRecipe result;
    result.version = input.u32();
    check(result.version >= 1 && result.version <= kRetailReplayFighterVersion,
          "Unsupported reference input version");
    const auto max_frames = result.version >= kRetailReplayV8Version
        ? kRetailReplayWholeSessionMaxFrames : kRetailReplayLegacyMaxFrames;
    /* Version 7 was emitted by the provisional producer before the runtime
     * could install the source's first-CSS context.  Accepting its bytes and
     * silently entering CSS would make the declared PAD/RNG context inert, so
     * fail at the transport boundary with an actionable migration error. */
    check(result.version != 7,
          "Whole-session MWRC v7 is unsupported: re-export with v8 first-CSS context");
    result.seed = input.u32();
    const auto count = input.u32();
    const size_t profile_bytes = result.version >= 4 ? 4 : 0;
    uint16_t unlocked_characters = 0;
    uint16_t unlocked_stages = 0;
    if (result.version >= 4) {
        /* Save profile belongs to the transport envelope, not StartMeleeData. */
        unlocked_characters = input.u16();
        unlocked_stages = input.u16();
    }
    size_t clock_bytes = result.version == 5 ? 40 : 0;
    const size_t context_bytes = result.version >= kRetailReplayV8Version
        ? kRetailReplayContextHeaderBytes + kRetailReplayContextBytes : 0;
    if (result.version == 5) {
        RetailDrawClock clock;
        clock.pad_period = input.u64(); clock.vi_period = input.u64();
        clock.next_pad = input.u64(); clock.first_vi_poll = input.u64();
        clock.startup_draws = input.u32();
        check(input.u32() == 0, "Unsupported clock context flags");
        check(count && count <= max_frames,
              "Reference input frame count is outside its bounds");
        result.draw_boundaries = clock.boundaries(count);
    }
    if (result.version == 6) {
        const auto batches = input.u32();
        check(input.u32() == 0, "Unsupported recorded input-queue flags");
        check(count && count <= max_frames && batches && batches <= count,
              "Invalid recorded input-queue size");
        clock_bytes = 8 + size_t(batches) * 9;
        check(bytes.size() == 20 + clock_bytes + 0x138 + MELEE_WEB_PAD_STATE_BYTES + size_t(count) * 44,
              "Recorded input-queue size disagrees with transport");
        std::vector<RetailQueueBatch> events;
        events.reserve(batches);
        for (uint32_t i = 0; i < batches; ++i) {
            const auto poll = input.u64();
            const auto available = input.u8();
            events.push_back({poll, available});
        }
        result.draw_boundaries = retail_queue_boundaries(events, count);
    }
    if (result.version >= kRetailReplayV8Version) {
        check(input.u16() == kRetailReplayContextVersion,
              "Unsupported whole-session first-CSS context header");
        const auto context_flags = input.u16();
        check(context_flags == 0 || (result.version == kRetailReplayV8Version &&
              context_flags == kRetailReplayEntityPrefixFlag),
              "Unsupported whole-session first-CSS context flags");
        result.diagnostic_entity_prefix = context_flags == kRetailReplayEntityPrefixFlag;
        check(input.u32() == kRetailReplayContextBytes,
              "Whole-session first-CSS context size disagrees with its transport");
        result.initial_css = std::make_unique<RetailReplayInitialCssContext>();
        for (auto& byte : result.initial_css->game_rules) byte = input.u8();
        for (auto& byte : result.initial_css->save_data) byte = input.u8();
        for (auto& byte : result.initial_css->css_data) byte = input.u8();
        for (auto& byte : result.initial_css->ko_counts) byte = input.u8();
        check((uint16_t(result.initial_css->save_data[0]) << 8 |
               result.initial_css->save_data[1]) == unlocked_characters &&
              (uint16_t(result.initial_css->save_data[2]) << 8 |
               result.initial_css->save_data[3]) == unlocked_stages,
              "Whole-session profile masks disagree with first-CSS SaveData");
    }
    size_t setup_bytes = 0x138;
    if (result.version == kRetailReplayVersion ||
        result.version == kRetailReplayFighterVersion) {
        const auto setup_count = input.u16();
        check(input.u16() == 0 && setup_count == kRetailReplayMaxMatchSetups &&
              setup_count <= kRetailReplayMaxMatchSetups,
              "Whole-session setup table requires exactly three setups and zero flags");
        setup_bytes = 4 + size_t(setup_count) * 0x138;
        check(input.cursor + size_t(setup_count) * 0x138 <= bytes.size(),
              "Whole-session setup table is truncated");
        result.match_setups.resize(setup_count);
        for (auto& setup : result.match_setups)
            for (auto& byte : setup) byte = input.u8();
        result.setup = result.match_setups.front();
    } else {
        for (auto& byte : result.setup) byte = input.u8();
        if (result.version == kRetailReplayV8Version)
            result.match_setups.push_back(result.setup);
    }
    const size_t envelope_bytes = 16 + profile_bytes + clock_bytes + context_bytes + setup_bytes +
        (result.version >= 2 ? MELEE_WEB_PAD_STATE_BYTES : 0) + size_t(count) * 44;
    if (result.version >= kRetailReplayV8Version)
        // The whole-session span table follows the frames; its own length is
        // validated once the table has been read.
        check(count && count <= max_frames && bytes.size() >= envelope_bytes + 2,
              "Whole-session input size disagrees with its transport");
    else
        check(count && count <= max_frames && bytes.size() == envelope_bytes,
              "Reference input frame count disagrees with its size");
    char error[256]{};
    check(melee_web_retail_setup(result.setup.data(), result.seed, &result.selection,
                                error, sizeof(error)), error);
    if (result.version == kRetailReplayVersion ||
        result.version == kRetailReplayFighterVersion) {
        result.match_selections.resize(result.match_setups.size());
        for (size_t index = 0; index < result.match_setups.size(); ++index) {
            auto& decoded = result.match_selections[index];
            check(melee_web_retail_setup(result.match_setups[index].data(), result.seed,
                                         &decoded, error, sizeof(error)), error);
            if (result.version == kRetailReplayVersion)
                validate_milestone_setup(result.match_setups[index], decoded, index);
            else
                validate_fighter_v10_setup(result.match_setups[index], decoded, index);
        }
        result.selection = result.match_selections.front();
    }
    if (result.diagnostic_entity_prefix) {
        constexpr uint8_t roster[] = {15, 14, 8, 2};
        check(std::equal(kMilestoneRules.begin(), kMilestoneRules.end(), result.setup.begin()),
              "Entity prefix requires the declared ordinary stock rules");
        check(result.selection.player_count == 4 && result.selection.start.rules.stkind == 0x20,
              "Entity prefix requires four source players on Final Destination");
        for (unsigned slot = 0; slot < 4; ++slot) {
            const auto& p = result.selection.start.players[slot];
            check(p.slot_type == Gm_PKind_Cpu && p.cpu_kind == 4 && p.cpu_level == 9 &&
                  p.stocks == 4 && p.color == slot && !p.rumble_enabled && p.ckind == roster[slot],
                  "Entity prefix differs from its declared CPU9 roster");
        }
        for (unsigned slot = 4; slot < GM_MAX_PLAYERS; ++slot)
            check(result.selection.start.players[slot].slot_type == Gm_PKind_NA,
                  "Entity prefix has a foreign active tail player");
    }
    check(result.version >= 3 || result.selection.player_count == 2,
          "Multiplayer input requires reference version 3");
    check(result.version < 3 ||
          (result.selection.player_count >= MELEE_WEB_MENU_MIN_PLAYERS &&
           result.selection.player_count <= MELEE_WEB_MENU_MAX_PLAYERS),
          "Reference version 3/4 requires two through four active players");
    if (result.version >= 4) {
        result.selection.unlocked_characters = unlocked_characters;
        result.selection.unlocked_stages = unlocked_stages;
        result.selection.save_profile_present = 1;
    }
    if (result.version >= 2) {
        for (auto& byte : result.pad_bytes) byte = input.u8();
        result.initial_input.reset(melee_web_pad_state_decode(result.pad_bytes.data(),
            result.pad_bytes.size(), error, sizeof(error)));
        check(bool(result.initial_input), error);
    }
    if (result.version < 4) {
        std::cerr << "MWRC warning: save profile was not recorded in legacy input "
                  << "version " << result.version
                  << "; no all-unlocked profile is assumed\n";
    }
    result.frames.resize(count);
    for (auto& frame : result.frames) {
        const auto offset = input.cursor;
        for (auto& pad : frame.pads) {
            pad.button = input.u16();
            pad.stickX = std::bit_cast<int8_t>(input.u8());
            pad.stickY = std::bit_cast<int8_t>(input.u8());
            pad.substickX = std::bit_cast<int8_t>(input.u8());
            pad.substickY = std::bit_cast<int8_t>(input.u8());
            pad.triggerLeft = input.u8(); pad.triggerRight = input.u8();
            pad.analogA = input.u8(); pad.analogB = input.u8();
            pad.err = std::bit_cast<int8_t>(input.u8());
        }
        std::copy_n(bytes.data() + offset, frame.bytes.size(), frame.bytes.data());
    }
    if (result.version >= kRetailReplayV8Version) {
        const auto span_count = input.u16();
        check(span_count >= 1 && span_count <= kRetailReplayMaxSpans,
              "Whole-session span count is outside its bounds");
        check(bytes.size() == envelope_bytes + 2 + size_t(span_count) * kRetailReplaySpanBytes,
              "Whole-session span table disagrees with its transport");
        result.spans.reserve(span_count);
        uint32_t next_frame = 0;
        for (uint32_t index = 0; index < span_count; ++index) {
            RetailReplaySpan span;
            span.scene = input.u8();
            check(input.u8() == 0 && input.u16() == 0, "Unsupported whole-session span flags");
            span.first_frame = input.u32();
            span.last_frame = input.u32();
            check(span.scene >= kRetailReplayCss && span.scene <= kRetailReplayPrize,
                  "Whole-session span scene is not an admitted scene");
            check(span.first_frame < count && span.last_frame < count,
                  "Whole-session span frame index is outside its bounds");
            check(span.first_frame == next_frame && span.last_frame >= span.first_frame,
                  "Whole-session spans must be ordered and contiguous");
            next_frame = span.last_frame + 1;
            result.spans.push_back(span);
        }
        check(next_frame == count, "Whole-session spans must cover every input frame");
        check(result.spans.front().scene == kRetailReplayCss,
              "Whole-session timeline must start in CSS");
        if (result.diagnostic_entity_prefix) {
            check(result.spans.size() == 3 && result.spans[0].scene == kRetailReplayCss &&
                  result.spans[1].scene == kRetailReplaySss &&
                  result.spans[2].scene == kRetailReplayMatch,
                  "Entity prefix requires one CSS/SSS/Match route ending in Match");
            check(result.diagnostic_source_observations() == 60,
                  "Entity prefix captured batching is unsupported by per-tick browser draws");
        } else {
        check(result.spans.back().scene == kRetailReplayResults ||
              result.spans.back().scene == kRetailReplayPrize,
              "Whole-session timeline must end in Results or Prize");
        }
        if (result.version == kRetailReplayVersion ||
            result.version == kRetailReplayFighterVersion) {
            const auto match_spans = std::count_if(result.spans.begin(), result.spans.end(),
                [](const RetailReplaySpan& span) { return span.scene == kRetailReplayMatch; });
            check(match_spans == kRetailReplayMaxMatchSetups &&
                  match_spans == result.match_setups.size(),
                  "Whole-session setup table does not match its match scene spans");
        }
    }
    return result;
}

void retail_replay_session_initial(const RetailReplayRecipe& recipe) {
    check(recipe.whole_session(), "Session diagnostics require MWRC v8, v9, or v10");
    timer_audit_active = false;
    whole_session_cpu_observation_requested = melee_web_cpu_observation_available() != 0;
    whole_session_cpu_observation_started = false;
    whole_session_cpu_observation_finished = false;
    whole_session_match_index = 0;
    whole_session_entity_prefix_active = recipe.diagnostic_entity_prefix;
    whole_session_primary_identity_active =
        recipe.version == kRetailReplayVersion ||
        recipe.version == kRetailReplayFighterVersion;
    whole_session_setup_table_active = recipe.version == kRetailReplayVersion ||
                                       recipe.version == kRetailReplayFighterVersion;
    if (whole_session_primary_identity_active) melee_web_retail_entities_reset();
    std::cout << "{\"record\":\"header\",\"schema\":\"melee-web-port-session-diagnostic\","
        "\"version\":" << (recipe.diagnostic_entity_prefix ? 2 : 1);
    if (recipe.diagnostic_entity_prefix)
        std::cout << ",\"fighter_entities\":\"all_player_entity_slots\"";
    std::cout << ",\"frames_requested\":" << recipe.frames.size()
        << ",\"comparison\":\"not_run\",\"cpu_observations\":\""
        << (whole_session_cpu_observation_requested &&
            recipe.version == kRetailReplayVersion ? "second_match_only" : "not_captured") << "\","
        "\"draw_state\":\"not_captured\"}\n";
}

void retail_replay_validate_match_setup(const RetailReplayRecipe& recipe,
                                        unsigned match_index,
                                        const StartMeleeData& actual_setup) {
    if (recipe.diagnostic_entity_prefix) {
        check(match_index == 0 && same_setup(actual_setup, recipe.selection.start),
              "Entity prefix actual source setup differs from its declared setup");
        return;
    }
    if (recipe.version != kRetailReplayVersion &&
        recipe.version != kRetailReplayFighterVersion) return;
    check(match_index < recipe.match_selections.size(),
          "Whole-session replay observed a match setup beyond its declared setup table");
    if (!same_setup(actual_setup, recipe.match_selections[match_index].start))
        report_setup_difference(match_index, actual_setup,
                                recipe.match_selections[match_index].start);
    check(same_setup(actual_setup, recipe.match_selections[match_index].start),
          "Original menu match setup differs from its declared setup table");
    if (recipe.version == kRetailReplayFighterVersion)
        validate_fighter_v10_setup(recipe.match_setups[match_index],
                                   recipe.match_selections[match_index], match_index);
}

unsigned retail_replay_next_match_index(const RetailReplayRecipe& recipe,
                                        size_t next_frame) {
    check((recipe.version == kRetailReplayVersion ||
           recipe.version == kRetailReplayFighterVersion) &&
          next_frame < recipe.frames.size(),
          "Whole-session match index is unavailable outside its recorded input timeline");
    unsigned completed_matches = 0;
    for (const auto& span : recipe.spans) {
        if (span.scene == kRetailReplayMatch && span.last_frame < next_frame)
            ++completed_matches;
        if (span.first_frame >= next_frame ||
            (span.first_frame <= next_frame && next_frame <= span.last_frame))
            return completed_matches;
    }
    check(false, "Whole-session cursor is outside its declared scene spans");
    return 0;
}

void retail_replay_initial(const RetailReplayRecipe& recipe, bool source_drawing) {
    check(recipe.version != kRetailReplayVersion &&
          recipe.version != kRetailReplayFighterVersion,
          "Whole-session match entry requires the actual constructed setup");
    retail_replay_initial(recipe, source_drawing, recipe.selection.start);
}

void retail_replay_initial(const RetailReplayRecipe& recipe, bool source_drawing,
                           const StartMeleeData& actual_setup) {
    if (recipe.whole_session()) {
        check(!whole_session_setup_table_active ||
              whole_session_match_index < recipe.match_setups.size(),
              "Whole-session replay observed more entries than its setup table");
        retail_replay_validate_match_setup(recipe, whole_session_match_index, actual_setup);
        std::cout << "{\"record\":\"session_match_enter_complete\",";
        melee_web_retail_state();
        if (recipe.diagnostic_entity_prefix)
            melee_web_retail_entities_checked();
        if (whole_session_primary_identity_active)
            melee_web_retail_entities_index(whole_session_match_index);
        history(recipe);
        if (whole_session_setup_table_active) {
            std::cout << ",";
            declared_setup_json(actual_setup);
            ++whole_session_match_index;
        }
        if (recipe.diagnostic_entity_prefix) ++whole_session_match_index;
        std::cout << "}\n";
        if (whole_session_cpu_observation_requested &&
            !whole_session_cpu_observation_started &&
            !whole_session_cpu_observation_finished &&
            recipe.version == kRetailReplayVersion &&
            whole_session_match_index == 2) {
            // Bounded diagnostic for the second-match CPU branch. Keep the
            // observer disabled through the first match, then bind its header
            // to the independently recorded second setup.
            melee_web_cpu_observation_begin(recipe.match_setups[1].data(), recipe.frames.size(),
                                             source_drawing);
            whole_session_cpu_observation_started = true;
        }
        return;
    }
    timer_audit_active = recipe.selection.start.rules.timer_enabled;
    std::cout << "{\"record\":\"header\",\"schema\":\"melee-web-port-replay-candidate\",\"version\":"
        << (recipe.version >= 3 ? 3 : recipe.version)
        << ",\"frames_requested\":" << recipe.frames.size();
    if (recipe.version >= 3) std::cout << ",\"active_player_count\":" << recipe.selection.player_count;
    std::cout << ",\"phase\":\"after_source_tick_before_audio_transport\",\"rendering\":\""
        << (source_drawing ? "source_draws" : "excluded") << "\",\"comparison\":\"not_run\"}\n";
    std::cout << "{\"record\":\"match_enter\",\"rng\":" << recipe.seed << ",\"start_melee_hex\":\"";
    hex(recipe.setup); std::cout << "\"";
    if (recipe.version >= 2) { std::cout << ",\"pad_state_hex\":\""; hex(recipe.pad_bytes); std::cout << "\""; }
    std::cout << "}\n{\"record\":\"match_enter_complete\",";
    melee_web_retail_state(); history(recipe); std::cout << "}\n";
    if (recipe.version >= 3)
        melee_web_cpu_observation_begin(recipe.setup.data(), recipe.frames.size(), source_drawing);
    if (timer_audit_active) {
        std::cerr << "TIMER_AUDIT {\"record\":\"header\",\"schema\":\"melee-web-match-timer-audit\",\"version\":1,\"frames_requested\":"
            << recipe.frames.size() << ",\"setup_hex\":\"";
        hex(recipe.setup, std::cerr);
        std::cerr << "\",\"phase\":\"after_source_tick_before_audio_transport\"}\n";
        timer_state("initial");
    }
}

void retail_replay_frame(const RetailReplayRecipe& recipe, size_t index, unsigned scene) {
    check(index < recipe.frames.size(), "Reference observation index is outside the timeline");
    const auto& frame = recipe.frames[index];
    if (recipe.whole_session()) {
        check(scene >= kRetailReplayCss && scene <= kRetailReplayPrize,
              "Session diagnostics require a live source scene");
        std::cout << "{\"record\":\"session_frame\",\"scene\":" << scene;
    } else {
        std::cout << "{\"record\":\"frame\"";
    }
    std::cout << ",\"index\":" << index << ",\"supplied_inputs\":[";
    for (unsigned port = 0; port < 4; ++port) {
        if (port) std::cout << ",";
        std::cout << "\""; hex(std::span(frame.bytes).subspan(port * 11, 11)); std::cout << "\"";
    }
    std::cout << "],";
    if (!recipe.whole_session() || scene == kRetailReplayMatch)
        melee_web_retail_state();
    else
        std::cout << "\"rng\":" << melee_web_retail_rng();
    if (recipe.diagnostic_entity_prefix && scene == kRetailReplayMatch) {
        check(whole_session_match_index == 1,
              "Entity prefix tick preceded its single checked setup record");
        melee_web_retail_entities_checked();
    }
    if ((recipe.version == kRetailReplayVersion ||
         recipe.version == kRetailReplayFighterVersion) &&
        scene == kRetailReplayMatch) {
        check(whole_session_match_index > 0,
              "Whole-session match tick preceded its match setup record");
        if (whole_session_primary_identity_active)
            melee_web_retail_entities_index(whole_session_match_index - 1);
    }
    history(recipe); std::cout << "}\n";
    if (recipe.whole_session()) {
        if (whole_session_cpu_observation_started) {
            if (scene == kRetailReplayMatch) {
                melee_web_cpu_observation_tick(index);
            } else {
                melee_web_cpu_observation_end(index);
                whole_session_cpu_observation_started = false;
                whole_session_cpu_observation_finished = true;
            }
        }
    } else {
        timer_state("frame", index);
        if (recipe.version >= 3) melee_web_cpu_observation_tick(index);
    }
}
void retail_replay_draw(const RetailReplayRecipe& recipe, size_t index) {
    if (!recipe.whole_session() && recipe.version >= 3) melee_web_cpu_observation_draw(index);
}
void retail_replay_preparation_draw(const RetailReplayRecipe& recipe) {
    if (!recipe.whole_session() && recipe.version >= 3) melee_web_cpu_observation_preparation_draw();
}
void retail_replay_end(size_t frames, bool whole_session) {
    if (whole_session) {
        check(!whole_session_entity_prefix_active || whole_session_match_index == 1,
              "Entity prefix did not enter its single checked match");
        check(!whole_session_setup_table_active || whole_session_match_index == 3,
              "Whole-session replay did not enter exactly three matches");
        if (whole_session_cpu_observation_started)
            melee_web_cpu_observation_end(frames);
        whole_session_cpu_observation_requested = false;
        whole_session_cpu_observation_started = false;
        whole_session_cpu_observation_finished = false;
        whole_session_entity_prefix_active = false;
    } else {
        melee_web_cpu_observation_end(frames);
    }
    std::cout << "{\"record\":\"end\",\"frames\":" << frames << ",\"status\":\"captured\"}\n";
    if (timer_audit_active) {
        std::cerr << "TIMER_AUDIT {\"record\":\"end\",\"frames\":" << frames << ",\"status\":\"captured\"}\n";
        timer_audit_active = false;
    }
}
} // namespace melee_web
