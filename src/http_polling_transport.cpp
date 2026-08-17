#include "http_polling_transport.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cstdint>
#include <stdexcept>

#include "polling_protocol.hpp"

namespace sioxx
{
namespace http = boost::beast::http;
namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace
{
constexpr std::uint64_t max_http_response_body_size = 8 * 1024 * 1024;
constexpr std::size_t max_http_read_buffer_size = 64 * 1024;

template <typename Stream, typename Body>
void read_http_response(Stream& stream, beast::flat_buffer& buffer,
                        http::response_parser<Body>& parser)
{
  while (!parser.is_done())
  {
    beast::error_code ec;
    const auto bytes_consumed = http::read_some(stream, buffer, parser, ec);
    if (ec) throw beast::system_error(ec);
    if (bytes_consumed == 0 && !parser.is_done())
      throw std::runtime_error("HTTP response parser made no progress");
  }
}

void establish_proxy_tunnel(beast::tcp_stream& stream,
                            const url_parts& destination,
                            const detail::proxy_config& proxy)
{
  const auto destination_authority = detail::connect_authority(destination);
  http::request<http::empty_body> request{http::verb::connect,
                                          destination_authority, 11};
  request.set(http::field::host, destination_authority);
  request.set(http::field::user_agent, "sioxx-client");
  if (!proxy.authorization.empty())
    request.set(http::field::proxy_authorization, proxy.authorization);
  http::write(stream, request);

  beast::flat_buffer buffer{max_http_read_buffer_size};
  http::response_parser<http::empty_body> parser;
  parser.skip(true);
  read_http_response(stream, buffer, parser);
  if (parser.get().result_int() / 100 != 2)
    throw std::runtime_error("proxy CONNECT returned HTTP " +
                             std::to_string(parser.get().result_int()));
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
  return "b" + detail::base64_encode(payload);
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

http_polling_transport::http_polling_transport() = default;

http_polling_transport::~http_polling_transport()
{
  // close() may notify user code. Avoid invoking potentially throwing
  // callbacks from this noexcept destructor.
  closing_ = true;
  state_ = transport_state::closed;
  write_condition_.notify_all();
  join_write_thread();
  if (poll_thread_.joinable())
  {
    if (poll_thread_.get_id() == std::this_thread::get_id())
      poll_thread_.detach();
    else
      poll_thread_.join();
  }
  if (close_thread_.joinable()) close_thread_.join();
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

void http_polling_transport::set_proxy(const std::string& uri,
                                       const std::string& username,
                                       const std::string& password)
{
  proxy_ = detail::make_proxy_config(uri, username, password);
}

void http_polling_transport::connect(const std::string& url)
{
  url_ = parse_ws_url(url);
  closing_ = false;
  state_ = transport_state::connecting;
  write_thread_ = std::thread([this] { run_writes(); });
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_ || state_ != transport_state::open) return;
    write_queue_.emplace_back(payload, is_binary);
  }
  write_condition_.notify_one();
}

void http_polling_transport::run_writes()
{
  while (true)
  {
    std::pair<std::string, bool> write;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      write_condition_.wait(
        lock, [this] { return closing_ || !write_queue_.empty(); });
      if (write_queue_.empty()) return;
      write = std::move(write_queue_.front());
      write_queue_.pop_front();
    }
    post(std::move(write.first), write.second);
  }
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
  const auto& endpoint = proxy_ ? proxy_->endpoint : url_;
  auto endpoints = resolver.resolve(endpoint.host, endpoint.port);
  const auto request_target =
    proxy_ && !url_.tls ? detail::absolute_http_target(url_, target) : target;
  http::request<http::string_body> req{method, request_target, 11};
  req.set(http::field::host, detail::authority(url_));
  req.set(http::field::user_agent, "sioxx-client");
  req.set(http::field::content_type, "text/plain; charset=UTF-8");
  if (proxy_ && !url_.tls && !proxy_->authorization.empty())
    req.set(http::field::proxy_authorization, proxy_->authorization);
  for (const auto& [key, value] : extra_headers_) req.set(key, value);
  req.body() = body;
  req.prepare_payload();
  beast::flat_buffer buffer{max_http_read_buffer_size};
  http::response_parser<http::string_body> parser;
  parser.body_limit(max_http_response_body_size);

  if (!url_.tls)
  {
    beast::tcp_stream stream(ioc);
    stream.connect(endpoints);
    http::write(stream, req);
    read_http_response(stream, buffer, parser);
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    if (ec && ec != net::error::not_connected) throw beast::system_error(ec);
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
    if (proxy_)
      establish_proxy_tunnel(beast::get_lowest_layer(stream), url_, *proxy_);
    stream.handshake(ssl::stream_base::client);
    http::write(stream, req);
    read_http_response(stream, buffer, parser);
    beast::error_code ec;
    stream.shutdown(ec);
    if (ec == net::error::eof || ec == ssl::error::stream_truncated) ec = {};
    if (ec) throw beast::system_error(ec);
  }
  auto res = parser.release();
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
  for (const auto& packet : detail::polling_split_payload(body))
  {
    if (packet[0] == '0')
    {
      json handshake = json::parse(packet.substr(1), nullptr, false);
      if (!handshake.is_discarded())
        sid_ = handshake.value("sid", std::string());
    }
    if (packet[0] == 'b')
    {
      std::string decoded;
      if (detail::polling_decode_binary(packet, decoded) && on_message_)
        on_message_(decoded, true);
    }
    else if (on_message_)
    {
      on_message_(packet, false);
    }
  }
}

void http_polling_transport::close()
{
  if (closing_.exchange(true)) return;
  state_ = transport_state::closed;
  write_condition_.notify_all();
  join_write_thread();
  // Ending the Engine.IO session causes a pending poll to return promptly.
  if (!sid_.empty())
  {
    close_thread_ = std::thread([this] { post("1", false); });
  }
  if (close_thread_.joinable()) close_thread_.join();
  if (poll_thread_.joinable() &&
      poll_thread_.get_id() != std::this_thread::get_id())
    poll_thread_.join();
  if (on_close_) on_close_("closed");
}

void http_polling_transport::join_write_thread()
{
  if (!write_thread_.joinable()) return;
  if (write_thread_.get_id() == std::this_thread::get_id())
    write_thread_.detach();
  else
    write_thread_.join();
}

void http_polling_transport::fail(const std::string& message)
{
  if (closing_.exchange(true)) return;
  state_ = transport_state::closed;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    write_queue_.clear();
  }
  write_condition_.notify_all();
  if (on_error_) on_error_(message);
  if (on_close_) on_close_(message);
}

}  // namespace sioxx
