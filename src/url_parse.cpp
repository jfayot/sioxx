#include "sioxx/url_parse.hpp"

#include <stdexcept>

namespace sioxx
{

url_parts parse_ws_url(const std::string& url)
{
  url_parts p;
  size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos)
    throw std::invalid_argument("sioxx: invalid url, missing scheme: " + url);
  p.scheme = url.substr(0, scheme_end);
  p.tls = (p.scheme == "wss" || p.scheme == "https");

  size_t host_start = scheme_end + 3;
  size_t path_start = url.find('/', host_start);
  std::string authority = (path_start == std::string::npos)
                            ? url.substr(host_start)
                            : url.substr(host_start, path_start - host_start);
  p.target = (path_start == std::string::npos) ? "/" : url.substr(path_start);

  size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') < colon)
  {
    // IPv6 literal with a port after the closing ']', e.g. [::1]:8080
    colon = authority.find(':', authority.find(']'));
  }
  if (colon != std::string::npos)
  {
    p.host = authority.substr(0, colon);
    p.port = authority.substr(colon + 1);
  }
  else
  {
    p.host = authority;
    p.port = p.tls ? "443" : "80";
  }
  return p;
}

}  // namespace sioxx
