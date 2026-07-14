#include <gtest/gtest.h>

#include <sioxx/reconnection.hpp>

using namespace sioxx;
using namespace std::chrono_literals;

TEST(ReconnectionPolicy, DoublesDelayUntilMaximum)
{
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 0.0, 1, 0.5), 100ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 0.0, 2, 0.5), 200ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 0.0, 3, 0.5), 400ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 0.0, 4, 0.5), 500ms);
}

TEST(ReconnectionPolicy, AppliesSymmetricJitter)
{
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 1000ms, 0.5, 1, 0.0), 50ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 1000ms, 0.5, 1, 0.5), 100ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 1000ms, 0.5, 1, 1.0), 150ms);
}

TEST(ReconnectionPolicy, ClampsInputsAndMaximum)
{
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 50ms, 0.0, 1, 0.5), 50ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 2.0, 1, -1.0), 0ms);
  EXPECT_EQ(reconnect_delay_for_attempt(100ms, 500ms, 2.0, 1, 2.0), 200ms);
}
