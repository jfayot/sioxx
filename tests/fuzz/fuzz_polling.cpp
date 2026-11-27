#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "polling_protocol.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  const std::string payload(reinterpret_cast<const char*>(data), size);
  for (const auto& packet : sioxx::detail::polling_split_payload(payload))
  {
    if (packet.empty()) std::abort();

    std::string binary;
    if (!sioxx::detail::polling_decode_binary(packet, binary)) continue;
    std::string decoded;
    if (!sioxx::detail::polling_decode_binary(
          sioxx::detail::polling_encode_binary(binary), decoded) ||
        decoded != binary)
      std::abort();
  }
  return 0;
}
