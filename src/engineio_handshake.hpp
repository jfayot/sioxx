#ifndef SIOXX_SRC_ENGINEIO_HANDSHAKE_HPP
#define SIOXX_SRC_ENGINEIO_HANDSHAKE_HPP

#include <cstdint>
#include <limits>
#include <optional>
#include <sioxx/message.hpp>
#include <string>

namespace sioxx::detail
{

struct engineio_handshake
{
  std::string sid;
  int ping_interval_ms = 25000;
  int ping_timeout_ms = 20000;
};

inline bool read_handshake_duration(const json& handshake, const char* field,
                                    int& duration)
{
  const auto value = handshake.find(field);
  if (value == handshake.end()) return true;
  if (!value->is_number_integer()) return false;
  const auto milliseconds = value->get<std::uint64_t>();
  if (milliseconds > (std::numeric_limits<int>::max)()) return false;
  duration = static_cast<int>(milliseconds);
  return true;
}

inline std::optional<engineio_handshake> parse_engineio_handshake(
  const std::string& payload)
{
  const auto handshake = json::parse(payload, nullptr, false);
  if (!handshake.is_object()) return std::nullopt;

  const auto sid = handshake.find("sid");
  if (sid == handshake.end() || !sid->is_string()) return std::nullopt;

  engineio_handshake result;
  result.sid = sid->get<std::string>();
  if (result.sid.empty()) return std::nullopt;

  if (!read_handshake_duration(handshake, "pingInterval",
                               result.ping_interval_ms) ||
      result.ping_interval_ms == 0 ||
      !read_handshake_duration(handshake, "pingTimeout",
                               result.ping_timeout_ms))
    return std::nullopt;
  return result;
}

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_ENGINEIO_HANDSHAKE_HPP
