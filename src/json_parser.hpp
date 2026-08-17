#ifndef SIOXX_SRC_JSON_PARSER_HPP
#define SIOXX_SRC_JSON_PARSER_HPP
#include <optional>
#include <vector>

#include "sioxx/parser.hpp"

namespace sioxx
{

// Default socket.io text protocol:
//   <packet type>[<# attachments>-][<namespace>,][<ack id>][JSON payload]
// e.g. 2["chat message","hello"]   42["ack question",1]   2/admin,3["x"]
//
// Binary attachments are recursively replaced with placeholders in the text
// header and emitted as subsequent binary frames. Decoding retains a binary
// packet header until every attachment has arrived, then reconstructs the
// original nested payload.
class json_parser final : public parser_base
{
 public:
  void encode(const packet& packet, const frame_writer& write) const override;
  bool decode(const std::string& payload, bool is_binary, packet& out) override;
  std::string name() const override { return "json"; }

 private:
  std::optional<packet> pending_binary_packet_;
  std::vector<std::string> pending_attachments_;
};

}  // namespace sioxx

#endif  // SIOXX_SRC_JSON_PARSER_HPP
