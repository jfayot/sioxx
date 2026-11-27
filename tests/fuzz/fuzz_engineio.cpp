#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "engineio_client.hpp"

namespace
{
class in_memory_transport final : public sioxx::transport_base
{
 public:
  void connect(const std::string&) override {}
  void send(const std::string&, bool) override {}
  void close() override {}
  void sync_close() override {}

  void receive(const std::string& frame, bool is_binary)
  {
    on_message_(frame, is_binary);
  }
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  if (size == 0) return 0;
  const auto transport = std::make_shared<in_memory_transport>();
  const auto client = std::make_shared<sioxx::engineio_client>();
  client->set_transport(transport);
  transport->receive(
    std::string(reinterpret_cast<const char*>(data + 1), size - 1),
    (data[0] & 1) != 0);
  client->sync_close();
  return 0;
}
