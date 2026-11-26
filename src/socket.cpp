#include "sioxx/socket.hpp"

#include <algorithm>
#include <atomic>
#include <stdexcept>

#include "ack_scheduler.hpp"
#include "client_impl.hpp"

namespace sioxx
{

namespace
{
std::chrono::steady_clock::time_point ack_deadline(
  std::chrono::milliseconds timeout)
{
  const auto now = std::chrono::steady_clock::now();
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::time_point::max() - now);
  return timeout >= remaining ? std::chrono::steady_clock::time_point::max()
                              : now + timeout;
}
}  // namespace

struct socket::retry_emission
{
  packet original;
  std::chrono::milliseconds timeout;
  unsigned remaining;
  timed_ack_callback callback;
  std::shared_ptr<bool> cancelled;
};

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
  auto client = client_.lock();
  std::shared_ptr<engineio_client> engineio;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    require_emissions_open();
    packet.id = next_ack_id_++;
    pending_acks_[packet.id] = std::move(callback);
    if (!connected_ || flushing_send_buffer_)
    {
      send_buffer_.push_back(std::move(packet));
      return;
    }
    if (client) engineio = client->current_engineio();
  }
  if (client) client->send_packet(packet, engineio);
}

void socket::emit(const std::string& event, const message& data,
                  std::chrono::milliseconds timeout,
                  timed_ack_callback callback)
{
  emit(event, data, timeout, 0, std::move(callback));
}

void socket::emit(const std::string& event, const message& data,
                  std::chrono::milliseconds timeout, unsigned retries,
                  timed_ack_callback callback)
{
  if (timeout.count() <= 0 || !callback)
    throw std::invalid_argument(
      "acknowledgement timeout and callback must be valid");
  const auto deadline = ack_deadline(timeout);
  packet packet;
  packet.type = packet_type::event;
  packet.nsp = nsp_;
  packet.data = json::array({event});
  if (data.is_array())
    packet.data.insert(packet.data.end(), data.begin(), data.end());
  else if (!data.is_null())
    packet.data.push_back(data);
  if (retries == 0)
  {
    send_timed(std::move(packet), deadline, std::move(callback), {});
    return;
  }
  auto emission = std::make_shared<retry_emission>(retry_emission{
    std::move(packet), timeout, retries, std::move(callback), {}});
  {
    std::lock_guard<std::mutex> lock(mutex_);
    require_emissions_open();
    if (!retries_cancelled_) retries_cancelled_ = std::make_shared<bool>(false);
    emission->cancelled = retries_cancelled_;
  }
  emit_retry(emission, deadline, false);
}

void socket::emit_retry(const std::shared_ptr<retry_emission>& emission,
                        std::chrono::steady_clock::time_point deadline,
                        bool background)
{
  send_timed(
    emission->original, deadline,
    [weak = weak_from_this(), emission](ack_status status, message data)
    {
      if (status == ack_status::timeout && emission->remaining > 0)
      {
        --emission->remaining;
        if (auto self = weak.lock())
        {
          try
          {
            self->emit_retry(emission, ack_deadline(emission->timeout), true);
          }
          catch (...)
          {
            // Retry preparation failed before another attempt was scheduled.
            emission->callback(ack_status::timeout, json());
          }
        }
      }
      else
      {
        emission->callback(status, std::move(data));
      }
    },
    emission->cancelled, background);
}

void socket::send_timed(packet packet,
                        std::chrono::steady_clock::time_point deadline,
                        timed_ack_callback callback,
                        const std::shared_ptr<bool>& cancelled, bool background)
{
  auto client = client_.lock();
  std::shared_ptr<engineio_client> engineio;
  int id;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    // A retry callback may already have been claimed when shutdown starts.
    // Keep its cancellation token set even if connect() starts a new session.
    // All token accesses are serialized by mutex_.
    if (cancelled && *cancelled) return;
    id = register_timed_ack(deadline, std::move(callback));
    packet.id = id;
    const bool buffer = !connected_ || flushing_send_buffer_;
    if (!buffer && client && !background) engineio = client->current_engineio();
    const bool expired = deadline <= std::chrono::steady_clock::now();
    try
    {
      ack_scheduler_->add(id, deadline);
      if (expired) return;
      if (buffer)
      {
        send_buffer_.push_back(std::move(packet));
        return;
      }
      if (background && client)
        timed_acks_.at(id).queued.emplace(std::move(packet));
    }
    catch (...)
    {
      ack_scheduler_->remove(id);
      auto discarded = timed_acks_.extract(id);
      lock.unlock();
      throw;
    }
  }
  if (!client) return;
  if (!background)
  {
    send_timed_attempt(*client, engineio, packet, true);
    return;
  }

  try
  {
    client->post_retry(
      [weak = weak_from_this(), id]
      {
        if (auto self = weak.lock()) self->send_queued_retry(id);
      });
  }
  catch (...)
  {
    // If the worker cannot accept the job, the armed attempt still expires.
  }
}

