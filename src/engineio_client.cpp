#include "sioxx/engineio_client.hpp"

#include "sioxx/message.hpp"

namespace sioxx
{

engineio_client::engineio_client() = default;

engineio_client::~engineio_client() { close(); }

void engineio_client::set_transport(std::shared_ptr<transport_base> transport)
{
  transport_ = std::move(transport);
  transport_->set_message_handler(
    [self = shared_from_this()](const std::string& payload, bool is_binary)
    { self->handle_transport_message(payload, is_binary); });
  transport_->set_open_handler([self = shared_from_this()]
                               { self->handle_transport_open(); });
  transport_->set_close_handler(
    [self = shared_from_this()](const std::string& reason)
    { self->handle_transport_close(reason); });
  transport_->set_error_handler(
    [self = shared_from_this()](const std::string& msg)
    {
      if (self->on_error_) self->on_error_(msg);
    });
}

void engineio_client::open(const std::string& ws_url)
{
  closing_ = false;
  open_ = false;
  if (!transport_)
    throw std::runtime_error("sioxx: no transport set on engineio_client");
  transport_->connect(ws_url);
}

void engineio_client::close()
{
  if (closing_.exchange(true)) return;
  stop_heartbeat_timer();
  open_ = false;
  if (transport_) transport_->close();
}

void engineio_client::send(const std::string& payload, bool is_binary)
{
  if (!transport_ || !open_) return;
  // engine.io text MESSAGE frames are prefixed with '4'; binary frames carry
  // the payload as-is (websocket already frames binary vs. text at the
  // transport level, so no extra prefix byte is needed there).
  if (is_binary)
  {
    transport_->send(payload, true);
  }
  else
  {
    transport_->send("4" + payload, false);
  }
}

void engineio_client::handle_transport_open()
{
  // Nothing to do yet: engine.io isn't "open" from the socket.io layer's
  // perspective until we've received the OPEN (0) handshake packet.
}

void engineio_client::handle_transport_close(const std::string& reason)
{
  open_ = false;
  stop_heartbeat_timer();
  if (on_close_) on_close_(reason);
}

void engineio_client::handle_transport_message(const std::string& payload,
                                               bool is_binary)
{
  if (is_binary)
  {
    // Binary frames are always socket.io payloads (used by msgpack_parser),
    // pass straight through.
    if (on_frame_) on_frame_(payload, true);
    return;
  }

  if (payload.empty()) return;
  char type_char = payload[0];
  std::string rest = payload.substr(1);

  switch (type_char)
  {
  case '0':
  {  // open
    json handshake = json::parse(rest, nullptr, false);
    if (!handshake.is_discarded())
    {
      sid_ = handshake.value("sid", std::string());
      ping_interval_ms_ = handshake.value("pingInterval", 25000);
      ping_timeout_ms_ = handshake.value("pingTimeout", 20000);
    }
    open_ = true;
    last_pong_ = std::chrono::steady_clock::now();
    start_heartbeat_timer();
    if (on_open_) on_open_();
    break;
  }
  case '1':  // close
    open_ = false;
    stop_heartbeat_timer();
    if (on_close_) on_close_("server closed connection");
    break;
  case '2':  // ping (engine.io v4: server pings, client pongs)
  {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    last_pong_ = std::chrono::steady_clock::now();
  }
    if (transport_) transport_->send("3", false);  // pong
    break;
  case '3':  // pong
  {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    last_pong_ = std::chrono::steady_clock::now();
  }
  break;
  case '4':  // message -> hand payload to socket.io parser layer
    if (on_frame_) on_frame_(rest, false);
    break;
  case '6':  // noop
  default:  break;
  }
}

void engineio_client::start_heartbeat_timer()
{
  stop_heartbeat_timer();
  auto self = shared_from_this();
  heartbeat_thread_ = std::thread(
    [self]
    {
      while (self->open_.load() && !self->closing_.load())
      {
        std::unique_lock<std::mutex> lock(self->heartbeat_mutex_);
        auto timeout = std::chrono::milliseconds(self->ping_interval_ms_ +
                                                 self->ping_timeout_ms_);
        if (self->heartbeat_cv_.wait_for(
              lock, std::chrono::milliseconds(self->ping_interval_ms_),
              [self] { return self->closing_.load(); }))
        {
          return;  // closing requested
        }
        auto elapsed = std::chrono::steady_clock::now() - self->last_pong_;
        if (elapsed > timeout)
        {
          lock.unlock();
          if (self->on_error_) self->on_error_("engine.io ping timeout");
          if (self->transport_) self->transport_->close();
          return;
        }
      }
    });
}

void engineio_client::stop_heartbeat_timer()
{
  {
    std::lock_guard<std::mutex> lock(heartbeat_mutex_);
    heartbeat_cv_.notify_all();
  }
  if (heartbeat_thread_.joinable() &&
      heartbeat_thread_.get_id() != std::this_thread::get_id())
  {
    heartbeat_thread_.join();
  }
  else if (heartbeat_thread_.joinable())
  {
    heartbeat_thread_.detach();
  }
}

}  // namespace sioxx
