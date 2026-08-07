#include "sioxx/client.hpp"

#include <random>
#include <stdexcept>
#include <thread>

#include "client_impl.hpp"
#include "engineio_url.hpp"
#include "http_polling_transport.hpp"
#include "json_parser.hpp"
#include "msgpack_parser.hpp"
#include "websocket_transport.hpp"

namespace sioxx
{

client_impl::client_impl(client_options options) : options_(std::move(options))
{
  if (options_.parser_factory)
  {
    parser_ = options_.parser_factory();
    if (!parser_)
      throw std::invalid_argument(
        "client_options::parser_factory returned null");
  }
  else if (options_.parser == parser_kind::msgpack)
  {
    parser_ = std::make_unique<msgpack_parser>();
  }
  else
  {
    parser_ = std::make_unique<json_parser>();
  }
}

void client_impl::connect(const std::string& uri)
{
  base_uri_ = uri;
  intentional_close_ = false;
  using_polling_ = options_.force_http_polling;
  reconnect_attempts_used_ = 0;
  ensure_engineio();
  engineio_->open(
    detail::build_engineio_url(uri, options_.engineio_path, options_.query,
                               using_polling_ ? "polling" : "websocket"));
}

void client_impl::ensure_engineio()
{
  engineio_ = std::make_shared<engineio_client>();
  std::shared_ptr<transport_base> transport;
  if (using_polling_)
  {
    auto polling = std::make_shared<http_polling_transport>();
    polling->set_verify_tls(options_.verify_tls);
    if (!options_.extra_headers.empty())
      polling->set_extra_headers(options_.extra_headers);
    transport = std::move(polling);
  }
  else
  {
    auto websocket = std::make_shared<websocket_transport>();
    websocket->set_verify_tls(options_.verify_tls);
    if (!options_.extra_headers.empty())
      websocket->set_extra_headers(options_.extra_headers);
    transport = std::move(websocket);
  }
  engineio_->set_transport(transport);

  engineio_->on_open([self = shared_from_this()] { self->on_engineio_open(); });
  engineio_->on_close([self = shared_from_this()](const std::string& reason)
                      { self->on_engineio_close(reason); });
  engineio_->on_frame(
    [self = shared_from_this()](const std::string& payload, bool is_binary)
    { self->on_engineio_frame(payload, is_binary); });
  engineio_->on_error(
    [self = shared_from_this()](const std::string& msg)
    {
      if (!self->using_polling_ && !self->engineio_->is_open())
      {
        self->activate_polling_fallback();
      }
      if (self->on_error_) self->on_error_(msg);
    });
}

void client_impl::activate_polling_fallback()
{
  if (intentional_close_ || using_polling_) return;
  using_polling_ = true;
  if (on_error_)
    on_error_("WebSocket connection failed; switching to HTTP long-polling");
  ensure_engineio();
  engineio_->open(detail::build_engineio_url(base_uri_, options_.engineio_path,
                                             options_.query, "polling"));
}

void client_impl::on_engineio_open()
{
  // Auto-CONNECT every namespace socket that's already been requested.
  std::lock_guard<std::mutex> lock(sockets_mutex_);
  for (auto& [nsp, sock] : sockets_)
  {
    sock->connect();
  }
}

void client_impl::on_engineio_close(const std::string& reason)
{
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [nsp, sock] : sockets_) sock->mark_connected(false);
  }
  if (on_close_) on_close_(reason);

  if (!intentional_close_ && options_.reconnect_attempts > 0)
  {
    schedule_reconnect();
  }
}

