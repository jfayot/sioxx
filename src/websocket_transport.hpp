#ifndef SIOXX_SRC_WEBSOCKET_TRANSPORT_HPP
#define SIOXX_SRC_WEBSOCKET_TRANSPORT_HPP
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "proxy.hpp"
#include "transport.hpp"
#include "url_parse.hpp"

namespace sioxx
{

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;

// ws:// and wss:// transport built on Boost.Beast, replacing websocketpp.
// Runs its own io_context on a background thread so the public API
// (connect/send/close) can be called freely from user code; handlers
// (on_message/on_open/on_close/on_error) fire on that background thread,
// so callers that touch UI state should hop back to their own thread/queue.
class websocket_transport final
    : public transport_base,
      public std::enable_shared_from_this<websocket_transport>
{
 public:
  websocket_transport();
  ~websocket_transport() override;

  void connect(const std::string& url) override;
  void send(const std::string& payload, bool is_binary) override;
  void close() override;
  void sync_close() override;

  // Extra headers to send with the websocket upgrade request, e.g. cookies.
  void set_extra_headers(
    std::vector<std::pair<std::string, std::string>> headers);

  // Disable TLS certificate verification (development/self-signed only).
  void set_verify_tls(bool verify);
  void set_proxy(const std::string& uri, const std::string& username,
                 const std::string& password);

 private:
  void run_plain();
  void run_tls();
  void establish_proxy_tunnel(std::function<void()> continuation);
  void start_plain_handshake();
  void start_tls_handshake();
  void do_read_plain();
  void do_read_tls();
  void queue_write(std::string payload, bool is_binary);
  void pump_write_queue_plain();
  void pump_write_queue_tls();
  void shutdown_on_io_thread();
  void join_io_thread();
  void fail(const std::string& what);
  void fail(const std::string& what, beast::error_code ec);

  net::io_context ioc_;
  std::optional<net::executor_work_guard<net::io_context::executor_type>>
    work_guard_;
  std::thread io_thread_;
  tcp::resolver resolver_;
  ssl::context ssl_ctx_;

  std::unique_ptr<websocket::stream<beast::tcp_stream>> ws_plain_;
  std::unique_ptr<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>
    ws_tls_;

  beast::flat_buffer buffer_;
  beast::flat_buffer proxy_buffer_;
  boost::beast::http::request<boost::beast::http::empty_body> proxy_request_;
  boost::beast::http::response_parser<boost::beast::http::empty_body>
    proxy_response_;
  url_parts url_;
  std::vector<std::pair<std::string, std::string>> extra_headers_;
  std::optional<detail::proxy_config> proxy_;
  bool verify_tls_{true};

  std::deque<std::pair<std::string, bool>> write_queue_;
  bool write_in_progress_{false};
  std::atomic<bool> closing_{false};
  std::once_flag shutdown_once_;
  std::mutex connect_mutex_;
  std::mutex join_mutex_;
};

}  // namespace sioxx

#endif  // SIOXX_SRC_WEBSOCKET_TRANSPORT_HPP
