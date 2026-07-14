#include "sioxx/http_polling_transport.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <stdexcept>

#include "sioxx/polling_protocol.hpp"

namespace sioxx
{
namespace http = boost::beast::http;
namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace
{
constexpr char base64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& input)
{
  std::string out;
  out.reserve((input.size() + 2) / 3 * 4);
  for (size_t i = 0; i < input.size(); i += 3)
  {
    unsigned value = static_cast<unsigned char>(input[i]) << 16;
    if (i + 1 < input.size())
      value |= static_cast<unsigned char>(input[i + 1]) << 8;
    if (i + 2 < input.size()) value |= static_cast<unsigned char>(input[i + 2]);
    out += base64[(value >> 18) & 63];
    out += base64[(value >> 12) & 63];
    out += i + 1 < input.size() ? base64[(value >> 6) & 63] : '=';
    out += i + 2 < input.size() ? base64[value & 63] : '=';
  }
  return out;
}

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
    unsigned value = (a << 18) | (b << 12) | (c << 6) | d;
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

http_polling_transport::http_polling_transport() = default;

http_polling_transport::~http_polling_transport()
{
  close();
  if (poll_thread_.joinable())
  {
    if (poll_thread_.get_id() == std::this_thread::get_id())
      poll_thread_.detach();
    else
      poll_thread_.join();
  }
}

void http_polling_transport::set_extra_headers(
  std::vector<std::pair<std::string, std::string>> headers)
{
  extra_headers_ = std::move(headers);
}

void http_polling_transport::set_verify_tls(bool verify)
{
  verify_tls_ = verify;
}

void http_polling_transport::connect(const std::string& url)
{
  url_ = parse_ws_url(url);
  closing_ = false;
  state_ = transport_state::connecting;
  poll_thread_ = std::thread([self = shared_from_this()] { self->run(); });
}

void http_polling_transport::run()
{
  try
  {
    auto initial = request(http::verb::get, url_.target);
    if (initial.status / 100 != 2)
      return fail("polling handshake returned HTTP " +
                  std::to_string(initial.status));
    state_ = transport_state::open;
    if (on_open_) on_open_();
    deliver(initial.body);

    while (!closing_)
    {
      auto next = request(http::verb::get, poll_target());
      if (closing_) break;
      if (next.status / 100 != 2)
        return fail("polling request returned HTTP " +
                    std::to_string(next.status));
      deliver(next.body);
    }
  }
  catch (const std::exception& e)
  {
    if (!closing_) fail(std::string("polling transport: ") + e.what());
  }
}

void http_polling_transport::send(const std::string& payload, bool is_binary)
{
  if (closing_ || state_ != transport_state::open) return;
  std::thread([self = shared_from_this(), payload, is_binary]
              { self->post(payload, is_binary); })
    .detach();
}

void http_polling_transport::post(std::string payload, bool is_binary)
{
  try
  {
    if (is_binary) payload = detail::polling_encode_binary(payload);
    auto result = request(http::verb::post, poll_target(), payload);
    if (result.status / 100 != 2 && !closing_)
      fail("polling write returned HTTP " + std::to_string(result.status));
  }
  catch (const std::exception& e)
  {
    if (!closing_) fail(std::string("polling write: ") + e.what());
  }
}

http_polling_transport::response http_polling_transport::request(
  http::verb method, const std::string& target, const std::string& body)
{
  net::io_context ioc;
  tcp::resolver resolver(ioc);
  auto endpoints = resolver.resolve(url_.host, url_.port);
  http::request<http::string_body> req{method, target, 11};
  req.set(http::field::host, url_.host);
  req.set(http::field::user_agent, "sioxx-client");
  req.set(http::field::content_type, "text/plain; charset=UTF-8");
  for (const auto& [key, value] : extra_headers_) req.set(key, value);
  req.body() = body;
  req.prepare_payload();
  beast::flat_buffer buffer;
  http::response<http::string_body> res;

  if (!url_.tls)
  {
    beast::tcp_stream stream(ioc);
    stream.connect(endpoints);
    http::write(stream, req);
    http::read(stream, buffer, res);
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
  }
  else
  {
    ssl::context ctx(ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(verify_tls_ ? ssl::verify_peer : ssl::verify_none);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), url_.host.c_str()))
      throw std::runtime_error("unable to set TLS server name");
    beast::get_lowest_layer(stream).connect(endpoints);
    stream.handshake(ssl::stream_base::client);
    http::write(stream, req);
    http::read(stream, buffer, res);
    beast::error_code ec;
    stream.shutdown(ec);
  }
  return {res.result_int(), std::move(res.body())};
}

std::string http_polling_transport::poll_target() const
{
  return url_.target +
         (url_.target.find('?') == std::string::npos ? "?sid=" : "&sid=") +
         sid_;
}

void http_polling_transport::deliver(const std::string& body)
{
  if (body.empty()) return;
  if (body[0] == '0')
  {
    json handshake = json::parse(body.substr(1), nullptr, false);
    if (!handshake.is_discarded()) sid_ = handshake.value("sid", std::string());
  }
  if (body[0] == 'b')
  {
    std::string decoded;
    if (detail::polling_decode_binary(body, decoded) && on_message_)
      on_message_(decoded, true);
  }
  else if (on_message_)
  {
    on_message_(body, false);
  }
}

void http_polling_transport::close()
{
  if (closing_.exchange(true)) return;
  state_ = transport_state::closed;
  // Ending the Engine.IO session causes a pending poll to return promptly.
  // weak_from_this() is empty when called from the final destructor, where a
  // best-effort network close is no longer safe to schedule.
  if (!sid_.empty())
  {
    if (auto self = weak_from_this().lock())
      std::thread([self] { self->post("1", false); }).detach();
  }
  if (on_close_) on_close_("closed");
}

void http_polling_transport::fail(const std::string& message)
{
  state_ = transport_state::closed;
  if (on_error_) on_error_(message);
  if (on_close_) on_close_(message);
}

}  // namespace sioxx
