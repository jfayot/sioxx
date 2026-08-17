#ifndef SIOXX_SRC_PROXY_HPP
#define SIOXX_SRC_PROXY_HPP

#include <optional>
#include <string>

#include "url_parse.hpp"

namespace sioxx::detail
{

struct proxy_config
{
  url_parts endpoint;
  std::string authorization;
};

std::string base64_encode(const std::string& input);

std::optional<proxy_config> make_proxy_config(const std::string& uri,
                                              const std::string& username,
                                              const std::string& password);

std::string authority(const url_parts& url);
std::string connect_authority(const url_parts& url);
std::string absolute_http_target(const url_parts& url,
                                 const std::string& target);

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_PROXY_HPP
