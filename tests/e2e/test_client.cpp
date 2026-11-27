#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <memory>
#include <mutex>
#include <sioxx/sioxx.hpp>
#include <string>
#include <thread>
#include <utility>
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

std::shared_ptr<event_counter> buffer_event_sequence(
  const std::shared_ptr<sioxx::socket>& socket, const std::string& prefix)
{
  auto acknowledgements = std::make_shared<event_counter>();
  socket->emit("buffer_probe", sioxx::json::array({prefix + "plain"}));
  socket->emit("buffer_probe", sioxx::json::array({prefix + "ack"}),
               [prefix, acknowledgements](sioxx::message reply)
               {
                 EXPECT_EQ(reply, sioxx::json::array({prefix + "ack"}));
                 acknowledgements->increment();
               });
  socket->emit("buffer_probe", sioxx::json::array({prefix + "timed"}), 1h,
               [prefix, acknowledgements](sioxx::socket::ack_status status,
                                          sioxx::message reply)
               {
                 EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                 EXPECT_EQ(reply, sioxx::json::array({prefix + "timed"}));
                 acknowledgements->increment();
               });
  return acknowledgements;
}

void expect_buffer_log(const std::shared_ptr<sioxx::socket>& socket,
                       const sioxx::json& expected)
{
  auto log = std::make_shared<async_value<sioxx::message>>();
  socket->emit("buffer_probe_log", sioxx::json::array(), 5s,
               [log](sioxx::socket::ack_status status, sioxx::message reply)
               {
                 EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                 log->set(std::move(reply));
               });
  ASSERT_TRUE(log->wait_for(5s));
  EXPECT_EQ(log->value(), sioxx::json::array({expected}));
}

void expect_retry_count(const std::shared_ptr<sioxx::socket>& socket,
                        const std::string& id, unsigned expected)
{
  auto count = std::make_shared<async_value<sioxx::message>>();
  socket->emit("retry_attempt_count", sioxx::json::array({id}), 5s,
               [count](sioxx::socket::ack_status status, sioxx::message reply)
               {
                 EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                 count->set(std::move(reply));
               });
  ASSERT_TRUE(count->wait_for(5s));
  EXPECT_EQ(count->value(), sioxx::json::array({expected}));
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

TEST(E2E, TimedAcknowledgementsSucceedAndExpireAcrossTransports)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    auto connected = std::make_shared<completion_signal>();
    auto reply = std::make_shared<async_value<sioxx::message>>();
    auto timeout = std::make_shared<async_value<sioxx::socket::ack_status>>();
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    socket->on_connect([connected] { connected->set(); });
    // Exercise the timed buffer path as well as real ACK delivery.
    socket->emit("echo_with_ack", sioxx::json::array({42}), 5s,
                 [reply](sioxx::socket::ack_status status, sioxx::message data)
                 {
                   EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                   reply->set(std::move(data));
                 });
    client.connect(server_url());
    ASSERT_TRUE(connected->wait_for(5s));
    ASSERT_TRUE(reply->wait_for(5s));
    EXPECT_EQ(reply->value(),
              sioxx::json::array({{{"received", sioxx::json::array({42})}}}));
    socket->emit(
      "no_ack_handler", sioxx::json(), 50ms,
      [timeout](sioxx::socket::ack_status status, sioxx::message data)
      {
        EXPECT_TRUE(data.is_null());
        timeout->set(status);
      });
    ASSERT_TRUE(timeout->wait_for(5s));
    EXPECT_EQ(timeout->value(), sioxx::socket::ack_status::timeout);
    client.sync_close();
  }
}

TEST(E2E, AcknowledgementRetriesSucceedAcrossTransportsAndParsers)
{
  for (auto parser : {sioxx::parser_kind::json, sioxx::parser_kind::msgpack})
  {
    for (bool polling : {false, true})
    {
      SCOPED_TRACE(polling ? "polling" : "websocket");
      SCOPED_TRACE(parser == sioxx::parser_kind::json ? "json" : "msgpack");
      sioxx::client_options options;
      options.parser = parser;
      options.force_http_polling = polling;
      sioxx::client client(options);
      auto socket = client.socket("/e2e");
      auto reply = std::make_shared<async_value<sioxx::message>>();
      auto calls = std::make_shared<std::atomic<int>>(0);
      const std::string id = polling ? "success-polling" : "success-websocket";
      const sioxx::json payload = {
        {"nested", sioxx::binary_message({0x00, 0x7f, 0xff})}};
      const sioxx::json request = {
        {"id", id}, {"ackAfter", 2}, {"payload", payload}};
      // Include the initial namespace connection in the first attempt's budget.
      socket->emit(
        "retry_with_ack", sioxx::json::array({request}), 500ms, 2,
        [reply, calls](sioxx::socket::ack_status status, sioxx::message data)
        {
          EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
          ++*calls;
          reply->set(std::move(data));
        });
      client.connect(server_url(parser == sioxx::parser_kind::msgpack
                                  ? "SIOXX_E2E_MSGPACK_URL"
                                  : "SIOXX_E2E_URL"));
      ASSERT_TRUE(reply->wait_for(5s));
      EXPECT_EQ(reply->value(),
                sioxx::json::array({{{"attempt", 2}, {"payload", payload}}}));
      expect_retry_count(socket, id, 2);
      client.sync_close();
      EXPECT_EQ(calls->load(), 1);
    }
  }
}

