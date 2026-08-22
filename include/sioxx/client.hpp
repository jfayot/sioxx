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
 *   c.connect("wss://example.com");
 *   auto chat = c.socket("/chat");
 *   chat->on("msg", [](const std::string&, sioxx::message data){
 *       std::cout << "msg: " << data.dump() << '\n';
 *   });
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

  client(const client&) = delete;
  client& operator=(const client&) = delete;

  /**
   * @brief Destructor that requests shutdown and waits for background workers.
   *
   * As with sync_close(), destroy the client outside library callbacks. A
   * worker executing the destructor cannot wait for itself and finishes after
   * the callback returns.
   */
  ~client();

  /**
   * @brief Open a connection to the given URI.
   * @param uri  Server URL (e.g. `wss://host:port`). Any path or query in the
   *             URL is replaced by `client_options::engineio_path` and
   *             `client_options::query`.
   * @throws std::invalid_argument if the configured query contains a reserved
   *         Engine.IO parameter.
   */
  void connect(const std::string& uri);

  /**
   * @brief Start closing the connection and return without waiting.
   *
   * This operation is safe to call from library callbacks. Use sync_close()
   * when the caller must wait for all background workers to stop. Repeated
   * calls are safe.
   */
  void close();

  /**
   * @brief Close the connection and wait for background workers to stop.
   *
   * Do not call this operation from a library callback: a worker cannot wait
   * for itself to finish. Use close() in callbacks instead. Repeated calls are
   * safe.
   */
  void sync_close();

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
   */
  std::shared_ptr<sioxx::socket> socket(const std::string& nsp = "/",
                                        message auth = json());

  /** @name Lifecycle listeners */
  /** @{ */
  /** @brief Set a callback that runs when the Engine.IO connection opens. */
  void set_open_listener(std::function<void()> h);
  /** @brief Set a callback that runs when the Engine.IO connection closes. */
  void set_close_listener(std::function<void(const std::string&)> h);
  /** @brief Set a callback that runs when the Engine.IO handshake fails. */
  void set_fail_listener(std::function<void()> h);
  /** @brief Set a callback that runs on any error condition. */
  void set_error_listener(std::function<void(const std::string&)> h);
  /** @} */

 private:
  std::shared_ptr<client_impl> impl_;
};

}  // namespace sioxx

#endif  // SIOXX_CLIENT_HPP
