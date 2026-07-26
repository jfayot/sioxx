/**
 * @file client.hpp
 * @brief Public entry point that mirrors the ergonomics of the original
 *        `socket.io-client-cpp` library.
 *
 * A `sioxx::client` owns an internal implementation (`client_impl`) and
 * provides a thin, RAII‑friendly facade for connecting to a Socket.IO server,
 * obtaining namespace sockets and registering lifecycle listeners.
 */

#pragma once

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
  /**
   * @brief Construct a client with optional configuration.
   * @param options  Client options; defaults to an empty `client_options`.
   */
  explicit client(client_options options = {});

  /** @brief Destructor – cleans up the internal implementation. */
  ~client();

  /**
   * @brief Open a connection to the given URI.
   * @param uri  Full URL (e.g. `wss://host:port/path`).
   */
  void connect(const std::string& uri);

  /** @brief Close the underlying Engine.IO connection. */
  void close();

  /**
   * @brief Obtain a socket for a specific namespace.
   *
   * @param nsp  Namespace string (default is the root namespace `/`).
   * @return A shared pointer to a `sioxx::socket` bound to this client.
   *
   * The returned socket can be used to register event listeners, emit events,
   * and manually connect/disconnect the namespace.
   */
  std::shared_ptr<sioxx::socket> socket(const std::string& nsp = "/");

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
