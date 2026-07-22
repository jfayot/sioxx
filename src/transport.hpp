#pragma once
#include <functional>
#include <string>

namespace sioxx
{

enum class transport_state
{
  closed,
  connecting,
  open,
  closing
};

// Abstract transport so engineio_client isn't tied to any specific socket
// implementation. websocket_transport and http_polling_transport are the
// concrete implementations shipped by sioxx.
class transport_base
{
 public:
  using message_handler =
    std::function<void(const std::string& payload, bool is_binary)>;
  using open_handler = std::function<void()>;
  using close_handler = std::function<void(const std::string& reason)>;
  using error_handler = std::function<void(const std::string& message)>;

  virtual ~transport_base() = default;

  virtual void connect(const std::string& url) = 0;
  virtual void send(const std::string& payload, bool is_binary) = 0;
  virtual void close() = 0;

  void set_message_handler(message_handler h) { on_message_ = std::move(h); }
  void set_open_handler(open_handler h) { on_open_ = std::move(h); }
  void set_close_handler(close_handler h) { on_close_ = std::move(h); }
  void set_error_handler(error_handler h) { on_error_ = std::move(h); }

  transport_state state() const { return state_; }

 protected:
  message_handler on_message_;
  open_handler on_open_;
  close_handler on_close_;
  error_handler on_error_;
  transport_state state_{transport_state::closed};
};

}  // namespace sioxx
