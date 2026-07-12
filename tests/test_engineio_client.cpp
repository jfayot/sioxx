#include <gtest/gtest.h>

#include <sioxx/engineio_client.hpp>
#include <sioxx/message.hpp>

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
    state_ = transport_state::closed;
    if (on_close_) on_close_("closed");
  }

  void simulate_message(const std::string& payload, bool is_binary = false)
  {
    if (on_message_) on_message_(payload, is_binary);
  }

  std::string last_connect_url;
  std::vector<std::pair<std::string, bool>> sent;
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

TEST_F(EngineioClientFixture, NotOpenBeforeHandshake)
{
  EXPECT_FALSE(client->is_open());
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
  EXPECT_TRUE(open_fired);
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
