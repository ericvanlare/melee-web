#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <dolphin/pad.h>

namespace melee_web {

constexpr std::size_t kGameplayReplayGameInfoSize = 0x138;

struct GameplayReplayPlayer {
    uint8_t source_port = 0;
    uint8_t source_character = 0;
    uint8_t character = 0;
    uint8_t player_type = 0;
    uint8_t source_stocks = 0;
    uint8_t stocks = 4;
    uint8_t costume = 0;
};

struct GameplayReplayFrame {
    int32_t number = 0;
    std::array<PADStatus, 2> pads{};
};

struct GameplayReplayTransport {
    uint16_t schema_version = 0;
    uint16_t flags = 0;
    std::array<uint8_t, 4> slippi_version{};
    int32_t first_frame = 0;
    int32_t last_frame = 0;
    uint32_t initial_seed = 0;
    uint16_t stage = 0;
    uint16_t source_stage = 0;
    std::array<uint8_t, 32> source_sha256{};
    std::array<uint8_t, kGameplayReplayGameInfoSize> source_game_info{};
    std::array<GameplayReplayPlayer, 2> players{};
    std::vector<GameplayReplayFrame> frames;

    bool physical_input() const noexcept { return flags & (1u << 0); }
    bool derived_setup() const noexcept { return flags & (1u << 1); }
};

GameplayReplayTransport load_gameplay_replay_transport(
    const std::filesystem::path& path);

} // namespace melee_web
