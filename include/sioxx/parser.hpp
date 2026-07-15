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

#include "message.hpp"

namespace sioxx
{

enum class engineio_packet_type : int
{
  open = 0,
  close = 1,
  ping = 2,
  pong = 3,
  message = 4,
  upgrade = 5,
  noop = 6
};

enum class socketio_packet_type : int
{
  connect = 0,
  disconnect = 1,
  event = 2,
  ack = 3,
  connect_error = 4,
  binary_event = 5,
  binary_ack = 6
};

struct socketio_packet
{
  socketio_packet_type type{socketio_packet_type::event};
  std::string nsp{"/"};
  int id{-1};  // -1 => no ack id requested/present
  json data;   // array for event/ack, object for connect/connect_error
  int attachments{
    0};  // binary_event/binary_ack attachment count (json_parser only)
};

// write_frame(payload, is_binary) is how a parser hands a fully-encoded
// engine.io frame back to the caller (engineio_client), which forwards it to
// the transport.
using frame_writer =
  std::function<void(const std::string& payload, bool is_binary)>;

class parser_base
{
 public:
  virtual ~parser_base() = default;

  virtual void encode(const socketio_packet& packet,
                      const frame_writer& write) const = 0;

  // Decode a single engine.io payload frame. Returns true if `out` was
  // fully populated (json_parser is single-frame per packet as long as
  // there are no binary attachments; msgpack_parser is always single-frame).
  virtual bool decode(const std::string& payload, bool is_binary,
                      socketio_packet& out) = 0;

  virtual std::string name() const = 0;
};

}  // namespace sioxx
