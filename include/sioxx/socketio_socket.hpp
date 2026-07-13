#pragma once
// sioxx/socketio_socket.hpp
//
// Represents one socket.io namespace connection ("/", "/chat", ...),
// mirroring sio::socket from the original socket.io-client-cpp.

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "message.hpp"

namespace sioxx
{

class socketio_client_impl;  // fwd decl, defined in socketio_client.hpp/.cpp

class socketio_socket : public std::enable_shared_from_this<socketio_socket>
{
 public:
  using event_listener =
    std::function<void(const std::string& event, message data)>;
  using ack_callback = std::function<void(message data)>;
  using connect_listener = std::function<void()>;
  using disconnect_listener = std::function<void(const std::string& reason)>;

  socketio_socket(std::weak_ptr<socketio_client_impl> client, std::string nsp);

  const std::string& nsp() const { return nsp_; }
  bool connected() const { return connected_; }

  // Register a listener for a named event. Overwrites any previous
  // listener for the same event name (matches sio::socket::on semantics).
  void on(const std::string& event, event_listener listener);
  void off(const std::string& event);
  void off_all();

  // Fired when *this* namespace's CONNECT is acknowledged by the server --
  // i.e. the earliest safe point to start emitting on this socket. Since
  // connect()/emit() are asynchronous, emitting right after
  // client.connect() (before this fires) will silently drop the packet
  // because the underlying engine.io connection isn't open yet.
  void on_connect(connect_listener listener);
  void on_disconnect(disconnect_listener listener);

  // emit without expecting an ack.
  void emit(const std::string& event, message data = json::array());

  // emit with an ack callback invoked when the server acknowledges.
  void emit(const std::string& event, message data, ack_callback callback);

  void connect();     // send a CONNECT packet for this namespace
  void disconnect();  // send a DISCONNECT packet for this namespace

  // --- internal, called by socketio_client_impl ---
  void dispatch_event(const std::string& event, message data);
  void dispatch_ack(int id, message data);
  void mark_connected(bool connected,
                      const std::string& disconnect_reason = "");

 private:
  std::weak_ptr<socketio_client_impl> client_;
  std::string nsp_;
  bool connected_{false};

  std::mutex mutex_;
  std::map<std::string, event_listener> listeners_;
  std::map<int, ack_callback> pending_acks_;
  int next_ack_id_{0};
  connect_listener on_connect_;
  disconnect_listener on_disconnect_;
};

}  // namespace sioxx
