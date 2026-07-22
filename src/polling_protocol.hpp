#pragma once

#include <string>

namespace sioxx::detail
{

// Engine.IO polling represents binary packets as `b` followed by base64.
std::string polling_encode_binary(const std::string& payload);
bool polling_decode_binary(const std::string& packet, std::string& payload);

}  // namespace sioxx::detail
