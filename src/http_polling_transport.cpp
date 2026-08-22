#include "http_polling_transport.hpp"

#include <openssl/err.h>

#include <algorithm>
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
thread_local http_polling_transport* active_http_polling_transport = nullptr;

class polling_worker_scope
{
 public:
  explicit polling_worker_scope(http_polling_transport* transport)
      : previous_(active_http_polling_transport)
  {
    active_http_polling_transport = transport;
  }

  ~polling_worker_scope() { active_http_polling_transport = previous_; }

 private:
  http_polling_transport* previous_;
};

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

struct http_polling_transport::async_request_operation
    : std::enable_shared_from_this<async_request_operation>
{
  async_request_operation(std::shared_ptr<http_polling_transport> transport,
                          http::verb method, std::string target,
                          std::string body, request_handler handler)
      : transport(std::move(transport)),
        resolver(*this->transport->strand_),
        ssl_context(ssl::context::tls_client),
        request(method, std::move(target), 11),
        handler(std::move(handler))
  {
    const auto& url = this->transport->url_;
    request.set(http::field::host, detail::authority(url));
    request.set(http::field::user_agent, "sioxx-client");
    request.set(http::field::content_type, "text/plain; charset=UTF-8");
    if (this->transport->proxy_ && !url.tls &&
        !this->transport->proxy_->authorization.empty())
      request.set(http::field::proxy_authorization,
                  this->transport->proxy_->authorization);
    for (const auto& [key, value] : this->transport->extra_headers_)
      request.set(key, value);
    request.body() = std::move(body);
    request.prepare_payload();
    parser.body_limit(max_http_response_body_size);
  }

  void start()
  {
    const auto& endpoint =
      transport->proxy_ ? transport->proxy_->endpoint : transport->url_;
    resolver.async_resolve(
      endpoint.host, endpoint.port,
      [self = shared_from_this()](beast::error_code ec,
                                  const tcp::resolver::results_type& results)
      {
        if (ec) return self->finish(ec);
        self->connect(results);
      });
  }

  void connect(const tcp::resolver::results_type& results)
  {
    if (transport->url_.tls)
    {
      ssl_context.set_default_verify_paths();
      ssl_context.set_verify_mode(transport->verify_tls_ ? ssl::verify_peer
                                                         : ssl::verify_none);
      tls_stream = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(
        *transport->strand_, ssl_context);
      if (!SSL_set_tlsext_host_name(tls_stream->native_handle(),
                                    transport->url_.host.c_str()))
      {
        return finish({static_cast<int>(::ERR_get_error()),
                       net::error::get_ssl_category()});
      }
      beast::get_lowest_layer(*tls_stream)
        .async_connect(results,
                       [self = shared_from_this()](
                         beast::error_code ec,
                         const tcp::resolver::results_type::endpoint_type&)
                       {
                         if (ec) return self->finish(ec);
                         if (self->transport->proxy_)
                           self->write_proxy_connect();
                         else
                           self->handshake_tls();
                       });
      return;
    }

    plain_stream = std::make_unique<beast::tcp_stream>(*transport->strand_);
    plain_stream->async_connect(
      results,
      [self = shared_from_this()](
        beast::error_code ec, const tcp::resolver::results_type::endpoint_type&)
      {
        if (ec) return self->finish(ec);
        self->write_request();
      });
  }

  void write_proxy_connect()
  {
    const auto authority = detail::connect_authority(transport->url_);
    proxy_request = {http::verb::connect, authority, 11};
    proxy_request.set(http::field::host, authority);
    proxy_request.set(http::field::user_agent, "sioxx-client");
    if (!transport->proxy_->authorization.empty())
      proxy_request.set(http::field::proxy_authorization,
                        transport->proxy_->authorization);
    proxy_parser.skip(true);
    http::async_write(
      beast::get_lowest_layer(*tls_stream), proxy_request,
      [self = shared_from_this()](beast::error_code ec, std::size_t)
      {
        if (ec) return self->finish(ec);
        http::async_read(beast::get_lowest_layer(*self->tls_stream),
                         self->proxy_buffer, self->proxy_parser,
                         [self](beast::error_code read_ec, std::size_t)
                         {
                           if (read_ec) return self->finish(read_ec);
                           const auto status =
                             self->proxy_parser.get().result_int();
                           if (status / 100 != 2)
                           {
                             self->transport->fail(
                               "polling transport: proxy CONNECT "
                               "returned HTTP " +
                               std::to_string(status));
                             return self->finish(net::error::operation_aborted);
                           }
                           self->handshake_tls();
                         });
      });
  }

  void handshake_tls()
  {
    tls_stream->async_handshake(
      ssl::stream_base::client,
      [self = shared_from_this()](beast::error_code ec)
      {
        if (ec) return self->finish(ec);
        self->write_request();
      });
  }

  void write_request()
  {
    if (tls_stream)
      http::async_write(
        *tls_stream, request,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        {
          if (ec) return self->finish(ec);
          self->read_response();
        });
    else
      http::async_write(
        *plain_stream, request,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        {
          if (ec) return self->finish(ec);
          self->read_response();
        });
  }

  void read_response()
  {
    if (tls_stream)
      http::async_read(
        *tls_stream, buffer, parser,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        { self->complete_response(ec); });
    else
      http::async_read(
        *plain_stream, buffer, parser,
        [self = shared_from_this()](beast::error_code ec, std::size_t)
        { self->complete_response(ec); });
  }

  void complete_response(beast::error_code ec)
  {
    if (ec) return finish(ec);
    auto result = parser.release();
    finish({}, {result.result_int(), std::move(result.body())});
  }

  void cancel()
  {
    resolver.cancel();
    beast::error_code ignored;
    if (plain_stream) plain_stream->socket().close(ignored);
    if (tls_stream)
      beast::get_lowest_layer(*tls_stream).socket().close(ignored);
  }

  void finish(beast::error_code ec, response result = {})
  {
    if (finished) return;
    finished = true;
    transport->remove_external_request(this);
    auto completion = std::move(handler);
    completion(ec, std::move(result));
  }

  std::shared_ptr<http_polling_transport> transport;
  tcp::resolver resolver;
  ssl::context ssl_context;
  http::request<http::string_body> request;
  beast::flat_buffer buffer{max_http_read_buffer_size};
  http::response_parser<http::string_body> parser;
  std::unique_ptr<beast::tcp_stream> plain_stream;
  std::unique_ptr<beast::ssl_stream<beast::tcp_stream>> tls_stream;
  http::request<http::empty_body> proxy_request;
  beast::flat_buffer proxy_buffer{max_http_read_buffer_size};
  http::response_parser<http::empty_body> proxy_parser;
  request_handler handler;
  bool finished{false};
};

