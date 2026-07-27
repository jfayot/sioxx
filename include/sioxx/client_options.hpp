/**
 * @file client_options.hpp
 * @brief Configuration options that control the behaviour of a sioxx client.
 */

#ifndef SIOXX_CLIENT_OPTIONS_HPP
#define SIOXX_CLIENT_OPTIONS_HPP

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser.hpp"

namespace sioxx
{

/**
 * @enum parser_kind
 * @brief Select the wire‑protocol parser used for Socket.IO packets.
 *
 * - **json** – classic text‑based protocol compatible with the reference
 *   `socket.io-parser`.
 * - **msgpack** – binary protocol that uses MessagePack via
 *   `nlohmann::json::to_msgpack`/`from_msgpack`.
 */
enum class parser_kind
{
  json,    ///< Text protocol (default)
  msgpack  ///< Binary MessagePack protocol
};

/**
 * @struct client_options
 * @brief All tunable settings for a `sioxx::client`.
 *
 * The struct is deliberately POD‑friendly; all members have sensible defaults.
 *
 * @note When `parser_factory` is provided it takes precedence over the
 *       `parser` enum, allowing custom parser implementations.
 */
struct client_options
{
  /** @brief Which built‑in parser to use (default: `json`). */
  parser_kind parser{parser_kind::json};

  /** @brief Optional factory that creates a custom `parser_base` instance.
   *
   * The factory must return a non‑null `std::unique_ptr<parser_base>`.
   */
  std::function<std::unique_ptr<parser_base>()> parser_factory;

  /** @brief Verify TLS certificates (default: true). */
  bool verify_tls{true};

  /** @brief Force Engine.IO HTTP long‑polling instead of WebSocket.
   *
   * Useful for environments where WebSocket upgrades are blocked.
   */
  bool force_http_polling{false};

  /** @brief Extra HTTP/WebSocket headers to send on the upgrade request. */
  std::vector<std::pair<std::string, std::string>> extra_headers;

  /** @brief Number of retry attempts; zero disables reconnection. */
  int reconnect_attempts{0};

  /** @brief Initial back-off delay, doubled after each failed attempt. */
  std::chrono::milliseconds reconnect_delay{2000};

  /** @brief Maximum back-off delay between reconnection attempts. */
  std::chrono::milliseconds reconnect_delay_max{30000};

  /** @brief Jitter factor applied to delays, in the range 0 to 1. */
  double reconnect_randomization_factor{0.5};
};

}  // namespace sioxx

#endif  // SIOXX_CLIENT_OPTIONS_HPP
