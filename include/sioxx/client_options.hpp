
#pragma once
// sioxx/client_options.hpp
//
// Available options for the socket.io client

#include <functional>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser.hpp"

namespace sioxx
{

enum class parser_kind
{
  json,
  msgpack
};

struct client_options
{
  parser_kind parser{parser_kind::json};
  // When set, creates the parser strategy owned by this client and takes
  // precedence over `parser`. The factory must return a non-null parser.
  std::function<std::unique_ptr<parser_base>()> parser_factory;
  bool verify_tls{true};
  // Start with Engine.IO HTTP long-polling instead of WebSocket. Intended for
  // environments where WebSocket is unavailable and for transport testing.
  bool force_http_polling{false};
  std::vector<std::pair<std::string, std::string>> extra_headers;
  // Reconnection (set attempts=0 to disable). The delay doubles on each
  // attempt, is capped, and then receives symmetric jitter.
  int reconnect_attempts{0};
  std::chrono::milliseconds reconnect_delay{2000};
  std::chrono::milliseconds reconnect_delay_max{30000};
  double reconnect_randomization_factor{0.5};
};

}  // namespace sioxx
