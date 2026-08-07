#ifndef SIOXX_SRC_ENGINEIO_URL_HPP
#define SIOXX_SRC_ENGINEIO_URL_HPP

#include <map>
#include <string>

namespace sioxx::detail
{

std::string build_engineio_url(const std::string& uri, const std::string& path,
                               const std::map<std::string, std::string>& query,
                               const std::string& transport);

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_ENGINEIO_URL_HPP
