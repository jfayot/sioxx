#include "sioxx/socket.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

#include "ack_scheduler.hpp"
#include "client_impl.hpp"

namespace sioxx
{

socket::socket(std::weak_ptr<client_impl> client, std::string nsp, message auth)
    : client_(std::move(client)), nsp_(std::move(nsp)), auth_(std::move(auth))
{
}

socket::~socket() = default;

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

void socket::emit(const std::string& event, const message& data,
                  std::chrono::milliseconds timeout,
                  timed_ack_callback callback)
{
  if (timeout.count() <= 0 || !callback)
    throw std::invalid_argument(
      "acknowledgement timeout and callback must be valid");
  const auto now = std::chrono::steady_clock::now();
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::time_point::max() - now);
  const auto deadline = timeout >= remaining
                          ? std::chrono::steady_clock::time_point::max()
                          : now + timeout;
  packet packet;
  packet.type = packet_type::event;
  packet.nsp = nsp_;
  packet.data = json::array({event});
  if (data.is_array())
    for (const auto& value : data) packet.data.push_back(value);
  else if (!data.is_null())
    packet.data.push_back(data);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    packet.id = register_timed_ack(deadline, std::move(callback));
    if (!connected_ || flushing_send_buffer_)
    {
      send_buffer_.push_back(std::move(packet));
      return;
    }
  }
  if (auto c = client_.lock()) c->send_packet(packet);
}

int socket::register_timed_ack(std::chrono::steady_clock::time_point deadline,
                               timed_ack_callback callback)
{
  if (timed_acks_closed_)
    throw std::logic_error(
      "timed emissions require client.connect() after shutdown");
  if (!ack_scheduler_)
    ack_scheduler_ = std::make_unique<ack_scheduler>(
      [weak = weak_from_this()](int id)
      {
        if (auto self = weak.lock()) self->expire_ack(id);
      });
  const int id = next_ack_id_++;
  timed_acks_.emplace(id, std::move(callback));
  try
  {
    ack_scheduler_->add(id, deadline);
  }
  catch (...)
  {
    timed_acks_.erase(id);
    throw;
  }
  return id;
}

void socket::expire_ack(int id)
{
  timed_ack_callback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timed_acks_.find(id);
    if (it == timed_acks_.end()) return;
    callback = std::move(it->second);
    timed_acks_.erase(it);
    ++active_timed_acks_;
    send_buffer_.erase(std::remove_if(send_buffer_.begin(), send_buffer_.end(),
                                      [id](const packet& buffered)
                                      { return buffered.id == id; }),
                       send_buffer_.end());
  }
  invoke_timed_ack(std::move(callback), ack_status::timeout, json());
}

void socket::send_or_buffer(packet packet)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || flushing_send_buffer_)
    {
      send_buffer_.push_back(std::move(packet));
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
    bool expired = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!connected_ || send_buffer_.empty())
      {
        flushing_send_buffer_ = false;
        return;
      }
      packet = std::move(send_buffer_.front());
      send_buffer_.pop_front();
      expired =
        timed_acks_.count(packet.id) && ack_scheduler_->expired(packet.id);
    }

    if (expired)
      expire_ack(packet.id);
    else if (auto c = client_.lock())
      c->send_packet(packet);
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
    acknowledge = [weak_self, sent = std::move(sent), ack_id](message reply)
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
  timed_ack_callback timed_cb;
  bool expired = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto timed = timed_acks_.find(id);
    if (timed != timed_acks_.end())
    {
      expired = ack_scheduler_->remove(id);
      timed_cb = std::move(timed->second);
      ++active_timed_acks_;
      timed_acks_.erase(timed);
      send_buffer_.erase(
        std::remove_if(send_buffer_.begin(), send_buffer_.end(),
                       [id](const packet& buffered)
                       { return buffered.id == id; }),
        send_buffer_.end());
    }
    else
    {
      auto it = pending_acks_.find(id);
      if (it == pending_acks_.end()) return;
      cb = std::move(it->second);
      pending_acks_.erase(it);
    }
  }
  if (timed_cb)
    invoke_timed_ack(std::move(timed_cb),
                     expired ? ack_status::timeout : ack_status::acknowledged,
                     expired ? message(json()) : std::move(data));
  else if (cb)
    cb(std::move(data));
}

void socket::cancel_timed_acks()
{
  std::map<int, timed_ack_callback> cancelled;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    timed_acks_closed_ = true;
    if (ack_scheduler_) ack_scheduler_->clear();
    send_buffer_.erase(
      std::remove_if(send_buffer_.begin(), send_buffer_.end(),
                     [this](const packet& buffered)
                     { return timed_acks_.count(buffered.id) != 0; }),
      send_buffer_.end());
    cancelled.swap(timed_acks_);
  }
  // Destroy captures outside locks: they may release application-owned objects.
  cancelled.clear();
}

void socket::wait_for_timed_acks()
{
  std::unique_lock<std::mutex> lock(mutex_);
  ack_condition_.wait(lock, [this] { return active_timed_acks_ == 0; });
}

void socket::resume_timed_acks()
{
  std::lock_guard<std::mutex> lock(mutex_);
  timed_acks_closed_ = false;
}

void socket::invoke_timed_ack(timed_ack_callback callback, ack_status status,
                              message data)
{
  try
  {
    callback(status, std::move(data));
  }
  catch (...)
  {
    finish_timed_ack();
    throw;
  }
  finish_timed_ack();
}

void socket::finish_timed_ack()
{
  std::lock_guard<std::mutex> lock(mutex_);
  --active_timed_acks_;
  ack_condition_.notify_all();
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
