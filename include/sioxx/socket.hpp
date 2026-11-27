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

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "message.hpp"
#include "packet.hpp"

namespace sioxx
{

class client_impl;
class ack_scheduler;
class engineio_client;

namespace detail
{
class socket_test_access;
}

/**
 * @class socket
 * @brief One namespace‑scoped communication channel.
 *
 * The class mirrors the API of the original `socket.io-client-cpp` socket.
 * Listeners are stored per‑event name; registering a listener for an existing
 * name overwrites the previous one (consistent with the reference client).
 *
 * Each namespace buffers outgoing events in FIFO order while disconnected.
 * Namespace/transport disconnects and reconnect exhaustion preserve this
 * buffer. A later namespace CONNECT flushes it; timed entries retain their
 * original deadlines. Events handed to a transport are only retried when
 * explicitly requested through the retry overload of emit().
 * Explicit client shutdown discards the buffer and all pending ACK callbacks
 * without notification. All emit() overloads then throw std::logic_error
 * until client::connect() is called again. The buffer has no size limit.
 *
 * All public methods are thread‑safe; internal state is protected by a mutex.
 * Keep any state captured by reference alive through callback completion.
 * Removing or replacing a listener allows already-dispatched calls to finish.
 * Listeners must not throw.
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

  /** @brief Outcome of an emission with an acknowledgement deadline. */
  enum class ack_status
  {
    acknowledged, /**< The server supplied an acknowledgement. */
    timeout       /**< The deadline elapsed without an acknowledgement. */
  };

  /** @brief Receives an outcome and reply (null on timeout). */
  using timed_ack_callback = std::function<void(ack_status, message)>;

  /** @brief Listener for events that may request an acknowledgement. */
  using ack_event_listener = std::function<void(
    const std::string& event, message data, ack_callback acknowledge)>;

  /** @brief Listener for the *connect* event of this namespace. */
  using connect_listener = std::function<void()>;

  /** @brief Listener for the *disconnect* event of this namespace. */
  using disconnect_listener = std::function<void(const std::string& reason)>;

  /** @brief Discard buffered events and pending ACKs without notification. */
  ~socket();

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
   * @throws std::logic_error If the client has explicitly shut down.
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
   * Pending callbacks have no deadline and survive disconnects, including
   * reconnect exhaustion. Client shutdown cancels them without notification.
   * @throws std::logic_error If the client has explicitly shut down.
   */
  void emit(const std::string& event, const message& data,
            ack_callback callback);

  /**
   * @brief Emit with a finite acknowledgement deadline.
   *
   * The deadline starts at this call, including time buffered before CONNECT
   * and time disconnected. Disconnects do not reset the deadline. An expired
   * buffered event is discarded; an event already handed to the transport
   * cannot be recalled. Late acknowledgements are ignored. No retry occurs.
   * A synchronous encoding error before expiry cancels a pending
   * acknowledgement and propagates to the caller. A buffered send that fails
   * to encode expires normally.
   * The callback runs at most once, outside socket locks, on the ACK delivery
   * thread or a socket-owned deadline worker (also with a caller-owned
   * io_context). Callbacks must not throw. Destroying the socket cancels
   * pending callbacks without notification. Explicit client close() also
   * cancels pending timed emissions; sync_close() waits for callbacks already
   * running. Timed emissions are rejected after client shutdown until the
   * next client.connect(). Ordinary namespace/transport disconnects preserve
   * deadlines.
   *
   * @param event Event name.
   * @param data Payload.
   * @param timeout Positive duration before expiry.
   * @param callback Receives acknowledged with reply data, or timeout with
   * null.
   * @throws std::invalid_argument If timeout is not positive or callback is
   * empty.
   * @throws std::logic_error If the client has explicitly shut down.
   */
  void emit(const std::string& event, const message& data,
            std::chrono::milliseconds timeout, timed_ack_callback callback);

