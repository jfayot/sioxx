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

#include "http_polling_transport.hpp"

using namespace std::chrono_literals;

namespace
{

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

  transport->close();
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_EQ(error_count, 1);
  EXPECT_EQ(close_count, 1);
}

}  // namespace
