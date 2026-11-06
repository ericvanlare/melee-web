/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "gameplay_match_session.hpp"
#include "slippi_profile/profile.h"
#include "stadium_first_css_diagnostic_snapshot_json.hpp"

#include <array>
#include <cstdint>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace melee_web::slippi_profile {

static_assert(SLIPPI_PROFILE_ACTIVE_IDENTITY == SLIPPI_PROFILE_IDENTITY_CAMERA_HISTORY_V20,
              "Runtime match identity is scoped to the active private V20 profile");

// These host-owned hashes must be computed from the loaded runtime, selected
// asset set, and active save profile before calling this source serializer.
// This helper checks their shape and binds their values; it does not attest
// that caller-provided hashes came from those sources.
struct RuntimeMatchIdentityHostAgreement {
    std::string_view runtime_sha256;
    std::string_view asset_identity_sha256;
    std::string_view save_profile_sha256;
    std::string_view profile_identity;
};

namespace detail {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

inline bool lower_sha256(std::string_view value) noexcept {
    if (value.size() != 64) return false;
    for (const char digit : value) {
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            return false;
    }
    return true;
}

inline void validate_host_agreement(const RuntimeMatchIdentityHostAgreement& host) {
    require(lower_sha256(host.runtime_sha256),
            "Runtime match identity requires a lowercase runtime SHA-256");
    require(lower_sha256(host.asset_identity_sha256),
            "Runtime match identity requires a lowercase asset identity SHA-256");
    require(lower_sha256(host.save_profile_sha256),
            "Runtime match identity requires a lowercase save profile SHA-256");
    require(host.profile_identity == SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID,
            "Runtime match identity requires the exact active V20 profile identity");
}

inline void validate_pointer_free_start(const StartMeleeRules& rules) {
    require(rules.on_unpause_override == nullptr,
            "Runtime match identity refuses a nonnull unpause override");
    require(rules.on_pause_override == nullptr,
            "Runtime match identity refuses a nonnull pause override");
    require(rules.check_for_pauser_override == nullptr,
            "Runtime match identity refuses a nonnull pauser override");
    require(rules.on_match_start == nullptr,
            "Runtime match identity refuses a nonnull match-start callback");
    require(rules.on_frame_start == nullptr,
            "Runtime match identity refuses a nonnull frame-start callback");
    require(rules.on_frame_end == nullptr,
            "Runtime match identity refuses a nonnull frame-end callback");
    require(rules.on_match_end == nullptr,
            "Runtime match identity refuses a nonnull match-end callback");
    require(rules.x54 == nullptr,
            "Runtime match identity refuses nonnull StartMeleeRules x54 data");
    require(rules.x58 == nullptr,
            "Runtime match identity refuses nonnull StartMeleeRules x58 data");
}

inline void validate_ordinary_mario_fd_stock_start(
    const MeleeWebMenuMatchSelection& selection) {
    const auto& rules = selection.start.rules;
    require(selection.player_count == 2,
            "Runtime match identity supports exactly two source players");
    require(selection.opening_demo == 0 && selection.sudden_death == 0 &&
            rules.x6 == 0,
            "Runtime match identity refuses opening-demo or Sudden Death selection");
    require(rules.is_vs == 1 && rules.is_stock == 1 && rules.is_teams == 0 &&
            rules.match_kind == MatchKind_Stock && rules.stkind == St_Kind_Last,
            "Runtime match identity requires ordinary non-team stock VS on Final Destination");
    require(selection.hud_layout == rules.x0_3,
            "Runtime match identity HUD selection differs from source rules");
    require(selection.save_profile_present <= 1,
            "Runtime match identity save-profile presence is not a boolean");
    require(selection.opening_demo <= 1 && selection.sudden_death <= 1,
            "Runtime match identity menu-mode flags are not booleans");

    std::array<unsigned, 2> active_slots{};
    unsigned active_count = 0;
    for (unsigned slot = 0; slot < GM_MAX_PLAYERS; ++slot) {
        if (selection.start.players[slot].slot_type == Gm_PKind_NA) continue;
        require(active_count < active_slots.size(),
                "Runtime match identity source selection has more than two active slots");
        active_slots[active_count++] = slot;
    }
    require(active_count == 2 && active_slots[0] == 0 && active_slots[1] == 1,
            "Runtime match identity requires active source slots zero and one");

    for (unsigned source_slot = 0; source_slot < 2; ++source_slot) {
        const auto& source = selection.start.players[source_slot];
        const unsigned source_port = source.slot ? source.slot - 1u : source_slot;
        const auto& compatibility = selection.players[source_slot];
        require(source.slot_type == Gm_PKind_Human && source.ckind == CKIND_MARIO &&
                source.stocks == 4 && source_port == source_slot,
                "Runtime match identity requires two source Mario players on ports zero and one with four stocks");
        require(compatibility.controller == source_port && compatibility.stocks == source.stocks &&
                compatibility.costume == source.color &&
                compatibility.sub_color == source.sub_color,
                "Runtime match identity controller/compatibility view differs from source player data");
    }
}

// Encoding primitive used by the focused control. Production callers must
// enter through serialize_live_match_identity(), which binds this source data
// to a checked live GameplayMatchSession owner tuple before and after reading.
inline std::string serialize_selection_for_control(
    const MeleeWebMenuMatchSelection& selection, uint32_t current_rng,
    uint32_t source_frame, const RuntimeMatchIdentityHostAgreement& host) {
    validate_host_agreement(host);
    validate_pointer_free_start(selection.start.rules);
    validate_ordinary_mario_fd_stock_start(selection);

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema\":\"melee-web-runtime-match-identity-v1\""
        << ",\"runtime_sha256\":\"" << host.runtime_sha256 << '\"'
        << ",\"asset_identity_sha256\":\"" << host.asset_identity_sha256 << '\"'
        << ",\"save_profile_sha256\":\"" << host.save_profile_sha256 << '\"'
        << ",\"profile_identity\":\"" << host.profile_identity << '\"'
        << ",\"source_frame\":" << source_frame
        << ",\"initial_selection_seed\":\""
        << melee_web::stadium_first_css_diagnostic::hex32(selection.random_seed) << '\"'
        << ",\"current_rng\":\""
        << melee_web::stadium_first_css_diagnostic::hex32(current_rng) << '\"'
        << ",\"selection\":{\"player_count\":" << selection.player_count
        << ",\"hud_layout\":" << selection.hud_layout
        << ",\"unlocked_characters\":" << selection.unlocked_characters
        << ",\"unlocked_stages\":" << selection.unlocked_stages
        << ",\"save_profile_present\":" << unsigned(selection.save_profile_present)
        << ",\"opening_demo\":" << unsigned(selection.opening_demo)
        << ",\"sudden_death\":" << unsigned(selection.sudden_death)
        << ",\"controller_mapping\":[";
    for (unsigned source_slot = 0; source_slot < 2; ++source_slot) {
        if (source_slot) out << ',';
        const auto& source = selection.start.players[source_slot];
        const unsigned source_port = source.slot ? source.slot - 1u : source_slot;
        out << "{\"source_slot\":" << source_slot
            << ",\"source_port\":" << source_port
            << ",\"raw_controller\":" << selection.players[source_slot].controller << '}';
    }
    out << "],\"start\":";
    melee_web::stadium_first_css_diagnostic::write_start(out, selection.start);
    out << "}}";
    return out.str();
}

inline bool same_live_owners(const GameplayMatchCpuOwners& a,
                             const GameplayMatchCpuOwners& b) noexcept {
    return a.storage == b.storage && a.context == b.context && a.flow == b.flow &&
        a.audio == b.audio && a.world == b.world &&
        a.context_epoch == b.context_epoch && a.frame == b.frame;
}

} // namespace detail

