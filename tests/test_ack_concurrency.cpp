#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <sioxx/client.hpp>
#include <sioxx/socket.hpp>

using namespace sioxx;
using namespace std::chrono_literals;

namespace
{

// Keep a callback active while another thread delivers an ACK or closes the
// client. Every wait is bounded so a failed expectation can still unwind.
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

}  // namespace

TEST(AckConcurrency, ConcurrentTimedAcksInvokeCallbackOnce)
{
  client instance;
  auto socket = instance.socket();
  auto gate = std::make_shared<callback_gate>();
  auto calls = std::make_shared<std::atomic<int>>(0);
  socket->emit("request", json(), 1h,
               [gate, calls](socket::ack_status status, message data)
               {
                 EXPECT_EQ(status, socket::ack_status::acknowledged);
                 EXPECT_EQ(data, json::array({42}));
                 if (++*calls == 1) gate->enter();
               });

  std::promise<void> start;
  auto ready = start.get_future().share();
  std::atomic<int> delivered{0};
  std::promise<void> first_delivery;
  auto delivery_completed = first_delivery.get_future();
  auto deliver = [&]
  {
    EXPECT_EQ(ready.wait_for(5s), std::future_status::ready);
    socket->dispatch_ack(0, json::array({42}));
    if (++delivered == 1) first_delivery.set_value();
  };
  auto first = std::async(std::launch::async, deliver);
  auto second = std::async(std::launch::async, deliver);
  start.set_value();
  EXPECT_TRUE(gate->wait_until_entered());
  // The duplicate ACK must return while the winning callback is still active.
  EXPECT_EQ(delivery_completed.wait_for(5s), std::future_status::ready);
  gate->release();
  first.get();
  second.get();
  instance.sync_close();
  socket->dispatch_ack(0, json::array({43}));
  EXPECT_EQ(calls->load(), 1);
}

TEST(AckConcurrency, LateAcksDoNotReenterRunningTimeoutCallback)
{
  client instance;
  auto socket = instance.socket();
  auto gate = std::make_shared<callback_gate>();
  auto calls = std::make_shared<std::atomic<int>>(0);
  socket->emit("request", json(), 1ms,
               [gate, calls](socket::ack_status status, message data)
               {
                 EXPECT_EQ(status, socket::ack_status::timeout);
                 EXPECT_TRUE(data.is_null());
                 if (++*calls == 1) gate->enter();
               });
  EXPECT_TRUE(gate->wait_until_entered());
  auto first = std::async(std::launch::async,
                          [socket] { socket->dispatch_ack(0, json()); });
  auto second = std::async(std::launch::async,
                           [socket] { socket->dispatch_ack(0, json()); });
  EXPECT_EQ(first.wait_for(5s), std::future_status::ready);
  EXPECT_EQ(second.wait_for(5s), std::future_status::ready);
  gate->release();
  first.get();
  second.get();
  instance.sync_close();
  EXPECT_EQ(calls->load(), 1);
}

TEST(AckConcurrency, CloseCancelsQueuedTimeoutsWhileCallbackRuns)
{
  client instance;
  auto socket = instance.socket();
  auto gate = std::make_shared<callback_gate>();
  socket->emit("blocker", json(), 1ms,
               [gate](socket::ack_status status, message)
               {
                 EXPECT_EQ(status, socket::ack_status::timeout);
                 gate->enter();
               });
  EXPECT_TRUE(gate->wait_until_entered());
  auto calls = std::make_shared<std::atomic<int>>(0);
  auto capture = std::make_shared<int>(0);
  std::weak_ptr<int> retained = capture;
  socket->emit("queued", json(), 1ms,
               [calls, capture](socket::ack_status, message) { ++*calls; });
  capture.reset();
  instance.close();
  EXPECT_TRUE(retained.expired());
  socket->dispatch_ack(1, json());
  gate->release();
  instance.sync_close();
  socket->dispatch_ack(1, json());
  EXPECT_EQ(calls->load(), 0);
}
