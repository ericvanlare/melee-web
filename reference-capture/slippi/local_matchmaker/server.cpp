// SPDX-License-Identifier: MIT
#include "pairing.hpp"
#include "protocol.hpp"

#include <enet/enet.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

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

std::string peer_endpoint(const Ticket& ticket)
{
  return "127.0.0.1:" + std::to_string(ticket.peer_port);
}

Json player_json(const Ticket& ticket, int port, bool local)
{
  return {{"uid", ticket.uid},
          {"displayName", ticket.display_name},
          {"connectCode", ticket.connect_code},
          {"port", port},
          {"isBot", false},
          {"isLocalPlayer", local},
          {"ipAddress", peer_endpoint(ticket)},
          {"ipAddressLan", peer_endpoint(ticket)}};
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

Json assignment(const std::string& match_id, const Pair& pair, bool first_player)
{
  return {{"type", "get-ticket-resp"},
          {"matchId", match_id},
          {"isHost", first_player},
          {"players", Json::array({player_json(pair.first, 1, first_player),
                                    player_json(pair.second, 2, !first_player)})},
          {"stages", Json::array({3, 8, 28, 31, 32, 2})},
          {"items", 0}};
}

class MatchmakingService
{
public:
  MatchmakingService(ENetHost* host, EventLog& events, std::chrono::seconds timeout)
      : m_host(host), m_events(events), m_ticket_timeout(timeout)
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

    const bool first_sent = send_json(m_host, first_peer, assignment(match_id, *pair, true));
    const bool second_sent = send_json(m_host, second_peer, assignment(match_id, *pair, false));
    m_peer_has_ticket[first_peer] = false;
    m_peer_has_ticket[second_peer] = false;
    m_events.write({{"event", "pair_assigned"},
                    {"match_id", match_id},
                    {"first_uid", pair->first.uid},
                    {"second_uid", pair->second.uid},
                    {"first_peer", peer_endpoint(pair->first)},
                    {"second_peer", peer_endpoint(pair->second)},
                    {"both_assignments_sent", first_sent && second_sent}});
  }

  ENetHost* m_host;
  EventLog& m_events;
  std::chrono::seconds m_ticket_timeout;
  PairingRegistry m_registry;
  std::map<ENetPeer*, bool> m_peer_has_ticket;
  std::uint64_t m_next_match_id{};
};

int run(const std::string& event_log_path, std::chrono::seconds ticket_timeout)
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
  events.write({{"event", "service_started"},
                {"matchmaking", "127.0.0.1:43113/udp"},
                {"local_api_sink", "127.0.0.1:43114/tcp"}});
  std::cout << "local matchmaking ready at 127.0.0.1:43113; local API sink at 127.0.0.1:43114\n";

  MatchmakingService service(host, events, ticket_timeout);
  while (!g_stopping.load(std::memory_order_relaxed))
  {
    ENetEvent event{};
    if (enet_host_service(host, &event, 100) > 0)
      service.process(event);
    service.expire();
  }

  service.cancel_all();
  http_sink.stop();
  enet_host_destroy(host);
  enet_deinitialize();
  events.write({{"event", "service_stopped"}, {"pending_tickets", service.pending_count()}});
  return 0;
}
}  // namespace

int main(int argc, char** argv)
{
  if ((argc != 3 && argc != 5) || std::string_view(argv[1]) != "--event-log")
  {
    std::cerr << "usage: slippi-local-matchmaker --event-log PATH [--ticket-timeout-seconds N]\n";
    return 2;
  }

  auto timeout = kDefaultTicketTimeout;
  if (argc == 5)
  {
    if (std::string_view(argv[3]) != "--ticket-timeout-seconds")
      return 2;
    unsigned parsed_seconds = 0;
    const std::string_view value(argv[4]);
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                              parsed_seconds);
    if (error != std::errc{} || end != value.data() + value.size() || parsed_seconds < 1 ||
        parsed_seconds > 120)
    {
      std::cerr << "ticket timeout must be between 1 and 120 seconds\n";
      return 2;
    }
    timeout = std::chrono::seconds(parsed_seconds);
  }

  std::signal(SIGINT, stop_signal);
  std::signal(SIGTERM, stop_signal);
  try
  {
    return run(argv[2], timeout);
  }
  catch (const std::exception& error)
  {
    std::cerr << "local matchmaking failed: " << error.what() << '\n';
    return 1;
  }
}
