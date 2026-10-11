#pragma once

#include "gameplay_compat.h"
#include "gameplay_pad_state.h"
extern "C" {
#include <melee/gm/types.h>
}
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace melee_web {

// One retained entry, never a session recorder. Object representations include
// opaque fields/padding and belong only to this generated Wasm ABI. They are not
// PPC images, portable serialization, or sufficient evidence for exact replay.
class ResultsEntryPacket {
    static_assert(sizeof(void*) == 4 && std::endian::native == std::endian::little);
#if !defined(__wasm32__)
#error Results entry packets require the Wasm32 target
#endif
    static_assert(std::is_trivially_copyable_v<MatchExitInfo>);
    static_assert(std::is_trivially_copyable_v<ResultsMatchInfo>);
    MatchExitInfo terminal_{};
    ResultsMatchInfo results_{};
    std::array<uint8_t, MELEE_WEB_PAD_STATE_BYTES> input_{};
    uint32_t match_index_ = 0, seed_ = 0;
    bool available_ = false;

    static std::string hex(const void* data, size_t size)
    {
        constexpr char digits[] = "0123456789abcdef";
        const auto* bytes = static_cast<const unsigned char*>(data);
        std::string result(size * 2, '0');
        for (size_t i = 0; i < size; ++i) {
            result[i * 2] = digits[bytes[i] >> 4];
            result[i * 2 + 1] = digits[bytes[i] & 15];
        }
        return result;
    }

public:
    // Caller supplies the existing match-final semantic PAD buffer captured
    // AFTER terminal publication and BEFORE teardown restores external PAD
    // owners. Seed is the Results-entry value after original mode callbacks,
    // distinct from the ordinary match-final seed. Never recapture PAD globals.
    // Only diagnostic storage is written; no allocation at this boundary.
    void capture(uint32_t match_index, const MatchExitInfo& terminal,
                 const ResultsMatchInfo& results, uint32_t seed,
                 const uint8_t (&final_input)[MELEE_WEB_PAD_STATE_BYTES]) noexcept
    {
        std::memcpy(&terminal_, &terminal, sizeof(terminal_));
        std::memcpy(&results_, &results, sizeof(results_));
        std::memcpy(input_.data(), final_input, input_.size());
        match_index_ = match_index;
        seed_ = seed;
        available_ = true;
    }

    // A typed SD finish keeps its final bank internal; never expose a stale
    // previous ordinary entry as if those bytes had been observed.
    void clear() noexcept { available_ = false; }

    // Serialize only on an explicit observer read, never per tick or at entry.
    std::string json() const
    {
        if (!available_) return "null";
        const auto number = [](auto value) { return std::to_string(value); };
        std::string out =
            "{\"schema\":\"melee-web-results-entry-v1\","
            "\"scope\":\"local-debug-only; not a PPC image or exact replay\","
            "\"build_identity\":\"bind-to-served-JS-and-Wasm-hashes-in-harness\","
            "\"entry_seed_origin\":\"post-mode-callbacks Results entry\","
            "\"pad_origin\":\"ordinary VS post-publication before teardown\","
            "\"abi\":{\"target\":\"wasm32\",\"byte_order\":\"little-endian\",\"pointer_bytes\":4},"
            "\"match_index\":" + number(match_index_) +
            ",\"entry_seed\":" + number(seed_) +
            ",\"sizeof\":{\"MatchExitInfo\":" + number(sizeof(MatchExitInfo)) +
            ",\"ResultsMatchInfo\":" + number(sizeof(ResultsMatchInfo)) +
            ",\"MatchEnd\":" + number(sizeof(MatchEnd)) +
            ",\"MatchPlayerData\":" + number(sizeof(MatchPlayerData)) +
            ",\"MatchTeamData\":" + number(sizeof(MatchTeamData)) +
            "},\"offsetof\":{\"MatchExitInfo.match_end\":" + number(offsetof(MatchExitInfo, match_end)) +
            ",\"ResultsMatchInfo.match_end\":" + number(offsetof(ResultsMatchInfo, match_end)) +
            ",\"MatchEnd.player_standings\":" + number(offsetof(MatchEnd, player_standings)) +
            ",\"MatchEnd.team_standings\":" + number(offsetof(MatchEnd, team_standings)) +
            "},\"terminal_hex\":\"" + hex(&terminal_, sizeof(terminal_)) +
            "\",\"results_info_hex\":\"" + hex(&results_, sizeof(results_)) +
            "\",\"pad\":{\"encoding\":\"gameplay-pad-state semantic wire (big-endian scalars; no native pointers/padding)\","
            "\"bytes\":" + number(input_.size()) + ",\"hex\":\"" + hex(input_.data(), input_.size()) +
            "\"},\"summary\":{\"x0_0\":" + number(results_.x0_0) +
            ",\"x0_1\":" + number(results_.x0_1) + ",\"x1\":" + number(results_.x1) +
            ",\"x4\":" + number(results_.x4) +
            ",\"outcome\":" + number(results_.match_end.outcome) +
            ",\"match_kind\":" + number(results_.match_end.match_kind) +
            ",\"is_teams\":" + number(results_.match_end.is_teams) +
            ",\"frame_count\":" + number(results_.match_end.frame_count) +
            ",\"n_winners\":" + number(results_.match_end.n_winners) + ",\"winners\":[";
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i) {
            if (i) out += ',';
            out += number(results_.match_end.winners[i]);
        }
        out += "],\"players\":[";
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i) {
            const auto& p = results_.match_end.player_standings[i];
            if (i) out += ',';
            out += "{\"slot_type\":" + number(p.slot_type) + ",\"ckind\":" + number(p.ckind) +
                ",\"ftkind\":" + number(p.ftkind) + ",\"costume\":" + number(p.x3) +
                ",\"x4\":" + number(p.x4) + ",\"is_big_loser\":" + number(p.is_big_loser) +
                ",\"is_small_loser\":" + number(p.is_small_loser) +
                ",\"team\":" + number(p.team) + ",\"stocks\":" + number(p.stocks) + "}";
        }
        return out + "]}}";
    }
};
} // namespace melee_web
