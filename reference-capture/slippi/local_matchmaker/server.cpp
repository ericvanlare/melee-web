// SPDX-License-Identifier: MIT
#include "pairing.hpp"
#include "protocol.hpp"
#include "browser_relay.hpp"

#include <enet/enet.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace
{
using namespace std::chrono_literals;
using local_matchmaker::Pair;
using local_matchmaker::PairingRegistry;
using local_matchmaker::Ticket;
using Json = nlohmann::json;

constexpr std::string_view kBindIp = "127.0.0.1";
constexpr std::uint16_t kMatchmakingPort = 43113;
constexpr std::uint16_t kLocalHttpPort = 43114;
constexpr std::size_t kMaximumMessageBytes = 8192;
constexpr std::chrono::seconds kDefaultTicketTimeout{20};
constexpr std::uint32_t kMaximumPeers = 16;

std::atomic<bool> g_stopping{false};

void stop_signal(int)
{
  g_stopping.store(true, std::memory_order_relaxed);
}

class EventLog
{
public:
  explicit EventLog(const std::string& path) : m_stream(path, std::ios::app)
  {
    if (!m_stream)
      throw std::runtime_error("cannot open event log");
  }

  void write(Json event)
  {
    std::lock_guard lock(m_mutex);
    m_stream << event.dump() << '\n';
    m_stream.flush();
    if (!m_stream)
      throw std::runtime_error("cannot write event log");
  }

private:
  std::mutex m_mutex;
  std::ofstream m_stream;
};

std::uint64_t peer_id(const ENetPeer* peer)
{
  return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(peer));
}

std::string peer_endpoint(const Ticket& ticket, std::uint16_t relay_port = 0)
{
  return "127.0.0.1:" + std::to_string(relay_port == 0 ? ticket.peer_port : relay_port);
}

Json player_json(const Ticket& ticket, int port, bool local, std::uint16_t relay_port = 0)
{
  return {{"uid", ticket.uid},
          {"displayName", ticket.display_name},
          {"connectCode", ticket.connect_code},
          {"port", port},
          {"isBot", false},
          {"isLocalPlayer", local},
          {"ipAddress", peer_endpoint(ticket, relay_port)},
          {"ipAddressLan", peer_endpoint(ticket, relay_port)}};
}

bool send_json(ENetHost* host, ENetPeer* peer, const Json& message)
{
  const auto wire = message.dump();
  ENetPacket* packet = enet_packet_create(wire.data(), wire.size(), ENET_PACKET_FLAG_RELIABLE);
  if (packet == nullptr)
    return false;
  if (enet_peer_send(peer, 0, packet) < 0)
  {
    enet_packet_destroy(packet);
    return false;
  }
  enet_host_flush(host);
  return true;
}

class HttpSink
{
public:
  explicit HttpSink(EventLog& events) : m_events(events)
  {
    m_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_socket < 0)
      throw std::runtime_error("cannot create loopback HTTP sink socket");

    const int reuse = 1;
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    const int flags = fcntl(m_socket, F_GETFL, 0);
    if (flags < 0 || fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) < 0)
      throw std::runtime_error("cannot configure loopback HTTP sink socket");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kLocalHttpPort);
    if (inet_pton(AF_INET, kBindIp.data(), &address.sin_addr) != 1 ||
        bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(m_socket, 8) < 0)
    {
      throw std::runtime_error("cannot bind loopback HTTP sink on 127.0.0.1:43114");
    }
  }

  ~HttpSink()
  {
    if (m_socket >= 0)
      close(m_socket);
  }

  void start()
  {
    m_thread = std::thread([this] { serve(); });
  }

  void stop()
  {
    if (m_thread.joinable())
      m_thread.join();
  }

