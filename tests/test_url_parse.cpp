#include <gtest/gtest.h>

#include "url_parse.hpp"

using namespace sioxx;

TEST(UrlParse, PlainWsWithDefaultPort)
{
  auto p = parse_ws_url("ws://localhost/socket.io/?EIO=4");
  EXPECT_EQ(p.scheme, "ws");
  EXPECT_FALSE(p.tls);
  EXPECT_EQ(p.host, "localhost");
  EXPECT_EQ(p.port, "80");
  EXPECT_EQ(p.target, "/socket.io/?EIO=4");
}

TEST(UrlParse, TlsWssWithDefaultPort)
{
  auto p = parse_ws_url("wss://your.socketio.server/socket.io/");
  EXPECT_EQ(p.scheme, "wss");
  EXPECT_TRUE(p.tls);
  EXPECT_EQ(p.host, "your.socketio.server");
  EXPECT_EQ(p.port, "443");
  EXPECT_EQ(p.target, "/socket.io/");
}

TEST(UrlParse, ExplicitPort)
{
  auto p =
    parse_ws_url("ws://192.168.1.10:3000/socket.io/?EIO=4&transport=websocket");
  EXPECT_EQ(p.host, "192.168.1.10");
  EXPECT_EQ(p.port, "3000");
  EXPECT_EQ(p.target, "/socket.io/?EIO=4&transport=websocket");
}

TEST(UrlParse, NoPathDefaultsToSlash)
{
  auto p = parse_ws_url("ws://localhost:8080");
  EXPECT_EQ(p.host, "localhost");
  EXPECT_EQ(p.port, "8080");
  EXPECT_EQ(p.target, "/");
}

TEST(UrlParse, MissingSchemeThrows)
{
  EXPECT_THROW(parse_ws_url("localhost:3000/socket.io/"),
               std::invalid_argument);
}