TEST(E2E, AcknowledgementRetriesStopAtConfiguredLimit)
{
  for (bool polling : {false, true})
  {
    for (unsigned retries : {0U, 2U})
    {
      SCOPED_TRACE(polling ? "polling" : "websocket");
      SCOPED_TRACE(retries);
      sioxx::client_options options;
      options.force_http_polling = polling;
      sioxx::client client(options);
      auto socket = client.socket("/e2e");
      auto connected = std::make_shared<completion_signal>();
      auto completed = std::make_shared<completion_signal>();
      auto calls = std::make_shared<std::atomic<int>>(0);
      socket->on_connect([connected] { connected->set(); });
      client.connect(server_url());
      ASSERT_TRUE(connected->wait_for(5s));
      const auto id =
        std::string(polling ? "limit-polling-" : "limit-websocket-") +
        std::to_string(retries);
      const sioxx::json request = {{"id", id}, {"ackAfter", 0}};
      socket->emit("retry_with_ack", sioxx::json::array({request}), 100ms,
                   retries,
                   [completed, calls](sioxx::socket::ack_status status,
                                      sioxx::message data)
                   {
                     EXPECT_EQ(status, sioxx::socket::ack_status::timeout);
                     EXPECT_TRUE(data.is_null());
                     ++*calls;
                     completed->set();
                   });
      ASSERT_TRUE(completed->wait_for(5s));
      expect_retry_count(socket, id, retries + 1);
      client.sync_close();
      EXPECT_EQ(calls->load(), 1);
    }
  }
}

TEST(E2E, AcknowledgementRetriesSurviveTransportReconnect)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    sioxx::client_options options;
    options.force_http_polling = polling;
    options.reconnect_attempts = 5;
    options.reconnect_delay = 10ms;
    options.reconnect_delay_max = 10ms;
    options.reconnect_randomization_factor = 0.0;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    auto connections = std::make_shared<event_counter>();
    auto reply = std::make_shared<async_value<sioxx::message>>();
    socket->on_connect([connections] { connections->increment(); });
    client.connect(server_url());
    ASSERT_TRUE(connections->wait_for(1, 5s));
    const std::string id =
      polling ? "reconnect-polling" : "reconnect-websocket";
    const sioxx::json request = {
      {"id", id}, {"ackAfter", 2}, {"dropFirst", true}, {"payload", 42}};
    socket->emit("retry_with_ack", sioxx::json::array({request}), 500ms, 2,
                 [reply](sioxx::socket::ack_status status, sioxx::message data)
                 {
                   EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                   reply->set(std::move(data));
                 });
    ASSERT_TRUE(connections->wait_for(2, 5s));
    ASSERT_TRUE(reply->wait_for(5s));
    EXPECT_EQ(reply->value(),
              sioxx::json::array({{{"attempt", 2}, {"payload", 42}}}));
    expect_retry_count(socket, id, 2);
    client.sync_close();
  }
}

TEST(E2E, AcknowledgementRetryShutdownCancelsRemainingAttempts)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    auto connections = std::make_shared<event_counter>();
    auto attempted = std::make_shared<completion_signal>();
    auto calls = std::make_shared<std::atomic<int>>(0);
    socket->on_connect([connections] { connections->increment(); });
    const std::string id = polling ? "cancel-polling" : "cancel-websocket";
    socket->on("retry_attempt",
               [attempted, id](const std::string&, sioxx::message data)
               {
                 if (data[0] == id) attempted->set();
               });
    client.connect(server_url());
    ASSERT_TRUE(connections->wait_for(1, 5s));
    const sioxx::json request = {{"id", id}, {"ackAfter", 0}};
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> retained = capture;
    socket->emit("retry_with_ack", sioxx::json::array({request}), 1h, 100,
                 [capture, calls](sioxx::socket::ack_status, sioxx::message)
                 { ++*calls; });
    capture.reset();
    ASSERT_TRUE(attempted->wait_for(5s));
    client.sync_close();
    EXPECT_TRUE(retained.expired());
    client.connect(server_url());
    ASSERT_TRUE(connections->wait_for(2, 5s));
    expect_retry_count(socket, id, 1);
    auto resumed = std::make_shared<completion_signal>();
    socket->emit("echo_with_ack", sioxx::json::array({42}), 5s, 1,
                 [resumed](sioxx::socket::ack_status status, sioxx::message)
                 {
                   EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                   resumed->set();
                 });
    ASSERT_TRUE(resumed->wait_for(5s));
    client.sync_close();
    EXPECT_EQ(calls->load(), 0);
  }
}