http_polling_transport::http_polling_transport() = default;

http_polling_transport::http_polling_transport(net::io_context& io_context)
    : io_context_(&io_context), strand_(net::make_strand(io_context))
{
}

http_polling_transport::~http_polling_transport()
{
  // close() may notify user code. Avoid invoking potentially throwing
  // callbacks from this noexcept destructor.
  closing_ = true;
  state_ = transport_state::closed;
  write_condition_.notify_all();
  join_threads();
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
  std::lock_guard<std::mutex> lock(connect_mutex_);
  if (closing_) return;
  url_ = parse_ws_url(url);
  state_ = transport_state::connecting;
  if (io_context_)
  {
    net::post(*strand_,
              [self = shared_from_this()] { self->start_external(); });
    return;
  }
  write_thread_ =
    std::thread([self = shared_from_this()] { self->run_writes(); });
  poll_thread_ = std::thread([self = shared_from_this()] { self->run(); });
}

void http_polling_transport::run()
{
  polling_worker_scope worker_scope(this);
  try
  {
    auto initial = request(http::verb::get, url_.target);
    if (initial.status / 100 != 2)
      return fail("polling handshake returned HTTP " +
                  std::to_string(initial.status));
    state_ = transport_state::open;
    if (on_open_) on_open_();
    if (closing_) return;
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
  if (io_context_)
  {
    net::post(*strand_,
              [self = shared_from_this(), payload, is_binary]
              {
                if (self->closing_ || self->state_ != transport_state::open)
                  return;
                self->write_queue_.emplace_back(payload, is_binary);
                self->start_next_external_write();
              });
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closing_ || state_ != transport_state::open) return;
    write_queue_.emplace_back(payload, is_binary);
  }
  write_condition_.notify_one();
}