private:
  void serve()
  {
    while (!g_stopping.load(std::memory_order_relaxed))
    {
      fd_set ready;
      FD_ZERO(&ready);
      FD_SET(m_socket, &ready);
      timeval timeout{0, 200000};
      const int result = select(m_socket + 1, &ready, nullptr, nullptr, &timeout);
      if (result <= 0)
        continue;

      sockaddr_in source{};
      socklen_t source_length = sizeof(source);
      const int client = accept(m_socket, reinterpret_cast<sockaddr*>(&source), &source_length);
      if (client < 0)
        continue;

      const bool is_loopback = ntohl(source.sin_addr.s_addr) == INADDR_LOOPBACK;
      m_events.write({{"event", "local_api_connected"}, {"source_loopback", is_loopback}});
      const timeval client_timeout{0, 250000};
      if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &client_timeout,
                     sizeof(client_timeout)) < 0 ||
          setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &client_timeout,
                     sizeof(client_timeout)) < 0)
      {
        m_events.write({{"event", "local_api_client_dropped"},
                        {"reason", "could not configure bounded client I/O"}});
        close(client);
        continue;
      }

      char input[16384];
      const auto bytes = recv(client, input, sizeof(input) - 1, 0);
      std::string request_line;
      if (bytes > 0)
      {
        input[bytes] = '\0';
        const std::string_view received(input, static_cast<std::size_t>(bytes));
        const auto line_end = received.find("\r\n");
        request_line = std::string(received.substr(0, line_end));
        m_events.write({{"event", "local_api_rejected"},
                        {"request_line", request_line},
                        {"source_loopback", is_loopback}});
      }

      static constexpr std::string_view body =
          R"({"error":"local-only Slippi API is disabled"})";
      const std::string response =
          "HTTP/1.1 503 Service Unavailable\r\n"
          "Content-Type: application/json\r\n"
          "Connection: close\r\nContent-Length: " + std::to_string(body.size()) +
          "\r\n\r\n" + std::string(body);
      (void)send(client, response.data(), response.size(), 0);
      shutdown(client, SHUT_RDWR);
      close(client);
    }
  }

  EventLog& m_events;
  int m_socket{-1};
  std::thread m_thread;
};

Json assignment(const std::string& match_id, const Pair& pair, bool first_player,
                std::uint16_t relay_port = 0)
{
  return {{"type", "get-ticket-resp"},
          {"matchId", match_id},
          {"isHost", first_player},
          {"players", Json::array({player_json(pair.first, 1, first_player, relay_port),
                                    player_json(pair.second, 2, !first_player, relay_port)})},
          {"stages", Json::array({3, 8, 28, 31, 32, 2})},
          {"items", 0}};
}

class MatchmakingService
{
public:
  MatchmakingService(ENetHost* host, EventLog& events, std::chrono::seconds timeout,
                     std::uint16_t relay_port)
      : m_host(host), m_events(events), m_ticket_timeout(timeout), m_relay_port(relay_port)
  {
  }

  void process(const ENetEvent& event)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
      m_peer_has_ticket[event.peer] = false;
      m_events.write({{"event", "client_connected"}, {"peer", peer_id(event.peer)}});
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      receive(event.peer, event.packet);
      enet_packet_destroy(event.packet);
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      if (m_registry.cancel(peer_id(event.peer)))
        m_events.write({{"event", "ticket_cancelled"}, {"peer", peer_id(event.peer)}});
      m_events.write({{"event", "client_disconnected"}, {"peer", peer_id(event.peer)}});
      m_peer_has_ticket.erase(event.peer);
      break;
    default:
      break;
    }
  }

  void expire()
  {
    const auto expired = m_registry.expire(std::chrono::steady_clock::now(), m_ticket_timeout);
    for (const auto& ticket : expired)
    {
      auto* peer = find_peer(ticket.peer_id);
      if (peer)
      {
        send_json(m_host, peer,
                  {{"type", "get-ticket-resp"}, {"error", "Local direct pairing timed out"}});
        m_peer_has_ticket[peer] = false;
      }
      m_events.write({{"event", "ticket_timeout"}, {"uid", ticket.uid}});
    }
  }

  void cancel_all()
  {
    for (auto& [peer, has_ticket] : m_peer_has_ticket)
    {
      if (has_ticket)
      {
        if (m_registry.cancel(peer_id(peer)))
        {
          send_json(m_host, peer,
                    {{"type", "get-ticket-resp"}, {"error", "Local pairing service stopped"}});
          m_events.write({{"event", "ticket_cancelled"}, {"peer", peer_id(peer)}});
        }
        has_ticket = false;
      }
    }
    enet_host_flush(m_host);
  }

  std::size_t pending_count() const { return m_registry.pending_count(); }

