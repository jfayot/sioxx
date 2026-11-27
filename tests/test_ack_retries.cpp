#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <limits>
#include <mutex>
#include <sioxx/client.hpp>
#include <sioxx/socket.hpp>
#include <stdexcept>
#include <thread>
#include <vector>

#include "socket_test_access.hpp"

using namespace sioxx;
using sioxx::detail::socket_test_access;
using namespace std::chrono_literals;

namespace
{

struct recorded_events
{
  void add(const packet& value)
  {
    if (value.type != packet_type::event) return;
    if (before_recording) before_recording(value);
    std::lock_guard<std::mutex> lock(mutex);
    events.push_back(value);
    condition.notify_all();
  }

  bool wait_for(std::size_t count)
  {
    std::unique_lock<std::mutex> lock(mutex);
    return condition.wait_for(lock, 5s, [&] { return events.size() >= count; });
  }

  std::vector<packet> snapshot()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return events;
  }

  std::mutex mutex;
  std::condition_variable condition;
  std::vector<packet> events;
  std::function<void(const packet&)> before_recording;
};

class recording_parser : public parser_base
{
 public:
  explicit recording_parser(std::shared_ptr<recorded_events> events)
      : events_(std::move(events))
  {
  }

  void encode(const packet& value, const frame_writer&) const override
  {
    events_->add(value);
  }

  bool decode(const std::string&, bool, packet&) override { return false; }
  std::string name() const override { return "retry-recording"; }

 private:
  std::shared_ptr<recorded_events> events_;
};

class callback_gate
{
 public:
  void enter()
  {
    entered_.set_value();
    EXPECT_EQ(released_.wait_for(5s), std::future_status::ready);
  }
  bool wait_until_entered()
  {
    return started_.wait_for(5s) == std::future_status::ready;
  }
  void release() { release_.set_value(); }

 private:
  std::promise<void> entered_;
  std::future<void> started_{entered_.get_future()};
  std::promise<void> release_;
  std::shared_future<void> released_{release_.get_future().share()};
};

struct AckRetries : ::testing::Test
{
  void SetUp() override
  {
    client_options options;
    options.parser_factory = [events = events]
    { return std::make_unique<recording_parser>(events); };
    instance = std::make_unique<client>(context, options);
    sock = instance->socket();
    // Leave the caller-owned context stopped: the parser records attempts
    // without opening a network connection.
    instance->connect("ws://127.0.0.1:1");
    socket_test_access::mark_connected(*sock, true);
  }

  void TearDown() override { instance->sync_close(); }

  boost::asio::io_context context;
  std::shared_ptr<recorded_events> events = std::make_shared<recorded_events>();
  std::unique_ptr<client> instance;
  std::shared_ptr<sioxx::socket> sock;
};

}  // namespace

TEST_F(AckRetries, SecondAttemptSucceedsAndOldAcknowledgementsAreIgnored)
{
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json::array({42}), 100ms, 2,
             [calls](socket::ack_status status, message data)
             {
               ++*calls;
               EXPECT_EQ(status, socket::ack_status::acknowledged);
               EXPECT_EQ(data, json::array({"reply"}));
             });
  ASSERT_TRUE(events->wait_for(2));
  auto attempts = events->snapshot();
  ASSERT_EQ(attempts.size(), 2u);
  EXPECT_NE(attempts[0].id, attempts[1].id);
  EXPECT_EQ(attempts[0].data, attempts[1].data);
  socket_test_access::dispatch_ack(*sock, attempts[0].id,
                                   json::array({"late"}));
  EXPECT_EQ(calls->load(), 0);
  socket_test_access::dispatch_ack(*sock, attempts[1].id,
                                   json::array({"reply"}));
  socket_test_access::dispatch_ack(*sock, attempts[1].id,
                                   json::array({"duplicate"}));
  instance->sync_close();
  EXPECT_EQ(calls->load(), 1);
  EXPECT_EQ(events->snapshot().size(), 2u);
}

