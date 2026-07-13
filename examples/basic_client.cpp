// Minimal example: connect, join a namespace, emit with/without ack, listen.
//
//   ./sioxx_basic_client ws://localhost:3000          (JSON parser, default)
//   ./sioxx_basic_client ws://localhost:3000 msgpack   (MessagePack parser)

#include <chrono>
#include <iostream>
#include <sioxx/sioxx.hpp>
#include <thread>

int main(int argc, char** argv)
{
  std::string uri = argc > 1 ? argv[1] : "ws://localhost:3000";
  sioxx::parser_kind parser = sioxx::parser_kind::json;
  if (argc > 2 && std::string(argv[2]) == "msgpack")
  {
    parser = sioxx::parser_kind::msgpack;
  }

  sioxx::client_options opts;
  opts.parser = parser;
  opts.reconnect_attempts = 5;
  opts.reconnect_delay = std::chrono::milliseconds(2000);

  sioxx::client client(opts);

  client.set_open_listener(
    [&](auto sock)
    {
      std::cout << "[sioxx] connected\n";

      // now it is safe to emit
      sock->emit("hello", sioxx::json{"world"});
      sock->emit("ping_ack", sioxx::json::array({1, 2, 3}),
                 [](sioxx::message reply)
                 { std::cout << "[ack] " << reply.dump() << "\n"; });
    });

  client.set_close_listener(
    [](const std::string& reason)
    { std::cout << "[sioxx] closed: " << reason << "\n"; });
  client.set_fail_listener(
    [] { std::cout << "[sioxx] reconnect attempts exhausted\n"; });

  auto sock = client.socket("/your_namespace");

  sock->on(
    "your_message", [](const std::string& event, sioxx::message data)
    { std::cout << "[event] " << event << " -> " << data.dump() << "\n"; });

  client.connect(uri);

  std::this_thread::sleep_for(std::chrono::minutes(10));
  client.close();
  return 0;
}