private:
  ENetPeer* find_peer(std::uint64_t id) const
  {
    for (const auto& [peer, has_ticket] : m_peer_has_ticket)
    {
      if (has_ticket && peer_id(peer) == id && peer->state == ENET_PEER_STATE_CONNECTED)
        return peer;
    }
    return nullptr;
  }

  void reject(ENetPeer* peer, const std::string& reason)
  {
    send_json(m_host, peer, {{"type", "create-ticket-resp"}, {"error", reason}});
    m_events.write({{"event", "request_rejected"}, {"reason", reason}});
  }

  void receive(ENetPeer* peer, const ENetPacket* packet)
  {
    if (packet->dataLength == 0 || packet->dataLength > kMaximumMessageBytes)
    {
      reject(peer, "Request size is outside the local service limit");
      return;
    }

    const std::string wire(reinterpret_cast<const char*>(packet->data), packet->dataLength);
    const Json message = Json::parse(wire, nullptr, false);
    if (message.is_discarded())
    {
      reject(peer, "Malformed JSON request");
      return;
    }

    const auto state = m_peer_has_ticket.find(peer);
    if (state == m_peer_has_ticket.end() || state->second)
    {
      reject(peer, "One ticket is allowed per connection");
      return;
    }

    auto parsed = local_matchmaker::parse_create_ticket(
        message, peer_id(peer), std::chrono::steady_clock::now());
    if (!parsed)
    {
      reject(peer, parsed.error);
      return;
    }

    auto ticket = std::move(parsed.ticket);
    const auto ticket_uid = ticket.uid;
    if (!m_registry.can_submit(ticket))
    {
      reject(peer, "Duplicate local identity or peer port");
      return;
    }
    auto pair = m_registry.submit(std::move(ticket));
    state->second = true;
    send_json(m_host, peer, {{"type", "create-ticket-resp"}});
    m_events.write({{"event", "ticket_accepted"}, {"uid", ticket_uid}});

    if (!pair)
      return;

    const auto match_id = "local.mode.direct." + std::to_string(++m_next_match_id);
    auto* first_peer = find_peer(pair->first.peer_id);
    auto* second_peer = find_peer(pair->second.peer_id);
    if (!first_peer || !second_peer)
    {
      if (first_peer)
        send_json(m_host, first_peer,
                  {{"type", "get-ticket-resp"}, {"error", "Local peer left before assignment"}});
      if (second_peer)
        send_json(m_host, second_peer,
                  {{"type", "get-ticket-resp"}, {"error", "Local peer left before assignment"}});
      if (first_peer)
        m_peer_has_ticket[first_peer] = false;
      if (second_peer)
        m_peer_has_ticket[second_peer] = false;
      m_events.write({{"event", "pair_abandoned"}, {"match_id", match_id}});
      return;
    }

    const bool first_sent =
        send_json(m_host, first_peer, assignment(match_id, *pair, true, m_relay_port));
    const bool second_sent =
        send_json(m_host, second_peer, assignment(match_id, *pair, false, m_relay_port));
    m_peer_has_ticket[first_peer] = false;
    m_peer_has_ticket[second_peer] = false;
    m_events.write({{"event", "pair_assigned"},
                    {"match_id", match_id},
                    {"first_uid", pair->first.uid},
                    {"second_uid", pair->second.uid},
                    {"first_peer", peer_endpoint(pair->first, m_relay_port)},
                    {"second_peer", peer_endpoint(pair->second, m_relay_port)},
                    {"relay_mode", m_relay_port != 0},
                    {"both_assignments_sent", first_sent && second_sent}});
  }

  ENetHost* m_host;
  EventLog& m_events;
  std::chrono::seconds m_ticket_timeout;
  PairingRegistry m_registry;
  std::map<ENetPeer*, bool> m_peer_has_ticket;
  std::uint64_t m_next_match_id{};
  std::uint16_t m_relay_port{};
};