// Read a source-owned ordinary VS selection into a pointer-free host agreement
// value. This proves a live constructed match owner and stable source frame/
// epoch around the read; it does not establish GPU or Asyncify idleness.
inline std::string serialize_live_match_identity(
    const GameplayMatchSession& session,
    const RuntimeMatchIdentityHostAgreement& host) {
    const auto before = session.diagnostic_cpu_owners();
    detail::require(before.storage && before.context && before.flow && before.audio &&
                    before.world != 0,
                    "Runtime match identity requires a live constructed source match owner");
    detail::require(session.diagnostic_source_files_quiescent(),
                    "Runtime match identity requires quiescent source files");

    const auto& selection = session.start_selection();
    const uint32_t current_rng = session.random_seed();
    const uint32_t source_frame = session.source_frames();
    detail::require(before.frame == source_frame,
                    "Runtime match identity source frame differs from its live owner tuple");
    const std::string identity = detail::serialize_selection_for_control(
        selection, current_rng, source_frame, host);

    const auto& selection_after = session.start_selection();
    const uint32_t rng_after = session.random_seed();
    const uint32_t frame_after = session.source_frames();
    const auto after = session.diagnostic_cpu_owners();
    detail::require(session.diagnostic_source_files_quiescent() &&
                    detail::same_live_owners(before, after) && frame_after == source_frame &&
                    rng_after == current_rng,
                    "Runtime match identity source owner, epoch, RNG, or frame changed during read");
    detail::require(detail::serialize_selection_for_control(
                        selection_after, rng_after, frame_after, host) == identity,
                    "Runtime match selection changed during identity read");
    return identity;
}

} // namespace melee_web::slippi_profile
