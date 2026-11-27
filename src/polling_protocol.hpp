#ifndef SIOXX_SRC_POLLING_PROTOCOL_HPP
#define SIOXX_SRC_POLLING_PROTOCOL_HPP

#include <string>
#include <string_view>

namespace sioxx::detail
{

// Engine.IO polling represents binary packets as `b` followed by base64.
std::string polling_encode_binary(const std::string& payload);
bool polling_decode_binary(std::string_view packet, std::string& payload);

// Advance past the next nonempty packet and return a view into the response.
// An empty view signals that no packets remain.
std::string_view polling_next_packet(std::string_view& remaining);

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_POLLING_PROTOCOL_HPP
