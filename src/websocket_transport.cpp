#include "websocket_transport.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <future>
#include <memory>
#include <string>

namespace sioxx
{

namespace
{
thread_local websocket_transport* active_websocket_transport = nullptr;
}

websocket_transport::websocket_transport()
    : work_guard_(net::make_work_guard(ioc_)),
      resolver_(net::make_strand(ioc_)),
      ssl_ctx_(ssl::context::tlsv12_client)
{
  ssl_ctx_.set_default_verify_paths();
}

websocket_transport::~websocket_transport()
{
  // Do not call close() here: it uses shared_from_this(), which is invalid
  // once destruction has started. Destructors must also not invoke user
  // callbacks, since an exception escaping a destructor terminates the
  // process.
  closing_ = true;
  state_ = transport_state::closed;
  resolver_.cancel();
  work_guard_.reset();
  ioc_.stop();
  if (io_thread_.joinable())
  {
    if (io_thread_.get_id() == std::this_thread::get_id())
      io_thread_.detach();
    else
      io_thread_.join();
  }
}

void websocket_transport::set_extra_headers(
  std::vector<std::pair<std::string, std::string>> headers)
{
  extra_headers_ = std::move(headers);
}

void websocket_transport::set_verify_tls(bool verify)
{
  verify_tls_ = verify;
  ssl_ctx_.set_verify_mode(verify ? ssl::verify_peer : ssl::verify_none);
}

void websocket_transport::set_proxy(const std::string& uri,
                                    const std::string& username,
                                    const std::string& password)
{
  proxy_ = detail::make_proxy_config(uri, username, password);
}

void websocket_transport::fail(const std::string& what)
{
  if (closing_) return;
  state_ = transport_state::closed;
  if (on_error_) on_error_(what);
}

void websocket_transport::fail(const std::string& what, beast::error_code ec)
{
  if (closing_) return;
  state_ = transport_state::closed;
  if (on_error_) on_error_(what + ": " + ec.message());
}

void websocket_transport::connect(const std::string& url)
{
  std::promise<void> start_signal;
  auto start = start_signal.get_future();
  {
    std::lock_guard<std::mutex> lock(connect_mutex_);
    if (closing_) return;
    url_ = parse_ws_url(url);
    state_ = transport_state::connecting;
    const bool use_tls = url_.tls;
    io_thread_ = std::thread(
      [self = shared_from_this(), start = std::move(start), use_tls]() mutable
      {
        start.wait();
        if (use_tls)
          self->run_tls();
        else
          self->run_plain();
      });
  }
  start_signal.set_value();
}

void websocket_transport::run_plain()
{
  active_websocket_transport = this;
  auto self = shared_from_this();
  const auto& endpoint = proxy_ ? proxy_->endpoint : url_;
  resolver_.async_resolve(
    endpoint.host, endpoint.port,
    [this, self = std::move(self)](beast::error_code ec,
                                   const tcp::resolver::results_type& results)
    {
      if (closing_) return;
      if (ec) return fail("resolve", ec);

      ws_plain_ = std::make_unique<websocket::stream<beast::tcp_stream>>(
        net::make_strand(ioc_));
      beast::get_lowest_layer(*ws_plain_)
        .expires_after(std::chrono::seconds(15));
      beast::get_lowest_layer(*ws_plain_)
        .async_connect(results,
                       [this, self = std::move(self)](
                         beast::error_code ec2,
                         const tcp::resolver::results_type::endpoint_type&)
                       {
                         if (closing_) return;
                         if (ec2) return fail("connect", ec2);
                         beast::get_lowest_layer(*ws_plain_).expires_never();
                         if (proxy_)
                           establish_proxy_tunnel([this, self = std::move(self)]
                                                  { start_plain_handshake(); });
                         else
                           start_plain_handshake();
                       });
    });
  ioc_.run();
  if (closing_)
  {
    ioc_.restart();
    ioc_.poll();
  }
  active_websocket_transport = nullptr;
}

void websocket_transport::run_tls()
{
  active_websocket_transport = this;
  auto self = shared_from_this();
  if (!verify_tls_) ssl_ctx_.set_verify_mode(ssl::verify_none);

  const auto& endpoint = proxy_ ? proxy_->endpoint : url_;
  resolver_.async_resolve(
    endpoint.host, endpoint.port,
    [this, self = std::move(self)](beast::error_code ec,
                                   const tcp::resolver::results_type& results)
    {
      if (closing_) return;
      if (ec) return fail("resolve", ec);

      ws_tls_ = std::make_unique<
        websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(
        net::make_strand(ioc_), ssl_ctx_);

      if (!SSL_set_tlsext_host_name(ws_tls_->next_layer().native_handle(),
                                    url_.host.c_str()))
      {
        beast::error_code sni_ec{static_cast<int>(::ERR_get_error()),
                                 net::error::get_ssl_category()};
        return fail("sni", sni_ec);
      }

      beast::get_lowest_layer(*ws_tls_).expires_after(std::chrono::seconds(15));
      beast::get_lowest_layer(*ws_tls_).async_connect(
        results,
        [this, self = std::move(self)](
          beast::error_code ec2,
          const tcp::resolver::results_type::endpoint_type&)
        {
          if (closing_) return;
          if (ec2) return fail("connect", ec2);
          if (proxy_)
            establish_proxy_tunnel([this, self = std::move(self)]
                                   { start_tls_handshake(); });
          else
            start_tls_handshake();
        });
    });
  ioc_.run();
  if (closing_)
  {
    ioc_.restart();
    ioc_.poll();
  }
  active_websocket_transport = nullptr;
}

void websocket_transport::establish_proxy_tunnel(
  std::function<void()> continuation)
{
  namespace http = boost::beast::http;
  const auto destination = detail::connect_authority(url_);
  proxy_request_ = {http::verb::connect, destination, 11};
  proxy_request_.set(http::field::host, destination);
  proxy_request_.set(http::field::user_agent, "sioxx-client");
  if (!proxy_->authorization.empty())
    proxy_request_.set(http::field::proxy_authorization, proxy_->authorization);
  proxy_response_.skip(true);

  auto& stream = url_.tls ? beast::get_lowest_layer(*ws_tls_)
                          : beast::get_lowest_layer(*ws_plain_);
  auto self = shared_from_this();
  http::async_write(
    stream, proxy_request_,
    [this, self = std::move(self), continuation = std::move(continuation)](
      beast::error_code ec, std::size_t) mutable
    {
      if (closing_) return;
      if (ec) return fail("proxy write", ec);
      auto& read_stream = url_.tls ? beast::get_lowest_layer(*ws_tls_)
                                   : beast::get_lowest_layer(*ws_plain_);
      http::async_read(
        read_stream, proxy_buffer_, proxy_response_,
        [this, self = std::move(self), continuation = std::move(continuation)](
          beast::error_code read_ec, std::size_t) mutable
        {
          if (closing_) return;
          if (read_ec) return fail("proxy read", read_ec);
          const auto status = proxy_response_.get().result_int();
          if (status / 100 != 2)
            return fail("proxy CONNECT returned HTTP " +
                        std::to_string(status));
          continuation();
        });
    });
}

void websocket_transport::start_plain_handshake()
{
  ws_plain_->set_option(
    websocket::stream_base::timeout::suggested(beast::role_type::client));
  ws_plain_->set_option(websocket::stream_base::decorator(
    [this](websocket::request_type& req)
    {
      req.set(boost::beast::http::field::user_agent,
              std::string(BOOST_BEAST_VERSION_STRING) + " sioxx-client");
      for (auto& [k, v] : extra_headers_) req.set(k, v);
    }));
  auto self = shared_from_this();
  ws_plain_->async_handshake(
    url_.host, url_.target,
    [this, self = std::move(self)](beast::error_code ec)
    {
      if (closing_) return;
      if (ec) return fail("handshake", ec);
      ws_plain_->binary(true);
      state_ = transport_state::open;
      if (on_open_) on_open_();
      if (!closing_) do_read_plain();
    });
}

void websocket_transport::start_tls_handshake()
{
  auto self = shared_from_this();
  ws_tls_->next_layer().async_handshake(
    ssl::stream_base::client,
    [this, self = std::move(self)](beast::error_code ec)
    {
      if (closing_) return;
      if (ec) return fail("ssl_handshake", ec);
      beast::get_lowest_layer(*ws_tls_).expires_never();
      ws_tls_->set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::client));
      ws_tls_->set_option(websocket::stream_base::decorator(
        [this](websocket::request_type& req)
        {
          req.set(boost::beast::http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) + " sioxx-client");
          for (auto& [k, v] : extra_headers_) req.set(k, v);
        }));
      auto handshake_self = shared_from_this();
      ws_tls_->async_handshake(
        url_.host, url_.target,
        [this, self = std::move(handshake_self)](beast::error_code ws_ec)
        {
          if (closing_) return;
          if (ws_ec) return fail("handshake", ws_ec);
          ws_tls_->binary(true);
          state_ = transport_state::open;
          if (on_open_) on_open_();
          if (!closing_) do_read_tls();
        });
    });
}

