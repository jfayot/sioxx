#ifndef SIOXX_SRC_CLIENT_IMPL_HPP
#define SIOXX_SRC_CLIENT_IMPL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "engineio_client.hpp"
#include "reconnection.hpp"
#include "sioxx/client_options.hpp"
#include "sioxx/parser.hpp"
#include "sioxx/socket.hpp"

namespace boost
{
namespace asio
{
class io_context;
}
}  // namespace boost

namespace sioxx
{

class client_impl : public std::enable_shared_from_this<client_impl>
{
 public:
  using connect_handler = std::function<void()>;
  using close_handler = std::function<void(const std::string&)>;
  using error_handler = std::function<void(const std::string&)>;
  using fail_handler = std::function<void()>;
  using reconnect_handler = std::function<void(unsigned, unsigned)>;

  explicit client_impl(client_options&& options,
                       boost::asio::io_context* io_context = nullptr);
  void connect(const std::string& uri);
  void close();
  void sync_close();
  bool opened() const;
  std::string get_sessionid() const;
  std::shared_ptr<sioxx::socket> socket(const std::string& nsp = "/",
                                        message auth = json());
  void set_open_handler(connect_handler h)
  {
    set_handler(on_open_, std::move(h));
  }
  void set_close_handler(close_handler h)
  {
    set_handler(on_close_, std::move(h));
  }
  void set_fail_handler(fail_handler h) { set_handler(on_fail_, std::move(h)); }
  void set_reconnecting_handler(connect_handler h)
  {
    set_handler(on_reconnecting_, std::move(h));
  }
  void set_reconnect_handler(reconnect_handler h)
  {
    set_handler(on_reconnect_, std::move(h));
  }
  void set_error_handler(error_handler h)
  {
    set_handler(on_error_, std::move(h));
  }
  void send_packet(const packet& packet);

 private:
  std::vector<std::shared_ptr<sioxx::socket>> sockets_snapshot();
  std::shared_ptr<engineio_client> ensure_engineio();
  void bind_engineio_callbacks(
    const std::shared_ptr<engineio_client>& engineio);
  void handle_engineio_open(const std::shared_ptr<engineio_client>& engineio);
  void handle_engineio_close(const std::shared_ptr<engineio_client>& engineio,
                             const std::string& reason);
  void handle_engineio_frame(const std::shared_ptr<engineio_client>& engineio,
                             const std::string& payload, bool is_binary);
  void handle_engineio_error(const std::shared_ptr<engineio_client>& engineio,
                             const std::string& msg);
  std::shared_ptr<engineio_client> current_engineio();
  bool is_current_engineio(
    const std::shared_ptr<engineio_client>& engineio) const;
  void on_engineio_open();
  void on_engineio_close(const std::string& reason);
  void on_engineio_frame(const std::string& payload, bool is_binary);
  void schedule_reconnect();
  void stop_reconnect_thread();
  void activate_polling_fallback();

  template <typename Handler>
  void set_handler(Handler& destination, Handler handler)
  {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    destination = std::move(handler);
  }

  template <typename Handler>
  Handler current_handler(const Handler& handler) const
  {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    return handler;
  }

  client_options options_;
  boost::asio::io_context* io_context_;
  std::unique_ptr<parser_base> parser_;
  mutable std::mutex engineio_mutex_;
  std::shared_ptr<engineio_client> engineio_;
  std::string base_uri_;
  std::mutex send_mutex_;
  std::mutex sockets_mutex_;
  std::map<std::string, std::shared_ptr<sioxx::socket>> sockets_;
  mutable std::mutex handler_mutex_;
  connect_handler on_open_;
  close_handler on_close_;
  fail_handler on_fail_;
  connect_handler on_reconnecting_;
  reconnect_handler on_reconnect_;
  error_handler on_error_;
  int reconnect_attempts_used_{0};
  std::atomic<bool> intentional_close_{false};
  std::mutex reconnect_mutex_;
  std::condition_variable reconnect_condition_;
  std::thread reconnect_thread_;
  std::atomic<bool> using_polling_{false};
};

}  // namespace sioxx

#endif  // SIOXX_SRC_CLIENT_IMPL_HPP
