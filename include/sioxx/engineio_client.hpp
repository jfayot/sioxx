#pragma once
// sioxx/engineio_client.hpp
//
// Engine.IO v4 client: owns the websocket transport, performs the OPEN
// handshake, runs the ping/pong heartbeat, and exposes a plain
// send-frame / on-frame interface to the socket.io layer above it.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "transport.hpp"

namespace sioxx
{

class engineio_client : public std::enable_shared_from_this<engineio_client>
{
 public:
  using frame_handler =
    std::function<void(const std::string& payload, bool is_binary)>;
  using open_handler = std::function<void()>;
  using close_handler = std::function<void(const std::string& reason)>;
  using error_handler = std::function<void(const std::string& message)>;

  engineio_client();
  ~engineio_client();

  void set_transport(std::shared_ptr<transport_base> transport);

  // ws_url is the *engine.io* endpoint, e.g.
  // wss://host:port/socket.io/?EIO=4&transport=websocket
  void open(const std::string& ws_url);
  void close();

  // Send a raw engine.io MESSAGE frame (payload already encoded by the
  // socket.io parser).
  void send(const std::string& payload, bool is_binary);

  void on_frame(frame_handler h) { on_frame_ = std::move(h); }
  void on_open(open_handler h) { on_open_ = std::move(h); }
  void on_close(close_handler h) { on_close_ = std::move(h); }
  void on_error(error_handler h) { on_error_ = std::move(h); }

  bool is_open() const { return open_.load(); }

 private:
  void handle_transport_message(const std::string& payload, bool is_binary);
  void handle_transport_open();
  void handle_transport_close(const std::string& reason);
  void start_heartbeat_timer();
  void stop_heartbeat_timer();

  std::shared_ptr<transport_base> transport_;
  frame_handler on_frame_;
  open_handler on_open_;
  close_handler on_close_;
  error_handler on_error_;

  std::atomic<bool> open_{false};
  std::atomic<bool> closing_{false};

  int ping_interval_ms_{25000};
  int ping_timeout_ms_{20000};
  std::string sid_;

  std::thread heartbeat_thread_;
  std::mutex heartbeat_thread_mutex_;
  std::mutex heartbeat_mutex_;
  std::condition_variable heartbeat_cv_;
  std::chrono::steady_clock::time_point last_pong_;
};

}  // namespace sioxx
