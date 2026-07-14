#include <gtest/gtest.h>

#include <sioxx/json_parser.hpp>

using namespace sioxx;

namespace
{

std::string encode_to_string(const json_parser& p, const socketio_packet& pkt)
{
  std::string out;
  p.encode(pkt,
           [&](const std::string& payload, bool is_binary)
           {
             EXPECT_FALSE(is_binary);
             out = payload;
           });
  return out;
}

}  // namespace

TEST(JsonParser, EncodesEventOnDefaultNamespace)
{
  json_parser parser;
  socketio_packet pkt;
  pkt.type = socketio_packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array({"chat message", "hello"});

  EXPECT_EQ(encode_to_string(parser, pkt), R"(2["chat message","hello"])");
}

TEST(JsonParser, EncodesEventOnCustomNamespace)
{
  json_parser parser;
  socketio_packet pkt;
  pkt.type = socketio_packet_type::event;
  pkt.nsp = "/your_namespace";
  pkt.data = json::array({"your_message", 1});

  EXPECT_EQ(encode_to_string(parser, pkt),
            R"(2/your_namespace,["your_message",1])");
}

TEST(JsonParser, EncodesEventWithAckId)
{
  json_parser parser;
  socketio_packet pkt;
  pkt.type = socketio_packet_type::event;
  pkt.nsp = "/";
  pkt.id = 12;
  pkt.data = json::array({"ping_ack"});

  EXPECT_EQ(encode_to_string(parser, pkt), R"(212["ping_ack"])");
}

TEST(JsonParser, EncodesConnectAndDisconnect)
{
  json_parser parser;
  socketio_packet connect_pkt;
  connect_pkt.type = socketio_packet_type::connect;
  connect_pkt.nsp = "/";
  EXPECT_EQ(encode_to_string(parser, connect_pkt), "0");

  socketio_packet disconnect_pkt;
  disconnect_pkt.type = socketio_packet_type::disconnect;
  disconnect_pkt.nsp = "/orders";
  EXPECT_EQ(encode_to_string(parser, disconnect_pkt), "1/orders,");
}

TEST(JsonParser, EncodesBinaryEventHeader)
{
  json_parser parser;
  socketio_packet pkt;
  pkt.type = socketio_packet_type::binary_event;
  pkt.nsp = "/";
  pkt.attachments = 1;
  pkt.data = json::array({"upload"});

  EXPECT_EQ(encode_to_string(parser, pkt), R"(51-["upload"])");
}

TEST(JsonParser, DecodesEventRoundTrip)
{
  json_parser parser;
  socketio_packet decoded;
  ASSERT_TRUE(parser.decode(R"(2/your_namespace,3["your_message",{"id":7}])",
                            false, decoded));

  EXPECT_EQ(decoded.type, socketio_packet_type::event);
  EXPECT_EQ(decoded.nsp, "/your_namespace");
  EXPECT_EQ(decoded.id, 3);
  ASSERT_TRUE(decoded.data.is_array());
  EXPECT_EQ(decoded.data[0].get<std::string>(), "your_message");
  EXPECT_EQ(decoded.data[1]["id"].get<int>(), 7);
}

TEST(JsonParser, DecodesEventOnDefaultNamespaceWithoutId)
{
  json_parser parser;
  socketio_packet decoded;
  ASSERT_TRUE(parser.decode(R"(2["hello","world"])", false, decoded));

  EXPECT_EQ(decoded.type, socketio_packet_type::event);
  EXPECT_EQ(decoded.nsp, "/");
  EXPECT_EQ(decoded.id, -1);
  EXPECT_EQ(decoded.data[1].get<std::string>(), "world");
}

TEST(JsonParser, DecodesConnectWithNoPayload)
{
  json_parser parser;
  socketio_packet decoded;
  ASSERT_TRUE(parser.decode("0/chat,", false, decoded));
  EXPECT_EQ(decoded.type, socketio_packet_type::connect);
  EXPECT_EQ(decoded.nsp, "/chat");
  EXPECT_TRUE(decoded.data.is_null());
}

TEST(JsonParser, DecodesBinaryEventHeader)
{
  json_parser parser;
  socketio_packet decoded;
  ASSERT_TRUE(parser.decode(R"(51-/chat,["upload"])", false, decoded));
  EXPECT_EQ(decoded.type, socketio_packet_type::binary_event);
  EXPECT_EQ(decoded.attachments, 1);
  EXPECT_EQ(decoded.nsp, "/chat");
}

TEST(JsonParser, RejectsBinaryFrames)
{
  json_parser parser;
  socketio_packet decoded;
  EXPECT_FALSE(parser.decode("irrelevant", true, decoded));
}

TEST(JsonParser, RejectsGarbageJson)
{
  json_parser parser;
  socketio_packet decoded;
  EXPECT_FALSE(parser.decode("2[not valid json", false, decoded));
}

TEST(JsonParser, RejectsEmptyPayload)
{
  json_parser parser;
  socketio_packet decoded;
  EXPECT_FALSE(parser.decode("", false, decoded));
}
