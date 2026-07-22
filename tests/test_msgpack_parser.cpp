#include <gtest/gtest.h>

#include "msgpack_parser.hpp"

using namespace sioxx;

TEST(MsgpackParser, EncodeProducesBinaryFrame)
{
  msgpack_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array({"hello", "world"});

  bool saw_binary = false;
  std::string payload;
  parser.encode(pkt,
                [&](const std::string& p, bool is_binary)
                {
                  saw_binary = is_binary;
                  payload = p;
                });

  EXPECT_TRUE(saw_binary);
  EXPECT_FALSE(payload.empty());
}

TEST(MsgpackParser, RoundTripsEvent)
{
  msgpack_parser parser;
  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/your_namespace";
  pkt.id = 5;
  pkt.data = json::array(
    {"your_message", json::object({{"lat", 48.85}, {"lon", 2.35}})});

  std::string payload;
  parser.encode(pkt, [&](const std::string& p, bool) { payload = p; });

  packet decoded;
  ASSERT_TRUE(parser.decode(payload, true, decoded));
  EXPECT_EQ(decoded.type, packet_type::event);
  EXPECT_EQ(decoded.nsp, "/your_namespace");
  EXPECT_EQ(decoded.id, 5);
  ASSERT_TRUE(decoded.data.is_array());
  EXPECT_EQ(decoded.data[0].get<std::string>(), "your_message");
  EXPECT_DOUBLE_EQ(decoded.data[1]["lat"].get<double>(), 48.85);
}

TEST(MsgpackParser, RoundTripsWithoutAckId)
{
  msgpack_parser parser;
  packet pkt;
  pkt.type = packet_type::connect;
  pkt.nsp = "/";

  std::string payload;
  parser.encode(pkt, [&](const std::string& p, bool) { payload = p; });

  packet decoded;
  ASSERT_TRUE(parser.decode(payload, true, decoded));
  EXPECT_EQ(decoded.type, packet_type::connect);
  EXPECT_EQ(decoded.id, -1);
  EXPECT_TRUE(decoded.data.is_null());
}

TEST(MsgpackParser, CarriesBinaryPayloadNatively)
{
  msgpack_parser parser;
  std::vector<uint8_t> raw{0xDE, 0xAD, 0xBE, 0xEF};

  packet pkt;
  pkt.type = packet_type::event;
  pkt.nsp = "/";
  pkt.data = json::array({"blob", json::binary(raw)});

  std::string payload;
  parser.encode(pkt, [&](const std::string& p, bool) { payload = p; });

  packet decoded;
  ASSERT_TRUE(parser.decode(payload, true, decoded));
  ASSERT_TRUE(decoded.data[1].is_binary());
  EXPECT_EQ(static_cast<std::vector<uint8_t>>(decoded.data[1].get_binary()),
            raw);
}

TEST(MsgpackParser, RejectsTextFrames)
{
  msgpack_parser parser;
  packet decoded;
  EXPECT_FALSE(parser.decode("not msgpack", false, decoded));
}

TEST(MsgpackParser, RejectsMalformedBytes)
{
  msgpack_parser parser;
  packet decoded;
  std::string garbage = "\xff\xff\xff\xff";
  EXPECT_FALSE(parser.decode(garbage, true, decoded));
}
