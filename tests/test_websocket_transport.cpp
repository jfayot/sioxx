#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "websocket_transport.hpp"

using namespace std::chrono_literals;

namespace
{

void expect_queued_send_before_connect_is_discarded(const std::string& scheme)
{
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::acceptor acceptor(io_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  boost::beast::error_code close_error;
  acceptor.close(close_error);

  std::mutex mutex;
  std::condition_variable condition;
  std::string error;
  auto transport = std::make_shared<sioxx::websocket_transport>();
  transport->set_error_handler(
    [&](const std::string& message)
    {
      std::lock_guard<std::mutex> lock(mutex);
      error = message;
      condition.notify_all();
    });

  transport->send("queued before connect", false);
  transport->connect(scheme + "://127.0.0.1:" + std::to_string(port) +
                     "/socket.io/?EIO=4&transport=websocket");

  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(condition.wait_for(lock, 5s, [&] { return !error.empty(); }));
  }
  transport->sync_close();
}

TEST(WebSocketTransport, ConcurrentCloseAndSyncCloseReleaseTransport)
{
  namespace http = boost::beast::http;
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::acceptor acceptor(io_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::promise<void> request_received;
  auto request_received_future = request_received.get_future();
  std::promise<void> release_server;
  auto release_server_future = release_server.get_future();
  std::exception_ptr server_error;
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
        request_received.set_value();
        release_server_future.wait();
      }
      catch (...)
      {
        server_error = std::current_exception();
        request_received.set_value();
      }
    });

  auto transport = std::make_shared<sioxx::websocket_transport>();
  std::weak_ptr<sioxx::websocket_transport> weak_transport = transport;
  transport->connect("ws://127.0.0.1:" + std::to_string(port) +
                     "/socket.io/?EIO=4&transport=websocket");

  const bool request_was_received =
    request_received_future.wait_for(5s) == std::future_status::ready;
  std::thread first_close([&] { transport->close(); });
  std::thread second_close([&] { transport->close(); });
  first_close.join();
  second_close.join();
  std::thread first_sync_close([&] { transport->sync_close(); });
  std::thread second_sync_close([&] { transport->sync_close(); });
  first_sync_close.join();
  second_sync_close.join();
  transport.reset();
  const bool transport_was_released = weak_transport.expired();
  release_server.set_value();
  boost::beast::error_code ec;
  acceptor.close(ec);
  server.join();

  ASSERT_TRUE(request_was_received);
  ASSERT_EQ(server_error, nullptr);
  EXPECT_TRUE(transport_was_released);
}

TEST(WebSocketTransport, DiscardsQueuedSendBeforeStreamCreation)
{
  expect_queued_send_before_connect_is_discarded("ws");
  expect_queued_send_before_connect_is_discarded("wss");
}

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
