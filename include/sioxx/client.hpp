#pragma once
// sioxx/client.hpp
//
// Public entry point, roughly matching sio::client from the original
// socket.io-client-cpp: sioxx::client c; c.connect(url); c.socket("/chat");

#include <functional>
#include <memory>
#include <string>

#include "client_options.hpp"
#include "socket.hpp"

namespace sioxx
{

class client_impl;

// Thin RAII-friendly facade, mirroring the ergonomics of sio::client.
class client
{
 public:
  explicit client(client_options options = {});
  ~client();

  void connect(const std::string& uri);
  void close();

  std::shared_ptr<sioxx::socket> socket(const std::string& nsp = "/");

  void set_open_listener(std::function<void()> h);
  void set_close_listener(std::function<void(const std::string&)> h);
  void set_fail_listener(std::function<void()> h);
  void set_error_listener(std::function<void(const std::string&)> h);

 private:
  std::shared_ptr<client_impl> impl_;
};

}  // namespace sioxx
