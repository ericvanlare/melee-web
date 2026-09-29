// SPDX-License-Identifier: MIT
#pragma once

#include "pairing.hpp"

#include <nlohmann/json.hpp>

namespace local_matchmaker
{
struct ParseResult
{
  Ticket ticket;
  std::string error;

  explicit operator bool() const { return error.empty(); }
};

ParseResult parse_create_ticket(const nlohmann::json& message, std::uint64_t peer_id,
                                std::chrono::steady_clock::time_point queued_at);
bool parse_loopback_endpoint(const std::string& endpoint, std::uint16_t* port);
}  // namespace local_matchmaker
