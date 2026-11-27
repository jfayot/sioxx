#include "polling_protocol.hpp"

#include <algorithm>

#include "proxy.hpp"

namespace sioxx
{
namespace
{
int base64_value(char c)
{
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

bool base64_decode(std::string_view input, std::string& out)
{
  if (input.size() % 4 != 0) return false;
  out.clear();
  for (size_t i = 0; i < input.size(); i += 4)
  {
    int a = base64_value(input[i]), b = base64_value(input[i + 1]);
    int c = input[i + 2] == '=' ? 0 : base64_value(input[i + 2]);
    int d = input[i + 3] == '=' ? 0 : base64_value(input[i + 3]);
    if (std::min({a, b, c, d}) < 0) return false;
    const auto value =
      (static_cast<unsigned>(a) << 18) | (static_cast<unsigned>(b) << 12) |
      (static_cast<unsigned>(c) << 6) | static_cast<unsigned>(d);
    out += static_cast<char>((value >> 16) & 0xff);
    if (input[i + 2] != '=') out += static_cast<char>((value >> 8) & 0xff);
    if (input[i + 3] != '=') out += static_cast<char>(value & 0xff);
  }
  return true;
}
}  // namespace

std::string detail::polling_encode_binary(const std::string& payload)
{
  return "b" + base64_encode(payload);
}

bool detail::polling_decode_binary(std::string_view packet,
                                   std::string& payload)
{
  return !packet.empty() && packet[0] == 'b' &&
         base64_decode(packet.substr(1), payload);
}

std::string_view detail::polling_next_packet(std::string_view& remaining)
{
  constexpr char separator = '\x1e';  // Engine.IO uses ASCII Record Separator.
  const auto begin = remaining.find_first_not_of(separator);
  if (begin == std::string_view::npos)
  {
    remaining = {};
    return {};
  }

  remaining.remove_prefix(begin);
  const auto end = remaining.find(separator);
  const auto packet = remaining.substr(0, end);
  remaining.remove_prefix(end == std::string_view::npos ? remaining.size()
                                                        : end + 1);
  return packet;
}

}  // namespace sioxx