void websocket_transport::do_read_plain()
{
  auto self = shared_from_this();
  ws_plain_->async_read(
    buffer_,
    [this, self = std::move(self)](beast::error_code ec, std::size_t)
    {
      if (closing_) return;
      if (ec)
      {
        state_ = transport_state::closed;
        if (!closing_.load())
        {
          if (on_close_) on_close_(ec.message());
        }
        return;
      }
      bool is_binary = ws_plain_->got_binary();
      std::string payload = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_message_) on_message_(payload, is_binary);
      if (!closing_) do_read_plain();
    });
}

void websocket_transport::do_read_tls()
{
  auto self = shared_from_this();
  ws_tls_->async_read(
    buffer_,
    [this, self = std::move(self)](beast::error_code ec, std::size_t)
    {
      if (closing_) return;
      if (ec)
      {
        state_ = transport_state::closed;
        if (!closing_.load())
        {
          if (on_close_) on_close_(ec.message());
        }
        return;
      }
      bool is_binary = ws_tls_->got_binary();
      std::string payload = beast::buffers_to_string(buffer_.data());
      buffer_.consume(buffer_.size());
      if (on_message_) on_message_(payload, is_binary);
      if (!closing_) do_read_tls();
    });
}

void websocket_transport::send(const std::string& payload, bool is_binary)
{
  queue_write(payload, is_binary);
}

