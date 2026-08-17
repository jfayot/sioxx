#include <gtest/gtest.h>

#include "json_parser.hpp"

using namespace sioxx;

namespace
{

struct encoded_frame
{
  std::string payload;
  bool is_binary{false};
};

std::vector<encoded_frame> encode_frames(const json_parser& p,
                                         const packet& pkt)
{
  std::vector<encoded_frame> frames;
  p.encode(pkt, [&](const std::string& payload, bool is_binary)
           { frames.push_back({payload, is_binary}); });
  return frames;
}

std::string encode_to_string(const json_parser& p, const packet& pkt)
{
  const auto frames = encode_frames(p, pkt);
  EXPECT_EQ(frames.size(), 1u);
  if (frames.empty()) return {};
  EXPECT_FALSE(frames[0].is_binary);
  return frames[0].payload;
}

}  // namespace

TEST(JsonParser, EncodesEventOnDefaultNamespace)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array({"chat message", "hello"});

  EXPECT_EQ(encode_to_string(parser, pkt), R"(2["chat message","hello"])");
}

TEST(JsonParser, EncodesEventOnCustomNamespace)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/your_namespace";
  pkt.data = json::array({"your_message", 1});

  EXPECT_EQ(encode_to_string(parser, pkt),
            R"(2/your_namespace,["your_message",1])");
}

TEST(JsonParser, EncodesEventWithAckId)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.id = 12;
  pkt.data = json::array({"ping_ack"});

  EXPECT_EQ(encode_to_string(parser, pkt), R"(212["ping_ack"])");
}

TEST(JsonParser, EncodesConnectAndDisconnect)
{
  json_parser parser;
  packet connect_pkt;
  connect_pkt.type = packet_type::connect;
  connect_pkt.nsp = "/";
  EXPECT_EQ(encode_to_string(parser, connect_pkt), "0");

  packet disconnect_pkt;
  disconnect_pkt.type = packet_type::disconnect;
  disconnect_pkt.nsp = "/orders";
  EXPECT_EQ(encode_to_string(parser, disconnect_pkt), "1/orders,");
}

TEST(JsonParser, EncodesNamespaceConnectWithAuthPayload)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::connect;
  pkt.nsp = "/private";
  pkt.data = json{{"token", "secret"}, {"tenant", 42}};

  EXPECT_EQ(encode_to_string(parser, pkt),
            R"(0/private,{"tenant":42,"token":"secret"})");
}

TEST(JsonParser, EncodesNestedBinaryEventAsHeaderAndAttachments)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array(
    {"upload",
     json::binary({0x00, 0x7f, 0xff}),
     {{"nested", json::array({json::binary({0x01, 0x02}), "end"})}}});

  const auto frames = encode_frames(parser, pkt);

  ASSERT_EQ(frames.size(), 3u);
  EXPECT_FALSE(frames[0].is_binary);
  EXPECT_EQ(
    frames[0].payload,
    R"(52-["upload",{"_placeholder":true,"num":0},{"nested":[{"_placeholder":true,"num":1},"end"]}])");
  EXPECT_TRUE(frames[1].is_binary);
  EXPECT_EQ(frames[1].payload, std::string("\x00\x7f\xff", 3));
  EXPECT_TRUE(frames[2].is_binary);
  EXPECT_EQ(frames[2].payload, std::string("\x01\x02", 2));
}

TEST(JsonParser, EncodesBinaryAcknowledgementWithNamespaceAndId)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::ack;
  pkt.nsp = "/chat";
  pkt.id = 12;
  pkt.data = json::array({"ok", json::binary({0xaa, 0xbb})});

  const auto frames = encode_frames(parser, pkt);

  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].payload,
            R"(61-/chat,12["ok",{"_placeholder":true,"num":0}])");
  EXPECT_FALSE(frames[0].is_binary);
  EXPECT_EQ(frames[1].payload, std::string("\xaa\xbb", 2));
  EXPECT_TRUE(frames[1].is_binary);
}

TEST(JsonParser, RoundTripsEmptyBinaryAttachment)
{
  json_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array({"empty", json::binary({})});

  const auto frames = encode_frames(parser, pkt);

  ASSERT_EQ(frames.size(), 2u);
  EXPECT_EQ(frames[0].payload, R"(51-["empty",{"_placeholder":true,"num":0}])");
  EXPECT_TRUE(frames[1].is_binary);
  EXPECT_TRUE(frames[1].payload.empty());

  packet decoded;
  EXPECT_FALSE(parser.decode(frames[0].payload, false, decoded));
  ASSERT_TRUE(parser.decode(frames[1].payload, true, decoded));
  EXPECT_EQ(decoded.data, pkt.data);
}

TEST(JsonParser, DecodesEventRoundTrip)
{
  json_parser parser;
  packet decoded;
  ASSERT_TRUE(parser.decode(R"(2/your_namespace,3["your_message",{"id":7}])",
                            false, decoded));

  EXPECT_EQ(decoded.type, packet_type::event);
  EXPECT_EQ(decoded.nsp, "/your_namespace");
  EXPECT_EQ(decoded.id, 3);
  ASSERT_TRUE(decoded.data.is_array());
  EXPECT_EQ(decoded.data[0].get<std::string>(), "your_message");
  EXPECT_EQ(decoded.data[1]["id"].get<int>(), 7);
}

