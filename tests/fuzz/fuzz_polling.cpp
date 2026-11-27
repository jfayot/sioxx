#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "polling_protocol.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  std::string_view remaining(reinterpret_cast<const char*>(data), size);
  while (!remaining.empty())
  {
    const auto previous_size = remaining.size();
    const auto packet = sioxx::detail::polling_next_packet(remaining);
    if (remaining.size() >= previous_size) std::abort();

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
