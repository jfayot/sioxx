#include "sioxx/client.hpp"

#include <exception>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

#include "client_impl.hpp"
#include "engineio_url.hpp"
#include "http_polling_transport.hpp"
#include "json_parser.hpp"
#include "msgpack_parser.hpp"
#include "websocket_transport.hpp"

namespace sioxx
{

client_impl::client_impl(client_options&& options)
    : options_(std::move(options))
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
  if (intentional_close_.load()) stop_reconnect_thread();
  base_uri_ = uri;
  intentional_close_ = false;
  using_polling_ = options_.force_http_polling;
  reconnect_attempts_used_ = 0;
  auto engineio = ensure_engineio();
  if (!engineio) return;
  engineio->open(detail::build_engineio_url(
    uri, options_.engineio_path, options_.query,
    using_polling_.load() ? "polling" : "websocket"));
}

std::shared_ptr<engineio_client> client_impl::ensure_engineio()
{
  auto engineio = std::make_shared<engineio_client>();
  std::shared_ptr<transport_base> transport;
  if (using_polling_.load())
  {
    auto polling = std::make_shared<http_polling_transport>();
    polling->set_verify_tls(options_.verify_tls);
    if (!options_.extra_headers.empty())
      polling->set_extra_headers(options_.extra_headers);
    if (options_.proxy)
      polling->set_proxy(options_.proxy->uri, options_.proxy->username,
                         options_.proxy->password);
    transport = std::move(polling);
  }
  else
  {
    auto websocket = std::make_shared<websocket_transport>();
    websocket->set_verify_tls(options_.verify_tls);
    if (!options_.extra_headers.empty())
      websocket->set_extra_headers(options_.extra_headers);
    if (options_.proxy)
      websocket->set_proxy(options_.proxy->uri, options_.proxy->username,
                           options_.proxy->password);
    transport = std::move(websocket);
  }
  engineio->set_transport(std::move(transport));

  const auto weak_self = weak_from_this();
  const std::weak_ptr<engineio_client> weak_engineio = engineio;
  engineio->on_open(
    [weak_self, weak_engineio]
    {
      auto self = weak_self.lock();
      auto source = weak_engineio.lock();
      if (self && source && self->is_current_engineio(source))
        self->on_engineio_open();
    });
  engineio->on_close(
    [weak_self, weak_engineio](const std::string& reason)
    {
      auto self = weak_self.lock();
      auto source = weak_engineio.lock();
      if (self && source && self->is_current_engineio(source))
        self->on_engineio_close(reason);
    });
  engineio->on_frame(
    [weak_self, weak_engineio](const std::string& payload, bool is_binary)
    {
      auto self = weak_self.lock();
      auto source = weak_engineio.lock();
      if (self && source && self->is_current_engineio(source))
        self->on_engineio_frame(payload, is_binary);
    });
  engineio->on_error(
    [weak_self, weak_engineio](const std::string& msg)
    {
      auto self = weak_self.lock();
      auto source = weak_engineio.lock();
      if (!self || !source || !self->is_current_engineio(source)) return;
      if (!self->using_polling_.load() && !source->is_open())
      {
        self->activate_polling_fallback();
      }
      if (self->on_error_) self->on_error_(msg);
    });

  std::shared_ptr<engineio_client> previous_engineio;
  {
    std::lock_guard<std::mutex> lock(engineio_mutex_);
    // Shutdown sets intentional_close_ before taking this mutex. Therefore a
    // replacement is either installed in time for close() to select it, or
    // rejected here after shutdown has started.
    if (intentional_close_.load()) return {};
    previous_engineio = std::move(engineio_);
    engineio_ = engineio;
  }
  // An old polling transport can synchronously notify its close handler from
  // destruction, so release it without holding engineio_mutex_.
  previous_engineio.reset();
  return engineio;
}

std::shared_ptr<engineio_client> client_impl::current_engineio()
{
  std::lock_guard<std::mutex> lock(engineio_mutex_);
  return engineio_;
}

bool client_impl::is_current_engineio(
  const std::shared_ptr<engineio_client>& engineio) const
{
  std::lock_guard<std::mutex> lock(engineio_mutex_);
  return engineio_ == engineio;
}

void client_impl::activate_polling_fallback()
{
  if (intentional_close_.load() || using_polling_.exchange(true)) return;
  if (on_error_)
    on_error_("WebSocket connection failed; switching to HTTP long-polling");
  auto engineio = ensure_engineio();
  if (engineio)
    engineio->open(detail::build_engineio_url(base_uri_, options_.engineio_path,
                                              options_.query, "polling"));
}