TEST_F(AckRetries, ExhaustionReportsTimeoutOnceAfterExactAttemptBudget)
{
  auto completed = std::make_shared<std::promise<void>>();
  auto result = completed->get_future();
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json::array({42}), 100ms, 2,
             [completed, calls](socket::ack_status status, message data)
             {
               EXPECT_EQ(status, socket::ack_status::timeout);
               EXPECT_TRUE(data.is_null());
               if (++*calls == 1) completed->set_value();
             });
  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  instance->sync_close();
  auto attempts = events->snapshot();
  ASSERT_EQ(attempts.size(), 3u);
  for (std::size_t i = 0; i < attempts.size(); ++i)
  {
    EXPECT_EQ(attempts[i].data, json::array({"request", 42}));
    if (i > 0) EXPECT_GT(attempts[i].id, attempts[i - 1].id);
    socket_test_access::dispatch_ack(*sock, attempts[i].id, json());
  }
  EXPECT_EQ(calls->load(), 1);
}

TEST_F(AckRetries, ZeroRetriesMakesOnlyOneAttempt)
{
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  sock->emit("request", json(), 10ms, 0,
             [completed](socket::ack_status status, message)
             { completed->set_value(status); });
  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(result.get(), socket::ack_status::timeout);
  instance->sync_close();
  EXPECT_EQ(events->snapshot().size(), 1u);
}

TEST_F(AckRetries, DisconnectedAttemptsExpireWithoutLaterDelivery)
{
  socket_test_access::mark_connected(*sock, false);
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  sock->emit("request", json(), 10ms, 2,
             [completed](socket::ack_status status, message)
             { completed->set_value(status); });
  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(result.get(), socket::ack_status::timeout);
  socket_test_access::mark_connected(*sock, true);
  instance->sync_close();
  EXPECT_TRUE(events->snapshot().empty());
}

TEST_F(AckRetries, ShutdownCancelsAttemptsAndReleasesCaptures)
{
  auto capture = std::make_shared<int>(42);
  std::weak_ptr<int> retained = capture;
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json(), 1h, 2,
             [capture, calls](socket::ack_status, message) { ++*calls; });
  capture.reset();
  ASSERT_TRUE(events->wait_for(1));
  instance->sync_close();
  EXPECT_TRUE(retained.expired());
  socket_test_access::dispatch_ack(*sock, events->snapshot()[0].id, json());
  EXPECT_EQ(calls->load(), 0);
  EXPECT_EQ(events->snapshot().size(), 1u);
  EXPECT_THROW(
    sock->emit("closed", json(), 1s, 2, [](socket::ack_status, message) {}),
    std::logic_error);
}

TEST_F(AckRetries, RejectsInvalidTimeoutAndCallback)
{
  auto callback = [](socket::ack_status, message) {};
  EXPECT_THROW(sock->emit("request", json(), 0ms, 2, callback),
               std::invalid_argument);
  EXPECT_THROW(sock->emit("request", json(), -1ms, 2, callback),
               std::invalid_argument);
  EXPECT_THROW(sock->emit("request", json(), 1s, 2, {}), std::invalid_argument);
  EXPECT_TRUE(events->snapshot().empty());
}

TEST_F(AckRetries, LargeTimeoutAndRetryCountDoNotOverflow)
{
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json(), std::chrono::milliseconds::max(),
             std::numeric_limits<unsigned>::max(),
             [calls](socket::ack_status status, message)
             {
               EXPECT_EQ(status, socket::ack_status::acknowledged);
               ++*calls;
             });
  ASSERT_TRUE(events->wait_for(1));
  socket_test_access::dispatch_ack(*sock, events->snapshot()[0].id, json());
  instance->sync_close();
  EXPECT_EQ(calls->load(), 1);
  EXPECT_EQ(events->snapshot().size(), 1u);
}

TEST_F(AckRetries, SocketDestructionReleasesRetryCallback)
{
  auto detached = socket_test_access::make_socket();
  auto capture = std::make_shared<int>(42);
  std::weak_ptr<int> retained = capture;
  detached->emit("request", json(), 1h, 2,
                 [capture](socket::ack_status, message)
                 { ADD_FAILURE() << "Destroyed socket retried"; });
  capture.reset();
  detached.reset();
  EXPECT_TRUE(retained.expired());
}

TEST_F(AckRetries, ConcurrentAcknowledgementsInvokeCallbackOnce)
{
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json(), 100ms, 2,
             [calls](socket::ack_status status, message)
             {
               EXPECT_EQ(status, socket::ack_status::acknowledged);
               ++*calls;
             });
  ASSERT_TRUE(events->wait_for(2));
  const auto id = events->snapshot()[1].id;
  std::promise<void> start;
  auto ready = start.get_future().share();
  auto deliver = [&]
  {
    EXPECT_EQ(ready.wait_for(5s), std::future_status::ready);
    socket_test_access::dispatch_ack(*sock, id, json());
  };
  auto first = std::async(std::launch::async, deliver);
  auto second = std::async(std::launch::async, deliver);
  start.set_value();
  first.get();
  second.get();
  instance->sync_close();
  EXPECT_EQ(calls->load(), 1);
  EXPECT_EQ(events->snapshot().size(), 2u);
}