void websocket_transport::queue_write(std::string payload, bool is_binary)
{
  auto self = shared_from_this();
  net::post(ioc_,
            [this, self = std::move(self), payload = std::move(payload),
             is_binary]() mutable
            {
              if (closing_) return;
              write_queue_.emplace_back(std::move(payload), is_binary);
              const bool should_start = !write_in_progress_;
              write_in_progress_ = true;
              if (should_start)
              {
                if (url_.tls)
                  pump_write_queue_tls();
                else
                  pump_write_queue_plain();
              }
            });
}

void websocket_transport::pump_write_queue_plain()
{
  if (closing_) return;
  if (!ws_plain_)
  {
    write_in_progress_ = false;
    return;
  }
  std::string payload;
  bool is_binary;
  if (write_queue_.empty())
  {
    write_in_progress_ = false;
    return;
  }
  payload = std::move(write_queue_.front().first);
  is_binary = write_queue_.front().second;
  ws_plain_->binary(is_binary);
  auto self = shared_from_this();
  auto buf = std::make_shared<std::string>(std::move(payload));
  auto write_buffer = net::buffer(*buf);
  ws_plain_->async_write(write_buffer,
                         [this, self = std::move(self), buf = std::move(buf)](
                           beast::error_code ec, std::size_t)
                         {
                           if (closing_) return;
                           if (!write_queue_.empty()) write_queue_.pop_front();
                           if (ec)
                           {
                             fail("write", ec);
                             write_in_progress_ = false;
                             return;
                           }
                           pump_write_queue_plain();
                         });
}

void websocket_transport::pump_write_queue_tls()
{
  if (closing_) return;
  if (!ws_tls_)
  {
    write_in_progress_ = false;
    return;
  }
  std::string payload;
  bool is_binary;
  if (write_queue_.empty())
  {
    write_in_progress_ = false;
    return;
  }
  payload = std::move(write_queue_.front().first);
  is_binary = write_queue_.front().second;
  ws_tls_->binary(is_binary);
  auto self = shared_from_this();
  auto buf = std::make_shared<std::string>(std::move(payload));
  auto write_buffer = net::buffer(*buf);
  ws_tls_->async_write(write_buffer,
                       [this, self = std::move(self), buf = std::move(buf)](
                         beast::error_code ec, std::size_t)
                       {
                         if (closing_) return;
                         if (!write_queue_.empty()) write_queue_.pop_front();
                         if (ec)
                         {
                           fail("write", ec);
                           write_in_progress_ = false;
                           return;
                         }
                         pump_write_queue_tls();
                       });
}

void websocket_transport::close()
{
  const bool on_io_thread = active_websocket_transport == this;
  std::lock_guard<std::mutex> lock(connect_mutex_);
  std::call_once(shutdown_once_,
                 [this, on_io_thread]
                 {
                   closing_ = true;
                   state_ = transport_state::closing;
                   if (!io_thread_.joinable() || on_io_thread)
                     shutdown_on_io_thread();
                   else
                     net::post(ioc_, [this] { shutdown_on_io_thread(); });
                 });
}

void websocket_transport::sync_close()
{
  close();
  // A callback already running on the I/O thread cannot join its own thread.
  // Call close() from callbacks and reserve sync_close() for other threads.
  if (active_websocket_transport == this) return;

  join_io_thread();
}

void websocket_transport::shutdown_on_io_thread()
{
  resolver_.cancel();
  write_queue_.clear();
  write_in_progress_ = false;
  if (ws_tls_)
  {
    beast::get_lowest_layer(*ws_tls_).close();
  }
  if (ws_plain_)
  {
    beast::get_lowest_layer(*ws_plain_).close();
  }
  work_guard_.reset();
  state_ = transport_state::closed;
  ioc_.stop();
}

void websocket_transport::join_io_thread()
{
  std::lock_guard<std::mutex> lock(join_mutex_);
  if (!io_thread_.joinable()) return;
  io_thread_.join();

  // stop() leaves cancellation completions queued. Drain them before
  // destroying either WebSocket stream; Beast requires all asynchronous
  // operations to complete before their stream is destroyed.
  ioc_.restart();
  ioc_.poll();
  ws_tls_.reset();
  ws_plain_.reset();
}

}  // namespace sioxx
