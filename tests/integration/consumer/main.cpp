#include <boost/asio/io_context.hpp>
#include <chrono>
#include <memory>
#include <sioxx/sioxx.hpp>
#include <string>

namespace
{
class smoke_parser final : public sioxx::parser_base
{
 public:
  void encode(const sioxx::packet&, const sioxx::frame_writer&) const override
  {
  }

  bool decode(const std::string&, bool, sioxx::packet&) override
  {
    return false;
  }

  std::string name() const override { return "smoke"; }
};
}  // namespace

int main()
{
  sioxx::client client;
  const auto socket = client.socket("/installed");
  socket->on("installed", [](const std::string&, sioxx::message) {});
  client.set_fail_listener([] {});
  client.set_error_listener([](const std::string&) {});
  client.set_reconnect_listener([](unsigned, unsigned) {});

  sioxx::packet event;
  event.type = sioxx::packet_type::event;
  event.data = sioxx::make_args("installed", 1);
  socket->emit("installed", event.data, std::chrono::seconds(30),
               [](sioxx::socket::ack_status, sioxx::message) {});
  client.sync_close();

  bool parser_created = false;
  sioxx::client_options options;
  options.reconnect_attempts = 2;
  options.reconnect_delay = std::chrono::milliseconds(100);
  options.reconnect_delay_max = std::chrono::seconds(1);
  options.reconnect_randomization_factor = 0.25;
  options.parser_factory = [&parser_created]
  {
    parser_created = true;
    return std::make_unique<smoke_parser>();
  };
  boost::asio::io_context context;
  sioxx::client caller_owned_client(context, options);
  caller_owned_client.sync_close();

  const bool valid =
    socket->nsp() == "/installed" && event.data.size() == 2 && parser_created;
  return valid ? 0 : 1;
}
