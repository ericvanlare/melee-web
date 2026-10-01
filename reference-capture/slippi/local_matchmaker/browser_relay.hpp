// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace local_matchmaker
{
inline constexpr std::size_t kSlippiPadHeaderBytes = 14;
inline constexpr std::size_t kSlippiPadRecordBytes = 8;
inline constexpr std::size_t kMaximumBrowserPadFrames = 128;
inline constexpr std::size_t kMaximumBrowserInputLineBytes = 1024;

class BrowserInputLineBuffer
{
public:
  std::optional<std::vector<std::string>> append(std::string_view bytes);

private:
  std::string m_partial_line;
};

struct BrowserPadFrame
{
  std::int32_t frame{};
  std::array<std::uint8_t, kSlippiPadRecordBytes> bytes{};
};

struct BrowserPadReplacement
{
  std::string session_id;
  BrowserPadFrame pad;
};

class BrowserPadQueue
{
public:
  bool open_session(std::string session_id);
  bool close_session(std::string_view session_id);
  bool add(std::string_view session_id, std::int32_t frame,
           const std::array<std::uint8_t, kSlippiPadRecordBytes>& bytes);

  std::string_view session_id() const { return m_session_id; }
  std::size_t size() const { return m_frames.size(); }
  bool wants_peer_observation(std::int32_t frame) const;
  bool mark_peer_observed(std::int32_t frame);

  std::vector<BrowserPadReplacement> replace_pad_packet(std::vector<std::uint8_t>& packet,
                                                        std::uint8_t source_player_port);

private:
  static bool valid_session_id(std::string_view session_id);

  std::string m_session_id;
  std::int32_t m_latest_queued_frame{};
  std::map<std::int32_t, std::array<std::uint8_t, kSlippiPadRecordBytes>> m_frames;
  std::map<std::int32_t, bool> m_target_frames;
  std::map<std::int32_t, bool> m_observed_frames;
  std::map<std::int32_t, bool> m_applied_frames;
};

std::optional<std::array<std::uint8_t, kSlippiPadRecordBytes>> parse_pad_hex(
    std::string_view hex);
std::string pad_hex(const std::array<std::uint8_t, kSlippiPadRecordBytes>& bytes);
}  // namespace local_matchmaker
