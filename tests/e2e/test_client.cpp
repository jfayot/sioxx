#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <sioxx/sioxx.hpp>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{

class completion_signal
{
 public:
  void set()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ready_ = true;
    }
    condition_.notify_all();
  }

  bool wait_for(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout, [this] { return ready_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool ready_{false};
};

class event_counter
{
 public:
  void increment()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++count_;
    }
    condition_.notify_all();
  }

  bool wait_for(int expected, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout,
                               [this, expected] { return count_ >= expected; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  int count_{0};
};

template <typename T> class async_value
{
 public:
  void set(T value)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      value_ = std::move(value);
      ready_ = true;
    }
    condition_.notify_all();
  }

  bool wait_for(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout, [this] { return ready_; });
  }

  T value() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool ready_{false};
  T value_{};
};

std::string server_url(const char* variable = "SIOXX_E2E_URL")
{
  const char* value = std::getenv(variable);
  if (!value || std::string(value).empty())
    throw std::runtime_error(std::string(variable) + " is not set");
  return value;
}

void configure_failure_reporting(
  sioxx::client& client, std::shared_ptr<async_value<std::string>> error)
{
  client.set_error_listener([error](const std::string& message)
                            { error->set(message); });
  client.set_fail_listener([error] { error->set("connection failed"); });
}

}  // namespace

TEST(E2E, ConnectsAndReceivesMultipleArguments)
{
  auto connected = std::make_shared<completion_signal>();
  auto received = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  socket->on("server_arguments",
             [received](const std::string&, sioxx::message data)
             { received->set(std::move(data)); });

  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  ASSERT_TRUE(received->wait_for(5s));

  const auto arguments = received->value();
  ASSERT_EQ(arguments.size(), 3);
  EXPECT_EQ(arguments[0], 1);
  EXPECT_EQ(arguments[1], "two");
  EXPECT_EQ(arguments[2], sioxx::json({{"three", 3}}));

  client.close();
}

TEST(E2E, NonRootNamespaceInvokesClientOpenListener)
{
  auto opened = std::make_shared<completion_signal>();
  auto connected = std::make_shared<completion_signal>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  client.set_open_listener([opened] { opened->set(); });
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  EXPECT_TRUE(opened->wait_for(5s));

  client.close();
}

TEST(E2E, DeliversNamespaceConnectionErrors)
{
  auto connect_error = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/rejected");

  configure_failure_reporting(client, error);
  socket->on("connect_error",
             [connect_error](const std::string&, sioxx::message data)
             { connect_error->set(std::move(data)); });
  client.connect(server_url());

  ASSERT_TRUE(connect_error->wait_for(5s))
    << "connection error: " << error->value();
  EXPECT_EQ(connect_error->value().value("message", ""), "unauthorized");

  client.close();
}

