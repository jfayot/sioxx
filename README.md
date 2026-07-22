# sioxx — modern Socket.IO client for C++

![GitHub Release](https://img.shields.io/github/v/release/jfayot/sioxx)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://github.com/jfayot/sioxx/blob/main/LICENSE)
![build](https://github.com/jfayot/sioxx/actions/workflows/cmake-multi-platform.yml/badge.svg)

A C++ implementation of `socket.io`'s client functionality with the following stack:

| | |
| --- | --- |
| JSON | **nlohmann-json** |
| WebSocket | **Boost.Asio** + **Boost.Beast** |
| Wire protocol | **JSON or MessagePack**, selectable per-client |
| Build | **modern CMake**, [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) for nlohmann-json and Boost, full `install(EXPORT ...)` so `find_package(sioxx)` works downstream |

## Building

Requires: CMake ≥ 3.20, a C++17 compiler, Boost ≥ 1.75 (asio + beast),
OpenSSL. Boost.asio, Boost.beast and nlohmann-json are fetched automatically via the vendored
[`cmake/CPM.cmake`](cmake/CPM.cmake) unless you pass
`-DSIOXX_USE_SYSTEM_BOOST=ON` and `-DSIOXX_USE_SYSTEM_JSON=ON`. CPM caches the download under
`~/.cache/CPM` by default (override with `-DCPM_SOURCE_CACHE=<dir>` or the
`CPM_SOURCE_CACHE` env var), so repeat configures/CI runs don't re-fetch it.

```bash
sudo apt install cmake ccache

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

### Building a shared library

By default, sioxx is built as a static library. To build it as a shared
library, enable CMake's standard `BUILD_SHARED_LIBS` option when configuring:

```bash
cmake -S . -B build-shared \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON
cmake --build build-shared -j
cmake --install build-shared --prefix /usr/local   # optional
```

This produces `libsioxx.so` on Linux or `libsioxx.dylib` on macOS. Using a
separate build directory avoids retaining the static-library setting from an
earlier CMake configuration. Downstream projects link to the same
`sioxx::sioxx` target regardless of whether sioxx was built as a static or
shared library.

CMake options:

| Option | Default | Meaning |
| --- | --- | --- |
| `SIOXX_BUILD_EXAMPLES` | `ON` | build `examples/basic_client.cpp` |
| `SIOXX_BUILD_TESTS` | `ON` | build and register the GoogleTest suite in `tests/` |
| `SIOXX_USE_SYSTEM_BOOST` | `OFF` | use an already-installed `Boost` package instead of fetching one |
| `SIOXX_USE_SYSTEM_JSON` | `OFF` | use an already-installed `nlohmann_json` package instead of fetching one |
| `BUILD_SHARED_LIBS` | `OFF` | build sioxx as a shared library instead of a static library |

## Testing

Unit tests use GoogleTest (fetched automatically via `CPM.cmake`). Build with `-DSIOXX_BUILD_TESTS=ON` and run via `ctest` or
the test binary directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIOXX_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
# or: ./build/tests/sioxx_tests
```

There's deliberately no test that opens a real socket: `websocket_transport`
itself (the Boost.Beast plumbing) is exercised end-to-end by the
`sioxx_basic_client` example against a real server instead.

## Example test server

The repository includes a small Socket.IO server in
[`examples/test_server`](examples/test_server) for exercising
`sioxx_basic_client` against a live server. It uses the `/your_namespace`
namespace, logs the example's `hello` and `ping_ack` events, replies to the
acknowledgement, and periodically emits `your_message`.

Start it in a separate terminal:

```bash
cd examples/test_server
pnpm install
pnpm start              # JSON parser (default)
# or: pnpm start:msgpack
# or: pnpm start:cbor
# or: pnpm start:polling  # JSON over HTTP long-polling only
```

Then, from the repository root, run the matching client mode:

```bash
./build/sioxx_basic_client ws://localhost:3000
./build/sioxx_basic_client ws://localhost:3000 msgpack
./build/sioxx_basic_client ws://localhost:3000 cbor
./build/sioxx_basic_client polling  # default ws://localhost:3000
```

The `cbor` mode demonstrates a user-provided strategy in
[`examples/cbor_parser.hpp`](examples/cbor_parser.hpp). It uses
`nlohmann::json`'s built-in CBOR support. Run the matching bundled Node server
with `pnpm start:cbor`.

The server and client parser modes must match. The test server defaults to
port `3000`; override it with `PORT=3001 pnpm start` if needed. See the
[test-server README](examples/test_server/README.md) for details.

## API sketch

```cpp
#include <sioxx/sioxx.hpp>

sioxx::client_options opts;
opts.parser = sioxx::parser_kind::msgpack;   // or parser_kind::json (default)
opts.reconnect_attempts = 5;
opts.reconnect_delay = std::chrono::milliseconds(1000);
opts.reconnect_delay_max = std::chrono::milliseconds(30000);
opts.reconnect_randomization_factor = 0.5;

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

Applications can supply their own strategy with `parser_factory`. It takes
precedence over `parser`, and is called once for each client so the returned
parser may keep per-connection state:

```cpp
class my_parser : public sioxx::parser_base {
public:
    void encode(const sioxx::packet& packet,
                const sioxx::frame_writer& write) const override;
    bool decode(const std::string& payload, bool is_binary,
                sioxx::packet& out) override;
    std::string name() const override { return "my-parser"; }
};

sioxx::client_options opts;
opts.parser_factory = [] { return std::make_unique<my_parser>(); };
sioxx::client client(opts);
```

The factory must return a non-null `std::unique_ptr<parser_base>`; otherwise
client construction throws `std::invalid_argument`.

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

## Preparing a release

Keep the pending user-visible changes under `[Unreleased]` in
[`CHANGELOG.md`](CHANGELOG.md). When they are ready to publish, run the
release-preparation helper with the new version (with or without a leading
`v`):

```bash
./scripts/prepare-release.sh 0.0.5
```

The script updates the CMake project version, turns the current
`[Unreleased]` entries into a dated `0.0.5` section, creates a fresh
`[Unreleased]` section, and updates the changelog comparison links. Review
the result before committing it:

```bash
git diff -- CMakeLists.txt CHANGELOG.md
git add CMakeLists.txt CHANGELOG.md
git commit -m "chore: prepare release v0.0.5"
git tag v0.0.5
git push origin main v0.0.5
```

The tag-triggered GitHub Actions job verifies that the tag matches the CMake
version and that `CHANGELOG.md` contains non-empty notes for it. After the
multi-platform build and tests pass, those notes become the GitHub Release
description.

## Known limitations

- The JSON parser recognizes and emits the `BINARY_EVENT`/`BINARY_ACK`
  headers but does not implement the placeholder deconstruction/
  reconstruction scheme for multi-attachment binary payloads. Use the
  MessagePack parser if you need binary data — it carries it natively.
- Reconnection uses capped exponential backoff with symmetric jitter. Configure
  it with `reconnect_attempts`, `reconnect_delay`, `reconnect_delay_max`, and
  `reconnect_randomization_factor`.
- HTTP long-polling is used automatically only when the initial WebSocket
  connection fails. It is intentionally not upgraded back to WebSocket, and
  it opens a fresh HTTP connection for each poll/write.

## License

[MIT](LICENSE)