void socket::send_queued_retry(int id)
{
  auto client = client_.lock();
  if (!client) return;
  // Shutdown removes the attempt and its payload even if the worker has not
  // selected a transport yet.
  std::shared_ptr<engineio_client> engineio;
  std::optional<packet> queued;
  try
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timed_acks_.find(id);
    if (it == timed_acks_.end() ||
        it->second.deadline <= std::chrono::steady_clock::now())
      return;
    auto& pending = it->second.queued;
    if (!connected_ || flushing_send_buffer_)
      send_buffer_.push_back(std::move(*pending));
    else
    {
      engineio = client->current_engineio();
      queued = std::move(pending);
    }
    pending.reset();
  }
  catch (...)
  {
    return;  // The attempt will expire normally.
  }
  if (queued) send_timed_attempt(*client, engineio, *queued, false);
}

void socket::send_timed_attempt(
  client_impl& client, const std::shared_ptr<engineio_client>& engineio,
  const packet& packet, bool initial_send)
{
  const auto ready = [this, id = packet.id]
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timed_acks_.find(id);
    return connected_ && it != timed_acks_.end() &&
           it->second.deadline > std::chrono::steady_clock::now();
  };

  try
  {
    client.send_packet(packet, engineio, ready);
  }
  catch (...)
  {
    if (!initial_send) return;  // The attempt will expire normally.
    decltype(timed_acks_)::node_type discarded;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = timed_acks_.find(packet.id);
      // Once timeout or shutdown wins, that outcome owns the attempt.
      if (it == timed_acks_.end() ||
          it->second.deadline <= std::chrono::steady_clock::now())
        return;
      ack_scheduler_->remove(packet.id);
      discarded = timed_acks_.extract(it);
    }
    throw;
  }
}

void socket::require_emissions_open() const
{
  if (emissions_closed_)
    throw std::logic_error("emissions require client.connect() after shutdown");
}

int socket::register_timed_ack(std::chrono::steady_clock::time_point deadline,
                               timed_ack_callback callback)
{
  require_emissions_open();
  if (!ack_scheduler_)
    ack_scheduler_ = std::make_unique<ack_scheduler>(
      [weak = weak_from_this()](int id)
      {
        if (auto self = weak.lock()) self->expire_ack(id);
      });
  const int id = next_ack_id_++;
  timed_acks_.emplace(id, timed_ack{deadline, std::move(callback), {}});
  return id;
}

void socket::expire_ack(int id)
{
  timed_ack_callback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = timed_acks_.find(id);
    if (it == timed_acks_.end()) return;
    ack_scheduler_->remove(id);
    callback = std::move(it->second.callback);
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
  auto client = client_.lock();
  std::shared_ptr<engineio_client> engineio;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    require_emissions_open();
    if (!connected_ || flushing_send_buffer_)
    {
      send_buffer_.push_back(std::move(packet));
      return;
    }
    if (client) engineio = client->current_engineio();
  }

  if (client) client->send_packet(packet, engineio);
}

void socket::flush_send_buffer()
{
  while (true)
  {
    auto client = client_.lock();
    std::shared_ptr<engineio_client> engineio;
    packet packet;
    bool timed = false;
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
      auto it = timed_acks_.find(packet.id);
      timed = it != timed_acks_.end();
      expired =
        timed && it->second.deadline <= std::chrono::steady_clock::now();
      if (!expired && client) engineio = client->current_engineio();
    }

    if (expired)
      expire_ack(packet.id);
    else if (client && timed)
      send_timed_attempt(*client, engineio, packet, false);
    else if (client)
      client->send_packet(packet, engineio);
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
      expired = timed->second.deadline <= std::chrono::steady_clock::now();
      ack_scheduler_->remove(id);
      timed_cb = std::move(timed->second.callback);
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

void socket::cancel_pending_emissions()
{
  std::map<int, ack_callback> cancelled_acks;
  decltype(timed_acks_) cancelled;
  std::deque<packet> discarded;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    emissions_closed_ = true;
    if (retries_cancelled_) *retries_cancelled_ = true;
    retries_cancelled_.reset();
    if (ack_scheduler_) ack_scheduler_->clear();
    discarded.swap(send_buffer_);
    cancelled_acks.swap(pending_acks_);
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

void socket::resume_emissions()
{
  std::lock_guard<std::mutex> lock(mutex_);
  emissions_closed_ = false;
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
  if (!connected)
  {
    if (auto listener = mark_disconnected()) listener(disconnect_reason);
    return;
  }

  bool should_flush;
  connect_listener listener;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) return;
    connected_ = true;
    listener = on_connect_;
    should_flush = !flushing_send_buffer_;
    flushing_send_buffer_ = true;
  }
  if (should_flush) flush_send_buffer();
  if (listener) listener();
}

socket::disconnect_listener socket::mark_disconnected()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_) return {};
  connected_ = false;
  return on_disconnect_;
}

}  // namespace sioxx
