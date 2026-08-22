#include <gtest/gtest.h>

#include <sioxx/client.hpp>
#include <sioxx/socket.hpp>
#include <type_traits>

using namespace sioxx;

namespace
{

static_assert(!std::is_copy_constructible<client>::value,
              "client owns a single connection and must not be copied");
static_assert(!std::is_copy_assignable<client>::value,
              "client owns a single connection and must not be copied");

// A default-constructed weak_ptr<client_impl> is enough to build a
// socket for these tests: emit()/connect()/disconnect() will just
// find client_.lock() == nullptr and skip the network send, while still
// exercising the local listener/ack bookkeeping we care about here.
std::shared_ptr<socket> make_socket(std::string nsp = "/")
{
  return std::make_shared<socket>(std::weak_ptr<client_impl>{}, std::move(nsp));
}

}  // namespace

TEST(Socket, NspAccessorReturnsConstructedNamespace)
{
  auto sock = make_socket("/your_namespace");
  EXPECT_EQ(sock->nsp(), "/your_namespace");
}

TEST(Socket, StoresAndUpdatesNamespaceAuth)
{
  auto sock = std::make_shared<socket>(std::weak_ptr<client_impl>{}, "/private",
                                       json{{"token", "initial-token"}});

  EXPECT_EQ(sock->auth(), json({{"token", "initial-token"}}));

  sock->set_auth(json{{"token", "refreshed-token"}, {"tenant", 42}});

  EXPECT_EQ(sock->auth(), json({{"token", "refreshed-token"}, {"tenant", 42}}));
}

TEST(ClientSocket, ReusesSocketAndUpdatesOnlyNonNullAuth)
{
  client client;
  auto socket = client.socket("/private", json{{"token", "initial-token"}});

  auto updated = client.socket("/private", json{{"token", "refreshed-token"}});
  EXPECT_EQ(updated, socket);
  EXPECT_EQ(socket->auth(), json({{"token", "refreshed-token"}}));

  auto unchanged = client.socket("/private");
  EXPECT_EQ(unchanged, socket);
  EXPECT_EQ(socket->auth(), json({{"token", "refreshed-token"}}));
}

TEST(Socket, StartsDisconnected)
{
  auto sock = make_socket();
  EXPECT_FALSE(sock->connected());
}

TEST(Socket, MarkConnectedUpdatesState)
{
  auto sock = make_socket();
  sock->mark_connected(true);
  EXPECT_TRUE(sock->connected());
  sock->mark_connected(false);
  EXPECT_FALSE(sock->connected());
}

TEST(Socket, OnDispatchesMatchingEventWithData)
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

TEST(Socket, AckEventListenerReceivesReplyFunction)
{
  auto sock = make_socket();
  bool called = false;
  bool can_acknowledge = false;

  sock->on("question",
           [&](const std::string& event, message data,
               socket::ack_callback acknowledge)
           {
             called = true;
             can_acknowledge = static_cast<bool>(acknowledge);
             EXPECT_EQ(event, "question");
             EXPECT_EQ(data, json::array({"answer?"}));
             acknowledge(json::array({42}));
           });

  sock->dispatch_event("question", json::array({"answer?"}), 7);

  EXPECT_TRUE(called);
  EXPECT_TRUE(can_acknowledge);
}

TEST(Socket, AckEventListenerCannotReplyWhenEventHasNoAckId)
{
  auto sock = make_socket();
  bool called = false;

  sock->on("notification",
           [&](const std::string&, message, socket::ack_callback acknowledge)
           {
             called = true;
             EXPECT_FALSE(acknowledge);
           });

  sock->dispatch_event("notification", json::array());

  EXPECT_TRUE(called);
}

TEST(Socket, RegisteringListenerReplacesOtherListenerKind)
{
  auto sock = make_socket();
  int regular_calls = 0;
  int ack_calls = 0;

  sock->on("event", [&](const std::string&, message) { ++regular_calls; });
  sock->on("event", [&](const std::string&, message, socket::ack_callback)
           { ++ack_calls; });
  sock->dispatch_event("event", json::array(), 1);

  EXPECT_EQ(regular_calls, 0);
  EXPECT_EQ(ack_calls, 1);

  sock->on("event", [&](const std::string&, message) { ++regular_calls; });
  sock->dispatch_event("event", json::array(), 2);

  EXPECT_EQ(regular_calls, 1);
  EXPECT_EQ(ack_calls, 1);
}

TEST(Socket, DispatchIgnoresUnregisteredEvent)
{
  auto sock = make_socket();
  int call_count = 0;
  sock->on("known", [&](const std::string&, const message&) { ++call_count; });

  EXPECT_NO_THROW(sock->dispatch_event("unknown", json::array()));
  EXPECT_EQ(call_count, 0);
}

