/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slippi_profile/runtime_match_identity.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using melee_web::GameplayMatchSession;
using melee_web::slippi_profile::RuntimeMatchIdentityHostAgreement;
using melee_web::slippi_profile::detail::serialize_selection_for_control;

static_assert(std::is_same_v<
    decltype(std::declval<const GameplayMatchSession&>().start_selection()),
    const MeleeWebMenuMatchSelection&>);

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void callback_player(int) {}
int callback_int(int) { return 0; }
int callback_none() { return 0; }
void callback_event() {}
void callback_end(uint8_t) {}

RuntimeMatchIdentityHostAgreement host_agreement() {
    return {
        "1111111111111111111111111111111111111111111111111111111111111111",
        "2222222222222222222222222222222222222222222222222222222222222222",
        "3333333333333333333333333333333333333333333333333333333333333333",
        SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID,
    };
}

MeleeWebMenuMatchSelection ordinary_source_selection() {
    MeleeWebMenuMatchSelection selection{};
    selection.random_seed = 0x01234567u;
    selection.player_count = 2;
    selection.hud_layout = 2;
    selection.unlocked_characters = 0x1234;
    selection.unlocked_stages = 0x5678;
    selection.save_profile_present = 1;
    selection.start.rules.match_kind = MatchKind_Stock;
    selection.start.rules.x0_3 = selection.hud_layout;
    selection.start.rules.is_vs = 1;
    selection.start.rules.is_stock = 1;
    selection.start.rules.stkind = St_Kind_Last;
    selection.start.rules.x2C = 1.0f;
    selection.start.rules.x30 = 1.0f;
    selection.start.rules.game_speed = 1.0f;
    for (unsigned i = 2; i < GM_MAX_PLAYERS; ++i)
        selection.start.players[i].slot_type = Gm_PKind_NA;
    for (unsigned i = 0; i < 2; ++i) {
        auto& player = selection.start.players[i];
        player.ckind = CKIND_MARIO;
        player.slot_type = Gm_PKind_Human;
        player.stocks = 4;
        player.slot = static_cast<uint8_t>(i + 1);
        selection.players[i].controller = i;
        selection.players[i].stocks = 4;
    }
    return selection;
}

std::string serialize(const MeleeWebMenuMatchSelection& selection,
                      const RuntimeMatchIdentityHostAgreement& host = host_agreement()) {
    return serialize_selection_for_control(selection, 0x89abcdefu, 17u, host);
}

