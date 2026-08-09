#include "sioxx/socket.hpp"

#include <atomic>

#include "client_impl.hpp"

namespace sioxx
{

socket::socket(std::weak_ptr<client_impl> client, std::string nsp, message auth)
    : client_(std::move(client)), nsp_(std::move(nsp)), auth_(std::move(auth))
{
}

void socket::set_auth(message auth)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auth_ = std::move(auth);
}

message socket::auth() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return auth_;
}

bool socket::connected() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

void socket::on(const std::string& event, event_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_[event] = std::move(listener);
  ack_listeners_.erase(event);
}

void socket::on(const std::string& event, ack_event_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  ack_listeners_[event] = std::move(listener);
  listeners_.erase(event);
}

void socket::on_any(event_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  any_listener_ = std::move(listener);
  any_ack_listener_ = nullptr;
}

void socket::on_any(ack_event_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  any_ack_listener_ = std::move(listener);
  any_listener_ = nullptr;
}

void socket::off(const std::string& event)
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.erase(event);
  ack_listeners_.erase(event);
}

void socket::off_all()
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.clear();
  ack_listeners_.clear();
  any_listener_ = nullptr;
  any_ack_listener_ = nullptr;
}

void socket::on_connect(connect_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  on_connect_ = std::move(listener);
}

void socket::on_disconnect(disconnect_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  on_disconnect_ = std::move(listener);
}

void socket::connect()
{
  packet packet;
  packet.type = packet_type::connect;
  packet.nsp = nsp_;
  packet.data = auth();
  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::disconnect()
{
  packet packet;
  packet.type = packet_type::disconnect;
  packet.nsp = nsp_;
  if (auto c = client_.lock()) c->send_packet(packet);
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
}

void socket::emit(const std::string& event, const message& data)
{
  packet packet;
  packet.type = packet_type::event;
  packet.nsp = nsp_;
  json arr = json::array();
  arr.push_back(event);
  if (data.is_array())
  {
    for (auto& v : data) arr.push_back(v);
  }
  else if (!data.is_null())
  {
    arr.push_back(data);
  }
  packet.data = std::move(arr);
  send_or_buffer(std::move(packet));
}

void socket::emit(const std::string& event, const message& data,
                  ack_callback callback)
{
  int id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = next_ack_id_++;
    pending_acks_[id] = std::move(callback);
  }

  packet packet;
  packet.type = packet_type::event;
  packet.nsp = nsp_;
  packet.id = id;
  json arr = json::array();
  arr.push_back(event);
  if (data.is_array())
  {
    for (auto& v : data) arr.push_back(v);
  }
  else if (!data.is_null())
  {
    arr.push_back(data);
  }
  packet.data = std::move(arr);
  send_or_buffer(std::move(packet));
}

void socket::send_or_buffer(packet packet)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || flushing_send_buffer_)
    {
      send_buffer_.push(std::move(packet));
      return;
    }
  }

  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::flush_send_buffer()
{
  while (true)
  {
    packet packet;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_ || send_buffer_.empty())
      {
        flushing_send_buffer_ = false;
        return;
      }
      packet = std::move(send_buffer_.front());
      send_buffer_.pop();
    }

    if (auto c = client_.lock()) c->send_packet(packet);
  }
}

void socket::dispatch_event(const std::string& event, message data, int ack_id)
{
  event_listener listener;
  ack_event_listener ack_listener;
  event_listener any_listener;
  ack_event_listener any_ack_listener;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = listeners_.find(event);
    if (it != listeners_.end()) listener = it->second;
    auto ack_it = ack_listeners_.find(event);
    if (ack_it != ack_listeners_.end()) ack_listener = ack_it->second;
    any_listener = any_listener_;
    any_ack_listener = any_ack_listener_;
  }

  ack_callback acknowledge;
  if ((ack_listener || any_ack_listener) && ack_id >= 0)
  {
    auto weak_self = weak_from_this();
    auto sent = std::make_shared<std::atomic<bool>>(false);
    acknowledge = [weak_self, sent, ack_id](message reply)
    {
      if (sent->exchange(true)) return;
      if (auto self = weak_self.lock())
        self->send_ack(ack_id, std::move(reply));
    };
  }

  if (listener)
    listener(event, data);
  else if (ack_listener)
    ack_listener(event, data, acknowledge);

  if (any_listener)
    any_listener(event, std::move(data));
  else if (any_ack_listener)
    any_ack_listener(event, std::move(data), std::move(acknowledge));
}

void socket::send_ack(int id, message data)
{
  packet packet;
  packet.type = packet_type::ack;
  packet.nsp = nsp_;
  packet.id = id;
  if (data.is_array())
  {
    packet.data = std::move(data);
  }
  else
  {
    packet.data = json::array();
    if (!data.is_null()) packet.data.push_back(std::move(data));
  }
  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::dispatch_ack(int id, message data)
{
  ack_callback cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_acks_.find(id);
    if (it == pending_acks_.end()) return;
    cb = std::move(it->second);
    pending_acks_.erase(it);
  }
  if (cb) cb(std::move(data));
}

void socket::mark_connected(bool connected,
                            const std::string& disconnect_reason)
{
  bool was_connected;
  bool should_flush = false;
  connect_listener connect_listener_to_notify;
  disconnect_listener disconnect_listener_to_notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    was_connected = connected_;
    connected_ = connected;

    if (connected && !was_connected)
    {
      connect_listener_to_notify = on_connect_;
      if (!flushing_send_buffer_)
      {
        flushing_send_buffer_ = true;
        should_flush = true;
      }
    }
    else if (!connected && was_connected)
    {
      disconnect_listener_to_notify = on_disconnect_;
    }
  }

  if (connected && !was_connected)
  {
    if (should_flush) flush_send_buffer();
    if (connect_listener_to_notify) connect_listener_to_notify();
  }
  else if (!connected && was_connected)
  {
    if (disconnect_listener_to_notify)
      disconnect_listener_to_notify(disconnect_reason);
  }
}

}  // namespace sioxx
