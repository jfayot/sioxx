#include "json_parser.hpp"

#include <cctype>

namespace sioxx
{

void json_parser::encode(const packet& packet,
                         const frame_writer& write) const
{
  std::string out;
  out += std::to_string(static_cast<int>(packet.type));

  if (packet.type == packet_type::binary_event ||
      packet.type == packet_type::binary_ack)
  {
    out += std::to_string(packet.attachments);
    out += '-';
  }

  if (!packet.nsp.empty() && packet.nsp != "/")
  {
    out += packet.nsp;
    out += ',';
  }

  if (packet.id >= 0)
  {
    out += std::to_string(packet.id);
  }

  if (!packet.data.is_null())
  {
    out += packet.data.dump();
  }

  write(out, false);
}

bool json_parser::decode(const std::string& payload, bool is_binary,
                         packet& out)
{
  if (is_binary || payload.empty()) return false;

  size_t i = 0;
  if (!std::isdigit(static_cast<unsigned char>(payload[i]))) return false;
  int type = payload[i] - '0';
  if (type < 0 || type > 6) return false;
  out.type = static_cast<packet_type>(type);
  ++i;

  out.attachments = 0;
  if (out.type == packet_type::binary_event ||
      out.type == packet_type::binary_ack)
  {
    size_t dash = payload.find('-', i);
    if (dash == std::string::npos) return false;
    out.attachments = std::stoi(payload.substr(i, dash - i));
    i = dash + 1;
  }

  if (i < payload.size() && payload[i] == '/')
  {
    size_t comma = payload.find(',', i);
    if (comma == std::string::npos)
    {
      out.nsp = payload.substr(i);
      i = payload.size();
    }
    else
    {
      out.nsp = payload.substr(i, comma - i);
      i = comma + 1;
    }
  }
  else
  {
    out.nsp = "/";
  }

  size_t id_start = i;
  while (i < payload.size() &&
         std::isdigit(static_cast<unsigned char>(payload[i])))
    ++i;
  out.id =
    (i > id_start) ? std::stoi(payload.substr(id_start, i - id_start)) : -1;

  if (i < payload.size())
  {
    json parsed = json::parse(payload.substr(i), nullptr, false);
    if (parsed.is_discarded()) return false;
    out.data = std::move(parsed);
  }
  else
  {
    out.data = json();
  }

  return true;
}

}  // namespace sioxx
