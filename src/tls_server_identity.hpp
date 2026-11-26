#ifndef SIOXX_SRC_TLS_SERVER_IDENTITY_HPP
#define SIOXX_SRC_TLS_SERVER_IDENTITY_HPP

#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <boost/asio/ip/address.hpp>
#include <string>

namespace sioxx::detail
{

inline bool set_tls_server_identity(SSL* stream, std::string host)
{
  if (host.size() >= 2 && host.front() == '[' && host.back() == ']')
    host = host.substr(1, host.size() - 2);

  // OpenSSL treats an empty name as disabling the check and a leading dot as
  // matching a domain suffix.
  if (host.empty() || host.front() == '.' ||
      host.find('\0') != std::string::npos)
    return false;

  auto* parameters = SSL_get0_param(stream);
  boost::system::error_code ec;
  boost::asio::ip::make_address(host, ec);
  if (!ec) return X509_VERIFY_PARAM_set1_ip_asc(parameters, host.c_str()) == 1;

  X509_VERIFY_PARAM_set_hostflags(parameters,
                                  X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
  return X509_VERIFY_PARAM_set1_host(parameters, host.data(), host.size()) == 1;
}

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_TLS_SERVER_IDENTITY_HPP
