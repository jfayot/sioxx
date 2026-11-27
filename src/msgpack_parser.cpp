#include "msgpack_parser.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace sioxx
{
namespace
{
constexpr std::size_t max_msgpack_depth = 128;

class msgpack_dom final : public json::json_sax_t
{
 public:
  explicit msgpack_dom(json& root) : root_(root) {}

  bool null() override { return scalar(nullptr); }
  bool boolean(bool value) override { return scalar(value); }
  bool number_integer(number_integer_t value) override { return scalar(value); }
  bool number_unsigned(number_unsigned_t value) override
  {
    return scalar(value);
  }
  bool number_float(number_float_t value, const string_t&) override
  {
    return scalar(value);
  }
  bool string(string_t& value) override { return scalar(std::move(value)); }
  bool binary(binary_t& value) override { return scalar(std::move(value)); }

  bool start_object(std::size_t) override
  {
    return start_container(json::value_t::object);
  }
  bool start_array(std::size_t) override
  {
    return start_container(json::value_t::array);
  }
  bool end_object() override { return end_container(); }
  bool end_array() override { return end_container(); }

  bool key(string_t& name) override
  {
    member_ = &(*containers_.back())[std::move(name)];
    return true;
  }

  bool parse_error(std::size_t, const string_t&,
                   const json::exception&) override
  {
    return false;
  }

 private:
  json& append(json value)
  {
    if (containers_.empty()) return root_ = std::move(value);
    auto& parent = *containers_.back();
    if (parent.is_array()) return parent.emplace_back(std::move(value));
    return *member_ = std::move(value);
  }

  bool scalar(json value)
  {
    append(std::move(value));
    return true;
  }

  bool start_container(json::value_t type)
  {
    if (containers_.size() == max_msgpack_depth) return false;
    containers_.push_back(&append(json(type)));
    return true;
  }

  bool end_container()
  {
    containers_.pop_back();
    return true;
  }

  json& root_;
  std::vector<json*> containers_;
  json* member_{nullptr};
};
}  // namespace

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
    json obj;
    msgpack_dom decoder(obj);
    if (!json::sax_parse(payload.begin(), payload.end(), &decoder,
                         json::input_format_t::msgpack))
      return false;
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