  /**
   * @brief Emit with a bounded number of acknowledgement retries.
   *
   * The timeout applies to each attempt, including packet preparation,
   * buffering, and disconnected time. On timeout, at most retries additional
   * attempts are made with fresh ACK IDs. Replies to older attempts are
   * ignored. A retry joins the end of the namespace buffer; later events may
   * be delivered before it. Zero retries behaves like the timeout overload.
   * Connected retries are sent on a separate worker. An encoding error during
   * a buffered or background send consumes an attempt when its deadline
   * expires. A synchronous encoding error before expiry cancels a pending
   * acknowledgement and propagates to the caller.
   *
   * The callback runs once on acknowledgement or final timeout, on the ACK
   * delivery thread or deadline worker, outside socket locks. It must not
   * throw. Explicit client shutdown and socket destruction silently cancel
   * remaining attempts. sync_close() waits for running retry operations and
   * timed callbacks. Retries may cause duplicate server-side effects; include
   * an application operation ID when deduplication is needed.
   *
   * @param event Event name.
   * @param data Payload, reused unchanged on each attempt.
   * @param timeout Positive timeout for each attempt.
   * @param retries Maximum additional attempts after the initial attempt.
   * @param callback Receives acknowledged with reply data, or timeout with
   * null.
   * @throws std::invalid_argument If timeout is not positive or callback is
   * empty.
   * @throws std::logic_error If the client has explicitly shut down.
   */
  void emit(const std::string& event, const message& data,
            std::chrono::milliseconds timeout, unsigned retries,
            timed_ack_callback callback);
  /** @} */

  /** @name Namespace control */
  /** @{ */
  /** @brief Send a CONNECT packet for this namespace. */
  void connect();

  /**
   * @brief Send a DISCONNECT packet for this namespace.
   *
   * Buffered events and pending ACKs are preserved. Subsequent emissions are
   * buffered until this namespace connects again; timed deadlines continue.
   */
  void disconnect();
  /** @} */

 private:
  friend class client_impl;
  friend class detail::socket_test_access;

  socket(std::weak_ptr<client_impl> client, std::string nsp, message auth);

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

  struct retry_emission;
  struct timed_ack
  {
    std::chrono::steady_clock::time_point deadline;
    timed_ack_callback callback;
    std::optional<packet> queued;
  };
  void emit_retry(const std::shared_ptr<retry_emission>& emission,
                  std::chrono::steady_clock::time_point deadline,
                  bool background);
  void send_timed(packet packet, std::chrono::steady_clock::time_point deadline,
                  timed_ack_callback callback,
                  const std::shared_ptr<bool>& cancelled,
                  bool background = false);
  void send_timed_attempt(client_impl& client,
                          const std::shared_ptr<engineio_client>& engineio,
                          const packet& packet, bool initial_send);
  void send_queued_retry(int id);
  void cancel_pending_emissions();
  void wait_for_timed_acks();
  void resume_emissions();
  void invoke_timed_ack(timed_ack_callback callback, ack_status status,
                        message data);
  void finish_timed_ack();
  // Requires mutex_ to be held by the caller.
  void require_emissions_open() const;
  int register_timed_ack(std::chrono::steady_clock::time_point deadline,
                         timed_ack_callback callback);
  void expire_ack(int id);
  void send_or_buffer(packet packet);
  void flush_send_buffer();
  void send_ack(int id, message data);
  disconnect_listener mark_disconnected();

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
  std::map<int, timed_ack> timed_acks_;
  std::condition_variable ack_condition_;
  std::size_t active_timed_acks_{0};
  bool emissions_closed_{false};
  std::shared_ptr<bool> retries_cancelled_;
  std::deque<packet> send_buffer_;
  int next_ack_id_{0};
  bool flushing_send_buffer_{false};
  connect_listener on_connect_;
  disconnect_listener on_disconnect_;
  std::unique_ptr<ack_scheduler> ack_scheduler_;
};

}  // namespace sioxx

#endif  // SIOXX_SOCKET_HPP
