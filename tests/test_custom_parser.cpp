#include <gtest/gtest.h>

#include <sioxx/client.hpp>
#include <stdexcept>

using namespace sioxx;

namespace
{

class custom_parser final : public parser_base
{
 public:
  void encode(const packet&, const frame_writer&) const override {}

  bool decode(const std::string&, bool, packet&) override
  {
    return false;
  }

  std::string name() const override { return "custom"; }
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