void expect_refusal(const MeleeWebMenuMatchSelection& selection,
                    const RuntimeMatchIdentityHostAgreement& host,
                    const char* message) {
    try {
        (void) serialize(selection, host);
    } catch (const std::runtime_error&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_source_fields_and_exact_encoding() {
    const auto selection = ordinary_source_selection();
    const auto encoded = serialize(selection);
    require(encoded.find("\"schema\":\"melee-web-runtime-match-identity-v1\"") != std::string::npos,
            "identity schema missing");
    require(encoded.find("\"runtime_sha256\":\"111111") != std::string::npos,
            "runtime identity missing");
    require(encoded.find("\"profile_identity\":\"" SLIPPI_PROFILE_CAMERA_HISTORY_V20_ID "\"") != std::string::npos,
            "active V20 identity missing");
    require(encoded.find("\"source_frame\":17") != std::string::npos,
            "source frame missing");
    require(encoded.find("\"initial_selection_seed\":\"01234567\"") != std::string::npos,
            "initial selection seed not preserved");
    require(encoded.find("\"current_rng\":\"89abcdef\"") != std::string::npos,
            "current RNG not serialized separately");
    require(encoded.find("\"controller_mapping\":[{\"source_slot\":0,\"source_port\":0,\"raw_controller\":0},{\"source_slot\":1,\"source_port\":1,\"raw_controller\":1}]") != std::string::npos,
            "source slot/controller mapping missing");
    require(encoded.find("\"player_count\":2") != std::string::npos &&
            encoded.find("\"hud_layout\":2") != std::string::npos &&
            encoded.find("\"unlocked_characters\":4660") != std::string::npos &&
            encoded.find("\"unlocked_stages\":22136") != std::string::npos &&
            encoded.find("\"save_profile_present\":1") != std::string::npos,
            "menu selection fields missing");
    require(encoded.find("\"on_match_start\":\"null\"") != std::string::npos &&
            encoded.find("\"x54_pointer\":\"null\"") != std::string::npos &&
            encoded.find("\"x58_pointer\":\"null\"") != std::string::npos,
            "pointer-free source writer output missing");

    auto changed_rule = selection;
    changed_rule.start.rules.x5_7 = 1;
    require(serialize(changed_rule) != encoded &&
            serialize(changed_rule).find("\"x5_7\":1") != std::string::npos,
            "a source rule bit omitted by older checksums was not bound");

    auto changed_inactive = selection;
    changed_inactive.start.players[4].x12 = 0x1234;
    require(serialize(changed_inactive) != encoded &&
            serialize(changed_inactive).find("\"damage_12\":4660") != std::string::npos,
            "inactive source player fields were not bound");

    auto changed_float = selection;
    changed_float.start.rules.x2C = std::bit_cast<float>(0x3f800001u);
    require(serialize(changed_float) != encoded &&
            serialize(changed_float).find("\"x2C_bits\":\"3f800001\"") != std::string::npos,
            "source float bits were rounded or omitted");

    auto changed_item_high_bit = selection;
    changed_item_high_bit.start.rules.x20 = uint64_t(1) << 63;
    require(serialize(changed_item_high_bit) != encoded &&
            serialize(changed_item_high_bit).find("\"item_mask\":\"8000000000000000\"") != std::string::npos,
            "64-bit source item-mask high bit was lost");
}

void test_all_source_callback_and_pointer_categories_refuse() {
    const auto baseline = ordinary_source_selection();
    const auto host = host_agreement();
    auto selection = baseline;

    selection.start.rules.on_unpause_override = callback_player;
    expect_refusal(selection, host, "nonnull unpause callback was accepted");
    selection = baseline; selection.start.rules.on_pause_override = callback_player;
    expect_refusal(selection, host, "nonnull pause callback was accepted");
    selection = baseline; selection.start.rules.check_for_pauser_override = callback_none;
    expect_refusal(selection, host, "nonnull pauser callback was accepted");
    selection = baseline; selection.start.rules.on_match_start = callback_event;
    expect_refusal(selection, host, "nonnull match-start callback was accepted");
    selection = baseline; selection.start.rules.on_frame_start = callback_event;
    expect_refusal(selection, host, "nonnull frame-start callback was accepted");
    selection = baseline; selection.start.rules.on_frame_end = callback_event;
    expect_refusal(selection, host, "nonnull frame-end callback was accepted");
    selection = baseline; selection.start.rules.on_match_end = callback_end;
    expect_refusal(selection, host, "nonnull match-end callback was accepted");

    int x54_storage{};
    selection = baseline;
    selection.start.rules.x54 = reinterpret_cast<decltype(selection.start.rules.x54)>(
        &x54_storage);
    expect_refusal(selection, host, "nonnull x54 data was accepted");
    struct lbl_8046B668_t x58{};
    selection = baseline; selection.start.rules.x58 = &x58;
    expect_refusal(selection, host, "nonnull x58 data was accepted");
}

void test_missing_or_unsupported_outer_and_menu_identity_refuses() {
    const auto baseline = ordinary_source_selection();
    auto host = host_agreement();
    host.runtime_sha256 = {};
    expect_refusal(baseline, host, "missing runtime hash was accepted");
    host = host_agreement(); host.asset_identity_sha256 = "ABC";
    expect_refusal(baseline, host, "malformed asset hash was accepted");
    host = host_agreement(); host.save_profile_sha256 = "";
    expect_refusal(baseline, host, "missing save profile hash was accepted");
    host = host_agreement(); host.profile_identity = "slippi-render-dependencies-logical-frame-lobj-tobj-tev-stage-scratch-v19";
    expect_refusal(baseline, host, "historical V19 identity was accepted");

    auto selection = baseline;
    selection.opening_demo = 1;
    expect_refusal(selection, host_agreement(), "opening-demo selection was accepted");
    selection = baseline; selection.sudden_death = 1;
    expect_refusal(selection, host_agreement(), "Sudden Death selection was accepted");
    selection = baseline; selection.start.rules.stkind = St_Kind_PStadium;
    expect_refusal(selection, host_agreement(), "alternate stage was accepted");
    selection = baseline; selection.start.players[1].slot = 3;
    selection.players[1].controller = 2;
    expect_refusal(selection, host_agreement(), "alternate controller port was accepted");
}
} // namespace

int main() {
    try {
        test_source_fields_and_exact_encoding();
        test_all_source_callback_and_pointer_categories_refuse();
        test_missing_or_unsupported_outer_and_menu_identity_refuses();
    } catch (const std::exception& error) {
        std::cerr << "Runtime match identity control failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Source-backed pointer-free match identity controls passed\n";
    return 0;
}