TEST(JsonParser, DecodesEventOnDefaultNamespaceWithoutId)
{
  json_parser parser;
  packet decoded;
  ASSERT_TRUE(parser.decode(R"(2["hello","world"])", false, decoded));

  EXPECT_EQ(decoded.type, packet_type::event);
  EXPECT_EQ(decoded.nsp, "/");
  EXPECT_EQ(decoded.id, -1);
  EXPECT_EQ(decoded.data[1].get<std::string>(), "world");
}

TEST(JsonParser, DecodesConnectWithNoPayload)
{
  json_parser parser;
  packet decoded;
  ASSERT_TRUE(parser.decode("0/chat,", false, decoded));
  EXPECT_EQ(decoded.type, packet_type::connect);
  EXPECT_EQ(decoded.nsp, "/chat");
  EXPECT_TRUE(decoded.data.is_null());
}

TEST(JsonParser, DecodesNamespaceWithoutTrailingComma)
{
  json_parser parser;
  packet decoded;
  ASSERT_TRUE(parser.decode("0/chat", false, decoded));
  EXPECT_EQ(decoded.type, packet_type::connect);
  EXPECT_EQ(decoded.nsp, "/chat");
  EXPECT_TRUE(decoded.data.is_null());
}

TEST(JsonParser, ReconstructsNestedBinaryEventAfterAllAttachments)
{
  json_parser parser;
  packet decoded;

  EXPECT_FALSE(parser.decode(
    R"(52-/chat,7["upload",{"_placeholder":true,"num":0},{"nested":[{"_placeholder":true,"num":1}]}])",
    false, decoded));
  EXPECT_FALSE(parser.decode(std::string("\x00\x7f\xff", 3), true, decoded));
  ASSERT_TRUE(parser.decode(std::string("\x01\x02", 2), true, decoded));

  EXPECT_EQ(decoded.type, packet_type::binary_event);
  EXPECT_EQ(decoded.attachments, 2);
  EXPECT_EQ(decoded.nsp, "/chat");
  EXPECT_EQ(decoded.id, 7);
  EXPECT_EQ(decoded.data[0], "upload");
  EXPECT_EQ(decoded.data[1], json::binary({0x00, 0x7f, 0xff}));
  EXPECT_EQ(decoded.data[2]["nested"][0], json::binary({0x01, 0x02}));
}

TEST(JsonParser, ReconstructsBinaryAcknowledgement)
{
  json_parser parser;
  packet decoded;

  EXPECT_FALSE(parser.decode(
    R"(61-/chat,12["ok",{"_placeholder":true,"num":0}])", false, decoded));
  ASSERT_TRUE(parser.decode(std::string("\xaa\xbb", 2), true, decoded));

  EXPECT_EQ(decoded.type, packet_type::binary_ack);
  EXPECT_EQ(decoded.nsp, "/chat");
  EXPECT_EQ(decoded.id, 12);
  EXPECT_EQ(decoded.data[1], json::binary({0xaa, 0xbb}));
}

TEST(JsonParser, RejectsUnexpectedBinaryFrames)
{
  json_parser parser;
  packet decoded;
  EXPECT_FALSE(parser.decode("irrelevant", true, decoded));
}

TEST(JsonParser, RejectsMalformedBinaryHeadersAndRecovers)
{
  json_parser parser;
  packet decoded;

  EXPECT_FALSE(parser.decode(R"(51-["missing placeholder"])", false, decoded));
  EXPECT_FALSE(parser.decode(
    R"(51-["bad index",{"_placeholder":true,"num":1}])", false, decoded));
  EXPECT_FALSE(parser.decode(
    R"(52-["duplicate",{"_placeholder":true,"num":0},{"_placeholder":true,"num":0}])",
    false, decoded));
  EXPECT_FALSE(parser.decode(R"(50-["zero attachments"])", false, decoded));
  EXPECT_FALSE(
    parser.decode(R"(599999999999999999999999-["overflow"])", false, decoded));

  ASSERT_TRUE(parser.decode(R"(2["recovered",1])", false, decoded));
  EXPECT_EQ(decoded.type, packet_type::event);
  EXPECT_EQ(decoded.data, json::array({"recovered", 1}));
}

TEST(JsonParser, NewTextPacketDiscardsIncompleteBinaryPacket)
{
  json_parser parser;
  packet decoded;

  EXPECT_FALSE(parser.decode(R"(51-["upload",{"_placeholder":true,"num":0}])",
                             false, decoded));
  ASSERT_TRUE(parser.decode(R"(2["next",42])", false, decoded));

  EXPECT_EQ(decoded.type, packet_type::event);
  EXPECT_EQ(decoded.data, json::array({"next", 42}));
  EXPECT_FALSE(parser.decode(std::string("\x01", 1), true, decoded));
}

TEST(JsonParser, RejectsGarbageJson)
{
  json_parser parser;
  packet decoded;
  EXPECT_FALSE(parser.decode("2[not valid json", false, decoded));
}

TEST(JsonParser, RejectsEmptyPayload)
{
  json_parser parser;
  packet decoded;
  EXPECT_FALSE(parser.decode("", false, decoded));
}

TEST(JsonParser, RejectsInvalidPacketHeaders)
{
  json_parser parser;
  packet decoded;
  EXPECT_FALSE(parser.decode("x", false, decoded));
  EXPECT_FALSE(parser.decode("7", false, decoded));
  EXPECT_FALSE(parser.decode(R"(51/chat,["upload"])", false, decoded));
}
