#include "gameplay_replay_transport.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace melee_web {
namespace {

constexpr uint16_t kFlagPhysicalInput = 1u << 0;
constexpr uint16_t kFlagDerivedSetup = 1u << 1;
constexpr uint32_t kMaxFrameCount = 36000;
constexpr std::size_t kHeaderSize = 66;
constexpr std::size_t kPlayerSize = 8;
constexpr std::size_t kFrameSize = 4;
constexpr std::size_t kPadSize = 12;
constexpr std::size_t kMaxTransportSize = 16 * 1024 * 1024;

class Reader {
public:
    explicit Reader(const std::filesystem::path& path)
        : stream_(path, std::ios::binary) {
        if (!stream_)
            throw std::runtime_error("Cannot open replay transport");
        try {
            const auto size = std::filesystem::file_size(path);
            if (size > kMaxTransportSize)
                throw std::runtime_error("Replay transport is too large");
            size_ = static_cast<std::size_t>(size);
        } catch (const std::filesystem::filesystem_error&) {
            throw std::runtime_error("Cannot stat replay transport");
        }
    }

    std::size_t size() const noexcept { return size_; }
    std::size_t position() const noexcept { return position_; }

    uint8_t u8() {
        require_bytes(1);
        unsigned char value = 0;
        stream_.read(reinterpret_cast<char*>(&value), 1);
        require();
        ++position_;
        return value;
    }

    int8_t i8() { return std::bit_cast<int8_t>(u8()); }

    uint16_t u16() {
        // Keep each read sequenced. Combining u8() calls directly in a bitwise
        // expression leaves their evaluation order unspecified in C++.
        const uint16_t high = u8();
        const uint16_t low = u8();
        return static_cast<uint16_t>((high << 8) | low);
    }

    uint32_t u32() {
        const uint32_t b0 = u8();
        const uint32_t b1 = u8();
        const uint32_t b2 = u8();
        const uint32_t b3 = u8();
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }

    int32_t i32() { return std::bit_cast<int32_t>(u32()); }

    template <std::size_t N>
    std::array<uint8_t, N> bytes() {
        require_bytes(N);
        std::array<uint8_t, N> result{};
        stream_.read(reinterpret_cast<char*>(result.data()),
                     static_cast<std::streamsize>(N));
        require();
        position_ += N;
        return result;
    }

    void skip(std::size_t count) {
        require_bytes(count);
        if (count > static_cast<std::size_t>(
                        std::numeric_limits<std::streamsize>::max()))
            throw std::runtime_error("Replay transport skip is too large");
        stream_.ignore(static_cast<std::streamsize>(count));
        require();
        position_ += count;
    }

    bool at_end() const noexcept { return position_ == size_; }

private:
    void require_bytes(std::size_t count) const {
        if (count > size_ - position_)
            throw std::runtime_error("Truncated replay transport");
    }

    void require() const {
        if (!stream_)
            throw std::runtime_error("Truncated replay transport");
    }

    std::ifstream stream_;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
};

uint16_t game_info_u16(
    const std::array<uint8_t, kGameplayReplayGameInfoSize>& game_info,
    std::size_t offset) {
    if (offset + 2 > game_info.size())
        throw std::runtime_error("Replay source Game Info bounds are invalid");
    const uint16_t high = game_info[offset];
    const uint16_t low = game_info[offset + 1];
    return static_cast<uint16_t>((high << 8) | low);
}

} // namespace

