#include "websocket_transport.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/beast/version.hpp>
#include <memory>
#include <string>

namespace sioxx
{

websocket_transport::websocket_transport()
    : work_guard_(net::make_work_guard(ioc_)),
      resolver_(net::make_strand(ioc_)),
      ssl_ctx_(ssl::context::tlsv12_client)
{
  ssl_ctx_.set_default_verify_paths();
}

websocket_transport::~websocket_transport()
{
  close();
  work_guard_.reset();
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

void websocket_transport::fail(const std::string& what, beast::error_code ec)
{
  state_ = transport_state::closed;
  if (on_error_) on_error_(what + ": " + ec.message());
}

void websocket_transport::connect(const std::string& url)
{
  url_ = parse_ws_url(url);
  state_ = transport_state::connecting;

  if (url_.tls)
  {
    io_thread_ = std::thread([self = shared_from_this()] { self->run_tls(); });
  }
  else
  {
    io_thread_ =
      std::thread([self = shared_from_this()] { self->run_plain(); });
  }
}

void websocket_transport::run_plain()
{
  auto self = shared_from_this();
  resolver_.async_resolve(
    url_.host, url_.port,
    [this, self](beast::error_code ec, tcp::resolver::results_type results)
    {
      if (ec) return fail("resolve", ec);

      ws_plain_ = std::make_unique<websocket::stream<beast::tcp_stream>>(
        net::make_strand(ioc_));
      beast::get_lowest_layer(*ws_plain_)
        .expires_after(std::chrono::seconds(15));
      beast::get_lowest_layer(*ws_plain_)
        .async_connect(
          results,
          [this, self](beast::error_code ec2,
                       tcp::resolver::results_type::endpoint_type)
          {
            if (ec2) return fail("connect", ec2);
            beast::get_lowest_layer(*ws_plain_).expires_never();
            ws_plain_->set_option(websocket::stream_base::timeout::suggested(
              beast::role_type::client));
            ws_plain_->set_option(websocket::stream_base::decorator(
              [this](websocket::request_type& req)
              {
                req.set(
                  boost::beast::http::field::user_agent,
                  std::string(BOOST_BEAST_VERSION_STRING) + " sioxx-client");
                for (auto& [k, v] : extra_headers_) req.set(k, v);
              }));
            ws_plain_->async_handshake(url_.host, url_.target,
                                       [this, self](beast::error_code ec3)
                                       {
                                         if (ec3) return fail("handshake", ec3);
                                         ws_plain_->binary(true);
                                         state_ = transport_state::open;
                                         if (on_open_) on_open_();
                                         do_read_plain();
                                       });
          });
    });
  ioc_.run();
}

void websocket_transport::run_tls()
{
  auto self = shared_from_this();
  if (!verify_tls_) ssl_ctx_.set_verify_mode(ssl::verify_none);

  resolver_.async_resolve(
    url_.host, url_.port,
    [this, self](beast::error_code ec, tcp::resolver::results_type results)
    {
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
        [this, self](beast::error_code ec2,
                     tcp::resolver::results_type::endpoint_type)
        {
          if (ec2) return fail("connect", ec2);
          ws_tls_->next_layer().async_handshake(
            ssl::stream_base::client,
            [this, self](beast::error_code ec3)
            {
              if (ec3) return fail("ssl_handshake", ec3);
              beast::get_lowest_layer(*ws_tls_).expires_never();
              ws_tls_->set_option(websocket::stream_base::timeout::suggested(
                beast::role_type::client));
              ws_tls_->set_option(websocket::stream_base::decorator(
                [this](websocket::request_type& req)
                {
                  req.set(
                    boost::beast::http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) + " sioxx-client");
                  for (auto& [k, v] : extra_headers_) req.set(k, v);
                }));
              ws_tls_->async_handshake(url_.host, url_.target,
                                       [this, self](beast::error_code ec4)
                                       {
                                         if (ec4) return fail("handshake", ec4);
                                         ws_tls_->binary(true);
                                         state_ = transport_state::open;
                                         if (on_open_) on_open_();
                                         do_read_tls();
                                       });
            });
        });
    });
  ioc_.run();
}

void websocket_transport::do_read_plain()
{
  auto self = shared_from_this();
  ws_plain_->async_read(buffer_,
                        [this, self](beast::error_code ec, std::size_t)
                        {
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
                          std::string payload =
                            beast::buffers_to_string(buffer_.data());
                          buffer_.consume(buffer_.size());
                          if (on_message_) on_message_(payload, is_binary);
                          do_read_plain();
                        });
}

void websocket_transport::do_read_tls()
{
  auto self = shared_from_this();
  ws_tls_->async_read(buffer_,
                      [this, self](beast::error_code ec, std::size_t)
                      {
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
                        std::string payload =
                          beast::buffers_to_string(buffer_.data());
                        buffer_.consume(buffer_.size());
                        if (on_message_) on_message_(payload, is_binary);
                        do_read_tls();
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
            [this, self, payload = std::move(payload), is_binary]() mutable
            {
              bool should_start = false;
              {
                std::lock_guard<std::recursive_mutex> lock(write_mutex_);
                write_queue_.emplace_back(std::move(payload), is_binary);
                if (!write_in_progress_)
                {
                  write_in_progress_ = true;
                  should_start = true;
                }
              }
              // pump_write_queue_* takes write_mutex_ itself, so it must be
              // called only after the lock above has gone out of scope --
              // calling it while still holding the lock (as before)
              // self-deadlocks the single io_context thread on the very first
              // queued write.
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
  if (!ws_plain_)
  {
    write_in_progress_ = false;
    return;
  }
  std::string payload;
  bool is_binary;
  {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    if (write_queue_.empty())
    {
      write_in_progress_ = false;
      return;
    }
    payload = write_queue_.front().first;
    is_binary = write_queue_.front().second;
  }
  ws_plain_->binary(is_binary);
  auto self = shared_from_this();
  auto buf = std::make_shared<std::string>(std::move(payload));
  ws_plain_->async_write(
    net::buffer(*buf),
    [this, self, buf](beast::error_code ec, std::size_t)
    {
      {
        std::lock_guard<std::recursive_mutex> lock(write_mutex_);
        if (!write_queue_.empty()) write_queue_.pop_front();
      }
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
  if (!ws_tls_)
  {
    write_in_progress_ = false;
    return;
  }
  std::string payload;
  bool is_binary;
  {
    std::lock_guard<std::recursive_mutex> lock(write_mutex_);
    if (write_queue_.empty())
    {
      write_in_progress_ = false;
      return;
    }
    payload = write_queue_.front().first;
    is_binary = write_queue_.front().second;
  }
  ws_tls_->binary(is_binary);
  auto self = shared_from_this();
  auto buf = std::make_shared<std::string>(std::move(payload));
  ws_tls_->async_write(
    net::buffer(*buf),
    [this, self, buf](beast::error_code ec, std::size_t)
    {
      {
        std::lock_guard<std::recursive_mutex> lock(write_mutex_);
        if (!write_queue_.empty()) write_queue_.pop_front();
      }
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
  if (closing_.exchange(true)) return;
  state_ = transport_state::closing;
  auto self = shared_from_this();
  net::post(ioc_,
            [this, self]()
            {
              beast::error_code ec;
              if (ws_tls_ && ws_tls_->is_open())
              {
                ws_tls_->close(websocket::close_code::normal, ec);
              }
              else if (ws_plain_ && ws_plain_->is_open())
              {
                ws_plain_->close(websocket::close_code::normal, ec);
              }
              state_ = transport_state::closed;
            });
}

}  // namespace sioxx
