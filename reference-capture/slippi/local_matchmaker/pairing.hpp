// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <string_view>

namespace local_matchmaker
{
struct Ticket
{
  std::uint64_t peer_id{};
  std::string uid;
  std::string connect_code;
  std::string display_name;
  std::string target_code;
  std::uint16_t peer_port{};
  std::chrono::steady_clock::time_point queued_at{};
};

struct Pair
{
  Ticket first;
  Ticket second;
};

class PairingRegistry
{
public:
  bool can_submit(const Ticket& ticket) const;
  std::optional<Pair> submit(Ticket ticket);
  bool cancel(std::uint64_t peer_id);
  std::vector<Ticket> expire(std::chrono::steady_clock::time_point now,
                             std::chrono::seconds timeout);
  std::size_t pending_count() const;

private:
  std::vector<Ticket> m_pending;
};
}  // namespace local_matchmaker
