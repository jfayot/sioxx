#include <gtest/gtest.h>

#include <sioxx/socketio_socket.hpp>

using namespace sioxx;

namespace
{

// A default-constructed weak_ptr<socketio_client_impl> is enough to build a
// socketio_socket for these tests: emit()/connect()/disconnect() will just
// find client_.lock() == nullptr and skip the network send, while still
// exercising the local listener/ack bookkeeping we care about here.
std::shared_ptr<socketio_socket> make_socket(std::string nsp = "/")
{
  return std::make_shared<socketio_socket>(
    std::weak_ptr<socketio_client_impl>{}, std::move(nsp));
}

}  // namespace

TEST(SocketioSocket, NspAccessorReturnsConstructedNamespace)
{
  auto sock = make_socket("/your_namespace");
  EXPECT_EQ(sock->nsp(), "/your_namespace");
}

TEST(SocketioSocket, StartsDisconnected)
{
  auto sock = make_socket();
  EXPECT_FALSE(sock->connected());
}

TEST(SocketioSocket, MarkConnectedUpdatesState)
{
  auto sock = make_socket();
  sock->mark_connected(true);
  EXPECT_TRUE(sock->connected());
  sock->mark_connected(false);
  EXPECT_FALSE(sock->connected());
}

TEST(SocketioSocket, OnDispatchesMatchingEventWithData)
{
  auto sock = make_socket();
  bool called = false;
  std::string seen_event;
  message seen_data;

  sock->on("greet",
           [&](const std::string& event, message data)
           {
             called = true;
             seen_event = event;
             seen_data = std::move(data);
           });

  sock->dispatch_event("greet", json::array({"hi", 42}));

  EXPECT_TRUE(called);
  EXPECT_EQ(seen_event, "greet");
  ASSERT_TRUE(seen_data.is_array());
  EXPECT_EQ(seen_data[0].get<std::string>(), "hi");
  EXPECT_EQ(seen_data[1].get<int>(), 42);
}

TEST(SocketioSocket, DispatchIgnoresUnregisteredEvent)
{
  auto sock = make_socket();
  int call_count = 0;
  sock->on("known", [&](const std::string&, message) { ++call_count; });

  EXPECT_NO_THROW(sock->dispatch_event("unknown", json::array()));
  EXPECT_EQ(call_count, 0);
}

TEST(SocketioSocket, OnOverwritesPreviousListenerForSameEvent)
{
  auto sock = make_socket();
  int first_calls = 0, second_calls = 0;
  sock->on("x", [&](const std::string&, message) { ++first_calls; });
  sock->on("x", [&](const std::string&, message) { ++second_calls; });

  sock->dispatch_event("x", json::array());

  EXPECT_EQ(first_calls, 0);
  EXPECT_EQ(second_calls, 1);
}

TEST(SocketioSocket, OffRemovesOnlyTheNamedListener)
{
  auto sock = make_socket();
  int a_calls = 0, b_calls = 0;
  sock->on("a", [&](const std::string&, message) { ++a_calls; });
  sock->on("b", [&](const std::string&, message) { ++b_calls; });

  sock->off("a");
  sock->dispatch_event("a", json::array());
  sock->dispatch_event("b", json::array());

  EXPECT_EQ(a_calls, 0);
  EXPECT_EQ(b_calls, 1);
}

TEST(SocketioSocket, OffAllRemovesEveryListener)
{
  auto sock = make_socket();
  int calls = 0;
  sock->on("a", [&](const std::string&, message) { ++calls; });
  sock->on("b", [&](const std::string&, message) { ++calls; });

  sock->off_all();
  sock->dispatch_event("a", json::array());
  sock->dispatch_event("b", json::array());

  EXPECT_EQ(calls, 0);
}

TEST(SocketioSocket, EmitWithAckRegistersCallbackInvokedByDispatchAck)
{
  auto sock = make_socket();
  bool called = false;
  message reply;

  // With no client attached (weak_ptr expired/empty), emit() can't reach a
  // real transport, but it still assigns and stores the ack id locally --
  // exactly the part of the flow this test cares about.
  sock->emit("ping", json::array({1}),
             [&](message data)
             {
               called = true;
               reply = std::move(data);
             });

  sock->dispatch_ack(0, json::array({"pong"}));

  EXPECT_TRUE(called);
  ASSERT_TRUE(reply.is_array());
  EXPECT_EQ(reply[0].get<std::string>(), "pong");
}

TEST(SocketioSocket, AckIdsIncrementPerCall)
{
  auto sock = make_socket();
  std::vector<int> received_ids;

  sock->emit("a", json::array(), [&](message) { received_ids.push_back(0); });
  sock->emit("b", json::array(), [&](message) { received_ids.push_back(1); });

  sock->dispatch_ack(1, json::array());
  sock->dispatch_ack(0, json::array());

  ASSERT_EQ(received_ids.size(), 2u);
  EXPECT_EQ(received_ids[0], 1);  // second emit's ack (id 1) fired first
  EXPECT_EQ(received_ids[1], 0);
}

TEST(SocketioSocket, DispatchAckWithUnknownIdIsIgnored)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->dispatch_ack(999, json::array()));
}

TEST(SocketioSocket, DispatchAckIsConsumedOnce)
{
  auto sock = make_socket();
  int calls = 0;
  sock->emit("once", json::array(), [&](message) { ++calls; });

  sock->dispatch_ack(0, json::array());
  sock->dispatch_ack(0, json::array());  // second delivery for same id: no-op

  EXPECT_EQ(calls, 1);
}

TEST(SocketioSocket, EmitWithoutAckDoesNotThrowWithNoClientAttached)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->emit("fire_and_forget", json::array({"x"})));
}

TEST(SocketioSocket, ConnectAndDisconnectDoNotThrowWithNoClientAttached)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->connect());
  sock->mark_connected(true);
  EXPECT_NO_THROW(sock->disconnect());
  EXPECT_FALSE(sock->connected());
}
