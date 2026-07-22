#include "sioxx/socket.hpp"

#include "client_impl.hpp"

namespace sioxx
{

socket::socket(std::weak_ptr<client_impl> client,
                                 std::string nsp)
    : client_(std::move(client)), nsp_(std::move(nsp))
{
}

void socket::on(const std::string& event, event_listener listener)
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_[event] = std::move(listener);
}

void socket::off(const std::string& event)
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.erase(event);
}

void socket::off_all()
{
  std::lock_guard<std::mutex> lock(mutex_);
  listeners_.clear();
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
  packet.data = json();
  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::disconnect()
{
  packet packet;
  packet.type = packet_type::disconnect;
  packet.nsp = nsp_;
  if (auto c = client_.lock()) c->send_packet(packet);
  connected_ = false;
}

void socket::emit(const std::string& event, message data)
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
  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::emit(const std::string& event, message data,
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
  if (auto c = client_.lock()) c->send_packet(packet);
}

void socket::dispatch_event(const std::string& event, message data)
{
  event_listener listener;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = listeners_.find(event);
    if (it == listeners_.end()) return;
    listener = it->second;
  }
  listener(event, std::move(data));
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
  bool was_connected = connected_;
  connected_ = connected;

  if (connected && !was_connected)
  {
    connect_listener listener;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      listener = on_connect_;
    }
    if (listener) listener();
  }
  else if (!connected && was_connected)
  {
    disconnect_listener listener;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      listener = on_disconnect_;
    }
    if (listener) listener(disconnect_reason);
  }
}

}  // namespace sioxx
