/**
 * @file parser.hpp
 * @brief Abstract interface for encoding and decoding Socket.IO packets.
 *
 * Two concrete parsers are provided in the library:
 * - `json_parser`  – implements the official text protocol.
 * - `msgpack_parser` – implements the binary MessagePack protocol.
 *
 * The parser is selected via `client_options::parser` or a custom
 * `parser_factory`.
 */

#ifndef SIOXX_PARSER_HPP
#define SIOXX_PARSER_HPP

#include <functional>
#include <string>

#include "packet.hpp"

namespace sioxx
{

/** @brief Writes an encoded Socket.IO payload to Engine.IO.
 *
 * Pass `true` for a binary frame and `false` for a text frame. Do not add
 * Engine.IO framing to the payload.
 */
using frame_writer =
  std::function<void(const std::string& payload, bool is_binary)>;

/**
 * @class parser_base
 * @brief Pure virtual base class that all parsers must derive from.
 *
 * The parser receives a `packet` and a `frame_writer`.  It must:
 *   - Encode a packet into one (or more) Engine.IO frames.
 *   - Decode a single Engine.IO payload back into a `packet`.
 *
 * A client owns the parser returned by its factory. If an implementation
 * shares state between encode() and decode(), it must synchronize access
 * because sending and receiving can run on different threads.
 */
class parser_base
{
 public:
  /** @brief Virtual destructor. */
  virtual ~parser_base() = default;

  /**
   * @brief Encode a `packet` into one or more Engine.IO payloads.
   *
   * Invoke `write` synchronously during encode(). The writer and packet
   * references are valid for the duration of the call.
   *
   * @param packet   Packet to encode.
   * @param write    Callback that forwards the encoded payload to the
   *                 transport layer.  The second argument indicates whether
   *                 the frame is binary (`true`) or text (`false`).
   */
  virtual void encode(const packet& packet,
                      const frame_writer& write) const = 0;

  /**
   * @brief Decode a single Engine.IO payload.
   *
   * @param payload   Socket.IO payload with Engine.IO framing removed.
   * @param is_binary `true` if the payload is binary, `false` otherwise.
   * @param out       Destination packet – filled on successful decode.
   * @return `true` when `out` contains a complete packet. Return `false` to
   *         discard an invalid payload or wait for more frames. The client
   *         ignores `out` when this function returns `false`.
   */
  virtual bool decode(const std::string& payload, bool is_binary,
                      packet& out) = 0;

  /**
   * @brief Human‑readable name of the parser implementation.
   * @return A string such as `"json_parser"` or `"msgpack_parser"`.
   */
  virtual std::string name() const = 0;
};

}  // namespace sioxx

#endif  // SIOXX_PARSER_HPP