TEST(E2E, ReconnectingAfterExplicitShutdownResumesTimedEmissions)
{
  auto calls = std::make_shared<std::atomic<int>>(0);
  auto received = std::make_shared<async_value<sioxx::message>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");
  // This event must be removed from the buffer when the client closes.
  socket->emit("timeout_probe", sioxx::json(), 1h,
               [calls](sioxx::socket::ack_status, sioxx::message)
               { ++*calls; });
  client.sync_close();
  socket->on_connect(
    [socket, received]
    {
      socket->emit(
        "timeout_probe_count", sioxx::json(), 5s,
        [received](sioxx::socket::ack_status status, sioxx::message data)
        {
          EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
          received->set(std::move(data));
        });
    });
  client.connect(server_url());
  ASSERT_TRUE(received->wait_for(5s));
  EXPECT_EQ(received->value(), sioxx::json::array({0}));
  EXPECT_EQ(calls->load(), 0);
  client.sync_close();
  socket->on_connect(nullptr);
}

TEST(E2E, ExpiredBufferedEventIsNeverSent)
{
  auto expired = std::make_shared<completion_signal>();
  auto count = std::make_shared<async_value<sioxx::message>>();
  sioxx::client client;
  auto socket = client.socket("/e2e");
  socket->emit("timeout_probe", sioxx::json(), 20ms,
               [expired](sioxx::socket::ack_status status, sioxx::message)
               {
                 EXPECT_EQ(status, sioxx::socket::ack_status::timeout);
                 expired->set();
               });
  ASSERT_TRUE(expired->wait_for(5s));
  socket->emit("timeout_probe_count", sioxx::json(), 5s,
               [count](sioxx::socket::ack_status status, sioxx::message data)
               {
                 EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                 count->set(std::move(data));
               });
  client.connect(server_url());
  ASSERT_TRUE(count->wait_for(5s));
  EXPECT_EQ(count->value(), sioxx::json::array({0}));
  client.sync_close();
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

TEST(E2E, BufferSurvivesNamespaceDisconnectAcrossTransports)
{
  for (bool polling : {false, true})
  {
    for (bool server_disconnect : {false, true})
    {
      SCOPED_TRACE(polling ? "polling" : "websocket");
      SCOPED_TRACE(server_disconnect ? "server disconnect"
                                     : "client disconnect");
      sioxx::client_options options;
      options.force_http_polling = polling;
      sioxx::client client(options);
      auto socket = client.socket("/e2e");
      auto control = client.socket("/e2e-a");
      auto control_connected = std::make_shared<completion_signal>();
      control->on_connect([control_connected] { control_connected->set(); });
      auto connections = std::make_shared<event_counter>();
      auto disconnected = std::make_shared<completion_signal>();
      socket->on_connect([connections] { connections->increment(); });
      socket->on_disconnect([disconnected](const std::string&)
                            { disconnected->set(); });
      client.connect(server_url());
      ASSERT_TRUE(connections->wait_for(1, 5s));
      ASSERT_TRUE(control_connected->wait_for(5s));
      if (server_disconnect)
      {
        socket->emit("disconnect_namespace");
        ASSERT_TRUE(disconnected->wait_for(5s));
      }
      else
      {
        socket->disconnect();
        // The server dispatches DISCONNECT on its next tick. A reply on a
        // second namespace confirms it processed that packet before CONNECT.
        auto processed = std::make_shared<completion_signal>();
        control->emit("namespace_with_ack", sioxx::json::array(),
                      [processed](sioxx::message) { processed->set(); });
        ASSERT_TRUE(processed->wait_for(5s));
      }
      auto acknowledgements = buffer_event_sequence(socket, "");
      socket->connect();
      ASSERT_TRUE(connections->wait_for(2, 5s));
      expect_buffer_log(socket, sioxx::json::array({"plain", "ack", "timed"}));
      EXPECT_TRUE(acknowledgements->wait_for(2, 5s));
      client.sync_close();
    }
  }
}

TEST(E2E, BufferSurvivesTransportFailureAcrossTransports)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    sioxx::client_options options;
    options.force_http_polling = polling;
    options.reconnect_attempts = 10;
    options.reconnect_delay = 100ms;
    options.reconnect_delay_max = 100ms;
    options.reconnect_randomization_factor = 0.0;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    auto sibling = client.socket("/e2e-a");
    auto sibling_connected = std::make_shared<completion_signal>();
    auto sibling_acknowledged = std::make_shared<completion_signal>();
    sibling->on_connect([sibling_connected] { sibling_connected->set(); });
    auto connections = std::make_shared<event_counter>();
    auto buffered_acks =
      std::make_shared<async_value<std::shared_ptr<event_counter>>>();
    socket->on_connect([connections] { connections->increment(); });
    const std::weak_ptr<sioxx::socket> weak = socket;
    socket->on_disconnect(
      [weak, buffered_acks, sibling, sibling_acknowledged](const std::string&)
      {
        if (auto instance = weak.lock())
          buffered_acks->set(buffer_event_sequence(instance, ""));
        EXPECT_FALSE(sibling->connected());
        sibling->emit("namespace_with_ack", sioxx::json::array(),
                      [sibling_acknowledged](sioxx::message reply)
                      {
                        EXPECT_EQ(reply, sioxx::json::array({"/e2e-a"}));
                        sibling_acknowledged->set();
                      });
      });
    client.connect(server_url());
    ASSERT_TRUE(connections->wait_for(1, 5s));
    ASSERT_TRUE(sibling_connected->wait_for(5s));
    // This event has left the buffer and must not be replayed on reconnection.
    socket->emit("buffer_probe", sioxx::json::array({"already-sent"}));
    socket->emit("drop_transport", sioxx::json::array(), [](sioxx::message) {});
    ASSERT_TRUE(connections->wait_for(2, 5s));
    expect_buffer_log(socket, sioxx::json::array({"plain", "ack", "timed"}));
    ASSERT_TRUE(buffered_acks->wait_for(5s));
    EXPECT_TRUE(buffered_acks->value()->wait_for(2, 5s));
    EXPECT_TRUE(sibling_acknowledged->wait_for(5s));
    socket->on_disconnect(nullptr);
    client.sync_close();
  }
}

