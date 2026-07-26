/**
 * @file socket.hpp
 * @brief Represents a Socket.IO namespace connection.
 *
 * A `socket` is obtained from a `client` via `client::socket("/")` (or any
 * other namespace).  It provides event registration, emission (with optional
 * acknowledgements), and explicit connect/disconnect handling.
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "message.hpp"

namespace sioxx
{

class client_impl;

/**
 * @class socket
 * @brief One namespace‑scoped communication channel.
 *
 * The class mirrors the API of the original `socket.io-client-cpp` socket.
 * Listeners are stored per‑event name; registering a listener for an existing
 * name overwrites the previous one (consistent with the reference client).
 *
 * All public methods are thread‑safe; internal state is protected by a mutex.
 */
class socket : public std::enable_shared_from_this<socket>
{
 public:
  /** @brief Type of a generic event listener (receives event name & payload). */
  using event_listener =
    std::function<void(const std::string& event, message data)>;

  /** @brief Callback invoked when a server ACK arrives for an emitted event. */
  using ack_callback = std::function<void(message data)>;

  /** @brief Listener for the *connect* event of this namespace. */
  using connect_listener = std::function<void()>;

  /** @brief Listener for the *disconnect* event of this namespace. */
  using disconnect_listener = std::function<void(const std::string& reason)>;

  /**
   * @brief Construct a socket bound to a client and a namespace.
   *
   * @param client   Weak reference to the owning `client_impl`.
   * @param nsp      Namespace string (e.g. `"/chat"`).
   */
  socket(std::weak_ptr<client_impl> client, std::string nsp);

  /** @brief Return the namespace this socket belongs to. */
  const std::string& nsp() const { return nsp_; }

  /** @brief Whether the namespace is currently connected. */
  bool connected() const { return connected_; }

  /** @name Event registration */
  /** @{ */
  /**
   * @brief Register a listener for a specific event name.
   *
   * If a listener for the same name already exists it is replaced.
   *
   * @param event    Name of the event (e.g. `"message"`).
   * @param listener Callable that receives the event name and its payload.
   */
  void on(const std::string& event, event_listener listener);

  /** @brief Remove the listener for a given event name. */
  void off(const std::string& event);

  /** @brief Remove **all** registered event listeners. */
  void off_all();
  /** @} */

  /** @name Connection lifecycle callbacks */
  /** @{ */
  /** @brief Set a callback invoked when this namespace receives a CONNECT. */
  void on_connect(connect_listener listener);

  /** @brief Set a callback invoked when this namespace receives a DISCONNECT. */
  void on_disconnect(disconnect_listener listener);
  /** @} */

  /** @name Emission */
  /** @{ */
  /**
   * @brief Emit an event without expecting an acknowledgement.
   *
   * @param event   Event name.
   * @param data    Payload (default empty JSON array).  May be any JSON value.
   */
  void emit(const std::string& event, message data = json::array());

  /**
   * @brief Emit an event and request an acknowledgement.
   *
   * @param event    Event name.
   * @param data     Payload.
   * @param callback Function called when the server ACK arrives.
   */
  void emit(const std::string& event,
            message data,
            ack_callback callback);
  /** @} */

  /** @name Namespace control */
  /** @{ */
  /** @brief Send a CONNECT packet for this namespace. */
  void connect();

  /** @brief Send a DISCONNECT packet for this namespace. */
  void disconnect();
  /** @} */

  /** @name Internal callbacks – called by `client_impl` */
  /** @{ */
  /** @brief Deliver an incoming event from the server. */
  void dispatch_event(const std::string& event, message data);

  /** @brief Deliver an incoming ACK from the server. */
  void dispatch_ack(int id, message data);

  /**
   * @brief Mark the socket as (dis)connected.
   *
   * Called by the client implementation after a successful CONNECT or when a
   * DISCONNECT packet is received.
   *
   * @param connected          New connection state.
   * @param disconnect_reason  Optional reason string (empty if none).
   */
  void mark_connected(bool connected,
                      const std::string& disconnect_reason = "");
  /** @} */

 private:
  std::weak_ptr<client_impl> client_;
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
