#pragma once
// sioxx/socketio_client.hpp
//
// Public entry point, roughly matching sio::client from the original
// socket.io-client-cpp: sioxx::client c; c.connect(url); c.socket("/chat");

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "engineio_client.hpp"
#include "parser.hpp"
#include "socketio_socket.hpp"

namespace sioxx
{

enum class parser_kind
{
  json,
  msgpack
};

struct client_options
{
  parser_kind parser{parser_kind::json};
  bool verify_tls{true};
  std::vector<std::pair<std::string, std::string>> extra_headers;
  // Reconnection (simple fixed-delay retry; set attempts=0 to disable).
  int reconnect_attempts{0};
  std::chrono::milliseconds reconnect_delay{2000};
};

// socketio_client_impl holds the shared engine.io connection and dispatches
// decoded packets to the right namespace socket. It's kept separate from the
// public `client` facade so sockets can hold a weak_ptr back to it safely.
class socketio_client_impl
    : public std::enable_shared_from_this<socketio_client_impl>
{
 public:
  using connect_handler = std::function<void()>;
  using close_handler = std::function<void(const std::string& reason)>;
  using error_handler = std::function<void(const std::string& message)>;
  using fail_handler = std::function<void()>;

  explicit socketio_client_impl(client_options options);

  void connect(const std::string& uri);
  void close();

  std::shared_ptr<socketio_socket> socket(const std::string& nsp = "/");

  void set_open_handler(connect_handler h) { on_open_ = std::move(h); }
  void set_close_handler(close_handler h) { on_close_ = std::move(h); }
  void set_fail_handler(fail_handler h) { on_fail_ = std::move(h); }
  void set_error_handler(error_handler h) { on_error_ = std::move(h); }

  // --- internal, used by socketio_socket ---
  void send_packet(const socketio_packet& packet);

 private:
  void ensure_engineio();
  void on_engineio_open();
  void on_engineio_close(const std::string& reason);
  void on_engineio_frame(const std::string& payload, bool is_binary);
  void schedule_reconnect();
  std::string build_engineio_url(const std::string& uri) const;

  client_options options_;
  std::unique_ptr<parser_base> parser_;
  std::shared_ptr<engineio_client> engineio_;
  std::string base_uri_;

  std::mutex sockets_mutex_;
  std::map<std::string, std::shared_ptr<socketio_socket>> sockets_;

  connect_handler on_open_;
  close_handler on_close_;
  fail_handler on_fail_;
  error_handler on_error_;

  int reconnect_attempts_used_{0};
  bool intentional_close_{false};
};

// Thin RAII-friendly facade, mirroring the ergonomics of sio::client.
class client
{
 public:
  explicit client(client_options options = {});

  void connect(const std::string& uri) { impl_->connect(uri); }
  void close() { impl_->close(); }

  std::shared_ptr<socketio_socket> socket(const std::string& nsp = "/")
  {
    return impl_->socket(nsp);
  }

  void set_open_listener(std::function<void()> h)
  {
    impl_->set_open_handler(std::move(h));
  }
  void set_close_listener(std::function<void(const std::string&)> h)
  {
    impl_->set_close_handler(std::move(h));
  }
  void set_fail_listener(std::function<void()> h)
  {
    impl_->set_fail_handler(std::move(h));
  }
  void set_error_listener(std::function<void(const std::string&)> h)
  {
    impl_->set_error_handler(std::move(h));
  }

 private:
  std::shared_ptr<socketio_client_impl> impl_;
};

}  // namespace sioxx
