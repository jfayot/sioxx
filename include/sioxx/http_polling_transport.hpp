#pragma once

#include <atomic>
#include <boost/beast/http.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "message.hpp"
#include "transport.hpp"
#include "url_parse.hpp"

namespace sioxx
{

// Engine.IO v4 HTTP long-polling transport. Each poll and write uses a fresh
// HTTP connection, as required by the Engine.IO polling protocol.
class http_polling_transport final
    : public transport_base,
      public std::enable_shared_from_this<http_polling_transport>
{
 public:
  http_polling_transport();
  ~http_polling_transport() override;

  void connect(const std::string& url) override;
  void send(const std::string& payload, bool is_binary) override;
  void close() override;

  void set_extra_headers(
    std::vector<std::pair<std::string, std::string>> headers);
  void set_verify_tls(bool verify);

 private:
  struct response
  {
    unsigned status{0};
    std::string body;
  };

  void run();
  void post(std::string payload, bool is_binary);
  response request(boost::beast::http::verb method, const std::string& target,
                   const std::string& body = {});
  std::string poll_target() const;
  void deliver(const std::string& body);
  void fail(const std::string& message);

  url_parts url_;
  std::string sid_;
  std::vector<std::pair<std::string, std::string>> extra_headers_;
  bool verify_tls_{true};
  std::atomic<bool> closing_{false};
  std::thread poll_thread_;
  std::mutex mutex_;
};

}  // namespace sioxx
