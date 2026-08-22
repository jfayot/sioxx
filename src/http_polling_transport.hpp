#ifndef SIOXX_SRC_HTTP_POLLING_TRANSPORT_HPP
#define SIOXX_SRC_HTTP_POLLING_TRANSPORT_HPP

#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "proxy.hpp"
#include "sioxx/message.hpp"
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
  explicit http_polling_transport(boost::asio::io_context& io_context);
  ~http_polling_transport() override;

  void connect(const std::string& url) override;
  void send(const std::string& payload, bool is_binary) override;
  void close() override;
  void sync_close() override;

  void set_extra_headers(
    std::vector<std::pair<std::string, std::string>> headers);
  void set_verify_tls(bool verify);
  void set_proxy(const std::string& uri, const std::string& username,
                 const std::string& password);

 private:
  struct response
  {
    unsigned status{0};
    std::string body;
  };

  using request_handler =
    std::function<void(boost::beast::error_code, const response&)>;
  struct async_request_operation;

  void run();
  void run_writes();
  void post(std::string payload, bool is_binary);
  response request(boost::beast::http::verb method, const std::string& target,
                   const std::string& body = {});
  void async_request(boost::beast::http::verb method, std::string target,
                     std::string body, request_handler handler);
  void start_external();
  void poll_external(bool handshake);
  void start_next_external_write();
  void cancel_external_requests();
  void remove_external_request(const async_request_operation* operation);
  std::string poll_target() const;
  void deliver(const std::string& body);
  void fail(const std::string& message);
  void join_thread(std::thread& thread);
  void join_threads();

  url_parts url_;
  std::string sid_;
  std::vector<std::pair<std::string, std::string>> extra_headers_;
  std::optional<detail::proxy_config> proxy_;
  bool verify_tls_{true};
  boost::asio::io_context* io_context_{nullptr};
  std::optional<boost::asio::strand<boost::asio::io_context::executor_type>>
    strand_;
  std::vector<std::weak_ptr<async_request_operation>> external_requests_;
  bool external_write_in_progress_{false};
  std::atomic<bool> closing_{false};
  std::thread poll_thread_;
  std::thread close_thread_;
  std::thread write_thread_;
  mutable std::mutex mutex_;
  std::condition_variable write_condition_;
  std::deque<std::pair<std::string, bool>> write_queue_;
  std::once_flag shutdown_once_;
  std::mutex connect_mutex_;
  std::mutex join_mutex_;
};

}  // namespace sioxx

#endif  // SIOXX_SRC_HTTP_POLLING_TRANSPORT_HPP
