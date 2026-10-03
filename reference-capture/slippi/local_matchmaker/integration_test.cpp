// SPDX-License-Identifier: MIT
#include <enet/enet.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <spawn.h>

extern char** environ;

namespace
{
using namespace std::chrono_literals;
using Json = nlohmann::json;

void check(bool condition, const std::string& message)
{
  if (!condition)
    throw std::runtime_error(message);
}

class ServiceProcess
{
public:
  ServiceProcess(const std::string& executable, const std::filesystem::path& event_log)
  {
    std::string event_flag = "--event-log";
    std::string timeout_flag = "--ticket-timeout-seconds";
    std::string timeout = "1";
    std::array<char*, 6> arguments{const_cast<char*>(executable.c_str()), event_flag.data(),
                                   const_cast<char*>(event_log.c_str()), timeout_flag.data(),
                                   timeout.data(), nullptr};
    const int result = posix_spawn(&m_pid, executable.c_str(), nullptr, nullptr, arguments.data(),
                                   environ);
    check(result == 0, "could not start local matchmaking service");
  }

  ~ServiceProcess()
  {
    stop();
  }

  void stop()
  {
    if (m_pid <= 0)
      return;
    kill(m_pid, SIGTERM);
    int status = 0;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
      const auto result = waitpid(m_pid, &status, WNOHANG);
      if (result == m_pid)
      {
        m_pid = -1;
        check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "local matchmaking service did not shut down cleanly");
        return;
      }
      std::this_thread::sleep_for(100ms);
    }
    kill(m_pid, SIGKILL);
    waitpid(m_pid, &status, 0);
    m_pid = -1;
    throw std::runtime_error("local matchmaking service exceeded shutdown deadline");
  }

private:
  pid_t m_pid{-1};
};

class EnetClient
{
public:
  EnetClient()
  {
    m_host = enet_host_create(nullptr, 1, 3, 0, 0);
    check(m_host != nullptr, "could not create ENet test client");
    ENetAddress address{};
    check(enet_address_set_host_ip(&address, "127.0.0.1") == 0,
          "could not parse loopback test address");
    address.port = 43113;

    for (int attempt = 0; attempt < 30; ++attempt)
    {
      m_peer = enet_host_connect(m_host, &address, 3, 0);
      if (!m_peer)
        break;
      ENetEvent event{};
      const int result = enet_host_service(m_host, &event, 100);
      if (result > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
        return;
      enet_peer_reset(m_peer);
      m_peer = nullptr;
      std::this_thread::sleep_for(50ms);
    }
    enet_host_destroy(m_host);
    m_host = nullptr;
    throw std::runtime_error("local ENet matchmaking service did not accept a client");
  }

  ~EnetClient()
  {
    if (m_host)
    {
      if (m_peer)
        enet_peer_disconnect_now(m_peer, 0);
      enet_host_destroy(m_host);
    }
  }

  void send(const std::string& wire)
  {
    ENetPacket* packet = enet_packet_create(wire.data(), wire.size(), ENET_PACKET_FLAG_RELIABLE);
    check(packet != nullptr, "could not allocate ENet test packet");
    if (enet_peer_send(m_peer, 0, packet) < 0)
    {
      enet_packet_destroy(packet);
      throw std::runtime_error("could not send ENet test packet");
    }
    enet_host_flush(m_host);
  }

  Json receive(std::chrono::milliseconds timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
      ENetEvent event{};
      const int result = enet_host_service(m_host, &event, 50);
      if (result <= 0)
        continue;
      if (event.type == ENET_EVENT_TYPE_DISCONNECT)
        throw std::runtime_error("local service disconnected an ENet test client");
      if (event.type != ENET_EVENT_TYPE_RECEIVE)
        continue;
      const std::string wire(reinterpret_cast<const char*>(event.packet->data),
                              event.packet->dataLength);
      enet_packet_destroy(event.packet);
      return Json::parse(wire);
    }
    throw std::runtime_error("timed out waiting for a local matchmaking response");
  }

private:
  ENetHost* m_host{};
  ENetPeer* m_peer{};
};

Json create_request(const std::string& uid, const std::string& own_code,
                    const std::string& target_code, int local_port)
{
  Json code = Json::array();
  for (const auto character : target_code)
    code.push_back(static_cast<unsigned char>(character));
  return {{"type", "create-ticket"},
          {"user", {{"uid", uid},
                    {"playKey", "local-test-key"},
                    {"connectCode", own_code},
                    {"displayName", uid}}},
          {"search", {{"mode", 2}, {"connectCode", code}}},
          {"appVersion", "local-test"},
          {"ipAddressLan", "127.0.0.1:" + std::to_string(local_port)}};
}