TEST_F(AckRetries, ExpiredAckQueuesRetryBehindNewerBufferedEvents)
{
  auto gate = std::make_shared<callback_gate>();
  socket_test_access::mark_connected(*sock, false);
  sock->emit("blocker", json(), 1ms,
             [gate](socket::ack_status, message) { gate->enter(); });
  ASSERT_TRUE(gate->wait_until_entered());
  socket_test_access::mark_connected(*sock, true);
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json::array({42}), 50ms, 1,
             [calls](socket::ack_status status, message)
             {
               EXPECT_EQ(status, socket::ack_status::acknowledged);
               ++*calls;
             });
  const auto initial = events->snapshot();
  const auto request =
    std::find_if(initial.begin(), initial.end(), [](const packet& attempt)
                 { return attempt.data.front() == "request"; });
  ASSERT_NE(request, initial.end());
  const auto old_id = request->id;
  socket_test_access::mark_connected(*sock, false);
  sock->emit("newer");
  std::this_thread::sleep_for(80ms);
  // Expiry is claimed on this ACK delivery thread while the worker is busy.
  socket_test_access::dispatch_ack(*sock, old_id, json());
  socket_test_access::dispatch_ack(*sock, old_id, json());
  EXPECT_EQ(calls->load(), 0);
  socket_test_access::mark_connected(*sock, true);
  auto attempts = events->snapshot();
  gate->release();
  ASSERT_EQ(attempts.size(), 3u);
  EXPECT_EQ(attempts[1].data[0], "newer");
  EXPECT_EQ(attempts[2].data, json::array({"request", 42}));
  EXPECT_NE(attempts[2].id, old_id);
  socket_test_access::dispatch_ack(*sock, attempts[2].id, json());
  instance->sync_close();
  EXPECT_EQ(calls->load(), 1);
}

TEST_F(AckRetries, ShutdownRacingExpiredAckCancelsRetry)
{
  auto gate = std::make_shared<callback_gate>();
  socket_test_access::mark_connected(*sock, false);
  sock->emit("blocker", json(), 1ms,
             [gate](socket::ack_status, message) { gate->enter(); });
  ASSERT_TRUE(gate->wait_until_entered());
  socket_test_access::mark_connected(*sock, true);
  auto calls = std::make_shared<std::atomic<int>>(0);
  sock->emit("request", json(), 50ms, 100,
             [calls](socket::ack_status, message) { ++*calls; });
  const auto attempts = events->snapshot();
  const auto request =
    std::find_if(attempts.begin(), attempts.end(), [](const packet& attempt)
                 { return attempt.data.front() == "request"; });
  ASSERT_NE(request, attempts.end());
  const auto id = request->id;
  std::this_thread::sleep_for(80ms);
  std::promise<void> start;
  auto ready = start.get_future().share();
  auto acknowledgement =
    std::async(std::launch::async,
               [&]
               {
                 EXPECT_EQ(ready.wait_for(5s), std::future_status::ready);
                 socket_test_access::dispatch_ack(*sock, id, json());
               });
  auto shutdown =
    std::async(std::launch::async,
               [&]
               {
                 EXPECT_EQ(ready.wait_for(5s), std::future_status::ready);
                 instance->close();
               });
  start.set_value();
  acknowledgement.get();
  shutdown.get();
  gate->release();
  instance->sync_close();
  EXPECT_EQ(calls->load(), 0);
}

TEST_F(AckRetries, SyncCloseDrainsRunningRetrySuccessCallback)
{
  auto gate = std::make_shared<callback_gate>();
  sock->emit("request", json(), 100ms, 1,
             [gate](socket::ack_status status, message)
             {
               EXPECT_EQ(status, socket::ack_status::acknowledged);
               gate->enter();
             });
  ASSERT_TRUE(events->wait_for(2));
  const auto id = events->snapshot()[1].id;
  auto acknowledgement =
    std::async(std::launch::async,
               [&] { socket_test_access::dispatch_ack(*sock, id, json()); });
  EXPECT_TRUE(gate->wait_until_entered());
  auto shutdown =
    std::async(std::launch::async, [&] { instance->sync_close(); });
  EXPECT_EQ(shutdown.wait_for(50ms), std::future_status::timeout);
  gate->release();
  acknowledgement.get();
  shutdown.get();
}

