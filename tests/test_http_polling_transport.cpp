#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "engineio_client.hpp"
#include "http_polling_transport.hpp"

using namespace std::chrono_literals;

namespace
{

void expect_response_limit(const std::string& response, bool external_io,
                           std::size_t expected_size,
                           boost::beast::error_code expected_error = {})
{
  namespace http = boost::beast::http;
  boost::asio::io_context server_context;
  boost::asio::ip::tcp::acceptor acceptor(server_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr server_error;
  std::thread server(
    [&]
    {
      try
      {
        boost::asio::ip::tcp::socket socket(server_context);
        acceptor.accept(socket);
        acceptor.close();
        boost::beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        http::read(socket, buffer, request);
        // Rejection can close the connection before the whole response is sent.
        boost::beast::error_code ec;
        boost::asio::write(socket, boost::asio::buffer(response), ec);
      }
      catch (...)
      {
        server_error = std::current_exception();
      }
    });

  boost::asio::io_context client_context;
  auto transport =
    external_io
      ? std::make_shared<sioxx::http_polling_transport>(client_context)
      : std::make_shared<sioxx::http_polling_transport>();
  std::mutex mutex;
  std::condition_variable condition;
  bool closed = false;
  std::size_t received_size = 0;
  std::string error;
  transport->set_message_handler(
    [&](const std::string& payload, bool)
    {
      received_size += payload.size();
      transport->close();
    });
  transport->set_error_handler([&](const std::string& message)
                               { error = message; });
  transport->set_close_handler(
    [&](const std::string&)
    {
      std::lock_guard<std::mutex> lock(mutex);
      closed = true;
      condition.notify_all();
    });
  transport->connect("ws://127.0.0.1:" + std::to_string(port) + "/");
  if (external_io)
    client_context.run();
  else
  {
    std::unique_lock<std::mutex> lock(mutex);
    EXPECT_TRUE(condition.wait_for(lock, 5s, [&] { return closed; }));
  }
  transport->sync_close();
  server.join();

  ASSERT_EQ(server_error, nullptr);
  EXPECT_TRUE(closed);
  EXPECT_EQ(received_size, expected_size);
  if (expected_error)
    EXPECT_NE(error.find(expected_error.message()), std::string::npos) << error;
  else
    EXPECT_TRUE(error.empty()) << error;
}

TEST(HttpPollingTransport, EnforcesResponseBodyLimitForEveryFramingAndIoMode)
{
  constexpr std::size_t limit = 8 * 1024 * 1024;
  for (const bool external_io : {false, true})
  {
    for (const std::string framing : {"length", "chunked", "eof"})
    {
      for (const std::size_t size : {limit, limit + 1})
      {
        SCOPED_TRACE(::testing::Message()
                     << external_io << " " << framing << " " << size);
        std::string response = "HTTP/1.1 200 OK\r\nConnection: close\r\n";
        if (framing == "length")
          response += "Content-Length: " + std::to_string(size) + "\r\n";
        else if (framing == "chunked")
          response += "Transfer-Encoding: chunked\r\n";
        response += "\r\n";
        if (framing == "chunked")
        {
          // Each chunk fits; their combined body must also fit.
          for (const auto chunk_size : {limit / 2, size - limit / 2})
          {
            std::ostringstream header;
            header << std::hex << chunk_size << "\r\n";
            response += header.str() + std::string(chunk_size, 'x') + "\r\n";
          }
          response += "0\r\n\r\n";
        }
        else
          response.append(size, 'x');
        expect_response_limit(response, external_io, size == limit ? size : 0,
                              size == limit
                                ? boost::beast::error_code{}
                                : boost::beast::http::error::body_limit);
      }
    }
  }
}

TEST(HttpPollingTransport, RejectsOversizedHeadersAndChunkMetadata)
{
  for (const bool external_io : {false, true})
  {
    SCOPED_TRACE(external_io);
    expect_response_limit(
      "HTTP/1.1 200 OK\r\nX-Padding: " + std::string(8192, 'x') +
        "\r\nContent-Length: 0\r\n\r\n",
      external_io, 0, boost::beast::http::error::header_limit);
    expect_response_limit(
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;pad=" +
        std::string(64 * 1024, 'x') + "\r\nx\r\n0\r\n\r\n",
      external_io, 0, boost::beast::http::error::buffer_overflow);
  }
}

TEST(HttpPollingTransport, ConnectionFailureNotifiesErrorAndCloseOnce)
{
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::acceptor acceptor(io_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr server_error;
  std::thread server(
    [&]
    {
      try
      {
        boost::asio::ip::tcp::socket socket(io_context);
        acceptor.accept(socket);
        boost::beast::flat_buffer buffer;
        boost::beast::http::request<boost::beast::http::string_body> request;
        boost::beast::http::read(socket, buffer, request);

        boost::beast::http::response<boost::beast::http::empty_body> response{
          boost::beast::http::status::internal_server_error, request.version()};
        response.keep_alive(false);
        response.prepare_payload();
        boost::beast::http::write(socket, response);
      }
      catch (...)
      {
        server_error = std::current_exception();
      }
    });

  std::mutex mutex;
  std::condition_variable condition;
  int error_count = 0;
  int close_count = 0;
  std::string error_message;
  std::string close_reason;
  auto transport = std::make_shared<sioxx::http_polling_transport>();
  std::weak_ptr<sioxx::http_polling_transport> weak_transport = transport;

  transport->set_error_handler(
    [&](const std::string& message)
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++error_count;
      error_message = message;
      condition.notify_all();
    });
  transport->set_close_handler(
    [&](const std::string& reason)
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++close_count;
      close_reason = reason;
      condition.notify_all();
    });

  transport->connect("ws://127.0.0.1:" + std::to_string(port) +
                     "/socket.io/?EIO=4&transport=polling");

  bool notified = false;
  {
    std::unique_lock<std::mutex> lock(mutex);
    notified = condition.wait_for(
      lock, 5s, [&] { return error_count == 1 && close_count == 1; });
  }
  server.join();

  ASSERT_EQ(server_error, nullptr);
  ASSERT_TRUE(notified);
  EXPECT_EQ(error_message, "polling handshake returned HTTP 500");
  EXPECT_EQ(close_reason, error_message);

  std::thread first_close([&] { transport->close(); });
  std::thread second_close([&] { transport->close(); });
  first_close.join();
  second_close.join();
  transport->sync_close();
  transport.reset();

  EXPECT_TRUE(weak_transport.expired());
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ(error_count, 1);
  EXPECT_EQ(close_count, 1);
}

TEST(HttpPollingTransport, UsesCallerOwnedIoContext)
{
  namespace http = boost::beast::http;
  boost::asio::io_context server_context;
  boost::asio::ip::tcp::acceptor acceptor(server_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::thread server(
    [&]
    {
      boost::asio::ip::tcp::socket socket(server_context);
      acceptor.accept(socket);
      acceptor.close();
      boost::beast::flat_buffer buffer;
      http::request<http::string_body> request;
      http::read(socket, buffer, request);
      http::response<http::string_body> response{http::status::ok,
                                                 request.version()};
      response.body() = "0{}";
      response.prepare_payload();
      http::write(socket, response);
    });

  boost::asio::io_context client_context;
  std::string error;
  std::thread::id callback_thread;
  auto transport =
    std::make_shared<sioxx::http_polling_transport>(client_context);
  transport->set_error_handler(
    [&](const std::string& message)
    {
      error = message;
      callback_thread = std::this_thread::get_id();
    });
  transport->connect("ws://127.0.0.1:" + std::to_string(port) +
                     "/socket.io/?EIO=4&transport=polling");
  transport->send("before open", false);

  EXPECT_TRUE(error.empty());
  const auto run_thread = std::this_thread::get_id();
  client_context.run();
  server.join();

  EXPECT_EQ(error, "invalid Engine.IO handshake");
  EXPECT_EQ(callback_thread, run_thread);
}

TEST(HttpPollingTransport, DeliversBatchInOrderUntilClientCloses)
{
  namespace http = boost::beast::http;
  boost::asio::io_context server_context;
  boost::asio::ip::tcp::acceptor acceptor(server_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr server_error;
  std::thread server(
    [&]
    {
      try
      {
        boost::asio::ip::tcp::socket socket(server_context);
        acceptor.accept(socket);
        acceptor.close();
        boost::beast::flat_buffer buffer;
        http::request<http::empty_body> request;
        http::read(socket, buffer, request);
        http::response<http::string_body> response{http::status::ok,
                                                   request.version()};
        response.body() =
          "0{\"sid\":\"batch-session\"}\x1e"
          "4first\x1e"
          "bAAE=\x1e"
          "4second\x1e"
          "4after-close";
        response.prepare_payload();
        http::write(socket, response);
      }
      catch (...)
      {
        server_error = std::current_exception();
      }
    });

  boost::asio::io_context client_context;
  std::vector<std::pair<std::string, bool>> frames;
  std::string error;
  auto client = std::make_shared<sioxx::engineio_client>();
  client->set_transport(
    std::make_shared<sioxx::http_polling_transport>(client_context));
  client->on_error([&](const std::string& message) { error = message; });
  client->on_frame(
    [&](const std::string& payload, bool is_binary)
    {
      frames.emplace_back(payload, is_binary);
      if (!is_binary && payload == "second") client->close();
    });
  client->open("ws://127.0.0.1:" + std::to_string(port) +
               "/socket.io/?EIO=4&transport=polling");
  client_context.run();
  server.join();

  const std::vector<std::pair<std::string, bool>> expected{
    {"first", false}, {std::string("\x00\x01", 2), true}, {"second", false}};
  ASSERT_EQ(server_error, nullptr);
  EXPECT_TRUE(error.empty()) << error;
  EXPECT_EQ(frames, expected);
  EXPECT_FALSE(client->is_open());
}

TEST(HttpPollingTransport, SendsPlainRequestsThroughAuthenticatedProxy)
{
  namespace http = boost::beast::http;
  boost::asio::io_context server_context;
  boost::asio::ip::tcp::acceptor acceptor(server_context,
                                          {boost::asio::ip::tcp::v4(), 0});
  const auto port = acceptor.local_endpoint().port();
  std::exception_ptr server_error;
  std::string target;
  std::string authorization;
  std::thread server(
    [&]
    {
      try
      {
        boost::asio::ip::tcp::socket socket(server_context);
        acceptor.accept(socket);
        boost::beast::flat_buffer buffer;
        http::request<http::string_body> request;
        http::read(socket, buffer, request);
        target = std::string(request.target());
        authorization = std::string(request[http::field::proxy_authorization]);

        http::response<http::empty_body> response{http::status::bad_gateway,
                                                  request.version()};
        response.prepare_payload();
        http::write(socket, response);
      }
      catch (...)
      {
        server_error = std::current_exception();
      }
    });

  boost::asio::io_context client_context;
  std::string error;
  auto transport =
    std::make_shared<sioxx::http_polling_transport>(client_context);
  transport->set_proxy("http://127.0.0.1:" + std::to_string(port), "user",
                       "password");
  transport->set_error_handler([&](const std::string& message)
                               { error = message; });
  transport->connect(
    "ws://origin.example:3000/socket.io/?EIO=4&transport=polling");

  client_context.run();
  server.join();

  ASSERT_EQ(server_error, nullptr);
  EXPECT_EQ(target,
            "http://origin.example:3000/socket.io/?EIO=4&transport=polling");
  EXPECT_EQ(authorization, "Basic dXNlcjpwYXNzd29yZA==");
  EXPECT_EQ(error, "polling handshake returned HTTP 502");
}

TEST(HttpPollingTransport, AuthenticatesConnectTunnelForTls)
{
  namespace http = boost::beast::http;
  boost::asio::io_context server_context;
  boost::asio::ip::tcp::acceptor acceptor(server_context,
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
        boost::asio::ip::tcp::socket socket(server_context);
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

  boost::asio::io_context client_context;
  std::string error;
  auto transport =
    std::make_shared<sioxx::http_polling_transport>(client_context);
  transport->set_proxy("http://127.0.0.1:" + std::to_string(port), "user",
                       "password");
  transport->set_error_handler([&](const std::string& message)
                               { error = message; });
  transport->connect("wss://origin.example/socket.io/?EIO=4&transport=polling");

  client_context.run();
  server.join();

  ASSERT_EQ(server_error, nullptr);
  EXPECT_EQ(method, "CONNECT");
  EXPECT_EQ(target, "origin.example:443");
  EXPECT_EQ(authorization, "Basic dXNlcjpwYXNzd29yZA==");
  EXPECT_EQ(error, "polling transport: proxy CONNECT returned HTTP 407");
}

}  // namespace