std::string local_http_request()
{
  const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  check(socket_fd >= 0, "cannot create HTTP sink test socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(43114);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
  {
    close(socket_fd);
    throw std::runtime_error("local API sink did not accept a connection");
  }
  constexpr std::string_view request = "GET /user HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
  (void)send(socket_fd, request.data(), request.size(), 0);
  std::string response;
  char buffer[4096];
  while (true)
  {
    const auto count = recv(socket_fd, buffer, sizeof(buffer), 0);
    if (count <= 0)
      break;
    response.append(buffer, static_cast<std::size_t>(count));
  }
  close(socket_fd);
  return response;
}

int idle_http_client()
{
  const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  check(socket_fd >= 0, "cannot create idle HTTP sink test socket");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(43114);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
  {
    close(socket_fd);
    throw std::runtime_error("local API sink did not accept an idle connection");
  }
  return socket_fd;
}

void wait_for_log_event(const std::filesystem::path& path, std::string_view event,
                        std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    std::ifstream stream(path);
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (contents.find(event) != std::string::npos)
      return;
    std::this_thread::sleep_for(25ms);
  }
  throw std::runtime_error("timed out waiting for service event: " + std::string(event));
}

void wait_for_log_count(const std::filesystem::path& path, std::string_view event,
                        std::size_t expected, std::chrono::milliseconds timeout)
{
  const auto needle = "\"event\":\"" + std::string(event) + "\"";
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    std::ifstream stream(path);
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = contents.find(needle, offset)) != std::string::npos)
    {
      ++count;
      offset += needle.size();
    }
    if (count >= expected)
      return;
    std::this_thread::sleep_for(25ms);
  }
  throw std::runtime_error("timed out waiting for " + std::to_string(expected) +
                           " service events: " + std::string(event));
}

void check_ports_released()
{
  const int udp = socket(AF_INET, SOCK_DGRAM, 0);
  check(udp >= 0, "cannot create UDP release-check socket");
  sockaddr_in udp_address{};
  udp_address.sin_family = AF_INET;
  udp_address.sin_port = htons(43113);
  inet_pton(AF_INET, "127.0.0.1", &udp_address.sin_addr);
  const int udp_bound = bind(udp, reinterpret_cast<sockaddr*>(&udp_address), sizeof(udp_address));
  close(udp);
  check(udp_bound == 0, "matchmaking UDP port was not released");

  const int tcp = socket(AF_INET, SOCK_STREAM, 0);
  check(tcp >= 0, "cannot create TCP release-check socket");
  int reuse = 1;
  setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in tcp_address{};
  tcp_address.sin_family = AF_INET;
  tcp_address.sin_port = htons(43114);
  inet_pton(AF_INET, "127.0.0.1", &tcp_address.sin_addr);
  const int tcp_bound = bind(tcp, reinterpret_cast<sockaddr*>(&tcp_address), sizeof(tcp_address));
  close(tcp);
  check(tcp_bound == 0, "local API sink port was not released");
}

