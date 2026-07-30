#ifndef SIOXX_SRC_POLLING_PROTOCOL_HPP
#define SIOXX_SRC_POLLING_PROTOCOL_HPP

#include <string>
#include <vector>

namespace sioxx::detail
{

// Engine.IO polling represents binary packets as `b` followed by base64.
std::string polling_encode_binary(const std::string& payload);
bool polling_decode_binary(const std::string& packet, std::string& payload);

// Engine.IO separates multiple packets in one polling payload with ASCII RS.
std::vector<std::string> polling_split_payload(const std::string& payload);

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_POLLING_PROTOCOL_HPP
