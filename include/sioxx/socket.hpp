/**
 * @file socket.hpp
 * @brief Represents a Socket.IO namespace connection.
 *
 * A `socket` is obtained from a `client` via `client::socket("/")` (or any
 * other namespace).  It provides event registration, emission (with optional
 * acknowledgements), and explicit connect/disconnect handling.
 */

#ifndef SIOXX_SOCKET_HPP
#define SIOXX_SOCKET_HPP

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

#include "message.hpp"
#include "packet.hpp"

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
  /** @brief Type of a generic event listener (receives event name & payload).
   */
  using event_listener =
    std::function<void(const std::string& event, message data)>;

  /** @brief Callback used to receive or send acknowledgement data. */
  using ack_callback = std::function<void(message data)>;

  /** @brief Listener for events that may request an acknowledgement. */
  using ack_event_listener = std::function<void(
    const std::string& event, message data, ack_callback acknowledge)>;

  /** @brief Listener for the *connect* event of this namespace. */
  using connect_listener = std::function<void()>;

  /** @brief Listener for the *disconnect* event of this namespace. */
  using disconnect_listener = std::function<void(const std::string& reason)>;

  /**
   * @brief Construct a socket bound to a client and a namespace.
   *
   * @param client   Weak reference to the owning `client_impl`.
   * @param nsp      Namespace string (e.g. `"/chat"`).
   * @param auth     Authentication payload for namespace CONNECT packets.
   */
  socket(std::weak_ptr<client_impl> client, std::string nsp,
         message auth = json());

  /** @brief Return the namespace this socket belongs to. */
  const std::string& nsp() const { return nsp_; }

  /** @brief Replace the authentication payload used by the next CONNECT.
   *
   * Passing a null message clears the payload.
   */
  void set_auth(message auth);

  /** @brief Return a copy of the current namespace authentication payload. */
  message auth() const;

  /** @brief Whether the namespace is currently connected. */
  bool connected() const;

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

  /**
   * @brief Register a listener that can acknowledge an incoming event.
   *
   * When the server includes an acknowledgement ID, `acknowledge` sends the
   * supplied arguments back to the server and can be called at most once. If
   * the event does not request an acknowledgement, `acknowledge` is empty.
   *
   * @param event     Name of the event.
   * @param listener  Callable that receives the event, payload, and reply
   *                  function.
   */
  void on(const std::string& event, ack_event_listener listener);

  /**
   * @brief Register a listener for every incoming event.
   *
   * The listener is invoked after any listener registered for the event name.
   * Registering another catch-all listener replaces the previous one.
   *
   * @param listener Callable that receives each event name and payload.
   */
  void on_any(event_listener listener);

  /**
   * @brief Register an acknowledgement-aware listener for every event.
   *
   * The listener is invoked after any listener registered for the event name.
   * When the event requests an acknowledgement, the reply callback is shared
   * with the named listener and sends at most one response between them.
   * Registering another catch-all listener replaces the previous one.
   *
   * @param listener Callable that receives each event, payload, and reply
   *                 function.
   */
  void on_any(ack_event_listener listener);

  /** @brief Remove the listener for a given event name. */
  void off(const std::string& event);

  /** @brief Remove **all** registered event listeners. */
  void off_all();
  /** @} */

  /** @name Connection lifecycle callbacks */
  /** @{ */
  /** @brief Set a callback invoked when this namespace receives a CONNECT. */
  void on_connect(connect_listener listener);

  /** @brief Set a callback invoked when this namespace receives a DISCONNECT.
   */
  void on_disconnect(disconnect_listener listener);
  /** @} */

  /** @name Emission */
  /** @{ */
  /**
   * @brief Emit an event without expecting an acknowledgement.
   *
   * Events emitted before the namespace connects are buffered and sent in
   * order once its CONNECT packet is acknowledged by the server.
   *
   * @param event   Event name.
   * @param data    Payload (default empty JSON array).  May be any JSON value.
   */
  void emit(const std::string& event, const message& data = json::array());

  /**
   * @brief Emit an event and request an acknowledgement.
   *
   * Events emitted before the namespace connects are buffered and sent in
   * order once its CONNECT packet is acknowledged by the server.
   *
   * @param event    Event name.
   * @param data     Payload.
   * @param callback Function called when the server ACK arrives.
   */
  void emit(const std::string& event, const message& data,
            ack_callback callback);
  /** @} */

  /** @name Namespace control */
  /** @{ */
  /** @brief Send a CONNECT packet for this namespace. */
  void connect();

  /** @brief Send a DISCONNECT packet for this namespace. */
  void disconnect();
  /** @} */

  /** @name Internal callbacks – called by client_impl */
  /** @{ */
  /** @brief Deliver an incoming event from the server. */
  void dispatch_event(const std::string& event, message data, int ack_id = -1);

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
  void send_or_buffer(packet packet);
  void flush_send_buffer();
  void send_ack(int id, message data);

  std::weak_ptr<client_impl> client_;
  std::string nsp_;
  bool connected_{false};

  mutable std::mutex mutex_;
  message auth_;
  std::map<std::string, event_listener> listeners_;
  std::map<std::string, ack_event_listener> ack_listeners_;
  event_listener any_listener_;
  ack_event_listener any_ack_listener_;
  std::map<int, ack_callback> pending_acks_;
  std::queue<packet> send_buffer_;
  int next_ack_id_{0};
  bool flushing_send_buffer_{false};
  connect_listener on_connect_;
  disconnect_listener on_disconnect_;
};

}  // namespace sioxx

#endif  // SIOXX_SOCKET_HPP
