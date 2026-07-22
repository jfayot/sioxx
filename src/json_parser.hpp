#pragma once
#include "sioxx/parser.hpp"

namespace sioxx
{

// Default socket.io text protocol:
//   <packet type>[<# attachments>-][<namespace>,][<ack id>][JSON payload]
// e.g. 2["chat message","hello"]   42["ack question",1]   2/admin,3["x"]
//
// Binary attachments (binary_event/binary_ack) use a placeholder scheme in
// the reference implementation; this parser recognizes/produces the header
// correctly but does not deconstruct/reconstruct placeholder buffers. If you
// need binary payloads, prefer msgpack_parser, which carries binary data
// natively and needs no placeholder dance.
class json_parser final : public parser_base
{
 public:
  void encode(const packet& packet,
              const frame_writer& write) const override;
  bool decode(const std::string& payload, bool is_binary,
              packet& out) override;
  std::string name() const override { return "json"; }
};

}  // namespace sioxx