TEST(E2E, BufferSurvivesReconnectExhaustionUntilManualConnect)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    sioxx::client_options options;
    options.force_http_polling = polling;
    options.reconnect_attempts = 1;
    options.reconnect_delay = 1ms;
    options.reconnect_delay_max = 1ms;
    options.reconnect_randomization_factor = 0.0;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    auto failed = std::make_shared<completion_signal>();
    auto connected = std::make_shared<completion_signal>();
    auto last_error = std::make_shared<async_value<std::string>>();
    auto attempts = std::make_shared<std::atomic<unsigned>>(0);
    client.set_error_listener([last_error](const std::string& error)
                              { last_error->set(error); });
    client.set_fail_listener([failed] { failed->set(); });
    client.set_reconnecting_listener([attempts] { ++*attempts; });
    socket->on_connect([connected] { connected->set(); });
    auto acknowledgements = buffer_event_sequence(socket, "");
    client.connect(server_url("SIOXX_E2E_REJECT_HANDSHAKE_URL"));
    ASSERT_TRUE(failed->wait_for(5s)) << last_error->value();
    EXPECT_EQ(attempts->load(), 1U);
    auto expired = std::make_shared<completion_signal>();
    socket->emit("buffer_probe", sioxx::json::array({"expired"}), 1ms,
                 [expired](sioxx::socket::ack_status status, sioxx::message)
                 {
                   EXPECT_EQ(status, sioxx::socket::ack_status::timeout);
                   expired->set();
                 });
    socket->emit("buffer_probe", sioxx::json::array({"after-exhaustion"}));
    ASSERT_TRUE(expired->wait_for(5s));
    client.connect(server_url());
    ASSERT_TRUE(connected->wait_for(5s));
    expect_buffer_log(socket, sioxx::json::array(
                                {"plain", "ack", "timed", "after-exhaustion"}));
    EXPECT_TRUE(acknowledgements->wait_for(2, 5s));
    client.sync_close();
  }
}

TEST(E2E, ShutdownDiscardsEveryBufferedEmissionAcrossTransports)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");
    buffer_event_sequence(socket, "old-");
    client.sync_close();
    auto connected = std::make_shared<completion_signal>();
    socket->on_connect([connected] { connected->set(); });
    client.connect(server_url());
    auto acknowledgements = buffer_event_sequence(socket, "new-");
    ASSERT_TRUE(connected->wait_for(5s));
    expect_buffer_log(
      socket, sioxx::json::array({"new-plain", "new-ack", "new-timed"}));
    EXPECT_TRUE(acknowledgements->wait_for(2, 5s));
    client.sync_close();
  }
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

