#ifndef SIOXX_TESTS_SOCKET_TEST_ACCESS_HPP
#define SIOXX_TESTS_SOCKET_TEST_ACCESS_HPP

#include <memory>
#include <sioxx/socket.hpp>
#include <string>
#include <utility>

namespace sioxx::detail
{

class socket_test_access
{
 public:
  // A detached socket exercises listener and ACK bookkeeping without a network.
  static std::shared_ptr<socket> make_socket(std::string nsp = "/",
                                             message auth = {})
  {
    return std::shared_ptr<socket>(
      new socket({}, std::move(nsp), std::move(auth)));
  }

  static void dispatch_event(socket& target, const std::string& event,
                             message data, int ack_id = -1)
  {
    target.dispatch_event(event, std::move(data), ack_id);
  }

  static void dispatch_ack(socket& target, int id, message data)
  {
    target.dispatch_ack(id, std::move(data));
  }

  static void mark_connected(socket& target, bool connected)
  {
    target.mark_connected(connected);
  }
};

}  // namespace sioxx::detail

#endif  // SIOXX_TESTS_SOCKET_TEST_ACCESS_HPP
