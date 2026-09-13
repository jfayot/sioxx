#ifndef SIOXX_SRC_ACK_SCHEDULER_HPP
#define SIOXX_SRC_ACK_SCHEDULER_HPP

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace sioxx
{

// One lazy worker per socket, independent of transport lifetimes. The shared
// state survives destruction from inside a timeout callback.
class ack_scheduler
{
 public:
  using clock = std::chrono::steady_clock;

  explicit ack_scheduler(std::function<void(int)> expire)
      : state_(std::make_shared<state>()),
        worker_(
          [shared = state_, expire = std::move(expire)]
          {
            std::unique_lock<std::mutex> lock(shared->mutex);
            while (!shared->stopped)
            {
              if (shared->deadlines.empty())
              {
                shared->condition.wait(lock);
                continue;
              }
              const auto next = *shared->ordered.begin();
              if (next.first > clock::now())
              {
                shared->condition.wait_until(lock, next.first);
                continue;
              }
              const int id = next.second;
              shared->ordered.erase(shared->ordered.begin());
              shared->deadlines.erase(id);
              lock.unlock();
              expire(id);
              lock.lock();
            }
          })
  {
  }

  ~ack_scheduler()
  {
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->stopped = true;
    }
    state_->condition.notify_one();
    if (worker_.get_id() == std::this_thread::get_id())
      worker_.detach();
    else
      worker_.join();
  }

  void add(int id, clock::time_point deadline)
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->deadlines.emplace(id, deadline);
    try
    {
      state_->ordered.emplace(deadline, id);
    }
    catch (...)
    {
      state_->deadlines.erase(id);
      throw;
    }
    state_->condition.notify_one();
  }

  bool expired(int id)
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->deadlines.find(id);
    return it == state_->deadlines.end() || it->second <= clock::now();
  }

  // Missing deadlines have already been claimed by the expiry worker.
  bool remove(int id)
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto it = state_->deadlines.find(id);
    if (it == state_->deadlines.end()) return true;
    const bool expired = it->second <= clock::now();
    state_->ordered.erase({it->second, id});
    state_->deadlines.erase(it);
    state_->condition.notify_one();
    return expired;
  }

  void clear()
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->deadlines.clear();
    state_->ordered.clear();
    state_->condition.notify_one();
  }

 private:
  struct state
  {
    std::mutex mutex;
    std::condition_variable condition;
    std::map<int, clock::time_point> deadlines;
    std::set<std::pair<clock::time_point, int>> ordered;
    bool stopped{false};
  };
  std::shared_ptr<state> state_;
  std::thread worker_;
};

}  // namespace sioxx

#endif
