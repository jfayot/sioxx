#pragma once
// sioxx/message.hpp
//
// socket.io payloads are just JSON values (arrays/objects/strings/numbers/
// binary), so instead of re-implementing sio::message's class hierarchy we
// use nlohmann::json directly as the wire value type. nlohmann::json already
// supports null/bool/int/double/string/array/object *and* raw binary blobs
// (json::binary_t), which the msgpack parser maps straight onto MessagePack
// bin types. This keeps the API small while staying fully compatible with
// how the original socket.io-client-cpp used sio::message.

#include <nlohmann/json.hpp>
#include <vector>

namespace sioxx
{

using json = nlohmann::json;

// A single socket.io payload value (what the old library called sio::message).
using message = json;

// A list of arguments passed to socket.emit(event, arg1, arg2, ...).
using message_list = json;  // always a JSON array

inline message_list make_args() { return json::array(); }

template <typename... Args> inline message_list make_args(Args&&... args)
{
  json arr = json::array();
  (arr.push_back(json(std::forward<Args>(args))), ...);
  return arr;
}

inline message binary_message(const uint8_t* data, size_t len)
{
  return json::binary(std::vector<uint8_t>(data, data + len));
}

inline message binary_message(std::vector<uint8_t> bytes)
{
  return json::binary(std::move(bytes));
}

}  // namespace sioxx
