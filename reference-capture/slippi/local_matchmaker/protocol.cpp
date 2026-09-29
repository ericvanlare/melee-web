// SPDX-License-Identifier: MIT
#include "protocol.hpp"

#include <charconv>

namespace local_matchmaker
{
namespace
{
bool string_field(const nlohmann::json& object, const char* name, std::string* value,
                  std::size_t maximum)
{
  const auto field = object.find(name);
  if (field == object.end() || !field->is_string())
    return false;
  *value = field->get<std::string>();
  return !value->empty() && value->size() <= maximum;
}
}  // namespace

bool parse_loopback_endpoint(const std::string& endpoint, std::uint16_t* port)
{
  constexpr std::string_view prefix = "127.0.0.1:";
  if (!endpoint.starts_with(prefix) || endpoint.size() <= prefix.size())
    return false;

  unsigned parsed_port = 0;
  const char* first = endpoint.data() + prefix.size();
  const char* last = endpoint.data() + endpoint.size();
  const auto [end, error] = std::from_chars(first, last, parsed_port);
  if (error != std::errc{} || end != last || parsed_port < 41000 || parsed_port > 51999)
    return false;

  *port = static_cast<std::uint16_t>(parsed_port);
  return true;
}

ParseResult parse_create_ticket(const nlohmann::json& message, std::uint64_t peer_id,
                               std::chrono::steady_clock::time_point queued_at)
{
  ParseResult result;
  const auto type = message.is_object() ? message.find("type") : message.end();
  if (type == message.end() || !type->is_string() || type->get<std::string>() != "create-ticket")
  {
    result.error = "Expected create-ticket request";
    return result;
  }

  const auto user = message.find("user");
  const auto search = message.find("search");
  if (user == message.end() || !user->is_object() || search == message.end() ||
      !search->is_object())
  {
    result.error = "Missing user or search object";
    return result;
  }

  std::string play_key;
  if (!string_field(*user, "uid", &result.ticket.uid, 64) ||
      !string_field(*user, "playKey", &play_key, 128) ||
      !string_field(*user, "connectCode", &result.ticket.connect_code, 32) ||
      !string_field(*user, "displayName", &result.ticket.display_name, 32) ||
      !result.ticket.uid.starts_with("local-") || !play_key.starts_with("local-"))
  {
    result.error = "Only explicit local test identities are accepted";
    return result;
  }

  const auto mode = search->find("mode");
  const auto code = search->find("connectCode");
  if (mode == search->end() || !mode->is_number_integer() || *mode != 2 ||
      code == search->end() || !code->is_array() || code->empty() || code->size() > 18)
  {
    result.error = "Local service accepts only bounded direct-mode requests";
    return result;
  }

  for (const auto& byte : *code)
  {
    if (!byte.is_number_integer())
    {
      result.error = "Direct code must be an ASCII byte array";
      return result;
    }
    std::uint64_t value = 0;
    if (byte.is_number_unsigned())
    {
      value = byte.get<std::uint64_t>();
    }
    else
    {
      const auto signed_value = byte.get<std::int64_t>();
      if (signed_value < 1)
      {
        result.error = "Direct code must be an ASCII byte array";
        return result;
      }
      value = static_cast<std::uint64_t>(signed_value);
    }
    if (value > 0x7f)
    {
      result.error = "Direct code must be an ASCII byte array";
      return result;
    }
    result.ticket.target_code.push_back(static_cast<char>(value));
  }

  std::string endpoint;
  if (!string_field(message, "ipAddressLan", &endpoint, 32) ||
      !parse_loopback_endpoint(endpoint, &result.ticket.peer_port))
  {
    result.error = "Peer endpoint must be a reserved loopback port";
    return result;
  }

  result.ticket.peer_id = peer_id;
  result.ticket.queued_at = queued_at;
  return result;
}
}  // namespace local_matchmaker