void run(const std::string& executable)
{
  const auto scratch = std::filesystem::temp_directory_path() /
                       ("melee-web-slippi-matchmaker-" + std::to_string(getpid()));
  std::filesystem::create_directories(scratch);
  const auto log = scratch / "events.jsonl";
  bool success = false;
  try
  {
    ServiceProcess service(executable, log);
    EnetClient malformed;
    malformed.send("not-json");
    const auto malformed_response = malformed.receive(2s);
    check(malformed_response.value("type", "") == "create-ticket-resp" &&
              malformed_response.find("error") != malformed_response.end(),
          "malformed ENet request must receive a bounded error response");

    {
      EnetClient cancelled;
      cancelled.send(create_request("local-p1", "PLAYER#001", "PLAYER#002", 41301).dump());
      check(cancelled.receive(2s).value("type", "") == "create-ticket-resp",
            "well-formed ticket must be acknowledged before pairing");
    }
    wait_for_log_event(log, "ticket_cancelled", 2s);

    EnetClient timed_out;
    timed_out.send(create_request("local-p2", "PLAYER#002", "PLAYER#001", 41302).dump());
    check(timed_out.receive(2s).value("type", "") == "create-ticket-resp",
          "second ticket should be accepted while waiting for its disconnected peer");
    const auto timeout_response = timed_out.receive(3s);
    check(timeout_response.value("type", "") == "get-ticket-resp" &&
              timeout_response.value("error", "").find("timed out") != std::string::npos,
          "unmatched ticket must expire with an error");

    EnetClient first;
    EnetClient second;
    first.send(create_request("local-p1-fresh", "PLAYER#001", "PLAYER#002", 41303).dump());
    check(first.receive(2s).value("type", "") == "create-ticket-resp",
          "fresh first direct ticket should be accepted");
    second.send(create_request("local-p2-fresh", "PLAYER#002", "PLAYER#001", 41304).dump());
    check(second.receive(2s).value("type", "") == "create-ticket-resp",
          "fresh second direct ticket should be accepted");
    const auto first_assignment = first.receive(2s);
    const auto second_assignment = second.receive(2s);
    check(first_assignment.value("type", "") == "get-ticket-resp" &&
              second_assignment.value("type", "") == "get-ticket-resp",
          "both ENet clients should receive the real pair assignment");
    check(first_assignment.value("matchId", "") == second_assignment.value("matchId", "") &&
              first_assignment["players"].size() == 2 &&
              first_assignment["players"][0].value("ipAddressLan", "").starts_with("127.0.0.1:") &&
              first_assignment["players"][1].value("ipAddressLan", "").starts_with("127.0.0.1:"),
          "paired endpoint data must be consistent and loopback-only");

    const auto http_response = local_http_request();
    constexpr std::string_view rejection_body =
        R"({"error":"local-only Slippi API is disabled"})";
    const auto header_end = http_response.find("\r\n\r\n");
    check(header_end != std::string::npos &&
              http_response.find("HTTP/1.1 503 Service Unavailable\r\n") == 0 &&
              http_response.find("Connection: close\r\n") != std::string::npos &&
              http_response.find("Content-Length:") == std::string::npos &&
              http_response.substr(header_end + 4) == rejection_body,
          "local API sink must fail closed with an exact close-delimited 503 rejection");

    EnetClient pending_at_shutdown;
    pending_at_shutdown.send(
        create_request("local-shutdown-pending", "SHUTDOWN#001", "MISSING#002", 41305).dump());
    check(pending_at_shutdown.receive(2s).value("type", "") == "create-ticket-resp",
          "shutdown ticket should be accepted before the service interruption test");

    const int idle_client = idle_http_client();
    wait_for_log_count(log, "local_api_connected", 2, 2s);
    const auto stop_started = std::chrono::steady_clock::now();
    service.stop();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
    close(idle_client);
    check(stop_elapsed < 2s,
          "service shutdown must stay bounded with a connected idle local API client");
    const auto shutdown_response = pending_at_shutdown.receive(2s);
    check(shutdown_response.value("type", "") == "get-ticket-resp" &&
              shutdown_response.value("error", "") == "Local pairing service stopped",
          "service shutdown must report cancellation to a pending client");
    check_ports_released();

    std::ifstream stream(log);
    const std::string events((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
    check(events.find("request_rejected") != std::string::npos &&
              events.find("ticket_cancelled") != std::string::npos &&
              events.find("ticket_timeout") != std::string::npos &&
              events.find("pair_assigned") != std::string::npos &&
              events.find("local_api_connected") != std::string::npos &&
              events.find("\"source_loopback\":true") != std::string::npos &&
              events.find("local_api_rejected") != std::string::npos &&
              events.find("\"pending_tickets\":0") != std::string::npos,
          "event log must retain malformed, cancel, timeout, pair, and API observations");
    success = true;
  }
  catch (...)
  {
    if (!success)
      std::cerr << "preserved local matchmaking test evidence: " << scratch << '\n';
    throw;
  }
  std::filesystem::remove_all(scratch);
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cerr << "usage: local-matchmaker-integration-test SERVICE_EXECUTABLE\n";
    return 2;
  }
  if (enet_initialize() != 0)
  {
    std::cerr << "ENet initialization failed\n";
    return 1;
  }
  try
  {
    run(argv[1]);
  }
  catch (const std::exception& error)
  {
    std::cerr << "local matchmaking integration test failed: " << error.what() << '\n';
    enet_deinitialize();
    return 1;
  }
  enet_deinitialize();
  std::cout << "local matchmaking integration test passed\n";
  return 0;
}
