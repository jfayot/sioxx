#include <gtest/gtest.h>

#include "engineio_url.hpp"

using sioxx::detail::build_engineio_url;

TEST(EngineioUrl, UsesDefaultPath)
{
  EXPECT_EQ(
    build_engineio_url("https://example.com", "/socket.io/", {}, "websocket"),
    "wss://example.com/socket.io/?EIO=4&transport=websocket");
}

TEST(EngineioUrl, ReplacesUriPathWithConfiguredEngineioPath)
{
  EXPECT_EQ(build_engineio_url("wss://example.com/ignored?old=value",
                               "/realtime", {}, "polling"),
            "wss://example.com/realtime/?EIO=4&transport=polling");
}

TEST(EngineioUrl, NormalizesPathAndEncodesQueryParameters)
{
  EXPECT_EQ(
    build_engineio_url("example.com:3000", "custom/socket.io",
                       {{"client name", "C++ device"}, {"token", "a/b?c=d"}},
                       "websocket"),
    "ws://example.com:3000/custom/socket.io/"
    "?EIO=4&transport=websocket&client%20name=C%2B%2B%20device"
    "&token=a%2Fb%3Fc%3Dd");
}

TEST(EngineioUrl, RejectsReservedQueryParameters)
{
  EXPECT_THROW(build_engineio_url("ws://localhost", "/socket.io/",
                                  {{"transport", "polling"}}, "websocket"),
               std::invalid_argument);
}
