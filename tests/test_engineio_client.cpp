#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <sioxx/message.hpp>
#include <stdexcept>
#include <string_view>

#include "engineio_client.hpp"
#include "polling_protocol.hpp"

using namespace sioxx;

namespace
{

// A trivial in-memory stand-in for websocket_transport: "connecting" is
// instantaneous and messages are delivered by calling simulate_message()
// directly, so these tests exercise engineio_client's framing/handshake
// logic without touching a real socket.
class fake_transport : public transport_base
{
 public:
  void connect(const std::string& url) override
  {
    last_connect_url = url;
    state_ = transport_state::open;
    if (on_open_) on_open_();
  }

  void send(const std::string& payload, bool is_binary) override
  {
    sent.emplace_back(payload, is_binary);
  }

  void close() override
  {
    ++close_calls;
    state_ = transport_state::closed;
    if (on_close_) on_close_("closed");
  }

  void sync_close() override
  {
    ++sync_close_calls;
    if (throw_on_sync_close) throw std::runtime_error("sync close failed");
  }

  void simulate_message(const std::string& payload, bool is_binary = false)
  {
    if (on_message_) on_message_(payload, is_binary);
  }

  void simulate_close(const std::string& reason)
  {
    state_ = transport_state::closed;
    if (on_close_) on_close_(reason);
  }

  void simulate_error(const std::string& message)
  {
    if (on_error_) on_error_(message);
  }

  std::string last_connect_url;
  std::vector<std::pair<std::string, bool>> sent;
  int close_calls{0};
  int sync_close_calls{0};
  bool throw_on_sync_close{false};
};

std::string make_open_payload(int ping_interval_ms = 25000,
                              int ping_timeout_ms = 20000)
{
  json handshake = {
    {"sid", "test-sid"},
    {"upgrades", json::array()},
    {"pingInterval", ping_interval_ms},
    {"pingTimeout", ping_timeout_ms},
  };
  return "0" + handshake.dump();
}

struct EngineioClientFixture : ::testing::Test
{
  std::shared_ptr<engineio_client> client = std::make_shared<engineio_client>();
  std::shared_ptr<fake_transport> transport =
    std::make_shared<fake_transport>();

  void SetUp() override { client->set_transport(transport); }
  void TearDown() override { client->close(); }
};

}  // namespace

TEST(EngineioClient, DestructorSuppressesTransportSyncCloseExceptions)
{
  auto transport = std::make_shared<fake_transport>();
  transport->throw_on_sync_close = true;
  {
    auto client = std::make_shared<engineio_client>();
    client->set_transport(transport);
  }

  EXPECT_EQ(transport->close_calls, 1);
  EXPECT_EQ(transport->sync_close_calls, 1);
}

TEST(HttpPollingProtocol, BinaryPayloadRoundTripsThroughBase64Packet)
{
  const std::string binary{"\x00\xff\x01\x02", 4};
  std::string decoded;
  const auto packet = detail::polling_encode_binary(binary);

  EXPECT_EQ(packet.substr(0, 1), "b");
  ASSERT_TRUE(detail::polling_decode_binary(packet, decoded));
  EXPECT_EQ(decoded, binary);
}