TEST(E2E, ReceivesAcknowledgementFromServer)
{
  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  socket->emit("echo_with_ack", sioxx::json::array({42, "hello"}),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  EXPECT_EQ(
    acknowledgement->value(),
    sioxx::json::array({{{"received", sioxx::json::array({42, "hello"})}}}));

  client.close();
}

TEST(E2E, BuffersOutgoingEventUntilNamespaceConnects)
{
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->emit("echo_with_ack", sioxx::json::array({"before-connect"}),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  client.connect(server_url());

  ASSERT_TRUE(acknowledgement->wait_for(5s))
    << "connection error: " << error->value();
  EXPECT_EQ(acknowledgement->value(),
            sioxx::json::array(
              {{{"received", sioxx::json::array({"before-connect"})}}}));

  client.close();
}

TEST(E2E, RepliesToServerEventThatRequestsAcknowledgement)
{
  auto connected = std::make_shared<completion_signal>();
  auto request = std::make_shared<async_value<sioxx::message>>();
  auto reply_received = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  socket->on("server_ack_request",
             [request](const std::string&, sioxx::message data,
                       sioxx::socket::ack_callback acknowledge)
             {
               request->set(std::move(data));
               acknowledge(sioxx::json::array({8, "answer"}));
             });
  socket->on("server_ack_reply_received",
             [reply_received](const std::string&, sioxx::message data)
             { reply_received->set(std::move(data)); });

  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  ASSERT_TRUE(request->wait_for(5s));
  EXPECT_EQ(request->value(), sioxx::json::array({7, "question"}));
  ASSERT_TRUE(reply_received->wait_for(5s));
  EXPECT_EQ(reply_received->value(), sioxx::json::array({8, "answer"}));

  client.close();
}

TEST(E2E, ConnectsWithHttpPollingOnly)
{
  sioxx::client_options options;
  options.force_http_polling = true;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "polling");

  client.close();
}

TEST(E2E, CloseFromPollingCallbackDoesNotDeadlock)
{
  sioxx::client_options options;
  options.force_http_polling = true;

  auto close_returned = std::make_shared<completion_signal>();
  sioxx::client client(options);
  client.set_open_listener(
    [&client, close_returned]
    {
      client.close();
      close_returned->set();
    });
  client.connect(server_url());

  EXPECT_TRUE(close_returned->wait_for(5s));
  client.sync_close();
}

TEST(E2E, ConnectsWithWebsocketThroughAuthenticatedProxy)
{
  sioxx::client_options options;
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "websocket");

  client.close();
}

TEST(E2E, ConnectsWithHttpPollingThroughAuthenticatedProxy)
{
  sioxx::client_options options;
  options.force_http_polling = true;
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "polling");

  client.close();
}

TEST(E2E, ConnectsWithSecureWebsocketThroughAuthenticatedProxy)
{
  sioxx::client_options options;
  options.verify_tls = false;
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "websocket");

  client.close();
}

TEST(E2E, ConnectsWithSecureHttpPollingThroughAuthenticatedProxy)
{
  sioxx::client_options options;
  options.verify_tls = false;
  options.force_http_polling = true;
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "polling");

  client.close();
}

TEST(E2E, SendsExtraHeadersWithWebsocket)
{
  sioxx::client_options options;
  options.extra_headers = {{"X-Sioxx-Test", "websocket-header"}};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("connection_headers_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0]["testHeader"], "websocket-header");
  EXPECT_EQ(acknowledgement->value()[0]["transport"], "websocket");

  client.close();
}

TEST(E2E, SendsExtraHeadersWithHttpPolling)
{
  sioxx::client_options options;
  options.force_http_polling = true;
  options.extra_headers = {{"X-Sioxx-Test", "polling-header"}};

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_POLLING_ONLY_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("connection_headers_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0]["testHeader"], "polling-header");
  EXPECT_EQ(acknowledgement->value()[0]["transport"], "polling");

  client.close();
}

TEST(E2E, ConnectsWithWebsocketOverTls)
{
  sioxx::client_options options;
  options.verify_tls = false;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "websocket");

  client.close();
}

TEST(E2E, ConnectsWithHttpPollingOverTls)
{
  sioxx::client_options options;
  options.verify_tls = false;
  options.force_http_polling = true;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "polling");

  client.close();
}

