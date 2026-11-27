#include <cstddef>
#include <cstdint>
#include <string>

#include "msgpack_parser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  sioxx::msgpack_parser parser;
  sioxx::packet packet;
  parser.decode(std::string(reinterpret_cast<const char*>(data), size), true,
                packet);
  return 0;
}
