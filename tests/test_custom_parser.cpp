#include <gtest/gtest.h>

#include <atomic>
#include <boost/asio/io_context.hpp>
#include <future>
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

class event_counting_parser final : public parser_base
{
 public:
  explicit event_counting_parser(std::atomic<int>& events) : events_(events) {}

  void encode(const packet& packet, const frame_writer&) const override
  {
    if (packet.type == packet_type::event) ++events_;
  }

  bool decode(const std::string&, bool, packet&) override { return false; }

  std::string name() const override { return "event-counting"; }

 private:
  std::atomic<int>& events_;
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

TEST(Client, SupportsCallerOwnedIoContextConstructors)
{
  boost::asio::io_context io_context;
  client_options options;

  EXPECT_NO_THROW(client default_instance(io_context));
  EXPECT_NO_THROW(client copied_options_instance(io_context, options));
  EXPECT_NO_THROW(client moved_options_instance(io_context, client_options{}));
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

TEST(CustomParser, TimedEmissionExpiredBeforeRegistrationIsNotEncoded)
{
  using namespace std::chrono_literals;
  std::atomic<int> events{0};
  boost::asio::io_context io_context;
  client_options options;
  options.parser_factory = [&]
  { return std::make_unique<event_counting_parser>(events); };
  client instance(io_context, options);
  auto socket = instance.socket();
  instance.connect("ws://127.0.0.1:1");
  socket->mark_connected(true);

  std::promise<void> locked;
  auto lock_acquired = locked.get_future();
  std::promise<void> release;
  auto gate = release.get_future().share();
  // Replacing this listener destroys its last capture under the socket lock.
  // Hold the lock so emit() consumes its deadline before ACK registration.
  auto blocker = std::shared_ptr<void>(nullptr,
                                       [&](void*)
                                       {
                                         locked.set_value();
                                         gate.wait();
                                       });
  socket->on_connect([blocker] {});
  blocker.reset();
  auto holding =
    std::async(std::launch::async, [&] { socket->on_connect(nullptr); });
  const auto lock_status = lock_acquired.wait_for(2s);
  if (lock_status != std::future_status::ready)
  {
    release.set_value();
    holding.get();
    FAIL() << "Socket lock was not acquired";
  }

  std::promise<void> started;
  auto emitting_started = started.get_future();
  auto completed = std::make_shared<std::promise<socket::ack_status>>();
  auto result = completed->get_future();
  auto emitting = std::async(
    std::launch::async,
    [&]
    {
      started.set_value();
      socket->emit("expired", json(), 10ms,
                   [completed](socket::ack_status status, message data)
                   {
                     EXPECT_TRUE(data.is_null());
                     completed->set_value(status);
                   });
    });
  const auto start_status = emitting_started.wait_for(2s);
  const auto emit_status = emitting.wait_for(100ms);
  release.set_value();
  holding.get();
  emitting.get();
  EXPECT_EQ(start_status, std::future_status::ready);
  EXPECT_EQ(emit_status, std::future_status::timeout);
  ASSERT_EQ(result.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(result.get(), socket::ack_status::timeout);
  instance.sync_close();
  EXPECT_EQ(events.load(), 0);
}
