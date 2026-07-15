// Minimal example: connect, join a namespace, emit with/without ack, listen.
//
//   ./sioxx_basic_client                              (JSON parser, default)
//   ./sioxx_basic_client msgpack                      (MessagePack parser)
//   ./sioxx_basic_client cbor                         (custom CBOR parser)
//   ./sioxx_basic_client polling                      (HTTP long-polling)
//   ./sioxx_basic_client ws://localhost:3001 polling (custom server URL)

#include <chrono>
#include <iostream>
#include <sioxx/sioxx.hpp>
#include <thread>

#include "cbor_parser.hpp"

int main(int argc, char** argv)
{
  std::string uri = "ws://localhost:3000";
  sioxx::parser_kind parser = sioxx::parser_kind::json;
  bool use_cbor = false;
  bool force_polling = false;
  for (int i = 1; i < argc; ++i)
  {
    const std::string option = argv[i];
    if (option == "msgpack")
      parser = sioxx::parser_kind::msgpack;
    else if (option == "cbor")
      use_cbor = true;
    else if (option == "polling")
      force_polling = true;
    else
      uri = option;
  }

  sioxx::client_options opts;
  opts.parser = parser;
  if (use_cbor)
    opts.parser_factory = [] { return std::make_unique<cbor_parser>(); };
  opts.force_http_polling = force_polling;
  opts.reconnect_attempts = 5;
  opts.reconnect_delay = std::chrono::milliseconds(2000);

  sioxx::client client(opts);

  client.set_open_listener([] { std::cout << "[sioxx] engine.io open\n"; });
  client.set_close_listener(
    [](const std::string& reason)
    { std::cout << "[sioxx] closed: " << reason << "\n"; });
  client.set_fail_listener(
    [] { std::cout << "[sioxx] reconnect attempts exhausted\n"; });
  client.set_error_listener([](const std::string& msg)
                            { std::cout << "[sioxx] error: " << msg << "\n"; });

  auto sock = client.socket("/your_namespace");

  sock->on(
    "your_message", [](const std::string& event, sioxx::message data)
    { std::cout << "[event] " << event << " -> " << data.dump() << "\n"; });

  sock->on_connect(
    [sock]
    {
      std::cout << "[sioxx] /your_namespace connected\n";

      sock->emit("hello", sioxx::json{"world"});

      sock->emit("ping_ack", sioxx::json::array({1, 2, 3}),
                 [](sioxx::message reply)
                 { std::cout << "[ack] " << reply.dump() << "\n"; });
    });

  client.connect(uri);

  std::this_thread::sleep_for(std::chrono::seconds(30));
  client.close();
  return 0;
}
