#include <gtest/gtest.h>

#include <sioxx/client.hpp>
#include <stdexcept>
#include <utility>

using namespace sioxx;

namespace
{

class custom_parser final : public parser_base
{
 public:
  void encode(const packet&, const frame_writer&) const override {}

  bool decode(const std::string&, bool, packet&) override { return false; }

  std::string name() const override { return "custom"; }
};

class throwing_parser final : public parser_base
{
 public:
  explicit throwing_parser(int& encode_calls) : encode_calls_(encode_calls) {}

  void encode(const packet&, const frame_writer&) const override
  {
    ++encode_calls_;
    throw std::runtime_error("encode failed");
  }

  bool decode(const std::string&, bool, packet&) override { return false; }

  std::string name() const override { return "throwing"; }

 private:
  int& encode_calls_;
};

}  // namespace

TEST(CustomParser, FactoryIsUsedInsteadOfStockParser)
{
  int calls = 0;
  client_options options;
  options.parser = parser_kind::msgpack;
  options.parser_factory = [&]
  {
    ++calls;
    return std::make_unique<custom_parser>();
  };

  client instance(options);

  EXPECT_EQ(calls, 1);
}

TEST(CustomParser, NullFactoryResultIsRejected)
{
  client_options options;
  options.parser_factory = [] { return std::unique_ptr<parser_base>{}; };

  EXPECT_THROW(client instance(options), std::invalid_argument);
}

TEST(CustomParser, FactoryIsUsedWithMovedOptions)
{
  int calls = 0;
  client_options options;
  options.parser_factory = [&]
  {
    ++calls;
    return std::make_unique<custom_parser>();
  };

  client instance(std::move(options));

  EXPECT_EQ(calls, 1);
}

TEST(CustomParser, ClientDestructorCompletesWhenDisconnectEncodingThrows)
{
  int encode_calls = 0;
  client_options options;
  options.parser_factory = [&]
  { return std::make_unique<throwing_parser>(encode_calls); };

  auto instance = std::make_unique<client>(options);
  auto socket = instance->socket();
  instance->connect("ws://127.0.0.1:1");
  socket->mark_connected(true);

  EXPECT_NO_THROW(instance.reset());
  EXPECT_EQ(encode_calls, 1);
}
