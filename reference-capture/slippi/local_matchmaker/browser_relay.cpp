// SPDX-License-Identifier: MIT
#include "browser_relay.hpp"

#include <algorithm>
#include <string>

namespace local_matchmaker
{
namespace
{
constexpr std::uint8_t kSlippiPadMessage = 0x80;
constexpr std::uint8_t kBrowserInputPlayerPort = 0;

int hex_nibble(char value)
{
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

std::int32_t read_be_i32(const std::uint8_t* data)
{
  const auto value = (static_cast<std::uint32_t>(data[0]) << 24) |
                     (static_cast<std::uint32_t>(data[1]) << 16) |
                     (static_cast<std::uint32_t>(data[2]) << 8) |
                     static_cast<std::uint32_t>(data[3]);
  return static_cast<std::int32_t>(value);
}
}  // namespace

std::optional<std::vector<std::string>> BrowserInputLineBuffer::append(
    std::string_view bytes)
{
  std::vector<std::string> lines;
  std::size_t offset = 0;
  while (offset < bytes.size())
  {
    const auto newline = bytes.find('\n', offset);
    const auto count = newline == std::string_view::npos ? bytes.size() - offset
                                                         : newline - offset;
    if (count > kMaximumBrowserInputLineBytes - m_partial_line.size())
      return std::nullopt;
    m_partial_line.append(bytes.data() + offset, count);
    if (newline == std::string_view::npos)
      break;

    lines.emplace_back(std::move(m_partial_line));
    m_partial_line.clear();
    offset = newline + 1;
  }
  return lines;
}

bool BrowserPadQueue::valid_session_id(std::string_view session_id)
{
  return session_id.size() == 32 &&
         std::all_of(session_id.begin(), session_id.end(), [](char value) {
           return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
         });
}

bool BrowserPadQueue::open_session(std::string session_id)
{
  if (!valid_session_id(session_id))
    return false;

  m_session_id = std::move(session_id);
  m_latest_queued_frame = 0;
  m_frames.clear();
  m_target_frames.clear();
  m_observed_frames.clear();
  return true;
}

bool BrowserPadQueue::close_session(std::string_view session_id)
{
  if (m_session_id.empty() || session_id != m_session_id)
    return false;

  m_session_id.clear();
  m_latest_queued_frame = 0;
  m_frames.clear();
  m_target_frames.clear();
  m_observed_frames.clear();
  return true;
}

bool BrowserPadQueue::add(
    std::string_view session_id, std::int32_t frame,
    const std::array<std::uint8_t, kSlippiPadRecordBytes>& bytes)
{
  if (m_session_id.empty() || session_id != m_session_id || frame < 1 || frame > 1'000'000 ||
      frame <= m_latest_queued_frame || m_frames.size() >= kMaximumBrowserPadFrames ||
      m_target_frames.size() >= kMaximumBrowserPadFrames)
  {
    return false;
  }

  m_latest_queued_frame = frame;
  m_frames.emplace(frame, bytes);
  m_target_frames.emplace(frame, true);
  return true;
}

bool BrowserPadQueue::wants_peer_observation(std::int32_t frame) const
{
  return !m_session_id.empty() && m_target_frames.contains(frame);
}

bool BrowserPadQueue::mark_peer_observed(std::int32_t frame)
{
  if (!wants_peer_observation(frame) || m_observed_frames.contains(frame))
    return false;
  m_observed_frames.emplace(frame, true);
  return true;
}

std::vector<BrowserPadReplacement> BrowserPadQueue::replace_pad_packet(
    std::vector<std::uint8_t>& packet, std::uint8_t source_player_port)
{
  std::vector<BrowserPadReplacement> replacements;
  if (m_session_id.empty() || packet.size() < kSlippiPadHeaderBytes ||
      packet[0] != kSlippiPadMessage || packet[5] != source_player_port ||
      source_player_port != kBrowserInputPlayerPort ||
      (packet.size() - kSlippiPadHeaderBytes) % kSlippiPadRecordBytes != 0)
  {
    return replacements;
  }

  const auto latest_frame = read_be_i32(packet.data() + 1);
  const auto record_count = (packet.size() - kSlippiPadHeaderBytes) / kSlippiPadRecordBytes;
  if (latest_frame < 1 || record_count > kMaximumBrowserPadFrames ||
      record_count > static_cast<std::size_t>(latest_frame))
  {
    return replacements;
  }

  for (std::size_t record_index = 0; record_index < record_count; ++record_index)
  {
    const auto frame = latest_frame - static_cast<std::int32_t>(record_index);
    const auto found = m_frames.find(frame);
    if (found == m_frames.end())
      continue;

    const auto offset = kSlippiPadHeaderBytes + record_index * kSlippiPadRecordBytes;
    std::copy(found->second.begin(), found->second.end(), packet.begin() + offset);
    replacements.push_back({m_session_id, {frame, found->second}});
    m_frames.erase(found);
  }

  if (record_count > 0)
  {
    const auto oldest_frame = latest_frame - static_cast<std::int32_t>(record_count) + 1;
    auto stale = m_frames.begin();
    while (stale != m_frames.end() && stale->first < oldest_frame)
      stale = m_frames.erase(stale);
  }
  return replacements;
}

std::optional<std::array<std::uint8_t, kSlippiPadRecordBytes>> parse_pad_hex(
    std::string_view hex)
{
  if (hex.size() != kSlippiPadRecordBytes * 2)
    return std::nullopt;

  std::array<std::uint8_t, kSlippiPadRecordBytes> bytes{};
  for (std::size_t index = 0; index < bytes.size(); ++index)
  {
    const int high = hex_nibble(hex[index * 2]);
    const int low = hex_nibble(hex[index * 2 + 1]);
    if (high < 0 || low < 0)
      return std::nullopt;
    bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return bytes;
}

std::string pad_hex(const std::array<std::uint8_t, kSlippiPadRecordBytes>& bytes)
{
  constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2);
  for (const auto byte : bytes)
  {
    hex.push_back(digits[byte >> 4]);
    hex.push_back(digits[byte & 0x0f]);
  }
  return hex;
}
}  // namespace local_matchmaker