void client_impl::on_engineio_open()
{
  {
    // Auto-CONNECT every namespace socket that's already been requested.
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [nsp, sock] : sockets_)
    {
      sock->connect();
    }
  }
  if (on_open_) on_open_();
}

void client_impl::on_engineio_close(const std::string& reason)
{
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [nsp, sock] : sockets_) sock->mark_connected(false);
  }
  if (on_close_) on_close_(reason);

  if (!intentional_close_.load() && options_.reconnect_attempts > 0)
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
  thread_local std::mt19937 jitter_engine{std::random_device{}()};
  std::uniform_real_distribution<double> jitter_distribution(0.0, 1.0);
  auto delay = reconnect_delay_for_attempt(
    options_.reconnect_delay, options_.reconnect_delay_max,
    options_.reconnect_randomization_factor, reconnect_attempts_used_,
    jitter_distribution(jitter_engine));
  auto uri = base_uri_;
  {
    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    if (intentional_close_.load()) return;
    // Acquiring reconnect_mutex_ means the previous worker has finished all
    // accesses to client state, so it is safe to reap before replacing it.
    if (reconnect_thread_.joinable()) reconnect_thread_.join();
    auto self = shared_from_this();
    reconnect_thread_ = std::thread(
      [self = std::move(self), delay, uri = std::move(uri)]
      {
        std::unique_lock<std::mutex> lock(self->reconnect_mutex_);
        if (self->reconnect_condition_.wait_for(
              lock, delay, [&self] { return self->intentional_close_.load(); }))
          return;
        if (self->intentional_close_.load()) return;
        try
        {
          auto engineio = self->ensure_engineio();
          if (engineio)
            engineio->open(detail::build_engineio_url(
              uri, self->options_.engineio_path, self->options_.query,
              self->using_polling_.load() ? "polling" : "websocket"));
        }
        catch (const std::exception& error)
        {
          lock.unlock();
          if (self->on_error_)
            self->on_error_(std::string("sioxx reconnect: ") + error.what());
        }
        catch (...)
        {
          lock.unlock();
          if (self->on_error_) self->on_error_("sioxx reconnect failed");
        }
      });
  }
}

void client_impl::stop_reconnect_thread()
{
  reconnect_condition_.notify_all();
  std::thread thread_to_join;
  {
    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    if (reconnect_thread_.joinable() &&
        reconnect_thread_.get_id() != std::this_thread::get_id())
    {
      thread_to_join = std::move(reconnect_thread_);
    }
  }
  if (thread_to_join.joinable()) thread_to_join.join();
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
    sock->dispatch_event(event_name, std::move(args), packet.id);
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
  auto engineio = current_engineio();
  if (engineio && engineio->is_open()) sock->connect();
  return sock;
}

void client_impl::send_packet(const packet& packet)
{
  std::lock_guard<std::mutex> lock(send_mutex_);
  auto engineio = current_engineio();
  if (!engineio) return;
  parser_->encode(packet, [engineio = std::move(engineio)](
                            const std::string& payload, bool is_binary)
                  { engineio->send(payload, is_binary); });
}

void client_impl::close()
{
  intentional_close_ = true;
  reconnect_condition_.notify_all();
  std::exception_ptr first_error;
  {
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [nsp, sock] : sockets_)
    {
      try
      {
        if (sock->connected()) sock->disconnect();
      }
      catch (...)
      {
        if (!first_error) first_error = std::current_exception();
      }
    }
  }
  try
  {
    if (auto engineio = current_engineio()) engineio->close();
  }
  catch (...)
  {
    if (!first_error) first_error = std::current_exception();
  }
  if (first_error) std::rethrow_exception(first_error);
}

void client_impl::sync_close()
{
  std::exception_ptr first_error;
  try
  {
    close();
  }
  catch (...)
  {
    first_error = std::current_exception();
  }

  try
  {
    stop_reconnect_thread();
  }
  catch (...)
  {
    if (!first_error) first_error = std::current_exception();
  }

  try
  {
    if (auto engineio = current_engineio()) engineio->sync_close();
  }
  catch (...)
  {
    if (!first_error) first_error = std::current_exception();
  }

  if (first_error) std::rethrow_exception(first_error);
}

client::client() : client(client_options{}) {}

client::client(const client_options& options) : client(client_options(options))
{
}

client::client(client_options&& options)
    : impl_(std::make_shared<client_impl>(std::move(options)))
{
}

client::~client()
{
  try
  {
    impl_->sync_close();
  }
  catch (...)
  {
    // Destructors cannot report shutdown errors.
  }
}

void client::connect(const std::string& uri) { impl_->connect(uri); }

void client::close() { impl_->close(); }

void client::sync_close() { impl_->sync_close(); }

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
