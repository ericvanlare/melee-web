// SPDX-License-Identifier: MIT
#include "pairing.hpp"
#include "protocol.hpp"
#include "browser_relay.hpp"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using namespace std::chrono_literals;
using local_matchmaker::PairingRegistry;
using local_matchmaker::Ticket;
using local_matchmaker::BrowserPadQueue;
using local_matchmaker::BrowserInputLineBuffer;
using Json = nlohmann::json;

void check(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

Ticket ticket(std::uint64_t peer, std::string uid, std::string code, std::string target,
              std::uint16_t port, std::chrono::steady_clock::time_point now)
{
  return {peer, std::move(uid), std::move(code), "Local player", std::move(target), port, now};
}

Json create_request(std::string uid, std::string own_code, std::string target_code,
                    std::uint16_t peer_port)
{
  Json target = Json::array();
  for (const auto value : target_code)
    target.push_back(static_cast<unsigned char>(value));
  return {{"type", "create-ticket"},
          {"user", {{"uid", std::move(uid)},
                    {"playKey", "local-test-key"},
                    {"connectCode", std::move(own_code)},
                    {"displayName", "Local player"}}},
          {"search", {{"mode", 2}, {"connectCode", target}}},
          {"appVersion", "test"},
          {"ipAddressLan", "127.0.0.1:" + std::to_string(peer_port)}};
}

void test_reciprocal_pairing_and_fresh_pair()
{
  PairingRegistry registry;
  const auto now = std::chrono::steady_clock::now();
  auto first = ticket(1, "local-p1", "PLAYER#001", "PLAYER#002", 41001, now);
  check(!registry.submit(first), "first direct ticket should wait");
  check(registry.pending_count() == 1, "first ticket should remain pending");

  auto wrong_target = ticket(2, "local-p2", "PLAYER#002", "PLAYER#003", 41002, now);
  check(!registry.submit(wrong_target), "nonreciprocal search should not pair");
  check(registry.pending_count() == 2, "unmatched tickets should stay pending");
  check(registry.cancel(2), "cancel should remove the selected pending ticket");

  auto second = ticket(3, "local-p2-fresh", "PLAYER#002", "PLAYER#001", 41003, now);
  auto pair = registry.submit(second);
  check(pair.has_value(), "reciprocal direct tickets should pair");
  check(pair->first.uid == "local-p1" && pair->second.uid == "local-p2-fresh",
        "pair should preserve accepted peer order");
  check(registry.pending_count() == 0, "pair assignment should remove both tickets");

  auto rematch_first = ticket(4, "local-p1", "PLAYER#001", "PLAYER#002", 41001, now);
  auto rematch_second = ticket(5, "local-p2-fresh", "PLAYER#002", "PLAYER#001", 41003, now);
  check(!registry.submit(rematch_first), "first rematch request should wait");
  check(registry.submit(rematch_second).has_value(), "same identities should be reusable");
}

void test_duplicate_and_cancellation()
{
  PairingRegistry registry;
  const auto now = std::chrono::steady_clock::now();
  const auto first = ticket(11, "local-one", "ONE#001", "TWO#002", 41101, now);
  check(registry.can_submit(first), "first identity can queue");
  registry.submit(first);
  check(!registry.can_submit(ticket(12, "local-one", "ONE#001", "TWO#002", 41102, now)),
        "duplicate local identity must be rejected");
  check(!registry.can_submit(ticket(13, "local-three", "THREE#003", "TWO#002", 41101, now)),
        "duplicate peer port must be rejected");
  check(registry.cancel(11), "disconnect cancellation should remove pending ticket");
  check(!registry.cancel(11), "repeated cancellation should be harmless");
  check(registry.can_submit(first), "identity can queue again after cancellation");
}

void test_timeout_and_stale_ticket_cleanup()
{
  PairingRegistry registry;
  const auto now = std::chrono::steady_clock::now();
  registry.submit(ticket(21, "local-stale", "STALE#001", "MISSING#002", 41201, now - 5s));
  registry.submit(ticket(22, "local-fresh", "FRESH#002", "MISSING#001", 41202, now));
  const auto expired = registry.expire(now, 5s);
  check(expired.size() == 1 && expired.front().uid == "local-stale",
        "only tickets past the queue deadline should expire");
  check(registry.pending_count() == 1, "fresh ticket should remain after stale cleanup");
  const auto next = registry.expire(now + 5s, 5s);
  check(next.size() == 1 && registry.pending_count() == 0,
        "remaining ticket should expire on its own deadline");
}

void test_request_validation()
{
  const auto now = std::chrono::steady_clock::now();
  const auto valid = create_request("local-p1", "PLAYER#001", "PLAYER#002", 41001);
  check(static_cast<bool>(local_matchmaker::parse_create_ticket(valid, 31, now)),
        "valid local direct request");
  check(!local_matchmaker::parse_create_ticket(Json::array(), 31, now),
        "non-object request must be rejected");
  check(!local_matchmaker::parse_create_ticket(
            create_request("official-uid", "PLAYER#001", "PLAYER#002", 41001), 31, now),
        "nonlocal identities must be rejected");
  auto external_endpoint = valid;
  external_endpoint["ipAddressLan"] = "192.168.1.5:41001";
  check(!local_matchmaker::parse_create_ticket(external_endpoint, 31, now),
        "nonloopback advertised address must be rejected");

  auto malformed = valid;
  malformed["search"]["connectCode"] = Json::array({"x"});
  check(!local_matchmaker::parse_create_ticket(malformed, 31, now),
        "non-numeric direct code must be rejected");

  auto wrong_mode = valid;
  wrong_mode["search"]["mode"] = 1;
  check(!local_matchmaker::parse_create_ticket(wrong_mode, 31, now),
        "non-direct mode must be rejected");
}

void test_browser_frame_replacement_is_exact_and_session_scoped()
{
  BrowserPadQueue queue;
  check(!queue.open_session("not-a-session"), "session IDs must use the bounded hex form");
  const std::string session = "0123456789abcdef0123456789abcdef";
  check(queue.open_session(session), "valid browser session should open");

  const auto browser_pad = local_matchmaker::parse_pad_hex("01007f0000000000");
  check(browser_pad.has_value(), "an exact eight-byte PAD record should parse");
  check(!local_matchmaker::parse_pad_hex("01007f"), "short PAD records must be rejected");
  check(queue.add(session, 160, *local_matchmaker::parse_pad_hex("01007f0000000000")),
        "frame 160 should queue");
  check(!queue.add(session, 160, *browser_pad), "duplicate frame tags must be rejected");

  std::vector<std::uint8_t> packet(local_matchmaker::kSlippiPadHeaderBytes + 3 * 8, 0x33);
  packet[0] = 0x80;
  packet[1] = 0;
  packet[2] = 0;
  packet[3] = 0;
  packet[4] = 161;
  packet[5] = 0;
  std::fill(packet.begin() + local_matchmaker::kSlippiPadHeaderBytes,
            packet.begin() + local_matchmaker::kSlippiPadHeaderBytes + 8, 0x11);
  std::fill(packet.begin() + local_matchmaker::kSlippiPadHeaderBytes + 8,
            packet.begin() + local_matchmaker::kSlippiPadHeaderBytes + 16, 0x22);
  std::fill(packet.begin() + local_matchmaker::kSlippiPadHeaderBytes + 16, packet.end(), 0x33);

  const auto replacements = queue.replace_pad_packet(packet, 0);
  check(replacements.size() == 1 && replacements.front().pad.frame == 160,
        "the browser record should match its exact frame inside a PAD packet");
  check(replacements.front().session_id == session, "replacement should retain its session ID");
  check(std::equal(browser_pad->begin(), browser_pad->end(),
                   packet.begin() + local_matchmaker::kSlippiPadHeaderBytes + 8),
        "frame 160 bytes should replace only the frame 160 record");
  check(packet[local_matchmaker::kSlippiPadHeaderBytes] == 0x11 &&
            packet[local_matchmaker::kSlippiPadHeaderBytes + 16] == 0x33,
        "neighboring frame records must remain byte-for-byte unchanged");
  check(queue.wants_peer_observation(160), "the peer observation should be frame tagged");
  check(queue.mark_peer_observed(160) && !queue.mark_peer_observed(160),
        "peer PAD observations should be emitted once per target frame");

  auto wrong_port = packet;
  wrong_port[5] = 1;
  check(queue.replace_pad_packet(wrong_port, 0).empty(),
        "browser records must not replace the opposite player's PAD stream");
  check(queue.close_session(session), "the active browser session should close");
  check(!queue.add(session, 162, *browser_pad), "closed-session frames must be rejected");
  const std::string next_session = "fedcba9876543210fedcba9876543210";
  check(queue.open_session(next_session), "a reconnect should create a fresh session");
  check(!queue.wants_peer_observation(160), "a new session must not inherit old target frames");
}

void test_browser_input_line_buffer_accepts_batched_records()
{
  BrowserInputLineBuffer input;
  std::string batch = "OPEN 0123456789abcdef0123456789abcdef\n";
  for (int frame = 90; frame < 114; ++frame)
  {
    batch += "PAD 0123456789abcdef0123456789abcdef " + std::to_string(frame) +
             " 01007f0000000000\n";
  }
  check(batch.size() > local_matchmaker::kMaximumBrowserInputLineBytes,
        "the reproducer batch should exceed the per-line limit in total size");

  const auto lines = input.append(batch);
  check(lines.has_value() && lines->size() == 25,
        "a batch of short newline-delimited records should parse together");
  check(lines->front().starts_with("OPEN ") && lines->back().ends_with("01007f0000000000"),
        "the batch parser should preserve each complete record");

  BrowserInputLineBuffer split_line;
  const auto partial = split_line.append("OPEN 0123456789abcdef");
  check(partial.has_value() && partial->empty(), "an incomplete line should remain buffered");
  const auto completed = split_line.append("0123456789abcdef\n");
  check(completed.has_value() && completed->size() == 1 &&
            completed->front() == "OPEN 0123456789abcdef0123456789abcdef",
        "a line split across reads should be reassembled");

  BrowserInputLineBuffer oversized;
  check(!oversized.append(std::string(local_matchmaker::kMaximumBrowserInputLineBytes + 1,
                                      'x')),
        "an individually oversized line should be rejected");
}
}  // namespace

int main()
{
  try
  {
    test_reciprocal_pairing_and_fresh_pair();
    test_duplicate_and_cancellation();
    test_timeout_and_stale_ticket_cleanup();
    test_request_validation();
    test_browser_frame_replacement_is_exact_and_session_scoped();
    test_browser_input_line_buffer_accepts_batched_records();
  }
  catch (const std::exception& error)
  {
    std::cerr << "local matchmaking test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "local matchmaking tests passed\n";
  return EXIT_SUCCESS;
}
