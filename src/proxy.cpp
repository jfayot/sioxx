#include "proxy.hpp"

#include <stdexcept>
#include <utility>

namespace sioxx::detail
{
namespace
{
constexpr char base64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}  // namespace

std::string base64_encode(const std::string& input)
{
  std::string out;
  out.reserve((input.size() + 2) / 3 * 4);
  for (size_t i = 0; i < input.size(); i += 3)
  {
    unsigned value = static_cast<unsigned char>(input[i]) << 16;
    if (i + 1 < input.size())
      value |= static_cast<unsigned char>(input[i + 1]) << 8;
    if (i + 2 < input.size()) value |= static_cast<unsigned char>(input[i + 2]);
    out += base64[(value >> 18) & 63];
    out += base64[(value >> 12) & 63];
    out += i + 1 < input.size() ? base64[(value >> 6) & 63] : '=';
    out += i + 2 < input.size() ? base64[value & 63] : '=';
  }
  return out;
}

std::optional<proxy_config> make_proxy_config(const std::string& uri,
                                              const std::string& username,
                                              const std::string& password)
{
  if (uri.empty()) return std::nullopt;

  const auto scheme_end = uri.find("://");
  if (scheme_end != std::string::npos &&
      uri.find_first_of("?#", scheme_end + 3) != std::string::npos)
    throw std::invalid_argument(
      "sioxx: proxy URI must not contain a query or fragment");

  auto endpoint = parse_ws_url(uri);
  if (endpoint.scheme != "http")
    throw std::invalid_argument("sioxx: proxy URI must use http://");
  if (endpoint.target != "/")
    throw std::invalid_argument("sioxx: proxy URI must not contain a path");

  std::string authorization;
  if (!username.empty() || !password.empty())
    authorization = "Basic " + base64_encode(username + ":" + password);
  return proxy_config{std::move(endpoint), std::move(authorization)};
}

std::string authority(const url_parts& url)
{
  const bool default_port =
    (!url.tls && url.port == "80") || (url.tls && url.port == "443");
  return default_port ? url.host : url.host + ":" + url.port;
}

std::string connect_authority(const url_parts& url)
{
  return url.host + ":" + url.port;
}

std::string absolute_http_target(const url_parts& url,
                                 const std::string& target)
{
  return std::string(url.tls ? "https://" : "http://") + authority(url) +
         target;
}

}  // namespace sioxx::detail
