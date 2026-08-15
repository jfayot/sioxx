#include "json_parser.hpp"

#include <cctype>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace sioxx
{
namespace
{

void deconstruct_binary(json& value, std::vector<std::string>& attachments)
{
  if (value.is_binary())
  {
    const auto& binary = value.get_binary();
    std::string attachment;
    attachment.reserve(binary.size());
    for (const auto byte : binary)
      attachment.push_back(static_cast<char>(byte));
    const auto index = attachments.size();
    attachments.push_back(std::move(attachment));
    value = json{{"_placeholder", true}, {"num", index}};
    return;
  }

  if (!value.is_array() && !value.is_object()) return;
  for (auto& child : value) deconstruct_binary(child, attachments);
}

bool parse_nonnegative_int(const std::string& value, size_t begin, size_t end,
                           int& out)
{
  if (begin == end) return false;
  int result = 0;
  for (size_t i = begin; i < end; ++i)
  {
    const auto character = static_cast<unsigned char>(value[i]);
    if (!std::isdigit(character)) return false;
    const int digit = value[i] - '0';
    if (result > (std::numeric_limits<int>::max() - digit) / 10) return false;
    result = result * 10 + digit;
  }
  out = result;
  return true;
}

bool collect_placeholder_indices(const json& value, int attachment_count,
                                 std::set<std::uint64_t>& indices)
{
  if (value.is_object() && value.contains("_placeholder") &&
      value["_placeholder"].is_boolean() && value["_placeholder"].get<bool>())
  {
    if (!value.contains("num") || (!value["num"].is_number_integer() &&
                                   !value["num"].is_number_unsigned()))
      return false;

    std::uint64_t index;
    if (value["num"].is_number_unsigned())
    {
      index = value["num"].get<std::uint64_t>();
    }
    else
    {
      const auto signed_index = value["num"].get<std::int64_t>();
      if (signed_index < 0) return false;
      index = static_cast<std::uint64_t>(signed_index);
    }
    if (index >= static_cast<std::uint64_t>(attachment_count)) return false;
    return indices.insert(index).second;
  }

  if (!value.is_array() && !value.is_object()) return true;
  for (const auto& child : value)
  {
    if (!collect_placeholder_indices(child, attachment_count, indices))
      return false;
  }
  return true;
}

bool has_valid_placeholders(const json& data, int attachment_count)
{
  std::set<std::uint64_t> indices;
  return collect_placeholder_indices(data, attachment_count, indices) &&
         indices.size() == static_cast<size_t>(attachment_count);
}

void reconstruct_binary(json& value,
                        const std::vector<std::string>& attachments)
{
  if (value.is_object() && value.contains("_placeholder") &&
      value["_placeholder"].is_boolean() && value["_placeholder"].get<bool>())
  {
    const auto index = value["num"].get<size_t>();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(attachments[index].size());
    for (const auto byte : attachments[index])
      bytes.push_back(static_cast<unsigned char>(byte));
    value = json::binary(std::move(bytes));
    return;
  }

  if (!value.is_array() && !value.is_object()) return;
  for (auto& child : value) reconstruct_binary(child, attachments);
}

}  // namespace

void json_parser::encode(const packet& packet, const frame_writer& write) const
{
  sioxx::packet encoded = packet;
  std::vector<std::string> attachments;
  if (encoded.type == packet_type::event ||
      encoded.type == packet_type::binary_event ||
      encoded.type == packet_type::ack ||
      encoded.type == packet_type::binary_ack)
    deconstruct_binary(encoded.data, attachments);

  if (attachments.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw std::length_error("too many Socket.IO binary attachments");

  if (attachments.empty())
  {
    if (encoded.type == packet_type::binary_event)
      encoded.type = packet_type::event;
    else if (encoded.type == packet_type::binary_ack)
      encoded.type = packet_type::ack;
  }
  else
  {
    if (encoded.type == packet_type::event)
      encoded.type = packet_type::binary_event;
    else if (encoded.type == packet_type::ack)
      encoded.type = packet_type::binary_ack;
  }
  encoded.attachments = static_cast<int>(attachments.size());

  std::string out;
  out += std::to_string(static_cast<int>(encoded.type));

  if (encoded.type == packet_type::binary_event ||
      encoded.type == packet_type::binary_ack)
  {
    out += std::to_string(encoded.attachments);
    out += '-';
  }

  if (!encoded.nsp.empty() && encoded.nsp != "/")
  {
    out += encoded.nsp;
    out += ',';
  }

  if (encoded.id >= 0)
  {
    out += std::to_string(encoded.id);
  }

  if (!encoded.data.is_null())
  {
    out += encoded.data.dump();
  }

  write(out, false);
  for (const auto& attachment : attachments) write(attachment, true);
}

bool json_parser::decode(const std::string& payload, bool is_binary,
                         packet& out)
{
  if (pending_binary_packet_)
  {
    if (is_binary)
    {
      pending_attachments_.push_back(payload);
      if (pending_attachments_.size() <
          static_cast<size_t>(pending_binary_packet_->attachments))
        return false;

      reconstruct_binary(pending_binary_packet_->data, pending_attachments_);
      out = std::move(*pending_binary_packet_);
      pending_binary_packet_.reset();
      pending_attachments_.clear();
      return true;
    }

    pending_binary_packet_.reset();
    pending_attachments_.clear();
  }

  if (is_binary || payload.empty()) return false;

  size_t i = 0;
  if (!std::isdigit(static_cast<unsigned char>(payload[i]))) return false;
  int type = payload[i] - '0';
  if (type < 0 || type > 6) return false;
  packet decoded;
  decoded.type = static_cast<packet_type>(type);
  ++i;

  decoded.attachments = 0;
  if (decoded.type == packet_type::binary_event ||
      decoded.type == packet_type::binary_ack)
  {
    size_t dash = payload.find('-', i);
    if (dash == std::string::npos) return false;
    if (!parse_nonnegative_int(payload, i, dash, decoded.attachments) ||
        decoded.attachments == 0)
      return false;
    i = dash + 1;
  }

  if (i < payload.size() && payload[i] == '/')
  {
    size_t comma = payload.find(',', i);
    if (comma == std::string::npos)
    {
      decoded.nsp = payload.substr(i);
      i = payload.size();
    }
    else
    {
      decoded.nsp = payload.substr(i, comma - i);
      i = comma + 1;
    }
  }
  else
  {
    decoded.nsp = "/";
  }

  size_t id_start = i;
  while (i < payload.size() &&
         std::isdigit(static_cast<unsigned char>(payload[i])))
    ++i;
  if (i > id_start)
  {
    if (!parse_nonnegative_int(payload, id_start, i, decoded.id)) return false;
  }
  else
  {
    decoded.id = -1;
  }

  if (i < payload.size())
  {
    json parsed = json::parse(payload.substr(i), nullptr, false);
    if (parsed.is_discarded()) return false;
    decoded.data = std::move(parsed);
  }
  else
  {
    decoded.data = json();
  }

  if (decoded.type == packet_type::binary_event ||
      decoded.type == packet_type::binary_ack)
  {
    if (!has_valid_placeholders(decoded.data, decoded.attachments))
      return false;
    pending_binary_packet_ = std::move(decoded);
    return false;
  }

  out = std::move(decoded);
  return true;
}

}  // namespace sioxx
