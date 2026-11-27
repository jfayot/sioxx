#include "polling_protocol.hpp"

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

bool base64_decode(const std::string& input, std::string& out)
{
  if (input.size() % 4 != 0) return false;
  out.clear();
  for (size_t i = 0; i < input.size(); i += 4)
  {
    int a = base64_value(input[i]), b = base64_value(input[i + 1]);
    int c = input[i + 2] == '=' ? 0 : base64_value(input[i + 2]);
    int d = input[i + 3] == '=' ? 0 : base64_value(input[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return false;
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

bool detail::polling_decode_binary(const std::string& packet,
                                   std::string& payload)
{
  return !packet.empty() && packet[0] == 'b' &&
         base64_decode(packet.substr(1), payload);
}

std::vector<std::string> detail::polling_split_payload(
  const std::string& payload)
{
  std::vector<std::string> packets;
  size_t begin = 0;
  while (begin <= payload.size())
  {
    const size_t separator = payload.find('\x1e', begin);
    const size_t end =
      separator == std::string::npos ? payload.size() : separator;
    if (end > begin) packets.push_back(payload.substr(begin, end - begin));
    if (separator == std::string::npos) break;
    begin = separator + 1;
  }
  return packets;
}

}  // namespace sioxx
