#pragma once
// sioxx/url_parse.hpp
//
// Tiny, dependency-free ws(s):// URL splitter used by websocket_transport.
// Pulled out into its own header/.cpp so it can be unit tested without
// spinning up Boost.Asio.

#include <string>

namespace sioxx
{

struct url_parts
{
  std::string scheme;
  std::string host;
  std::string port;
  std::string target;
  bool tls{false};
};

// Parses "ws://host[:port][/path]" or "wss://host[:port][/path]".
// Throws std::invalid_argument if the scheme separator "://" is missing.
url_parts parse_ws_url(const std::string& url);

}  // namespace sioxx
