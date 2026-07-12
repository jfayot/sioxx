# sioxx — modern socket.io-client for C++

A C++ implementation of `socket.io`'s client functionality with the following stack:

| | |
| --- | --- |
| JSON | **nlohmann/json** |
| WebSocket | **Boost.Beast** (`boost::asio` + `boost::beast::websocket`) |
| Wire protocol | **JSON or MessagePack**, selectable per-client |
| Build | **modern CMake**, [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) for nlohmann/json and Boost/OpenSSL, full `install(EXPORT ...)` so `find_package(sioxx)` works downstream |

## Building

Requires: CMake ≥ 3.20, a C++20 compiler, Boost ≥ 1.75 (asio + beast),
OpenSSL. nlohmann/json is fetched automatically via the vendored
[`cmake/CPM.cmake`](cmake/CPM.cmake) (CPM.cmake v0.43.1) unless you pass
`-DSIOXX_USE_SYSTEM_JSON=ON`. CPM caches the download under
`~/.cache/CPM` by default (override with `-DCPM_SOURCE_CACHE=<dir>` or the
`CPM_SOURCE_CACHE` env var), so repeat configures/CI runs don't re-fetch it.

```bash
sudo apt install cmake

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /usr/local   # optional
```

This produces `libsioxx.a` and the
`sioxx_basic_client` example. It also emits a `sioxxConfig.cmake`, so any
downstream project can just:

```cmake
find_package(sioxx REQUIRED)
target_link_libraries(my_app PRIVATE sioxx::sioxx)
```

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `SIOXX_BUILD_EXAMPLES` | `ON` | build `examples/basic_client.cpp` |
| `SIOXX_BUILD_TESTS` | `ON` | build and register the GoogleTest suite in `tests/` |
| `SIOXX_USE_SYSTEM_JSON` | `OFF` | use an already-installed `nlohmann_json` package instead of fetching one |

## Testing

Unit tests use GoogleTest (fetched automatically via `FetchContent`, same as
nlohmann/json). Build with `-DSIOXX_BUILD_TESTS=ON` and run via `ctest` or
the test binary directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIOXX_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
# or: ./build/tests/sioxx_tests
```

Coverage is intentionally focused on the parts that don't require a live
socket.io server:

- `test_json_parser.cpp` / `test_msgpack_parser.cpp` — encode/decode
  correctness for both wire protocols, including round-trips, ack ids,
  namespaces, binary attachments (msgpack), and malformed-input rejection.
- `test_message.cpp` — the `sioxx::message`/`make_args`/`binary_message`
  helpers built on `nlohmann::json`.
- `test_url_parse.cpp` — the `ws://`/`wss://` URL splitter used by
  `websocket_transport` (scheme, host, port defaults, path, error cases).
- `test_socketio_socket.cpp` — the `on`/`off`/`emit`/ack-callback bookkeeping
  in `socketio_socket`, exercised with no client attached (a default
  `std::weak_ptr<socketio_client_impl>{}`) so it needs no network at all.
- `test_engineio_client.cpp` — the engine.io v4 handshake, ping/pong
  heartbeat, and message framing (`4`-prefixing, binary passthrough),
  exercised against a small in-memory fake `transport_base` implementation
  instead of a real WebSocket.

There's deliberately no test that opens a real socket: `websocket_transport`
itself (the Boost.Beast plumbing) is exercised end-to-end by the
`sioxx_basic_client` example against a real server instead.

## API sketch

```cpp
#include <sioxx/sioxx.hpp>

sioxx::client_options opts;
opts.parser = sioxx::parser_kind::msgpack;   // or parser_kind::json (default)
opts.reconnect_attempts = 5;

sioxx::client client(opts);
client.set_open_listener([] { /* engine.io + "/" namespace connected */ });
client.set_close_listener([](const std::string& reason) { /* ... */ });

auto sock = client.socket("/your_namespace");  // any namespace path
sock->on("your_message", [](const std::string& event, sioxx::message data) {
    // data is an nlohmann::json array of the event's arguments
});

client.connect("wss://your.socketio.server");

sock->emit("hello", sioxx::json{"world"});
sock->emit("ping_ack", sioxx::json::array({1, 2, 3}), [](sioxx::message reply) {
    // ack callback
});

client.close();
```

## Parser selection

Both parsers implement the same `sioxx::parser_base` interface
(`json_parser` / `msgpack_parser`) and are picked with
`client_options::parser`. This must match whatever parser the server side is
configured with (e.g. Node's `socket.io` default vs.
`socket.io-msgpack-parser`) — sioxx does not negotiate it automatically,
exactly like the JS clients don't either.

`msgpack_parser` is implemented on top of `nlohmann::json::to_msgpack` /
`from_msgpack`, so it needs no extra MessagePack library and — unlike the
JSON parser — carries binary attachments natively via `nlohmann::json::binary_t`
without the placeholder/reconstruction dance the text protocol needs for
`BINARY_EVENT`/`BINARY_ACK` packets.

## Threading model

`websocket_transport` runs its own `boost::asio::io_context` on a background
thread per connection. All `on_*` callbacks (`socket->on(...)`, open/close
listeners, ack callbacks) fire on that thread — if you're updating UI state
or anything not thread-safe, hop back to your own thread/queue from inside
the callback.

## Known limitations

- The JSON parser recognizes and emits the `BINARY_EVENT`/`BINARY_ACK`
  headers but does not implement the placeholder deconstruction/
  reconstruction scheme for multi-attachment binary payloads. Use the
  MessagePack parser if you need binary data — it carries it natively.
- Reconnection is a simple fixed-delay retry loop (`reconnect_attempts` /
  `reconnect_delay`), not the exponential-backoff + jitter policy engine.io
  client normally uses.
- No HTTP long-polling fallback transport — WebSocket only.

## License

[MIT](LICENSE)
