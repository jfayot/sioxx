#pragma once
#include "sioxx/parser.hpp"

namespace sioxx
{

// socket.io-msgpack-parser compatible binary protocol: each packet is a
// single MessagePack-encoded map { type, nsp, data, id? } sent as one
// binary engine.io frame. Binary values nested in `data` (json::binary_t)
// round-trip natively through MessagePack's bin type -- no placeholder
// reconstruction step is needed, unlike the JSON parser.
//
// Implemented on top of nlohmann::json's built-in to_msgpack/from_msgpack,
// so no extra msgpack library dependency is required.
class msgpack_parser final : public parser_base
{
 public:
  void encode(const packet& packet,
              const frame_writer& write) const override;
  bool decode(const std::string& payload, bool is_binary,
              packet& out) override;
  std::string name() const override { return "msgpack"; }
};

}  // namespace sioxx