TEST(HttpPollingProtocol, RejectsNonBinaryAndMalformedPackets)
{
  std::string decoded;
  EXPECT_FALSE(detail::polling_decode_binary("4hello", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("b???", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("b!!!!", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("b!AAA", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("bA!AA", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("bAA!A", decoded));
  EXPECT_FALSE(detail::polling_decode_binary("bAAA!", decoded));
}

TEST(HttpPollingProtocol, HandlesBase64PaddingAndEmptyPayloads)
{
  for (const std::string payload :
       {std::string(), std::string("f"), std::string("fo"), std::string("foo"),
        std::string("\xfb", 1)})
  {
    std::string decoded;
    ASSERT_TRUE(detail::polling_decode_binary(
      detail::polling_encode_binary(payload), decoded));
    EXPECT_EQ(decoded, payload);
  }
}

TEST(HttpPollingProtocol, ReadsViewsOfBatchedEngineioPackets)
{
  const std::string payload =
    "40/e2e,{\"sid\":\"abc\"}\x1e"
    "42/e2e,[\"server_arguments\",1]";
  std::string_view remaining = payload;

  const auto first = detail::polling_next_packet(remaining);
  EXPECT_EQ(first, "40/e2e,{\"sid\":\"abc\"}");
  EXPECT_EQ(first.data(), payload.data());
  EXPECT_EQ(detail::polling_next_packet(remaining),
            "42/e2e,[\"server_arguments\",1]");
  EXPECT_TRUE(remaining.empty());
}

TEST(HttpPollingProtocol, SkipsEmptyPackets)
{
  std::string_view remaining =
    "\x1e"
    "2\x1e\x1e"
    "6\x1e\x1e";

  EXPECT_EQ(detail::polling_next_packet(remaining), "2");
  EXPECT_EQ(detail::polling_next_packet(remaining), "6");
  EXPECT_TRUE(detail::polling_next_packet(remaining).empty());
  EXPECT_TRUE(remaining.empty());
}

TEST_F(EngineioClientFixture, NotOpenBeforeHandshake)
{
  EXPECT_FALSE(client->is_open());
  EXPECT_TRUE(client->session_id().empty());
}

TEST(EngineioClient, OpenWithoutTransportThrows)
{
  auto client = std::make_shared<engineio_client>();
  EXPECT_THROW(client->open("ws://localhost/socket.io/"), std::runtime_error);
}

TEST_F(EngineioClientFixture, ForwardsTransportErrors)
{
  std::string received_error;
  client->on_error([&](const std::string& error) { received_error = error; });

  transport->simulate_error("transport failed");

  EXPECT_EQ(received_error, "transport failed");
}

TEST_F(EngineioClientFixture, OpenHandshakeMarksClientOpenAndFiresCallback)
{
  bool open_fired = false;
  client->on_open([&] { open_fired = true; });

  client->open("ws://localhost/socket.io/?EIO=4&transport=websocket");
  EXPECT_EQ(transport->last_connect_url,
            "ws://localhost/socket.io/?EIO=4&transport=websocket");
  EXPECT_FALSE(client->is_open());  // transport open != engine.io open yet

  transport->simulate_message(make_open_payload());

  EXPECT_TRUE(client->is_open());
  EXPECT_EQ(client->session_id(), "test-sid");
  EXPECT_TRUE(open_fired);
}

TEST_F(EngineioClientFixture, RejectsIncompleteOpenHandshake)
{
  std::string received_error;
  bool open_fired = false;
  client->on_error([&](const std::string& error) { received_error = error; });
  client->on_open([&] { open_fired = true; });

  transport->simulate_message("0{}");

  EXPECT_EQ(received_error, "invalid Engine.IO handshake");
  EXPECT_EQ(transport->close_calls, 1);
  EXPECT_FALSE(client->is_open());
  EXPECT_FALSE(open_fired);
}

TEST_F(EngineioClientFixture, RejectsFractionalHeartbeatTimeout)
{
  std::string received_error;
  client->on_error([&](const std::string& error) { received_error = error; });
  client->open("ws://localhost/socket.io/");

  transport->simulate_message(
    R"(0{"sid":"test-sid","pingInterval":25000,"pingTimeout":1.5})");

  EXPECT_EQ(received_error, "invalid Engine.IO handshake");
  EXPECT_EQ(transport->close_calls, 1);
  EXPECT_FALSE(client->is_open());
}

TEST_F(EngineioClientFixture, IncompleteOpenFramePreservesEstablishedSession)
{
  transport->simulate_message(make_open_payload());

  transport->simulate_message("0{}");

  EXPECT_EQ(transport->close_calls, 0);
  EXPECT_TRUE(client->is_open());
  EXPECT_EQ(client->session_id(), "test-sid");
}

TEST_F(EngineioClientFixture, PingFrameTriggersImmediatePongReply)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());
  transport->sent.clear();

  transport->simulate_message("2");  // server -> client ping

  ASSERT_FALSE(transport->sent.empty());
  EXPECT_EQ(transport->sent.back().first, "3");
  EXPECT_FALSE(transport->sent.back().second);
}

TEST_F(EngineioClientFixture, IgnoresPongNoopUnknownAndEmptyFrames)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());
  transport->sent.clear();

  transport->simulate_message("3");
  transport->simulate_message("6");
  transport->simulate_message("5upgrade");
  transport->simulate_message("");

  EXPECT_TRUE(client->is_open());
  EXPECT_TRUE(transport->sent.empty());
}

TEST_F(EngineioClientFixture, ServerCloseFrameNotifiesAndClearsOpenState)
{
  std::string close_reason;
  client->on_close([&](const std::string& reason) { close_reason = reason; });
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());

  transport->simulate_message("1");

  EXPECT_FALSE(client->is_open());
  EXPECT_EQ(close_reason, "server closed connection");

  client->close();
}

TEST_F(EngineioClientFixture, MessageFrameForwardedToFrameHandlerWithoutPrefix)
{
  std::string received_payload;
  bool received_binary = true;
  client->on_frame(
    [&](const std::string& payload, bool is_binary)
    {
      received_payload = payload;
      received_binary = is_binary;
    });

  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());
  transport->simulate_message(std::string("4") + R"(2["hello"])");

  EXPECT_EQ(received_payload, R"(2["hello"])");
  EXPECT_FALSE(received_binary);
}

