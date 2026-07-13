#include "sioxx/socketio_client.hpp"

#include <thread>

#include "sioxx/json_parser.hpp"
#include "sioxx/msgpack_parser.hpp"
#include "sioxx/websocket_transport.hpp"

namespace sioxx
{

socketio_client_impl::socketio_client_impl(client_options options)
    : options_(std::move(options))
{
  if (options_.parser == parser_kind::msgpack)
  {
    parser_ = std::make_unique<msgpack_parser>();
  }
  else
  {
    parser_ = std::make_unique<json_parser>();
  }
}

std::string socketio_client_impl::build_engineio_url(
  const std::string& uri) const
{
  std::string url = uri;
  size_t scheme_end = url.find("://");
  std::string scheme =
    scheme_end == std::string::npos ? "" : url.substr(0, scheme_end);
  if (scheme == "http")
    url = "ws" + url.substr(4);
  else if (scheme == "https")
    url = "wss" + url.substr(5);
  else if (scheme != "ws" && scheme != "wss")
    url = "ws://" + url;

  // Strip any existing query string / trailing slash quirks then append
  // the standard engine.io handshake query.
  size_t query_pos = url.find('?');
  std::string base =
    (query_pos == std::string::npos) ? url : url.substr(0, query_pos);
  if (!base.empty() && base.back() == '/') base.pop_back();

  base += "/socket.io/?EIO=4&transport=websocket";
  return base;
}

void socketio_client_impl::connect(const std::string& uri)
{
  base_uri_ = uri;
  intentional_close_ = false;
  reconnect_attempts_used_ = 0;
  ensure_engineio();
  engineio_->open(build_engineio_url(uri));
}

void socketio_client_impl::ensure_engineio()
{
  engineio_ = std::make_shared<engineio_client>();
  auto transport = std::make_shared<websocket_transport>();
  transport->set_verify_tls(options_.verify_tls);
  if (!options_.extra_headers.empty())
    transport->set_extra_headers(options_.extra_headers);
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
      if (self->on_error_) self->on_error_(msg);
    });
}

void socketio_client_impl::on_engineio_open()
{
  // Auto-CONNECT every namespace socket that's already been requested.
  std::lock_guard<std::mutex> lock(sockets_mutex_);
  for (auto& [nsp, sock] : sockets_)
  {
    sock->connect();
  }
}

void socketio_client_impl::on_engineio_close(const std::string& reason)
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

void socketio_client_impl::schedule_reconnect()
{
  if (reconnect_attempts_used_ >= options_.reconnect_attempts)
  {
    if (on_fail_) on_fail_();
    return;
  }
  ++reconnect_attempts_used_;
  auto self = shared_from_this();
  auto delay = options_.reconnect_delay;
  auto uri = base_uri_;
  std::thread(
    [self, delay, uri]
    {
      std::this_thread::sleep_for(delay);
      if (self->intentional_close_) return;
      self->ensure_engineio();
      self->engineio_->open(self->build_engineio_url(uri));
    })
    .detach();
}

void socketio_client_impl::on_engineio_frame(const std::string& payload,
                                             bool is_binary)
{
  socketio_packet packet;
  if (!parser_->decode(payload, is_binary, packet)) return;

  std::shared_ptr<socketio_socket> sock;
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    auto it = sockets_.find(packet.nsp);
    if (it != sockets_.end()) sock = it->second;
  }

  switch (packet.type)
  {
  case socketio_packet_type::connect:
    if (sock)
    {
      sock->mark_connected(true);
    }
    if (packet.nsp == "/" && on_open_) on_open_();
    break;

  case socketio_packet_type::disconnect:
    if (sock) sock->mark_connected(false);
    break;

  case socketio_packet_type::connect_error:
    if (sock) sock->dispatch_event("connect_error", packet.data);
    break;

  case socketio_packet_type::event:
  case socketio_packet_type::binary_event:
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

  case socketio_packet_type::ack:
  case socketio_packet_type::binary_ack:
    if (sock && packet.id >= 0) sock->dispatch_ack(packet.id, packet.data);
    break;
  }
}

std::shared_ptr<socketio_socket> socketio_client_impl::socket(
  const std::string& nsp)
{
  std::string norm_nsp = nsp.empty() ? "/" : nsp;
  std::lock_guard<std::mutex> lock(sockets_mutex_);
  auto it = sockets_.find(norm_nsp);
  if (it != sockets_.end()) return it->second;

  auto sock = std::make_shared<socketio_socket>(weak_from_this(), norm_nsp);
  sockets_[norm_nsp] = sock;
  if (engineio_ && engineio_->is_open()) sock->connect();
  return sock;
}

void socketio_client_impl::send_packet(const socketio_packet& packet)
{
  if (!engineio_) return;
  parser_->encode(packet, [this](const std::string& payload, bool is_binary)
                  { engineio_->send(payload, is_binary); });
}

void socketio_client_impl::close()
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
    : impl_(std::make_shared<socketio_client_impl>(std::move(options)))
{
}

}  // namespace sioxx
