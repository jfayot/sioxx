#include "msgpack_parser.hpp"

#include "bounded_json.hpp"

namespace sioxx
{

void msgpack_parser::encode(const packet& packet,
                            const frame_writer& write) const
{
  json obj = json::object();
  obj["type"] = static_cast<int>(packet.type);
  obj["nsp"] = packet.nsp;
  if (packet.id >= 0) obj["id"] = packet.id;
  if (!packet.data.is_null()) obj["data"] = packet.data;

  std::vector<uint8_t> bytes = json::to_msgpack(obj);
  write(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        true);
}

bool msgpack_parser::decode(const std::string& payload, bool is_binary,
                            packet& out)
{
  if (!is_binary) return false;
  try
  {
    const auto obj =
      detail::parse_bounded_json(payload, json::input_format_t::msgpack);
    if (obj.is_discarded()) return false;
    out.type = static_cast<packet_type>(obj.value("type", 0));
    out.nsp = obj.value("nsp", std::string("/"));
    out.id =
      obj.contains("id") && !obj["id"].is_null() ? obj["id"].get<int>() : -1;
    out.data = obj.contains("data") ? obj["data"] : json();
    out.attachments = 0;
    return true;
  }
  catch (const std::exception&)
  {
    return false;
  }
}

}  // namespace sioxx
