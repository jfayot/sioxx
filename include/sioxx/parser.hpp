#pragma once
// sioxx/parser.hpp
//
// Pluggable encode/decode strategy for the socket.io packet protocol.
// Two implementations are provided: json_parser (the default text protocol,
// socket.io-parser compatible) and msgpack_parser (binary protocol,
// socket.io-msgpack-parser compatible). Both sit on top of engine.io, which
// only knows how to move opaque text/binary frames around.

#include <functional>
#include <string>

#include "packet.hpp"

namespace sioxx
{

// write_frame(payload, is_binary) is how a parser hands a fully-encoded
// engine.io frame back to the caller (engineio_client), which forwards it to
// the transport.
using frame_writer =
  std::function<void(const std::string& payload, bool is_binary)>;

class parser_base
{
 public:
  virtual ~parser_base() = default;

  virtual void encode(const packet& packet,
                      const frame_writer& write) const = 0;

  // Decode a single engine.io payload frame. Returns true if `out` was
  // fully populated (json_parser is single-frame per packet as long as
  // there are no binary attachments; msgpack_parser is always single-frame).
  virtual bool decode(const std::string& payload, bool is_binary,
                      packet& out) = 0;

  virtual std::string name() const = 0;
};

}  // namespace sioxx
