#include <gtest/gtest.h>

#include <stdexcept>

#include "proxy.hpp"

namespace
{

TEST(Proxy, EmptyUriDisablesProxying)
{
  EXPECT_FALSE(sioxx::detail::make_proxy_config("", "user", "password"));
}

TEST(Proxy, ParsesHttpProxyAndEncodesBasicCredentials)
{
  const auto proxy = sioxx::detail::make_proxy_config(
    "http://proxy.example.com:8080", "user", "password");

  ASSERT_TRUE(proxy);
  EXPECT_EQ(proxy->endpoint.host, "proxy.example.com");
  EXPECT_EQ(proxy->endpoint.port, "8080");
  EXPECT_EQ(proxy->authorization, "Basic dXNlcjpwYXNzd29yZA==");
}

TEST(Proxy, RejectsUnsupportedProxyUris)
{
  EXPECT_THROW(
    sioxx::detail::make_proxy_config("https://proxy.example.com", "", ""),
    std::invalid_argument);
  EXPECT_THROW(
    sioxx::detail::make_proxy_config("http://proxy.example.com/path", "", ""),
    std::invalid_argument);
  EXPECT_THROW(sioxx::detail::make_proxy_config(
                 "http://proxy.example.com?mode=test", "", ""),
               std::invalid_argument);
}

TEST(Proxy, BuildsOriginAuthorityAndAbsoluteHttpTarget)
{
  const auto url = sioxx::parse_ws_url(
    "ws://example.com:3000/socket.io/?EIO=4&transport=polling");

  EXPECT_EQ(sioxx::detail::authority(url), "example.com:3000");
  EXPECT_EQ(sioxx::detail::connect_authority(url), "example.com:3000");
  EXPECT_EQ(sioxx::detail::absolute_http_target(url, url.target),
            "http://example.com:3000/socket.io/?EIO=4&transport=polling");
}

}  // namespace