void client_impl::schedule_reconnect()
{
  if (reconnect_attempts_used_ >= options_.reconnect_attempts)
  {
    if (on_fail_) on_fail_();
    return;
  }
  ++reconnect_attempts_used_;
  auto self = shared_from_this();
  thread_local std::mt19937 jitter_engine{std::random_device{}()};
  std::uniform_real_distribution<double> jitter_distribution(0.0, 1.0);
  auto delay = reconnect_delay_for_attempt(
    options_.reconnect_delay, options_.reconnect_delay_max,
    options_.reconnect_randomization_factor, reconnect_attempts_used_,
    jitter_distribution(jitter_engine));
  auto uri = base_uri_;
  std::thread(
    [self, delay, uri]
    {
      std::this_thread::sleep_for(delay);
      if (self->intentional_close_) return;
      self->ensure_engineio();
      self->engineio_->open(detail::build_engineio_url(
        uri, self->options_.engineio_path, self->options_.query,
        self->using_polling_ ? "polling" : "websocket"));
    })
    .detach();
}

void client_impl::on_engineio_frame(const std::string& payload, bool is_binary)
{
  packet packet;
  if (!parser_->decode(payload, is_binary, packet)) return;

  std::shared_ptr<sioxx::socket> sock;
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    auto it = sockets_.find(packet.nsp);
    if (it != sockets_.end()) sock = it->second;
  }

  switch (packet.type)
  {
  case packet_type::connect:
    if (sock)
    {
      sock->mark_connected(true);
    }
    if (packet.nsp == "/" && on_open_) on_open_();
    break;

  case packet_type::disconnect:
    if (sock) sock->mark_connected(false);
    break;

  case packet_type::connect_error:
    if (sock) sock->dispatch_event("connect_error", packet.data);
    break;

  case packet_type::event:
  case packet_type::binary_event:
  {
    if (!sock || !packet.data.is_array() || packet.data.empty()) break;
    std::string event_name =
      packet.data.at(0).is_string() ? packet.data.at(0).get<std::string>() : "";
    json args = json::array();
    for (size_t i = 1; i < packet.data.size(); ++i)
      args.push_back(packet.data[i]);
    sock->dispatch_event(event_name, std::move(args));
    break;
  }

  case packet_type::ack:
  case packet_type::binary_ack:
    if (sock && packet.id >= 0) sock->dispatch_ack(packet.id, packet.data);
    break;
  }
}

std::shared_ptr<sioxx::socket> client_impl::socket(const std::string& nsp,
                                                   message auth)
{
  std::string norm_nsp = nsp.empty() ? "/" : nsp;
  std::lock_guard<std::mutex> lock(sockets_mutex_);
  auto it = sockets_.find(norm_nsp);
  if (it != sockets_.end())
  {
    if (!auth.is_null()) it->second->set_auth(std::move(auth));
    return it->second;
  }

  auto sock = std::make_shared<sioxx::socket>(weak_from_this(), norm_nsp,
                                              std::move(auth));
  sockets_[norm_nsp] = sock;
  if (engineio_ && engineio_->is_open()) sock->connect();
  return sock;
}

void client_impl::send_packet(const packet& packet)
{
  if (!engineio_) return;
  parser_->encode(packet, [this](const std::string& payload, bool is_binary)
                  { engineio_->send(payload, is_binary); });
}

void client_impl::close()
{
  intentional_close_ = true;
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [nsp, sock] : sockets_)
    {
      if (sock->connected()) sock->disconnect();
    }
  }
  if (engineio_) engineio_->close();
}

client::client(client_options options)
    : impl_(std::make_shared<client_impl>(std::move(options)))
{
}

client::~client() = default;

void client::connect(const std::string& uri) { impl_->connect(uri); }

void client::close() { impl_->close(); }

std::shared_ptr<sioxx::socket> client::socket(const std::string& nsp,
                                              message auth)
{
  return impl_->socket(nsp, std::move(auth));
}

void client::set_open_listener(std::function<void()> h)
{
  impl_->set_open_handler(std::move(h));
}

void client::set_close_listener(std::function<void(const std::string&)> h)
{
  impl_->set_close_handler(std::move(h));
}

void client::set_fail_listener(std::function<void()> h)
{
  impl_->set_fail_handler(std::move(h));
}

void client::set_error_listener(std::function<void(const std::string&)> h)
{
  impl_->set_error_handler(std::move(h));
}

}  // namespace sioxx