GameplayReplayTransport load_gameplay_replay_transport(
    const std::filesystem::path& path) {
    Reader in(path);
    if (in.size() < kHeaderSize)
        throw std::runtime_error("Truncated replay transport header");
    if (in.bytes<4>() != std::array<uint8_t, 4>{'M', 'W', 'R', 'P'})
        throw std::runtime_error("Replay transport magic is invalid");

    GameplayReplayTransport result;
    result.schema_version = in.u16();
    if (result.schema_version != 2)
        throw std::runtime_error("Replay transport schema is unsupported");
    result.flags = in.u16();
    if (result.flags != (kFlagPhysicalInput | kFlagDerivedSetup))
        throw std::runtime_error(
            "Replay transport flags are unsupported; expected derived input workload");
    result.slippi_version = in.bytes<4>();
    const uint32_t frame_count = in.u32();
    result.first_frame = in.i32();
    result.last_frame = in.i32();
    result.initial_seed = in.u32();
    result.stage = in.u16();
    result.source_stage = in.u16();
    const uint8_t player_count = in.u8();
    const uint8_t reserved = in.u8();
    result.source_sha256 = in.bytes<32>();

    if (reserved != 0 || player_count != 2 || !frame_count ||
        frame_count > kMaxFrameCount)
        throw std::runtime_error("Replay transport frame/player bounds are invalid");
    if (std::all_of(result.source_sha256.begin(), result.source_sha256.end(),
                    [](uint8_t value) { return value == 0; }))
        throw std::runtime_error("Replay transport source SHA-256 is invalid");
    const int64_t frame_span = static_cast<int64_t>(result.last_frame) -
                               static_cast<int64_t>(result.first_frame) + 1;
    if (frame_span != static_cast<int64_t>(frame_count))
        throw std::runtime_error("Replay transport frame bounds are invalid");

    // The payload is fixed width. Check its complete size before reading any
    // source identity or input records so truncation and appended data fail
    // during validation rather than halfway through match setup.
    const uint64_t expected_size =
        static_cast<uint64_t>(kHeaderSize) + kGameplayReplayGameInfoSize +
        2 * kPlayerSize + static_cast<uint64_t>(frame_count) *
                              (kFrameSize + 2 * kPadSize);
    if (expected_size != in.size())
        throw std::runtime_error("Replay transport size bounds are invalid");

    result.source_game_info = in.bytes<kGameplayReplayGameInfoSize>();
    if (game_info_u16(result.source_game_info, 0xE) != result.source_stage)
        throw std::runtime_error(
            "Replay source Game Info stage does not match transport identity");

    for (auto& player : result.players) {
        player.source_port = in.u8();
        player.source_character = in.u8();
        player.character = in.u8();
        player.player_type = in.u8();
        player.source_stocks = in.u8();
        player.stocks = in.u8();
        player.costume = in.u8();
        const uint8_t reserved_player = in.u8();
        if (reserved_player != 0 || player.stocks != 4)
            throw std::runtime_error(
                "Replay transport derived stock setup is invalid");
    }
    if (result.players[0].source_port != 1 ||
        result.players[1].source_port != 2)
        throw std::runtime_error(
            "Replay transport only supports source ports 1 and 2 mapped to runtime slots 0 and 1");

    // Verify source player identity against the retained Game Info bytes. The
    // workload character remains separate and may be an explicit override.
    for (unsigned index = 0; index < 2; ++index) {
        const std::size_t offset = 0x60 + 0x24 * index;
        const auto& player = result.players[index];
        if (result.source_game_info[offset] != player.source_character ||
            result.source_game_info[offset + 1] != player.player_type ||
            result.source_game_info[offset + 2] != player.source_stocks ||
            result.source_game_info[offset + 3] != player.costume)
            throw std::runtime_error(
                "Replay transport source player identity is inconsistent");
    }

    result.frames.reserve(frame_count);
    for (uint32_t index = 0; index < frame_count; ++index) {
        GameplayReplayFrame frame;
        frame.number = in.i32();
        const int64_t expected_frame =
            static_cast<int64_t>(result.first_frame) + index;
        if (static_cast<int64_t>(frame.number) != expected_frame)
            throw std::runtime_error(
                "Replay transport frame sequence is not contiguous");
        for (auto& pad : frame.pads) {
            pad = PADStatus{};
            pad.button = in.u16();
            pad.stickX = in.i8();
            pad.stickY = in.i8();
            pad.substickX = in.i8();
            pad.substickY = in.i8();
            pad.triggerLeft = in.u8();
            pad.triggerRight = in.u8();
            pad.analogA = in.u8();
            pad.analogB = in.u8();
            const uint16_t reserved_pad = in.u16();
            if (reserved_pad != 0 || pad.triggerLeft > 140 ||
                pad.triggerRight > 140)
                throw std::runtime_error("Replay transport PAD bounds are invalid");
            pad.err = PAD_ERR_NONE;
        }
        result.frames.push_back(frame);
    }
    if (!in.at_end())
        throw std::runtime_error("Replay transport has trailing bytes");
    return result;
}

} // namespace melee_web
