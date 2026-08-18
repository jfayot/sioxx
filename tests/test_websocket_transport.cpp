#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "websocket_transport.hpp"

using namespace std::chrono_literals;

namespace
{

TEST(WebSocketTransport, AuthenticatesProxyConnectTunnel)
{
  namespace http = boost::beast::http;
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::acceptor acceptor(io_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr server_error;
  std::string method;
  std::string target;
  std::string authorization;
  std::thread server(
    [&]
    {
      try
      {
        boost::asio::ip::tcp::socket socket(io_context);
        acceptor.accept(socket);
        boost::beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        http::read(socket, buffer, request);
        method = std::string(request.method_string());
        target = std::string(request.target());
        authorization = std::string(request[http::field::proxy_authorization]);

        http::response<http::empty_body> response{
          http::status::proxy_authentication_required, request.version()};
        response.prepare_payload();
        http::write(socket, response);
      }
      catch (...)
      {
        server_error = std::current_exception();
      }
    });

  std::mutex mutex;
  std::condition_variable condition;
  std::string error;
  auto transport = std::make_shared<sioxx::websocket_transport>();
  transport->set_proxy("http://127.0.0.1:" + std::to_string(port), "user",
                       "password");
  transport->set_error_handler(
    [&](const std::string& message)
    {
      std::lock_guard<std::mutex> lock(mutex);
      error = message;
      condition.notify_all();
    });
  transport->connect(
    "ws://origin.example:3000/socket.io/?EIO=4&transport=websocket");

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock, 5s, [&] { return !error.empty(); }));
  }
  server.join();

  ASSERT_EQ(server_error, nullptr);
  EXPECT_EQ(method, "CONNECT");
  EXPECT_EQ(target, "origin.example:3000");
  EXPECT_EQ(authorization, "Basic dXNlcjpwYXNzd29yZA==");
  EXPECT_EQ(error, "proxy CONNECT returned HTTP 407");
}

}  // namespace
