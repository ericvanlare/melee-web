#include "gameplay_results_entry_packet.hpp"
extern "C" {
#include <sysdolphin/baselib/controller.h>
PadLibData HSD_PadLibData{};
HSD_PadStatus HSD_PadMasterStatus[4]{}, HSD_PadCopyStatus[4]{}, HSD_PadGameStatus[4]{};
}
#include <cassert>
#include <iostream>

int main()
{
    melee_web::ResultsEntryPacket packet;
    assert(packet.json() == "null");
    HSD_PadLibData.repeat_start = 45;
    HSD_PadLibData.repeat_interval = 8;
    HSD_PadLibData.scale_stick = 80;
    HSD_PadLibData.scale_analogLR = HSD_PadLibData.scale_analogAB = 140;
    HSD_PadStatus* histories[] = {HSD_PadMasterStatus,HSD_PadCopyStatus,HSD_PadGameStatus};
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned port = 0; port < 4; ++port) {
        histories[bank][port].button = PAD_BUTTON_START | (bank * 4 + port);
        histories[bank][port].last_button = PAD_BUTTON_A;
        histories[bank][port].repeat_count = 100 + bank * 4 + port;
        histories[bank][port].stickX = -37;
        histories[bank][port].nml_stickX = -0.5f;
    }
    uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(final_input); // production capture, before teardown
    const auto retained = std::to_array(final_input);

    // Model teardown's restoration of external PAD owners. The supplied entry
    // buffer must win over these now-different globals; a late capture is wrong.
    HSD_PadLibData.repeat_start = 99;
    for (auto* bank : histories) for (unsigned port = 0; port < 4; ++port)
        bank[port].button = PAD_BUTTON_B;
    uint8_t restored[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(restored);
    assert(std::memcmp(final_input, restored, sizeof(restored)) != 0);

    MatchExitInfo terminal{};
    terminal.x0 = -9; terminal.x4 = 0x12345678; terminal.x8 = 7;
    terminal.match_end.frame_count = 11725;
    terminal.match_end.pad_x186C[sizeof(terminal.match_end.pad_x186C)-1] = 0xa7;
    ResultsMatchInfo result{};
    result.match_end = terminal.match_end;
    result.x0_0 = 1; result.x0_1 = 1; result.x1 = 0x5a; result.x4 = -11;
    result.match_end.outcome = OUTCOME_ELIMINATION;
    result.match_end.match_kind = 1;
    result.match_end.n_winners = 1; result.match_end.winners[0] = 2;
    result.match_end.player_standings[2].ckind = CKIND_SEAK;
    result.match_end.player_standings[2].ftkind = FTKIND_SEAK;
    result.match_end.player_standings[2].stocks = -1;
    const auto original_terminal = terminal;
    const auto original_result = result;
    uint32_t seed = 0xfedcba98;
    packet.capture(1, terminal, result, seed, final_input);
    assert(seed == 0xfedcba98);
    assert(std::memcmp(&terminal, &original_terminal, sizeof(terminal)) == 0);
    assert(std::memcmp(&result, &original_result, sizeof(result)) == 0);
    assert(std::memcmp(final_input, retained.data(), sizeof(final_input)) == 0);
    const auto first = packet.json();
    std::memset(&terminal, 0, sizeof(terminal));
    std::memset(&result, 0, sizeof(result));
    std::memset(final_input, 0, sizeof(final_input));
    assert(packet.json() == first); // owns the copy, not live references
    uint8_t after_read[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(after_read);
    assert(std::memcmp(after_read, restored, sizeof(restored)) == 0);
    std::cout << first << '\n';
    packet.capture(2, terminal, result, 123, final_input);
    std::cout << packet.json() << '\n';
}