TEST(E2E, UsesCustomPathQueryAndRefreshedNamespaceAuth)
{
  sioxx::client_options options;
  options.force_http_polling = true;
  options.engineio_path = "/realtime/";
  options.query = {
    {"client name", "C++ device"},
    {"version", "2"},
  };

  auto connections = std::make_shared<event_counter>();
  auto disconnect_requested = std::make_shared<completion_signal>();
  auto disconnected = std::make_shared<completion_signal>();
  auto initial_details = std::make_shared<async_value<sioxx::message>>();
  auto refreshed_details = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket(
    "/private", sioxx::json{{"token", "initial-token"}, {"tenant", 42}});

  configure_failure_reporting(client, error);
  socket->on_connect([connections] { connections->increment(); });
  socket->on_disconnect([disconnected](const std::string&)
                        { disconnected->set(); });
  client.connect(server_url("SIOXX_E2E_CUSTOM_OPTIONS_URL"));

  ASSERT_TRUE(connections->wait_for(1, 5s))
    << "connection error: " << error->value();

  socket->emit("connection_details_with_ack", sioxx::json::array(),
               [initial_details](sioxx::message data)
               { initial_details->set(std::move(data)); });

  ASSERT_TRUE(initial_details->wait_for(5s));
  ASSERT_EQ(initial_details->value().size(), 1);
  EXPECT_EQ(initial_details->value()[0]["auth"],
            sioxx::json({{"token", "initial-token"}, {"tenant", 42}}));
  EXPECT_EQ(initial_details->value()[0]["query"]["client name"], "C++ device");
  EXPECT_EQ(initial_details->value()[0]["query"]["version"], "2");
  EXPECT_EQ(initial_details->value()[0]["transport"], "polling");

  socket->emit("disconnect_namespace", sioxx::json::array(),
               [disconnect_requested](sioxx::message)
               { disconnect_requested->set(); });

  ASSERT_TRUE(disconnect_requested->wait_for(5s));
  ASSERT_TRUE(disconnected->wait_for(5s));

  socket->set_auth(sioxx::json{{"token", "refreshed-token"}});
  socket->connect();

  ASSERT_TRUE(connections->wait_for(2, 5s));

  socket->emit("connection_details_with_ack", sioxx::json::array(),
               [refreshed_details](sioxx::message data)
               { refreshed_details->set(std::move(data)); });

  ASSERT_TRUE(refreshed_details->wait_for(5s));
  ASSERT_EQ(refreshed_details->value().size(), 1);
  EXPECT_EQ(refreshed_details->value()[0]["auth"],
            sioxx::json({{"token", "refreshed-token"}}));

  client.close();
}

TEST(E2E, FallsBackToPollingWhenWebsocketIsUnavailable)
{
  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_POLLING_ONLY_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  socket->emit("transport_with_ack", sioxx::json::array(),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], "polling");

  client.close();
}

TEST(E2E, CloseDuringFallbackNotificationPreventsPollingConnection)
{
  auto close_returned = std::make_shared<completion_signal>();
  auto connections = std::make_shared<event_counter>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  client.set_error_listener(
    [&client, close_returned](const std::string& message)
    {
      if (message.find("switching to HTTP long-polling") == std::string::npos)
        return;
      client.close();
      close_returned->set();
    });
  socket->on_connect([connections] { connections->increment(); });
  client.connect(server_url("SIOXX_E2E_POLLING_ONLY_URL"));

  ASSERT_TRUE(close_returned->wait_for(5s));
  EXPECT_FALSE(connections->wait_for(1, 1s));
  client.sync_close();
}

