#ifndef SIOXX_SRC_BOUNDED_JSON_HPP
#define SIOXX_SRC_BOUNDED_JSON_HPP

#include <cstddef>
#include <sioxx/message.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace sioxx::detail
{

inline constexpr std::size_t max_json_depth = 128;

class bounded_json_dom final : public json::json_sax_t
{
 public:
  explicit bounded_json_dom(json& root) : root_(root) {}

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
    if (containers_.size() == max_json_depth) return false;
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

inline json parse_bounded_json(
  std::string_view payload,
  json::input_format_t format = json::input_format_t::json)
{
  json result;
  bounded_json_dom decoder(result);
  if (!json::sax_parse(payload.begin(), payload.end(), &decoder, format))
    return json(json::value_t::discarded);
  return result;
}

}  // namespace sioxx::detail

#endif  // SIOXX_SRC_BOUNDED_JSON_HPP
