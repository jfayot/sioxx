#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "json_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  sioxx::json_parser parser;
  sioxx::packet packet;

  // Each frame starts with a binary flag and a two-byte big-endian length.
  constexpr std::size_t header_size = 3;
  while (size >= header_size)
  {
    const bool is_binary = (data[0] & 1) != 0;
    const std::size_t declared_size = (std::size_t{data[1]} << 8) | data[2];
    const auto payload_size = std::min(declared_size, size - header_size);
    const auto* payload = reinterpret_cast<const char*>(data + header_size);
    parser.decode(std::string(payload, payload_size), is_binary, packet);
    data += header_size + payload_size;
    size -= header_size + payload_size;
  }

  // A fresh text packet ends any incomplete binary assembly.
  if (!parser.decode(R"(2["probe",42])", false, packet) ||
      packet.type != sioxx::packet_type::event ||
      packet.data != sioxx::json::array({"probe", 42}))
    std::abort();

  return 0;
}
