// SPDX-License-Identifier: MIT
#include "pairing.hpp"

#include <algorithm>

namespace local_matchmaker
{
bool PairingRegistry::can_submit(const Ticket& ticket) const
{
  return std::none_of(m_pending.begin(), m_pending.end(), [&](const Ticket& other) {
    return other.peer_id == ticket.peer_id || other.uid == ticket.uid ||
           other.peer_port == ticket.peer_port;
  });
}

std::optional<Pair> PairingRegistry::submit(Ticket ticket)
{
  if (!can_submit(ticket))
    return std::nullopt;

  const auto partner = std::find_if(m_pending.begin(), m_pending.end(), [&](const Ticket& other) {
    return other.peer_id != ticket.peer_id && other.uid != ticket.uid &&
           other.peer_port != ticket.peer_port && other.target_code == ticket.connect_code &&
           ticket.target_code == other.connect_code;
  });

  if (partner == m_pending.end())
  {
    m_pending.push_back(std::move(ticket));
    return std::nullopt;
  }

  Pair pair{*partner, std::move(ticket)};
  m_pending.erase(partner);
  return pair;
}

bool PairingRegistry::cancel(std::uint64_t peer_id)
{
  const auto before = m_pending.size();
  std::erase_if(m_pending, [peer_id](const Ticket& ticket) { return ticket.peer_id == peer_id; });
  return m_pending.size() != before;
}

std::vector<Ticket> PairingRegistry::expire(std::chrono::steady_clock::time_point now,
                                            std::chrono::seconds timeout)
{
  std::vector<Ticket> expired;
  auto entry = m_pending.begin();
  while (entry != m_pending.end())
  {
    if (now - entry->queued_at >= timeout)
    {
      expired.push_back(std::move(*entry));
      entry = m_pending.erase(entry);
    }
    else
    {
      ++entry;
    }
  }
  return expired;
}

std::size_t PairingRegistry::pending_count() const
{
  return m_pending.size();
}
}  // namespace local_matchmaker