TEST(Socket, OnAnyDispatchesEveryEventAfterNamedListener)
{
  auto sock = make_socket();
  std::vector<std::string> calls;
  message seen_data;

  sock->on("known",
           [&](const std::string&, message) { calls.push_back("named"); });
  sock->on_any(
    [&](const std::string& event, message data)
    {
      calls.push_back(event);
      seen_data = std::move(data);
    });

  sock->dispatch_event("known", json::array({1}));
  sock->dispatch_event("unknown", json::array({2}));

  EXPECT_EQ(calls, (std::vector<std::string>{"named", "known", "unknown"}));
  EXPECT_EQ(seen_data, json::array({2}));
}

TEST(Socket, OnAnyAckListenerReceivesReplyFunction)
{
  auto sock = make_socket();
  bool called = false;

  sock->on_any(
    [&](const std::string& event, message data,
        socket::ack_callback acknowledge)
    {
      called = true;
      EXPECT_EQ(event, "question");
      EXPECT_EQ(data, json::array({"answer?"}));
      EXPECT_TRUE(acknowledge);
    });

  sock->dispatch_event("question", json::array({"answer?"}), 7);

  EXPECT_TRUE(called);
}

TEST(Socket, RegisteringOnAnyReplacesPreviousListenerKind)
{
  auto sock = make_socket();
  int regular_calls = 0;
  int ack_calls = 0;

  sock->on_any([&](const std::string&, message) { ++regular_calls; });
  sock->on_any([&](const std::string&, message, socket::ack_callback)
               { ++ack_calls; });
  sock->dispatch_event("event", json::array(), 1);

  EXPECT_EQ(regular_calls, 0);
  EXPECT_EQ(ack_calls, 1);
}

TEST(Socket, OnOverwritesPreviousListenerForSameEvent)
{
  auto sock = make_socket();
  int first_calls = 0, second_calls = 0;
  sock->on("x", [&](const std::string&, const message&) { ++first_calls; });
  sock->on("x", [&](const std::string&, const message&) { ++second_calls; });

  sock->dispatch_event("x", json::array());

  EXPECT_EQ(first_calls, 0);
  EXPECT_EQ(second_calls, 1);
}

TEST(Socket, OffRemovesOnlyTheNamedListener)
{
  auto sock = make_socket();
  int a_calls = 0, b_calls = 0;
  sock->on("a", [&](const std::string&, const message&) { ++a_calls; });
  sock->on("b", [&](const std::string&, const message&) { ++b_calls; });

  sock->off("a");
  sock->dispatch_event("a", json::array());
  sock->dispatch_event("b", json::array());

  EXPECT_EQ(a_calls, 0);
  EXPECT_EQ(b_calls, 1);
}

TEST(Socket, OffAllRemovesEveryListener)
{
  auto sock = make_socket();
  int calls = 0;
  sock->on("a", [&](const std::string&, const message&) { ++calls; });
  sock->on("b", [&](const std::string&, const message&) { ++calls; });
  sock->on_any([&](const std::string&, const message&) { ++calls; });

  sock->off_all();
  sock->dispatch_event("a", json::array());
  sock->dispatch_event("b", json::array());

  EXPECT_EQ(calls, 0);
}

TEST(Socket, EmitWithAckRegistersCallbackInvokedByDispatchAck)
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

TEST(Socket, AckIdsIncrementPerCall)
{
  auto sock = make_socket();
  std::vector<int> received_ids;

  sock->emit("a", json::array(),
             [&](const message&) { received_ids.push_back(0); });
  sock->emit("b", json::array(),
             [&](const message&) { received_ids.push_back(1); });

  sock->dispatch_ack(1, json::array());
  sock->dispatch_ack(0, json::array());

  ASSERT_EQ(received_ids.size(), 2u);
  EXPECT_EQ(received_ids[0], 1);  // second emit's ack (id 1) fired first
  EXPECT_EQ(received_ids[1], 0);
}

TEST(Socket, DispatchAckWithUnknownIdIsIgnored)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->dispatch_ack(999, json::array()));
}

TEST(Socket, DispatchAckIsConsumedOnce)
{
  auto sock = make_socket();
  int calls = 0;
  sock->emit("once", json::array(), [&](const message&) { ++calls; });

  sock->dispatch_ack(0, json::array());
  sock->dispatch_ack(0, json::array());  // second delivery for same id: no-op

  EXPECT_EQ(calls, 1);
}

TEST(Socket, EmitWithoutAckDoesNotThrowWithNoClientAttached)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->emit("fire_and_forget", json::array({"x"})));
}

TEST(Socket, ConnectAndDisconnectDoNotThrowWithNoClientAttached)
{
  auto sock = make_socket();
  EXPECT_NO_THROW(sock->connect());
  sock->mark_connected(true);
  EXPECT_NO_THROW(sock->disconnect());
  EXPECT_FALSE(sock->connected());
}