TEST_F(EngineioClientFixture, BinaryFramePassedThroughUntouched)
{
  std::string received_payload;
  bool received_binary = false;
  client->on_frame(
    [&](const std::string& payload, bool is_binary)
    {
      received_payload = payload;
      received_binary = is_binary;
    });

  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());

  std::string binary_payload{"\x01\x02\x03", 3};
  transport->simulate_message(binary_payload, true);

  EXPECT_EQ(received_payload, binary_payload);
  EXPECT_TRUE(received_binary);
}

TEST_F(EngineioClientFixture, SendBeforeHandshakeIsDroppedSilently)
{
  client->open(
    "ws://localhost/socket.io/");  // transport connects, but no OPEN frame yet
  client->send(R"(2["too_early"])", false);

  EXPECT_TRUE(transport->sent.empty());
}

TEST_F(EngineioClientFixture, SendAfterOpenPrefixesTextFrameWithMessageType)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());
  transport->sent.clear();

  client->send(R"(2["hi"])", false);

  ASSERT_FALSE(transport->sent.empty());
  EXPECT_EQ(transport->sent.back().first, R"(42["hi"])");
  EXPECT_FALSE(transport->sent.back().second);
}

TEST_F(EngineioClientFixture, SendBinaryFrameSkipsTextPrefix)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());
  transport->sent.clear();

  std::string blob{"\xAA\xBB\xCC", 3};
  client->send(blob, true);

  ASSERT_FALSE(transport->sent.empty());
  EXPECT_EQ(transport->sent.back().first, blob);
  EXPECT_TRUE(transport->sent.back().second);
}

TEST_F(EngineioClientFixture,
       TransportCloseNotifiesCloseHandlerAndClearsOpenState)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload());

  bool closed = false;
  std::string reason;
  client->on_close(
    [&](const std::string& r)
    {
      closed = true;
      reason = r;
    });

  client->close();

  EXPECT_TRUE(closed);
  EXPECT_EQ(reason, "closed");
  EXPECT_FALSE(client->is_open());
}

TEST_F(EngineioClientFixture, CloseIsIdempotent)
{
  client->close();
  client->close();

  EXPECT_EQ(transport->close_calls, 1);
}

TEST_F(EngineioClientFixture, SyncCloseWaitsAfterCloseRequest)
{
  client->close();

  EXPECT_EQ(transport->close_calls, 1);
  EXPECT_EQ(transport->sync_close_calls, 0);

  client->sync_close();

  EXPECT_EQ(transport->close_calls, 1);
  EXPECT_EQ(transport->sync_close_calls, 1);
}

TEST_F(EngineioClientFixture,
       UnexpectedTransportCloseStopsHeartbeatAndNotifiesImmediately)
{
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload(500, 20000));

  auto closed = std::make_shared<bool>(false);
  auto reason = std::make_shared<std::string>();
  client->on_close(
    [closed, reason](const std::string& r)
    {
      *closed = true;
      *reason = r;
    });

  const auto start = std::chrono::steady_clock::now();
  transport->simulate_close("connection reset");
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(250));
  EXPECT_TRUE(*closed);
  EXPECT_EQ(*reason, "connection reset");
  EXPECT_FALSE(client->is_open());
}

TEST_F(EngineioClientFixture, RepeatedOpenHandshakeRestartsHeartbeatSafely)
{
  auto error_promise = std::make_shared<std::promise<std::string>>();
  auto error = error_promise->get_future();
  client->on_error([error_promise](const std::string& message)
                   { error_promise->set_value(message); });

  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload(5000, 5000));
  transport->simulate_message(make_open_payload(10, 0));

  ASSERT_EQ(error.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  EXPECT_EQ(error.get(), "engine.io ping timeout");
}

TEST_F(EngineioClientFixture, HeartbeatTimeoutCallbackCanCloseClient)
{
  auto closed = std::make_shared<std::promise<void>>();
  auto close_returned = closed->get_future();
  const std::weak_ptr<engineio_client> weak_client = client;
  client->on_error(
    [weak_client, closed](const std::string& message)
    {
      EXPECT_EQ(message, "engine.io ping timeout");
      if (auto instance = weak_client.lock()) instance->close();
      closed->set_value();
    });
  client->open("ws://localhost/socket.io/");
  transport->simulate_message(make_open_payload(10, 0));
  EXPECT_EQ(close_returned.wait_for(std::chrono::seconds(2)),
            std::future_status::ready);
  client->sync_close();
  EXPECT_FALSE(client->is_open());
}