TEST_F(AckRetries, InitialEncodingErrorDiscardsAcknowledgement)
{
  boost::asio::io_context context;
  client json_client(context);
  auto json_socket = json_client.socket();
  json_client.connect("ws://127.0.0.1:1");
  socket_test_access::mark_connected(*json_socket, true);
  auto capture = std::make_shared<int>(42);
  std::weak_ptr<int> pending = capture;
  const auto invalid_utf8 = json(std::string(1, static_cast<char>(0xff)));

  EXPECT_THROW(
    json_socket->emit("request", invalid_utf8, 1s, 1,
                      [capture](socket::ack_status, message)
                      { ADD_FAILURE() << "Failed emit invoked its callback"; }),
    json::type_error);
  capture.reset();
  EXPECT_TRUE(pending.expired());
}

TEST_F(AckRetries, BackgroundEncodingErrorUsesRemainingRetryBudget)
{
  auto attempts = std::make_shared<std::atomic<int>>(0);
  events->before_recording = [attempts](const packet&)
  {
    if (++*attempts == 2) throw std::runtime_error("second encode failed");
  };
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  sock->emit("request", json(), 100ms, 2,
             [completed](socket::ack_status status, message)
             { completed->set_value(status); });

  ASSERT_EQ(result.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(result.get(), socket::ack_status::timeout);
  EXPECT_EQ(attempts->load(), 3);
  EXPECT_EQ(events->snapshot().size(), 2u);
}

TEST_F(AckRetries, SynchronousEncodingCanOutlastItsAcknowledgementDeadline)
{
  auto gate = std::make_shared<callback_gate>();
  events->before_recording = [gate](const packet&)
  {
    gate->enter();
    throw std::runtime_error("encoding failed after the deadline");
  };
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  auto emitting =
    std::async(std::launch::async,
               [this, completed]
               {
                 sock->emit("request", json(), 300ms,
                            [completed](socket::ack_status status, message)
                            { completed->set_value(status); });
               });
  EXPECT_TRUE(gate->wait_until_entered());
  const auto deadline_status = result.wait_for(2s);
  EXPECT_EQ(deadline_status, std::future_status::ready);
  gate->release();

  EXPECT_NO_THROW(emitting.get());
  if (deadline_status == std::future_status::ready)
    EXPECT_EQ(result.get(), socket::ack_status::timeout);
}

TEST_F(AckRetries, QueuedRetryLeavesDeadlinesRunningAndCannotCrossShutdown)
{
  auto gate = std::make_shared<callback_gate>();
  events->before_recording = [gate](const packet& value)
  {
    if (value.data.at(0) == "blocker") gate->enter();
  };
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  sock->emit("old", json(), 500ms, 1, [](socket::ack_status, message) {});
  sock->emit("independent", json(), 650ms,
             [completed](socket::ack_status status, message)
             { completed->set_value(status); });

  auto blocked =
    std::async(std::launch::async, [this] { sock->emit("blocker"); });
  EXPECT_TRUE(gate->wait_until_entered());
  const auto deadline_status = result.wait_for(2s);
  EXPECT_EQ(deadline_status, std::future_status::ready);
  if (deadline_status == std::future_status::ready)
    EXPECT_EQ(result.get(), socket::ack_status::timeout);

  socket_test_access::mark_connected(*sock, false);
  instance->close();
  instance->connect("ws://127.0.0.1:1");
  socket_test_access::mark_connected(*sock, true);
  gate->release();
  blocked.get();

  sock->emit("fresh", json(), 200ms, 1, [](socket::ack_status, message) {});
  ASSERT_TRUE(events->wait_for(5));
  instance->sync_close();
  const auto packets = events->snapshot();
  ASSERT_EQ(packets.size(), 5u);
  EXPECT_EQ(packets[0].data, json::array({"old"}));
  EXPECT_EQ(packets[1].data, json::array({"independent"}));
  EXPECT_EQ(packets[2].data, json::array({"blocker"}));
  EXPECT_EQ(packets[3].data, json::array({"fresh"}));
  EXPECT_EQ(packets[4].data, json::array({"fresh"}));
}
