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

#pragma once

#include <functional>
#include <string>

#include "packet.hpp"

namespace sioxx
{

/** @brief Signature of the callback that writes a fully‑encoded frame. */
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
 * @note The `name()` method is used for diagnostics and for selecting a
 *       parser in logs.
 */
class parser_base
{
 public:
  /** @brief Virtual destructor. */
  virtual ~parser_base() = default;

  /**
   * @brief Encode a `packet` into an Engine.IO frame.
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
   * @param payload   Raw payload received from the transport.
   * @param is_binary `true` if the payload is binary, `false` otherwise.
   * @param out       Destination packet – filled on successful decode.
   * @return `true` if the packet was completely decoded; `false` if more
   *         data is required (e.g., waiting for binary attachments).
   */
  virtual bool decode(const std::string& payload,
                      bool is_binary,
                      packet& out) = 0;

  /**
   * @brief Human‑readable name of the parser implementation.
   * @return A string such as `"json_parser"` or `"msgpack_parser"`.
   */
  virtual std::string name() const = 0;
};

}  // namespace sioxx
