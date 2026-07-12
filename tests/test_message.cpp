#include <gtest/gtest.h>

#include <sioxx/message.hpp>

using namespace sioxx;

TEST(Message, MakeArgsEmpty)
{
  message_list args = make_args();
  EXPECT_TRUE(args.is_array());
  EXPECT_TRUE(args.empty());
}

TEST(Message, MakeArgsMixedTypes)
{
  message_list args = make_args(std::string("hello"), 42, 3.14, true);
  ASSERT_TRUE(args.is_array());
  ASSERT_EQ(args.size(), 4u);
  EXPECT_EQ(args[0].get<std::string>(), "hello");
  EXPECT_EQ(args[1].get<int>(), 42);
  EXPECT_DOUBLE_EQ(args[2].get<double>(), 3.14);
  EXPECT_EQ(args[3].get<bool>(), true);
}

TEST(Message, BinaryMessageFromPointer)
{
  const uint8_t bytes[] = {0x01, 0x02, 0x03, 0xFF};
  message m = binary_message(bytes, sizeof(bytes));
  ASSERT_TRUE(m.is_binary());
  auto& bin = m.get_binary();
  EXPECT_EQ(bin.size(), 4u);
  EXPECT_EQ(bin[3], 0xFF);
}

TEST(Message, BinaryMessageFromVector)
{
  std::vector<uint8_t> bytes{0xAA, 0xBB};
  message m = binary_message(bytes);
  ASSERT_TRUE(m.is_binary());
  EXPECT_EQ(static_cast<std::vector<uint8_t>>(m.get_binary()), bytes);
}

TEST(Message, MessageIsJustJson)
{
  message m = json::object({{"key", "value"}});
  EXPECT_TRUE(m.is_object());
  EXPECT_EQ(m["key"].get<std::string>(), "value");
}