std::int32_t read_be_i32(const std::uint8_t* data)
{
  const auto value = (static_cast<std::uint32_t>(data[0]) << 24) |
                     (static_cast<std::uint32_t>(data[1]) << 16) |
                     (static_cast<std::uint32_t>(data[2]) << 8) |
                     static_cast<std::uint32_t>(data[3]);
  return static_cast<std::int32_t>(value);
}

class BrowserInputTail
{
public:
  BrowserInputTail(const std::string& path, EventLog& events)
      : m_path(path), m_events(events)
  {
  }

  void refresh(local_matchmaker::BrowserPadQueue& queue)
  {
    if (!m_enabled)
      return;
    std::ifstream input(m_path, std::ios::binary);
    if (!input)
      return;
    input.seekg(static_cast<std::streamoff>(m_offset));
    std::string added((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (m_offset > kMaximumBytes || added.size() > kMaximumBytes - m_offset)
    {
      m_enabled = false;
      reject("input log limit");
      return;
    }
    m_offset += added.size();
    const auto lines = m_lines.append(added);
    if (!lines)
    {
      m_enabled = false;
      reject("input line limit");
      return;
    }

    for (const auto& line : *lines)
    {
      if (m_enabled)
        process(line, queue);
    }
  }

private:
  void process(const std::string& line, local_matchmaker::BrowserPadQueue& queue)
  {
    std::istringstream stream(line);
    std::string command;
    std::string session;
    if (!(stream >> command >> session))
    {
      reject("malformed line");
      return;
    }

    if (command == "OPEN")
    {
      std::string extra;
      if (stream >> extra || !queue.open_session(session))
      {
        reject("invalid session open");
        return;
      }
      m_events.write({{"event", "browser_session_opened"}, {"session_id", session}});
      return;
    }
    if (command == "CLOSE")
    {
      std::string extra;
      if (stream >> extra || !queue.close_session(session))
      {
        reject("stale session close");
        return;
      }
      m_events.write({{"event", "browser_session_closed"}, {"session_id", session}});
      return;
    }
    if (command != "PAD")
    {
      reject("unknown browser input command");
      return;
    }

    std::string frame_text;
    std::string payload_hex;
    std::string extra;
    if (!(stream >> frame_text >> payload_hex) || (stream >> extra))
    {
      reject("malformed browser PAD input");
      return;
    }
    std::int32_t frame{};
    const auto [end, error] = std::from_chars(frame_text.data(),
                                               frame_text.data() + frame_text.size(), frame);
    const auto bytes = local_matchmaker::parse_pad_hex(payload_hex);
    if (error != std::errc{} || end != frame_text.data() + frame_text.size() || !bytes ||
        !queue.add(session, frame, *bytes))
    {
      reject("invalid, duplicate, out-of-order, stale, or over-limit browser PAD frame");
      return;
    }
    m_events.write({{"event", "browser_pad_queued"},
                    {"session_id", session},
                    {"frame", frame},
                    {"pad_hex", local_matchmaker::pad_hex(*bytes)}});
  }

  void reject(std::string_view reason)
  {
    m_events.write({{"event", "browser_input_rejected"}, {"reason", reason}});
  }

  static constexpr std::size_t kMaximumBytes = 64 * 1024;
  const std::string m_path;
  EventLog& m_events;
  std::uintmax_t m_offset{};
  local_matchmaker::BrowserInputLineBuffer m_lines;
  bool m_enabled{true};
};

class SlippiPeerRelay
{
public:
  SlippiPeerRelay(std::uint16_t port, EventLog& events, const std::string& input_path,
                  const std::string& browser_event_path)
      : m_events(events), m_input(input_path, events), m_browser_events(browser_event_path,
                                                                        std::ios::app)
  {
    if (!m_browser_events)
      throw std::runtime_error("cannot open local browser relay event log");
    ENetAddress address{};
    if (enet_address_set_host_ip(&address, kBindIp.data()) != 0)
      throw std::runtime_error("invalid loopback Slippi relay bind address");
    address.port = port;
    m_host = enet_host_create(&address, 4, 3, 0, 0);
    if (!m_host)
      throw std::runtime_error("cannot bind loopback Slippi relay");
    m_events.write({{"event", "peer_relay_started"},
                    {"endpoint", "127.0.0.1:" + std::to_string(port) + "/udp"}});
  }

  ~SlippiPeerRelay() { stop(); }

  void start()
  {
    if (m_thread.joinable())
      throw std::runtime_error("Slippi peer relay was started twice");
    m_thread = std::thread([this] { serve(); });
  }

  void stop()
  {
    m_stopping.store(true, std::memory_order_release);
    if (m_thread.joinable())
      m_thread.join();
    if (m_host)
    {
      enet_host_destroy(m_host);
      m_host = nullptr;
    }
  }

private:
  struct PendingPacket
  {
    std::vector<std::uint8_t> bytes;
    std::uint8_t channel{};
    enet_uint32 flags{};
  };

  int slot_for(const ENetPeer* peer) const
  {
    for (int slot = 0; slot < static_cast<int>(m_peers.size()); ++slot)
    {
      if (m_peers[slot] == peer)
        return slot;
    }
    return -1;
  }

  void serve()
  {
    while (!m_stopping.load(std::memory_order_acquire) &&
           !g_stopping.load(std::memory_order_acquire))
    {
      m_input.refresh(m_pad_queue);
      ENetEvent event{};
      int result = enet_host_service(m_host, &event, 2);
      while (result > 0)
      {
        process(event);
        result = enet_host_service(m_host, &event, 0);
      }
      enet_host_flush(m_host);
    }
    m_events.write({{"event", "peer_relay_stopped"},
                    {"packets_forwarded", m_packets_forwarded},
                    {"browser_frames_injected", m_browser_frames_injected},
                    {"pending_packets_discarded", m_pending_discarded}});
  }

  void process(const ENetEvent& event)
  {
    switch (event.type)
    {
    case ENET_EVENT_TYPE_CONNECT:
      on_connect(event.peer);
      break;
    case ENET_EVENT_TYPE_RECEIVE:
      on_receive(event);
      enet_packet_destroy(event.packet);
      break;
    case ENET_EVENT_TYPE_DISCONNECT:
      on_disconnect(event.peer, event.data);
      break;
    default:
      break;
    }
  }

  void on_connect(ENetPeer* peer)
  {
    const auto slot = std::find(m_peers.begin(), m_peers.end(), nullptr);
    if (slot == m_peers.end())
    {
      m_events.write({{"event", "peer_relay_rejected"}, {"reason", "session already has two peers"}});
      enet_peer_disconnect(peer, 1);
      return;
    }
    const auto index = static_cast<std::size_t>(std::distance(m_peers.begin(), slot));
    m_peers[index] = peer;
    m_events.write({{"event", "peer_relay_client_connected"}, {"slot", index}});
    if (m_peers[0] && m_peers[1])
    {
      for (std::size_t source = 0; source < m_pending.size(); ++source)
      {
        auto& queued = m_pending[source];
        while (!queued.empty())
        {
          auto packet = std::move(queued.front());
          queued.pop_front();
          m_pending_bytes[source] -= packet.bytes.size();
          (void)send_to_peer(1 - static_cast<int>(source), packet.bytes, packet.channel,
                             packet.flags);
        }
      }
    }
  }

  void on_receive(const ENetEvent& event)
  {
    const auto slot = slot_for(event.peer);
    if (slot < 0 || !event.packet)
      return;

    std::vector<std::uint8_t> bytes(event.packet->data,
                                    event.packet->data + event.packet->dataLength);
    if (bytes.size() >= 5 && bytes[0] == 0x82 && bytes[4] <= 1)
    {
      const auto player_port = static_cast<std::uint8_t>(bytes[4] + 1);
      const auto other_slot = 1 - slot;
      if (m_player_ports[other_slot] == player_port)
      {
        m_events.write({{"event", "peer_relay_rejected"},
                        {"reason", "both ENet peers claimed the same Slippi player port"}});
        enet_peer_disconnect(event.peer, 2);
        return;
      }
      m_player_ports[slot] = player_port;
      m_events.write({{"event", "peer_relay_player_identified"},
                      {"slot", slot},
                      {"player_port", player_port}});
    }

    if (bytes.size() >= local_matchmaker::kSlippiPadHeaderBytes && bytes[0] == 0x80)
    {
      const auto claimed_port = static_cast<std::uint8_t>(bytes[5] + 1);
      if (m_player_ports[slot] != 0 && claimed_port != m_player_ports[slot])
      {
        m_events.write({{"event", "peer_relay_rejected"},
                        {"reason", "PAD packet player port did not match its ENet peer"}});
        enet_peer_disconnect(event.peer, 2);
        return;
      }
      const auto replacements = m_pad_queue.replace_pad_packet(
          bytes, static_cast<std::uint8_t>(m_player_ports[slot] - 1));
      for (const auto& replacement : replacements)
      {
        ++m_browser_frames_injected;
        write_browser_event({{"event", "pad_applied"},
                             {"session_id", replacement.session_id},
                             {"frame", replacement.pad.frame},
                             {"pad_hex", local_matchmaker::pad_hex(replacement.pad.bytes)}});
        m_events.write({{"event", "browser_pad_injected"},
                        {"session_id", replacement.session_id},
                        {"frame", replacement.pad.frame},
                        {"pad_hex", local_matchmaker::pad_hex(replacement.pad.bytes)}});
      }
      if (m_player_ports[slot] == 2)
        observe_peer_pad(bytes);
    }

    const auto flags = event.packet->flags &
                       (ENET_PACKET_FLAG_RELIABLE | ENET_PACKET_FLAG_UNSEQUENCED |
                        ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
    const auto other_slot = 1 - slot;
    if (!m_peers[other_slot])
    {
      queue_before_pair(slot, std::move(bytes), event.channelID, flags);
      return;
    }
    (void)send_to_peer(other_slot, bytes, event.channelID, flags);
  }

  void observe_peer_pad(const std::vector<std::uint8_t>& packet)
  {
    if (packet.size() < local_matchmaker::kSlippiPadHeaderBytes ||
        (packet.size() - local_matchmaker::kSlippiPadHeaderBytes) %
                local_matchmaker::kSlippiPadRecordBytes !=
            0)
    {
      return;
    }
    const auto latest = read_be_i32(packet.data() + 1);
    const auto count = (packet.size() - local_matchmaker::kSlippiPadHeaderBytes) /
                       local_matchmaker::kSlippiPadRecordBytes;
    if (latest < 1 || count > local_matchmaker::kMaximumBrowserPadFrames ||
        count > static_cast<std::size_t>(latest))
    {
      return;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
      const auto frame = latest - static_cast<std::int32_t>(index);
      if (!m_pad_queue.wants_peer_observation(frame) || !m_pad_queue.mark_peer_observed(frame))
        continue;
      std::array<std::uint8_t, local_matchmaker::kSlippiPadRecordBytes> pad{};
      const auto offset = local_matchmaker::kSlippiPadHeaderBytes +
                          index * local_matchmaker::kSlippiPadRecordBytes;
      std::copy_n(packet.begin() + offset, pad.size(), pad.begin());
      const auto session = std::string(m_pad_queue.session_id());
      write_browser_event({{"event", "peer_pad"},
                           {"session_id", session},
                           {"frame", frame},
                           {"pad_hex", local_matchmaker::pad_hex(pad)}});
      m_events.write({{"event", "browser_peer_pad_received"},
                      {"session_id", session},
                      {"frame", frame},
                      {"pad_hex", local_matchmaker::pad_hex(pad)}});
    }
  }

  void queue_before_pair(int slot, std::vector<std::uint8_t> bytes, std::uint8_t channel,
                         enet_uint32 flags)
  {
    constexpr std::size_t kMaximumQueuedPackets = 256;
    constexpr std::size_t kMaximumQueuedBytes = 1024 * 1024;
    if (m_pending[slot].size() >= kMaximumQueuedPackets ||
        m_pending_bytes[slot] + bytes.size() > kMaximumQueuedBytes)
    {
      ++m_pending_discarded;
      m_events.write({{"event", "peer_relay_queue_overflow"}, {"slot", slot}});
      enet_peer_disconnect(m_peers[slot], 3);
      return;
    }
    m_pending_bytes[slot] += bytes.size();
    m_pending[slot].push_back({std::move(bytes), channel, flags});
  }

  bool send_to_peer(int slot, const std::vector<std::uint8_t>& bytes, std::uint8_t channel,
                    enet_uint32 flags)
  {
    if (slot < 0 || slot >= static_cast<int>(m_peers.size()) || !m_peers[slot])
      return false;
    ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), flags);
    if (!packet)
      return false;
    if (enet_peer_send(m_peers[slot], channel, packet) < 0)
    {
      enet_packet_destroy(packet);
      return false;
    }
    ++m_packets_forwarded;
    return true;
  }

  void on_disconnect(ENetPeer* peer, enet_uint32 reason)
  {
    const auto slot = slot_for(peer);
    if (slot < 0)
      return;
    const auto other_slot = 1 - slot;
    m_events.write({{"event", "peer_relay_client_disconnected"},
                    {"slot", slot},
                    {"reason", reason}});
    m_peers[slot] = nullptr;
    m_player_ports[slot] = 0;
    if (m_peers[other_slot])
      enet_peer_disconnect(m_peers[other_slot], reason);
    for (auto& pending : m_pending)
      pending.clear();
    m_pending_bytes = {};
    if (!m_pad_queue.session_id().empty())
      m_pad_queue.close_session(m_pad_queue.session_id());
  }

  void write_browser_event(const Json& event)
  {
    m_browser_events << event.dump() << '\n';
    m_browser_events.flush();
  }

  EventLog& m_events;
  BrowserInputTail m_input;
  local_matchmaker::BrowserPadQueue m_pad_queue;
  std::ofstream m_browser_events;
  ENetHost* m_host{};
  std::array<ENetPeer*, 2> m_peers{};
  std::array<std::uint8_t, 2> m_player_ports{};
  std::array<std::deque<PendingPacket>, 2> m_pending;
  std::array<std::size_t, 2> m_pending_bytes{};
  std::thread m_thread;
  std::atomic<bool> m_stopping{false};
  std::uint64_t m_packets_forwarded{};
  std::uint64_t m_browser_frames_injected{};
  std::uint64_t m_pending_discarded{};
};

int run(const std::string& event_log_path, std::chrono::seconds ticket_timeout,
        std::uint16_t relay_port, const std::string& browser_input_path,
        const std::string& browser_event_path)
{
  EventLog events(event_log_path);
  if (enet_initialize() != 0)
    throw std::runtime_error("ENet initialization failed");

  ENetAddress address{};
  if (enet_address_set_host_ip(&address, kBindIp.data()) != 0)
    throw std::runtime_error("invalid matchmaking bind address");
  address.port = kMatchmakingPort;
  ENetHost* host = enet_host_create(&address, kMaximumPeers, 3, 0, 0);
  if (!host)
  {
    enet_deinitialize();
    throw std::runtime_error("cannot bind loopback ENet matchmaking on 127.0.0.1:43113");
  }

  HttpSink http_sink(events);
  http_sink.start();
  std::unique_ptr<SlippiPeerRelay> peer_relay;
  if (relay_port != 0)
  {
    peer_relay = std::make_unique<SlippiPeerRelay>(relay_port, events, browser_input_path,
                                                   browser_event_path);
    peer_relay->start();
  }
  events.write({{"event", "service_started"},
                {"matchmaking", "127.0.0.1:43113/udp"},
                {"local_api_sink", "127.0.0.1:43114/tcp"},
                {"peer_relay", relay_port == 0
                                    ? Json(nullptr)
                                    : Json("127.0.0.1:" + std::to_string(relay_port) + "/udp")}});
  std::cout << "local matchmaking ready at 127.0.0.1:43113; local API sink at 127.0.0.1:43114";
  if (relay_port != 0)
    std::cout << "; Slippi peer relay at 127.0.0.1:" << relay_port;
  std::cout << '\n';

  MatchmakingService service(host, events, ticket_timeout, relay_port);
  while (!g_stopping.load(std::memory_order_relaxed))
  {
    ENetEvent event{};
    if (enet_host_service(host, &event, 100) > 0)
      service.process(event);
    service.expire();
  }

  service.cancel_all();
  if (peer_relay)
    peer_relay->stop();
  http_sink.stop();
  enet_host_destroy(host);
  enet_deinitialize();
  events.write({{"event", "service_stopped"}, {"pending_tickets", service.pending_count()}});
  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  auto timeout = kDefaultTicketTimeout;
  std::string event_log_path;
  std::string browser_input_path;
  std::string browser_event_path;
  std::uint16_t relay_port = 0;
  for (int index = 1; index < argc;)
  {
    const std::string_view option(argv[index++]);
    if (index >= argc)
    {
      return 2;
    }
    const std::string_view value(argv[index++]);
    if (option == "--event-log")
    {
      if (!event_log_path.empty())
        return 2;
      event_log_path = value;
    }
    else if (option == "--ticket-timeout-seconds")
    {
      unsigned parsed_seconds = 0;
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), parsed_seconds);
      if (error != std::errc{} || end != value.data() + value.size() || parsed_seconds < 1 ||
          parsed_seconds > 120)
      {
        std::cerr << "ticket timeout must be between 1 and 120 seconds\n";
        return 2;
      }
      timeout = std::chrono::seconds(parsed_seconds);
    }
    else if (option == "--relay-peer-port")
    {
      unsigned parsed_port = 0;
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), parsed_port);
      if (error != std::errc{} || end != value.data() + value.size() || parsed_port < 41000 ||
          parsed_port > 51999 || parsed_port == kMatchmakingPort ||
          parsed_port == kLocalHttpPort)
      {
        std::cerr << "relay peer port must be a reserved loopback port\n";
        return 2;
      }
      relay_port = static_cast<std::uint16_t>(parsed_port);
    }
    else if (option == "--browser-input-log")
    {
      browser_input_path = value;
    }
    else if (option == "--browser-event-log")
    {
      browser_event_path = value;
    }
    else
    {
      std::cerr << "unknown local matchmaker option\n";
      return 2;
    }
  }

  if (event_log_path.empty() ||
      ((relay_port == 0) != browser_input_path.empty()) ||
      ((relay_port == 0) != browser_event_path.empty()))
  {
    std::cerr << "usage: slippi-local-matchmaker --event-log PATH "
                 "[--ticket-timeout-seconds N] "
                 "[--relay-peer-port PORT --browser-input-log PATH --browser-event-log PATH]\n";
    return 2;
  }

  std::signal(SIGINT, stop_signal);
  std::signal(SIGTERM, stop_signal);
  try
  {
    return run(event_log_path, timeout, relay_port, browser_input_path, browser_event_path);
  }
  catch (const std::exception& error)
  {
    std::cerr << "local matchmaking failed: " << error.what() << '\n';
    return 1;
  }
}