TEST(E2E, ConnectsUsingCallerOwnedIoContext)
{
  boost::asio::io_context io_context;
  sioxx::client_options options;
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};
  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(io_context, options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));
  std::thread io_thread([&] { io_context.run(); });

  const bool did_connect = connected->wait_for(5s);
  bool did_acknowledge = false;
  if (did_connect)
  {
    socket->emit("transport_with_ack", sioxx::json::array(),
                 [acknowledgement](sioxx::message data)
                 { acknowledgement->set(std::move(data)); });
    did_acknowledge = acknowledgement->wait_for(5s);
  }

  client.close();
  io_thread.join();

  ASSERT_TRUE(did_connect) << "connection error: " << error->value();
  ASSERT_TRUE(did_acknowledge);
  EXPECT_EQ(acknowledgement->value()[0], "websocket");
}

TEST(E2E, HttpPollingUsesCallerOwnedIoContext)
{
  boost::asio::io_context io_context;
  sioxx::client_options options;
  options.force_http_polling = true;
  options.extra_headers = {{"X-Sioxx-Test", "external-polling-header"}};
  options.proxy = sioxx::proxy_options{server_url("SIOXX_E2E_PROXY_URL"),
                                       "sioxx", "proxy-password"};
  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(io_context, options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_URL"));
  std::thread io_thread([&] { io_context.run(); });

  const bool did_connect = connected->wait_for(5s);
  bool did_acknowledge = false;
  if (did_connect)
  {
    socket->emit("transport_with_ack", sioxx::json::array(),
                 [acknowledgement](sioxx::message data)
                 { acknowledgement->set(std::move(data)); });
    did_acknowledge = acknowledgement->wait_for(5s);
  }

  client.close();
  io_thread.join();

  ASSERT_TRUE(did_connect) << "connection error: " << error->value();
  ASSERT_TRUE(did_acknowledge);
  EXPECT_EQ(acknowledgement->value()[0], "polling");
}

TEST(E2E, SyncCloseLeavesStoppedCallerOwnedContextForCallerToDrain)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    boost::asio::io_context io_context;
    io_context.stop();
    bool application_work_ran = false;
    boost::asio::post(io_context, [&] { application_work_ran = true; });
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(io_context, options);
    client.connect(server_url());

    client.sync_close();
    EXPECT_TRUE(io_context.stopped());
    EXPECT_FALSE(application_work_ran);

    io_context.restart();
    io_context.run();
    EXPECT_TRUE(application_work_ran);
    EXPECT_FALSE(client.opened());
  }
}

TEST(E2E, SyncCloseReturnsWhileCallerOwnedEventCallbackIsActive)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    boost::asio::io_context io_context;
    completion_signal entered;
    completion_signal release;
    std::atomic<bool> callback_returned{false};
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(io_context, options);
    auto socket = client.socket("/e2e");
    socket->on("server_arguments",
               [&](const std::string&, sioxx::message)
               {
                 entered.set();
                 EXPECT_TRUE(release.wait_for(5s));
                 callback_returned = true;
               });
    client.connect(server_url());
    std::thread io_thread([&] { io_context.run(); });

    const bool did_enter = entered.wait_for(5s);
    client.sync_close();
    EXPECT_FALSE(callback_returned.load());
    EXPECT_FALSE(client.opened());
    bool application_work_ran = false;
    boost::asio::post(io_context, [&] { application_work_ran = true; });
    release.set();
    io_thread.join();

    ASSERT_TRUE(did_enter);
    EXPECT_TRUE(callback_returned.load());
    EXPECT_TRUE(application_work_ran);
  }
}

