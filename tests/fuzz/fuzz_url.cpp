#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "engineio_url.hpp"
#include "url_parse.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size)
{
  const std::string uri(reinterpret_cast<const char*>(data), size);
  try
  {
    sioxx::parse_ws_url(uri);
  }
  catch (const std::invalid_argument&)
  {
    // The raw URL parser requires a scheme separator.
  }

  sioxx::parse_ws_url(sioxx::detail::build_engineio_url(
    uri, "/socket.io/", {{"value", uri}}, "websocket"));
  return 0;
}