TEST(E2E, ReconnectsAfterUnexpectedWebsocketClose)
{
  sioxx::client_options options;
  options.reconnect_attempts = 10;
  options.reconnect_delay = 100ms;
  options.reconnect_delay_max = 100ms;
  options.reconnect_randomization_factor = 0.0;

  auto connections = std::make_shared<event_counter>();
  auto closed = std::make_shared<completion_signal>();
  auto drop_requested = std::make_shared<async_value<sioxx::message>>();
  auto initial_transport = std::make_shared<async_value<sioxx::message>>();
  auto reconnected_transport = std::make_shared<async_value<sioxx::message>>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  client.set_close_listener([closed](const std::string&) { closed->set(); });
  socket->on_connect([connections] { connections->increment(); });
  client.connect(server_url());

  ASSERT_TRUE(connections->wait_for(1, 5s))
    << "connection error: " << error->value();

  socket->emit("transport_with_ack", sioxx::json::array(),
               [initial_transport](sioxx::message data)
               { initial_transport->set(std::move(data)); });
  ASSERT_TRUE(initial_transport->wait_for(5s));
  ASSERT_EQ(initial_transport->value().size(), 1);
  ASSERT_EQ(initial_transport->value()[0], "websocket");

  socket->emit("drop_transport", sioxx::json::array(),
               [drop_requested](sioxx::message data)
               { drop_requested->set(std::move(data)); });

  ASSERT_TRUE(drop_requested->wait_for(5s));
  ASSERT_TRUE(closed->wait_for(5s))
    << "client did not observe the WebSocket transport close";
  ASSERT_TRUE(connections->wait_for(2, 5s))
    << "client did not reconnect after the server restarted";

  socket->emit("transport_with_ack", sioxx::json::array(),
               [reconnected_transport](sioxx::message data)
               { reconnected_transport->set(std::move(data)); });
  ASSERT_TRUE(reconnected_transport->wait_for(5s));
  ASSERT_EQ(reconnected_transport->value().size(), 1);
  EXPECT_EQ(reconnected_transport->value()[0], "websocket");

  socket->emit("echo_with_ack", sioxx::json::array({"after-reconnect"}),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  EXPECT_EQ(acknowledgement->value(),
            sioxx::json::array(
              {{{"received", sioxx::json::array({"after-reconnect"})}}}));

  client.close();
}

TEST(E2E, DestructionCancelsPendingReconnect)
{
  sioxx::client_options options;
  options.reconnect_attempts = 1;
  options.reconnect_delay = 200ms;
  options.reconnect_delay_max = 200ms;
  options.reconnect_randomization_factor = 0.0;

  auto connections = std::make_shared<event_counter>();
  auto closed = std::make_shared<completion_signal>();
  auto drop_requested = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  {
    sioxx::client client(options);
    auto socket = client.socket("/e2e");

    configure_failure_reporting(client, error);
    client.set_close_listener([closed](const std::string&) { closed->set(); });
    socket->on_connect([connections] { connections->increment(); });
    client.connect(server_url());

    ASSERT_TRUE(connections->wait_for(1, 5s))
      << "connection error: " << error->value();

    socket->emit("drop_transport", sioxx::json::array(),
                 [drop_requested](sioxx::message data)
                 { drop_requested->set(std::move(data)); });

    ASSERT_TRUE(drop_requested->wait_for(5s));
    ASSERT_TRUE(closed->wait_for(5s))
      << "client did not observe the WebSocket transport close";
  }

  EXPECT_FALSE(connections->wait_for(2, 2s));
}

TEST(E2E, ExchangesMessagePackEventsAcknowledgementsAndBinary)
{
  sioxx::client_options options;
  options.parser = sioxx::parser_kind::msgpack;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_MSGPACK_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  const auto binary =
    sioxx::binary_message(std::vector<uint8_t>{0x00, 0x7f, 0xff});
  socket->emit(
    "msgpack_echo_with_ack",
    sioxx::json::array({sioxx::json{{"nested", {{"value", 42}}}}, binary}),
    [acknowledgement](sioxx::message data)
    { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  const auto reply = acknowledgement->value();
  ASSERT_EQ(reply.size(), 2);
  EXPECT_EQ(reply[0], sioxx::json({{"nested", {{"value", 42}}}}));
  EXPECT_EQ(reply[1], binary);

  client.close();
}

TEST(E2E, ExchangesJsonBinaryEventsAndAcknowledgements)
{
  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  const auto first =
    sioxx::binary_message(std::vector<uint8_t>{0x00, 0x7f, 0xff});
  const auto second = sioxx::binary_message(std::vector<uint8_t>{0x01, 0x02});
  socket->emit("json_binary_echo_with_ack",
               sioxx::json::array({first, sioxx::json{{"nested", {second}}}}),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  const auto reply = acknowledgement->value();
  ASSERT_EQ(reply.size(), 2);
  EXPECT_EQ(reply[0], first);
  EXPECT_EQ(reply[1]["nested"][0], second);

  client.close();
}

TEST(E2E, ExchangesJsonBinaryEventWithBinaryAcknowledgementReply)
{
  auto connected = std::make_shared<completion_signal>();
  auto request = std::make_shared<async_value<sioxx::message>>();
  auto reply_received = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");
  const auto first =
    sioxx::binary_message(std::vector<uint8_t>{0x00, 0x7f, 0xff});
  const auto second = sioxx::binary_message(std::vector<uint8_t>{0x01, 0x02});
  const auto reply_binary =
    sioxx::binary_message(std::vector<uint8_t>{0xaa, 0xbb});

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  socket->on("json_binary_ack_request",
             [request, reply_binary](const std::string&, sioxx::message data,
                                     sioxx::socket::ack_callback acknowledge)
             {
               request->set(std::move(data));
               acknowledge(sioxx::json::array(
                 {reply_binary, sioxx::json{{"nested", {reply_binary}}}}));
             });
  socket->on("json_binary_ack_reply_received",
             [reply_received](const std::string&, sioxx::message data)
             { reply_received->set(std::move(data)); });

  client.connect(server_url());

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();
  ASSERT_TRUE(request->wait_for(5s));
  EXPECT_EQ(request->value()[0], first);
  EXPECT_EQ(request->value()[1]["nested"][0], second);
  ASSERT_TRUE(reply_received->wait_for(5s));
  EXPECT_EQ(reply_received->value()[0], reply_binary);
  EXPECT_EQ(reply_received->value()[1]["nested"][0], reply_binary);

  client.close();
}

TEST(E2E, ExchangesJsonBinaryAttachmentsWithHttpPolling)
{
  sioxx::client_options options;
  options.force_http_polling = true;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_POLLING_ONLY_URL"));

  ASSERT_TRUE(connected->wait_for(5s))
    << "connection error: " << error->value();

  const auto binary =
    sioxx::binary_message(std::vector<uint8_t>{0x00, 0x7f, 0xff});
  socket->emit("json_binary_echo_with_ack", sioxx::json::array({binary}),
               [acknowledgement](sioxx::message data)
               { acknowledgement->set(std::move(data)); });

  ASSERT_TRUE(acknowledgement->wait_for(5s));
  ASSERT_EQ(acknowledgement->value().size(), 1);
  EXPECT_EQ(acknowledgement->value()[0], binary);

  client.close();
}

TEST(E2E, RoutesEventsAndAcknowledgementsAcrossNamespaces)
{
  auto a_connected = std::make_shared<completion_signal>();
  auto b_connected = std::make_shared<completion_signal>();
  auto a_event = std::make_shared<async_value<sioxx::message>>();
  auto b_event = std::make_shared<async_value<sioxx::message>>();
  auto b_ack = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client;
  auto socket_a = client.socket("/e2e-a");
  auto socket_b = client.socket("/e2e-b");

  configure_failure_reporting(client, error);
  socket_a->on_connect([a_connected] { a_connected->set(); });
  socket_b->on_connect([b_connected] { b_connected->set(); });
  socket_a->on("scoped_event",
               [a_event](const std::string&, sioxx::message data)
               { a_event->set(std::move(data)); });
  socket_b->on("scoped_event",
               [b_event](const std::string&, sioxx::message data)
               { b_event->set(std::move(data)); });

  client.connect(server_url());

  ASSERT_TRUE(a_connected->wait_for(5s));
  ASSERT_TRUE(b_connected->wait_for(5s))
    << "connection error: " << error->value();

  socket_a->emit("request_scoped_event");
  ASSERT_TRUE(a_event->wait_for(5s));
  ASSERT_EQ(a_event->value().size(), 1);
  EXPECT_EQ(a_event->value()[0], "/e2e-a");
  EXPECT_FALSE(b_event->wait_for(100ms));

  socket_a->disconnect();
  socket_b->emit("namespace_with_ack", sioxx::json::array(),
                 [b_ack](sioxx::message data) { b_ack->set(std::move(data)); });

  ASSERT_TRUE(b_ack->wait_for(5s));
  ASSERT_EQ(b_ack->value().size(), 1);
  EXPECT_EQ(b_ack->value()[0], "/e2e-b");

  client.close();
}