TEST(E2E, SyncCloseWaitsForTimedAckOnCallerOwnedContext)
{
  for (bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "websocket");
    boost::asio::io_context io_context;
    completion_signal connected;
    completion_signal entered;
    completion_signal release;
    std::atomic<bool> callback_returned{false};
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(io_context, options);
    auto socket = client.socket("/e2e");
    socket->on_connect([&connected] { connected.set(); });
    client.connect(server_url());
    std::thread io_thread([&] { io_context.run(); });

    if (connected.wait_for(5s))
      socket->emit("echo_with_ack", sioxx::json::array(), 1h,
                   [&](sioxx::socket::ack_status status, sioxx::message)
                   {
                     EXPECT_EQ(status, sioxx::socket::ack_status::acknowledged);
                     entered.set();
                     EXPECT_TRUE(release.wait_for(5s));
                     callback_returned = true;
                   });
    const bool did_enter = entered.wait_for(5s);
    completion_signal shutdown_started;
    auto shutdown = std::async(std::launch::async,
                               [&]
                               {
                                 shutdown_started.set();
                                 client.sync_close();
                                 return callback_returned.load();
                               });
    EXPECT_TRUE(shutdown_started.wait_for(5s));
    EXPECT_EQ(shutdown.wait_for(50ms), std::future_status::timeout);
    release.set();
    const bool callback_finished_at_shutdown = shutdown.get();
    io_thread.join();

    ASSERT_TRUE(did_enter);
    EXPECT_TRUE(callback_finished_at_shutdown);
  }
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

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_HOSTNAME_URL"));

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
  options.force_http_polling = true;

  auto connected = std::make_shared<completion_signal>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  socket->on_connect([connected] { connected->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_HOSTNAME_URL"));

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

namespace
{

void expect_tls_identity_rejected(const std::string& url)
{
  for (const bool force_polling : {false, true})
  {
    for (const bool caller_owned_context : {false, true})
    {
      for (const bool use_proxy : {false, true})
      {
        SCOPED_TRACE(::testing::Message()
                     << "polling only: " << force_polling
                     << ", caller-owned I/O: " << caller_owned_context
                     << ", proxy: " << use_proxy);
        sioxx::client_options options;
        options.force_http_polling = force_polling;
        if (use_proxy)
          options.proxy = sioxx::proxy_options{
            server_url("SIOXX_E2E_PROXY_URL"), "sioxx", "proxy-password"};

        boost::asio::io_context io_context;
        auto client = caller_owned_context
                        ? std::make_unique<sioxx::client>(io_context, options)
                        : std::make_unique<sioxx::client>(options);
        auto websocket_error = std::make_shared<async_value<std::string>>();
        auto result = std::make_shared<async_value<std::string>>();
        client->set_error_listener(
          [websocket_error, result](const std::string& message)
          {
            if (message.find("ssl_handshake: ") == 0)
              websocket_error->set(message);
            if (message.find("polling transport: ") == 0) result->set(message);
          });
        client->set_open_listener([result]
                                  { result->set("connection opened"); });
        client->connect(url);
        std::thread io_thread;
        if (caller_owned_context)
          io_thread = std::thread([&io_context] { io_context.run(); });

        constexpr auto certificate_error = "certificate verify failed";
        const bool finished = result->wait_for(5s);
        const auto outcome = result->value();
        const bool rejected =
          outcome.find(certificate_error) != std::string::npos;
        const bool websocket_finished =
          force_polling || (rejected && websocket_error->wait_for(5s));
        client->close();
        if (io_thread.joinable()) io_thread.join();

        ASSERT_TRUE(finished)
          << "WebSocket error: " << websocket_error->value();
        ASSERT_TRUE(rejected) << outcome;
        ASSERT_TRUE(websocket_finished);
        if (!force_polling)
          EXPECT_NE(websocket_error->value().find(certificate_error),
                    std::string::npos)
            << websocket_error->value();
      }
    }
  }
}

}  // namespace

TEST(E2E, RejectsTlsCertificateForWrongHostname)
{
  expect_tls_identity_rejected(server_url("SIOXX_E2E_TLS_WRONG_HOSTNAME_URL"));
}

TEST(E2E, RejectsTlsCertificateForWrongIpAddress)
{
  expect_tls_identity_rejected(server_url("SIOXX_E2E_TLS_WRONG_IP_URL"));
}

TEST(E2E, RejectsUntrustedTlsCertificate)
{
  expect_tls_identity_rejected(server_url("SIOXX_E2E_TLS_UNTRUSTED_URL"));
}

TEST(E2E, ConnectsWithUntrustedCertificateWhenVerificationIsDisabled)
{
  sioxx::client_options options;
  options.verify_tls = false;

  auto opened = std::make_shared<completion_signal>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  configure_failure_reporting(client, error);
  client.set_open_listener([opened] { opened->set(); });
  client.connect(server_url("SIOXX_E2E_TLS_UNTRUSTED_URL"));

  EXPECT_TRUE(opened->wait_for(5s)) << "connection error: " << error->value();
  client.sync_close();
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
  auto opened_at_close = std::make_shared<async_value<bool>>();
  auto initial_transport = std::make_shared<async_value<sioxx::message>>();
  auto reconnected_transport = std::make_shared<async_value<sioxx::message>>();
  auto acknowledgement = std::make_shared<async_value<sioxx::message>>();
  auto reconnect_scheduled =
    std::make_shared<async_value<std::pair<unsigned, unsigned>>>();
  auto reconnecting = std::make_shared<completion_signal>();
  auto error = std::make_shared<async_value<std::string>>();
  sioxx::client client(options);
  auto socket = client.socket("/e2e");

  configure_failure_reporting(client, error);
  client.set_close_listener([&client, opened_at_close](const std::string&)
                            { opened_at_close->set(client.opened()); });
  client.set_reconnect_listener(
    [reconnect_scheduled](unsigned attempt, unsigned delay)
    { reconnect_scheduled->set({attempt, delay}); });
  client.set_reconnecting_listener([reconnecting] { reconnecting->set(); });
  socket->on_connect([connections] { connections->increment(); });
  client.connect(server_url());

  ASSERT_TRUE(connections->wait_for(1, 5s))
    << "connection error: " << error->value();
  EXPECT_TRUE(client.opened());
  const auto initial_session_id = client.get_sessionid();
  EXPECT_FALSE(initial_session_id.empty());

  socket->emit("transport_with_ack", sioxx::json::array(),
               [initial_transport](sioxx::message data)
               { initial_transport->set(std::move(data)); });
  ASSERT_TRUE(initial_transport->wait_for(5s));
  ASSERT_EQ(initial_transport->value().size(), 1);
  ASSERT_EQ(initial_transport->value()[0], "websocket");

  socket->emit("drop_websocket");

  ASSERT_TRUE(opened_at_close->wait_for(5s))
    << "client did not observe the WebSocket transport close";
  EXPECT_FALSE(opened_at_close->value());
  ASSERT_TRUE(reconnect_scheduled->wait_for(5s));
  EXPECT_EQ(reconnect_scheduled->value(),
            std::make_pair(0U, static_cast<unsigned>((100ms).count())));
  ASSERT_TRUE(reconnecting->wait_for(5s));
  ASSERT_TRUE(connections->wait_for(2, 5s))
    << "client did not reconnect after the WebSocket transport closed";
  EXPECT_TRUE(client.opened());
  EXPECT_FALSE(client.get_sessionid().empty());
  EXPECT_NE(client.get_sessionid(), initial_session_id);

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

namespace
{

void expect_close_from_disconnect_callback(bool polling)
{
  sioxx::client_options options;
  options.force_http_polling = polling;
  options.reconnect_attempts = 1;
  auto connected = std::make_shared<completion_signal>();
  auto close_returned = std::make_shared<completion_signal>();
  auto reconnects = std::make_shared<std::atomic<int>>(0);
  sioxx::client client(options);
  auto socket = client.socket("/e2e");
  socket->on_connect([connected] { connected->set(); });
  socket->on_disconnect(
    [&client, close_returned](const std::string&)
    {
      client.close();
      close_returned->set();
    });
  client.set_reconnect_listener([reconnects](unsigned, unsigned)
                                { ++*reconnects; });
  client.connect(server_url());
  ASSERT_TRUE(connected->wait_for(5s));
  socket->emit("drop_transport", sioxx::json::array(), [](sioxx::message) {});
  EXPECT_TRUE(close_returned->wait_for(5s));
  client.sync_close();
  EXPECT_FALSE(client.opened());
  EXPECT_EQ(reconnects->load(), 0);
}

void expect_close_from_reconnect_callback(bool starting)
{
  sioxx::client_options options;
  options.reconnect_attempts = 1;
  options.reconnect_delay = starting ? 1ms : 1h;
  options.reconnect_delay_max = options.reconnect_delay;
  options.reconnect_randomization_factor = 0.0;
  auto connected = std::make_shared<completion_signal>();
  auto close_returned = std::make_shared<completion_signal>();
  auto attempts = std::make_shared<std::atomic<int>>(0);
  sioxx::client client(options);
  auto socket = client.socket("/e2e");
  socket->on_connect([connected] { connected->set(); });
  auto close = [&client, close_returned]
  {
    client.close();
    close_returned->set();
  };
  client.set_reconnect_listener(
    [starting, close](unsigned, unsigned)
    {
      if (!starting) close();
    });
  client.set_reconnecting_listener(
    [starting, attempts, close]
    {
      ++*attempts;
      if (starting) close();
    });
  client.connect(server_url());
  ASSERT_TRUE(connected->wait_for(5s));
  socket->emit("drop_transport", sioxx::json::array(), [](sioxx::message) {});
  EXPECT_TRUE(close_returned->wait_for(5s));
  client.sync_close();
  EXPECT_FALSE(client.opened());
  EXPECT_EQ(attempts->load(), starting ? 1 : 0);
}

}  // namespace

TEST(E2E, CloseFromWebsocketDisconnectCallbackDoesNotDeadlock)
{
  expect_close_from_disconnect_callback(false);
}

TEST(E2E, CloseFromPollingDisconnectCallbackDoesNotDeadlock)
{
  expect_close_from_disconnect_callback(true);
}

TEST(E2E, CloseFromReconnectScheduledCallbackCancelsAttempt)
{
  expect_close_from_reconnect_callback(false);
}

TEST(E2E, CloseFromReconnectingCallbackCancelsAttempt)
{
  expect_close_from_reconnect_callback(true);
}

TEST(E2E, SyncCloseWaitsForRunningReconnectCallback)
{
  sioxx::client_options options;
  options.reconnect_attempts = 1;
  options.reconnect_delay = 1ms;
  options.reconnect_delay_max = 1ms;
  options.reconnect_randomization_factor = 0.0;
  auto connected = std::make_shared<completion_signal>();
  auto entered = std::make_shared<completion_signal>();
  auto release = std::make_shared<completion_signal>();
  auto returned = std::make_shared<std::atomic<bool>>(false);
  sioxx::client client(options);
  auto socket = client.socket("/e2e");
  socket->on_connect([connected] { connected->set(); });
  client.set_reconnecting_listener(
    [&client, entered, release, returned]
    {
      entered->set();
      EXPECT_TRUE(release->wait_for(5s));
      client.close();
      *returned = true;
    });
  client.connect(server_url());
  ASSERT_TRUE(connected->wait_for(5s));
  socket->emit("drop_transport", sioxx::json::array(), [](sioxx::message) {});
  ASSERT_TRUE(entered->wait_for(5s));
  std::promise<void> shutdown_started;
  auto started = shutdown_started.get_future();
  auto shutdown = std::async(std::launch::async,
                             [&client, &shutdown_started]
                             {
                               shutdown_started.set_value();
                               client.sync_close();
                             });
  EXPECT_EQ(started.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(shutdown.wait_for(50ms), std::future_status::timeout);
  release->set();
  EXPECT_EQ(shutdown.wait_for(5s), std::future_status::ready);
  shutdown.get();
  EXPECT_TRUE(returned->load());
  EXPECT_FALSE(client.opened());
}

TEST(E2E, SyncCloseCancelsLongReconnectDelay)
{
  sioxx::client_options options;
  options.reconnect_attempts = 1;
  options.reconnect_delay = 1h;
  options.reconnect_delay_max = 1h;
  options.reconnect_randomization_factor = 0.0;
  auto connected = std::make_shared<completion_signal>();
  auto scheduled = std::make_shared<completion_signal>();
  auto attempts = std::make_shared<std::atomic<int>>(0);
  sioxx::client client(options);
  auto socket = client.socket("/e2e");
  socket->on_connect([connected] { connected->set(); });
  client.set_reconnect_listener([scheduled](unsigned, unsigned)
                                { scheduled->set(); });
  client.set_reconnecting_listener([attempts] { ++*attempts; });
  client.connect(server_url());
  ASSERT_TRUE(connected->wait_for(5s));
  socket->emit("drop_transport", sioxx::json::array(), [](sioxx::message) {});
  ASSERT_TRUE(scheduled->wait_for(5s));
  const auto start = std::chrono::steady_clock::now();
  client.sync_close();
  EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
  EXPECT_EQ(attempts->load(), 0);
  EXPECT_FALSE(client.opened());
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

TEST(E2E, DiscardsOverLimitJsonBinaryEventAndReceivesNextEvent)
{
  for (const bool polling : {false, true})
  {
    SCOPED_TRACE(polling ? "polling" : "WebSocket");
    auto connected = std::make_shared<completion_signal>();
    auto oversized_received = std::make_shared<std::atomic<bool>>(false);
    auto received = std::make_shared<async_value<sioxx::message>>();
    auto error = std::make_shared<async_value<std::string>>();
    sioxx::client_options options;
    options.force_http_polling = polling;
    sioxx::client client(options);
    auto socket = client.socket("/e2e");

    configure_failure_reporting(client, error);
    socket->on_connect([connected] { connected->set(); });
    socket->on("json_binary_over_limit",
               [oversized_received](const std::string&, sioxx::message)
               { oversized_received->store(true); });
    socket->on("json_binary_within_limit",
               [received](const std::string&, sioxx::message data)
               { received->set(std::move(data)); });
    client.connect(polling ? server_url("SIOXX_E2E_POLLING_ONLY_URL")
                           : server_url());

    ASSERT_TRUE(connected->wait_for(5s)) << error->value();
    socket->emit("request_json_binary_attachment_batches");
    ASSERT_TRUE(received->wait_for(5s)) << error->value();
    EXPECT_FALSE(oversized_received->load());
    const auto reply = received->value();
    ASSERT_EQ(reply.size(), 2);
    EXPECT_EQ(reply[0], polling ? "polling" : "websocket");
    EXPECT_EQ(reply[1], sioxx::json::binary({0x42}));
    client.sync_close();
  }
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
