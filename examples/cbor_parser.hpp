#pragma once

#include <cstdint>
#include <exception>
#include <sioxx/parser.hpp>
#include <string>
#include <vector>

// Example application-defined parser. It uses the same packet-shaped object
// as sioxx::msgpack_parser, but serializes it as CBOR. A server must use the
// corresponding CBOR parser; this is not one of Socket.IO's stock protocols.
class cbor_parser final : public sioxx::parser_base
{
 public:
  void encode(const sioxx::socketio_packet& packet,
              const sioxx::frame_writer& write) const override
  {
    sioxx::json object = sioxx::json::object();
    object["type"] = static_cast<int>(packet.type);
    object["nsp"] = packet.nsp;
    if (packet.id >= 0) object["id"] = packet.id;
    if (!packet.data.is_null()) object["data"] = packet.data;

    const std::vector<std::uint8_t> bytes = sioxx::json::to_cbor(object);
    write(
      std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
      true);
  }

  bool decode(const std::string& payload, bool is_binary,
              sioxx::socketio_packet& out) override
  {
    if (!is_binary) return false;

    try
    {
      const std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
      const sioxx::json object = sioxx::json::from_cbor(bytes);
      if (!object.is_object()) return false;

      out.type =
        static_cast<sioxx::socketio_packet_type>(object.value("type", 0));
      out.nsp = object.value("nsp", std::string("/"));
      out.id = object.contains("id") && !object["id"].is_null()
                 ? object["id"].get<int>()
                 : -1;
      out.data = object.contains("data") ? object["data"] : sioxx::json();
      out.attachments = 0;
      return true;
    }
    catch (const std::exception&)
    {
      return false;
    }
  }

  std::string name() const override { return "cbor"; }
};
