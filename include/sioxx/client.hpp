/**
 * @file client.hpp
 * @brief Public entry point that mirrors the ergonomics of the original
 *        `socket.io-client-cpp` library.
 *
 * A `sioxx::client` owns an internal implementation (`client_impl`) and
 * provides a thin, RAII‑friendly façade for connecting to a Socket.IO server,
 * obtaining namespace sockets and registering lifecycle listeners.
 */

#ifndef SIOXX_CLIENT_HPP
#define SIOXX_CLIENT_HPP

#include <functional>
#include <memory>
#include <string>

#include "client_options.hpp"
#include "socket.hpp"

namespace boost
{
namespace asio
{
class io_context;
}
}  // namespace boost

namespace sioxx
{

class client_impl;

/**
 * @class client
 * @brief High‑level Socket.IO client.
 *
 * Typical usage:
 * @code
 *   sioxx::client c;
 *   c.set_open_listener([]{ std::cout << "connected\n"; });
 *   auto chat = c.socket("/chat");
 *   chat->on("msg", [](const std::string&, sioxx::message data){
 *       std::cout << "msg: " << data.dump() << '\n';
 *   });
 *   c.connect("wss://example.com");
 * @endcode
 */
class client
{
 public:
  /** @brief Construct a client with default configuration. */
  client();

  /**
   * @brief Construct a client by copying configuration.
   * @param options Client options to copy.
   */
  explicit client(const client_options& options);

  /**
   * @brief Construct a client by moving configuration.
   * @param options Client options to move from.
   */
  explicit client(client_options&& options);

  /**
   * @brief Construct a client that uses a caller-owned I/O context.
   * @param io_context Context used for all transport networking and callbacks.
   *
   * The caller must keep the context alive for the lifetime of the client and
   * is responsible for running it. sioxx never runs or stops this context.
   */
  explicit client(boost::asio::io_context& io_context);

  /**
   * @brief Construct a configured client using a caller-owned I/O context.
   * @param io_context Context used for all transport networking and callbacks.
   * @param options Client options to copy.
   */
  client(boost::asio::io_context& io_context, const client_options& options);

  /**
   * @brief Construct a configured client using a caller-owned I/O context.
   * @param io_context Context used for all transport networking and callbacks.
   * @param options Client options to move from.
   */
  client(boost::asio::io_context& io_context, client_options&& options);

  client(const client&) = delete;
  client& operator=(const client&) = delete;

  /**
   * @brief Destructor that requests shutdown and waits for background workers.
   *
   * Follows the shutdown rules of sync_close(), including for a caller-owned
   * context. Destroy the client outside library callbacks.
   */
  ~client();

  /**
   * @brief Open a connection to the given URI.
   * @param uri  Server URL (e.g. `wss://host:port`). Any path or query in the
   *             URL is replaced by `client_options::engineio_path` and
   *             `client_options::query`.
   * @throws std::invalid_argument if the configured query contains a reserved
   *         Engine.IO parameter or the configured proxy URI is invalid.
   */
  void connect(const std::string& uri);

  /**
   * @brief Start closing the connection and return without waiting.
   *
   * This operation is safe to call from library callbacks. Use sync_close()
   * when the caller must wait for library-owned workers and active timed ACK
   * callbacks to finish. All buffered events and pending ACK callbacks are
   * cancelled silently. Every emit() overload throws std::logic_error until
   * another connect() call. Queued Engine.IO frames are ignored after
   * shutdown. Already running callbacks may finish, and packets already
   * handed to a transport cannot be recalled.
   * Repeated calls are safe.
   */
  void close();

  /**
   * @brief Request shutdown and wait for library-owned work and active timed
   * ACKs.
   *
   * Waits for library-owned transport, reconnect, retry, and heartbeat workers
   * and for active timed ACK callbacks, even when they run on a supplied
   * context. Idle deadline workers can remain allocated until socket
   * destruction.
   *
   * For a caller-owned io_context, transport shutdown runs on that context.
   * I/O and ordinary transport callbacks may still be pending or active when
   * sync_close() returns. The caller owns running and draining the context and
   * must keep it alive longer than the client.
   *
   * Call from outside library callbacks. Use close() from callbacks. Each
   * concurrent or repeated call waits for shutdown.
   */
  void sync_close();

  /**
   * @brief Report whether the Engine.IO handshake has completed.
   * @return `true` while the underlying Engine.IO connection is open.
   */
  bool opened() const;

  /**
   * @brief Return the current Engine.IO session identifier.
   * @return The handshake `sid`, or an empty string before a session exists.
   */
  std::string get_sessionid() const;

  /**
   * @brief Obtain a socket for a specific namespace.
   *
   * @param nsp   Namespace string (default is the root namespace `/`).
   * @param auth  Authentication payload sent in the namespace CONNECT packet.
   * @return A shared pointer to a `sioxx::socket` bound to this client.
   *
   * The returned socket can be used to register event listeners, emit events,
   * and manually connect/disconnect the namespace. If the namespace socket
   * already exists, a non-null `auth` replaces its current authentication
   * payload; a null `auth` leaves it unchanged.
   * A retained pointer keeps the socket alive. Keep the client alive to use
   * the socket for network operations.
   */
  std::shared_ptr<sioxx::socket> socket(const std::string& nsp = "/",
                                        message auth = json());

  /** @name Lifecycle listeners
   * Keep any state captured by reference alive through callback completion.
   * Replacing a listener allows already-dispatched calls to finish.
   * Callbacks may run on library workers or on the caller-owned context and
   * must not throw. An explicit close can notify on the calling thread.
   */
  /** @{ */
  /** @brief Set a callback for a completed Engine.IO handshake.
   * Use socket::on_connect() to observe a namespace's own handshake.
   */
  void set_open_listener(std::function<void()> h);
  /** @brief Set a callback that runs when the Engine.IO connection closes. */
  void set_close_listener(std::function<void(const std::string&)> h);
  /** @brief Set a callback for exhaustion of the configured reconnect attempts.
   */
  void set_fail_listener(std::function<void()> h);
  /**
   * @brief Set a callback that runs when a reconnect attempt starts.
   *
   * This callback runs after the configured back-off delay, immediately
   * before opening the replacement Engine.IO connection.
   */
  void set_reconnecting_listener(std::function<void()> h);
  /**
   * @brief Set a callback that runs when a reconnect attempt is scheduled.
   *
   * The callback receives the zero-based attempt index and the selected
   * back-off delay in milliseconds.
   */
  void set_reconnect_listener(std::function<void(unsigned, unsigned)> h);
  /** @brief Set a callback for connection diagnostics.
   * Messages include transport errors and WebSocket-to-polling fallback. Use
   * a socket listener for `connect_error` to receive a namespace rejection.
   */
  void set_error_listener(std::function<void(const std::string&)> h);
  /** @} */

 private:
  std::shared_ptr<client_impl> impl_;
};

}  // namespace sioxx

#endif  // SIOXX_CLIENT_HPP