void http_polling_transport::run_writes()
{
  polling_worker_scope worker_scope(this);
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

void http_polling_transport::async_request(http::verb method,
                                           std::string target, std::string body,
                                           request_handler handler)
{
  if (proxy_ && !url_.tls) target = detail::absolute_http_target(url_, target);
  auto operation = std::make_shared<async_request_operation>(
    shared_from_this(), method, std::move(target), std::move(body),
    std::move(handler));
  external_requests_.push_back(operation);
  operation->start();
}

void http_polling_transport::start_external() { poll_external(true); }

void http_polling_transport::poll_external(bool handshake)
{
  const auto target = handshake ? url_.target : poll_target();
  async_request(
    http::verb::get, target, {},
    [self = shared_from_this(), handshake](beast::error_code ec,
                                           const response& result)
    {
      if (self->closing_) return;
      if (ec) return self->fail("polling transport: " + ec.message());
      if (result.status / 100 != 2)
        return self->fail(
          std::string(handshake ? "polling handshake" : "polling request") +
          " returned HTTP " + std::to_string(result.status));
      if (handshake)
      {
        self->state_ = transport_state::open;
        if (self->on_open_) self->on_open_();
        if (self->closing_) return;
      }
      self->deliver(result.body);
      if (!self->closing_) self->poll_external(false);
    });
}

void http_polling_transport::start_next_external_write()
{
  if (external_write_in_progress_ || write_queue_.empty() || closing_) return;
  external_write_in_progress_ = true;
  auto write = std::move(write_queue_.front());
  write_queue_.pop_front();
  if (write.second) write.first = detail::polling_encode_binary(write.first);
  async_request(
    http::verb::post, poll_target(), std::move(write.first),
    [self = shared_from_this()](beast::error_code ec, const response& result)
    {
      self->external_write_in_progress_ = false;
      if (self->closing_) return;
      if (ec) return self->fail("polling write: " + ec.message());
      if (result.status / 100 != 2)
        return self->fail("polling write returned HTTP " +
                          std::to_string(result.status));
      self->start_next_external_write();
    });
}

void http_polling_transport::cancel_external_requests()
{
  for (auto& weak_request : external_requests_)
    if (auto operation = weak_request.lock()) operation->cancel();
}

void http_polling_transport::remove_external_request(
  const async_request_operation* operation)
{
  external_requests_.erase(
    std::remove_if(external_requests_.begin(), external_requests_.end(),
                   [operation](const auto& weak_request)
                   {
                     const auto active = weak_request.lock();
                     return !active || active.get() == operation;
                   }),
    external_requests_.end());
}

std::string http_polling_transport::poll_target() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return url_.target +
         (url_.target.find('?') == std::string::npos ? "?sid=" : "&sid=") +
         sid_;
}

void http_polling_transport::deliver(const std::string& body)
{
  for (const auto& packet : detail::polling_split_payload(body))
  {
    if (closing_) return;
    if (packet[0] == '0')
    {
      json handshake = json::parse(packet.substr(1), nullptr, false);
      if (!handshake.is_discarded())
      {
        std::lock_guard<std::mutex> lock(mutex_);
        sid_ = handshake.value("sid", std::string());
      }
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
  if (io_context_ && !strand_->running_in_this_thread())
  {
    net::post(*strand_, [self = shared_from_this()] { self->close(); });
    return;
  }
  bool notify_close = false;
  {
    std::lock_guard<std::mutex> lock(connect_mutex_);
    std::call_once(
      shutdown_once_,
      [this, &notify_close]
      {
        notify_close = !closing_.exchange(true);
        state_ = transport_state::closed;
        if (io_context_)
        {
          write_queue_.clear();
          cancel_external_requests();
          if (!sid_.empty())
            async_request(http::verb::post, poll_target(), "1",
                          [](beast::error_code, const response&) {});
          return;
        }
        write_condition_.notify_all();
        // Ending the Engine.IO session causes a pending poll to
        // return promptly.
        bool has_session = false;
        {
          std::lock_guard<std::mutex> queue_lock(mutex_);
          has_session = !sid_.empty();
        }
        if (has_session)
        {
          auto self = shared_from_this();
          close_thread_ =
            std::thread([self = std::move(self)] { self->post("1", false); });
        }
      });
  }

  if (notify_close && on_close_) on_close_("closed");
}

void http_polling_transport::sync_close()
{
  close();
  if (io_context_) return;
  // A polling callback cannot wait for the worker executing that callback.
  // Call close() from callbacks and reserve sync_close() for other threads.
  if (active_http_polling_transport == this) return;
  join_threads();
}

void http_polling_transport::join_thread(std::thread& thread)
{
  if (!thread.joinable()) return;
  if (thread.get_id() == std::this_thread::get_id())
    thread.detach();
  else
    thread.join();
}

void http_polling_transport::join_threads()
{
  std::lock_guard<std::mutex> lock(join_mutex_);
  join_thread(write_thread_);
  join_thread(close_thread_);
  join_thread(poll_thread_);
}

void http_polling_transport::fail(const std::string& message)
{
  if (closing_.exchange(true)) return;
  state_ = transport_state::closed;
  if (io_context_) cancel_external_requests();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    write_queue_.clear();
  }
  write_condition_.notify_all();
  if (on_error_) on_error_(message);
  if (on_close_) on_close_(message);
}

}  // namespace sioxx
