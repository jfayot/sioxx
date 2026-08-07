#include "engineio_url.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace sioxx::detail
{
namespace
{

std::string encode_query_component(const std::string& value)
{
  std::ostringstream encoded;
  encoded << std::uppercase << std::hex;
  for (const unsigned char ch : value)
  {
    if (std::isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~')
      encoded << ch;
    else
      encoded << '%' << std::setw(2) << std::setfill('0')
              << static_cast<int>(ch);
  }
  return encoded.str();
}

}  // namespace

std::string build_engineio_url(const std::string& uri, const std::string& path,
                               const std::map<std::string, std::string>& query,
                               const std::string& transport)
{
  std::string url = uri;
  const size_t scheme_end = url.find("://");
  const std::string scheme =
    scheme_end == std::string::npos ? "" : url.substr(0, scheme_end);
  if (scheme == "http")
    url = "ws" + url.substr(4);
  else if (scheme == "https")
    url = "wss" + url.substr(5);
  else if (scheme != "ws" && scheme != "wss")
    url = "ws://" + url;

  const size_t authority_start = url.find("://") + 3;
  const size_t path_start = url.find('/', authority_start);
  const size_t query_start = url.find('?', authority_start);
  size_t authority_end =
    std::min(path_start == std::string::npos ? url.size() : path_start,
             query_start == std::string::npos ? url.size() : query_start);
  std::string base = url.substr(0, authority_end);

  std::string normalized_path = path.empty() ? "/socket.io/" : path;
  if (normalized_path.front() != '/') normalized_path.insert(0, "/");
  if (normalized_path.back() != '/') normalized_path.push_back('/');
  base += normalized_path;

  base += "?EIO=4&transport=" + encode_query_component(transport);
  for (const auto& [key, value] : query)
  {
    if (key == "EIO" || key == "transport" || key == "sid")
      throw std::invalid_argument(
        "sioxx: reserved Engine.IO query parameter: " + key);
    base +=
      "&" + encode_query_component(key) + "=" + encode_query_component(value);
  }
  return base;
}

}  // namespace sioxx::detail
