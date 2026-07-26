/**
 * @file message.hpp
 * @brief JSON‑based payload representation used throughout the library.
 *
 * The original `socket.io-client-cpp` defined a hierarchy of `sio::message`
 * types.  sioxx simply aliases `nlohmann::json` – which already supports all
 * required JSON primitives **and** binary blobs (`json::binary_t`).
 */
#pragma once

#include <nlohmann/json.hpp>
#include <vector>

namespace sioxx
{

/** @brief Alias for the JSON type used for every payload. */
using json = nlohmann::json;

/** @brief Alias for a single Socket.IO payload (formerly `sio::message`). */
using message = json;

/** @brief Alias for an array of arguments passed to `emit`. */
using message_list = json;  ///< always a JSON array

/**
 * @brief Create an empty argument list (`[]`).
 * @return An empty `message_list`.
 */
inline message_list make_args() { return json::array(); }

/**
 * @brief Create a `message_list` from a variadic pack of values.
 *
 * Each argument is converted to `json` via the constructor, preserving types.
 *
 * @tparam Args   Types of the arguments.
 * @param args   Arguments to be packed.
 * @return A JSON array containing the supplied values.
 */
template <typename... Args> inline message_list make_args(Args&&... args)
{
  json arr = json::array();
  (arr.push_back(json(std::forward<Args>(args))), ...);
  return arr;
}

/**
 * @brief Construct a binary payload from raw memory.
 *
 * The returned JSON value holds a `binary_t` which maps directly onto
 * MessagePack’s `bin` type when using the `msgpack` parser.
 *
 * @param data  Pointer to the first byte.
 * @param len   Number of bytes.
 * @return A `message` containing the binary data.
 */
inline message binary_message(const uint8_t* data, size_t len)
{
  return json::binary(std::vector<uint8_t>(data, data + len));
}

/**
 * @brief Construct a binary payload from a `std::vector<uint8_t>`.
 *
 * @param bytes  Vector that will be moved into the JSON binary value.
 * @return A `message` containing the binary data.
 */
inline message binary_message(std::vector<uint8_t> bytes)
{
  return json::binary(std::move(bytes));
}

}  // namespace sioxx
