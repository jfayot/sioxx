<h1><img src="docs/_static/sioxx-logo.svg" alt="sioxx logo" height="80" align="center"> sioxx: modern Socket.IO client for C++</h1>

![GitHub Release](https://img.shields.io/github/v/release/jfayot/sioxx)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/jfayot/sioxx/blob/main/LICENSE)
[![Docs](https://img.shields.io/badge/Docs-online-blue.svg)](https://jfayot.github.io/sioxx/)

![Linux](https://github.com/jfayot/sioxx/actions/workflows/build-linux.yml/badge.svg)
![macOS](https://github.com/jfayot/sioxx/actions/workflows/build-macos.yml/badge.svg)
![Windows](https://github.com/jfayot/sioxx/actions/workflows/build-windows.yml/badge.svg)

![CMake Integration](https://github.com/jfayot/sioxx/actions/workflows/cmake-integration.yml/badge.svg)
![Packaging](https://github.com/jfayot/sioxx/actions/workflows/packaging.yml/badge.svg)
![Documentation](https://github.com/jfayot/sioxx/actions/workflows/docs.yml/badge.svg)

A C++ implementation of `socket.io`'s client functionality with the following stack:

|               |                                                                                                                                    |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| JSON          | **nlohmann-json**                                                                                                                  |
| WebSocket     | **Boost.Asio** + **Boost.Beast**                                                                                                   |
| Wire protocol | **JSON or MessagePack**, selectable per-client                                                                                     |
| Build         | **modern CMake**, `FetchContent` for nlohmann-json and Boost, full `install(EXPORT ...)` so `find_package(sioxx)` works downstream |

## Table of contents

- [Quick start](#quick-start)
- [API example](#api-example)
- [Installation and configuration](#installation-and-configuration)
  - [Conan](#conan)
  - [Shared libraries](#shared-libraries)
  - [CMake options](#cmake-options)
- [Usage guide](#usage-guide)
  - [Parser selection](#parser-selection)
  - [Threading model](#threading-model)
  - [Example test server](#example-test-server)
- [Known limitations](#known-limitations)
- [Documentation](#documentation)
- [Development](#development)
  - [Testing](#testing)
  - [Static analysis](#static-analysis)
  - [Building the documentation](#building-the-documentation)
- [Contributing](#contributing)
- [AI disclosure](#ai-disclosure)
- [License](#license)

## Quick start

**Requires:** CMake ≥ 3.28, a C++17 compiler, Boost 1.90 (asio + beast), nlohmann-json 3.12.0 and OpenSSL.

Boost and nlohmann-json are fetched automatically with CMake's `FetchContent` unless you pass `-DSIOXX_USE_SYSTEM_BOOST=ON` and
`-DSIOXX_USE_SYSTEM_JSON=ON`.

```bash
sudo apt install cmake ccache libssl-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /usr/local   # optional
```

This produces `libsioxx.a` and the
`sioxx_basic_client` example. It also emits a `sioxxConfig.cmake`, so any
downstream project can just:

```cmake
find_package(sioxx CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE sioxx::sioxx)
```

## API example

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

auto sock = client.socket("/chat");  // any namespace path
sock->on("message", [](const std::string& event, sioxx::message data) {
    // data is an nlohmann::json array of the event's arguments
});

client.connect("wss://chat.example");

sock->emit("hello", sioxx::json{"world"});
sock->emit("ping_ack", sioxx::json::array({1, 2, 3}), [](sioxx::message reply) {
    // ack callback
});

client.close();
```

## Installation and configuration

### Conan

A Conan 2 recipe is provided for building and packaging sioxx together with
its Boost, nlohmann-json, and OpenSSL dependencies:

```bash
conan profile detect --force
conan create . --test-folder tests/packaging/conan --build=missing
```

Build the shared-library package with:

```bash
conan create . \
  --test-folder tests/packaging/conan \
  -o sioxx/*:shared=True \
  --build=missing
```

Consumers can require `sioxx/0.1.1` and use Conan's `CMakeDeps` and
`CMakeToolchain` generators. The generated CMake target is `sioxx::sioxx`.

### Shared libraries

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

### CMake options

| Option                    | Default | Meaning                                                                  |
| ------------------------- | ------- | ------------------------------------------------------------------------ |
| `SIOXX_BUILD_EXAMPLES`    | `ON`    | build `examples/basic_client.cpp`                                        |
| `SIOXX_BUILD_TESTS`       | `ON`    | build and register the GoogleTest suite in `tests/`                      |
| `SIOXX_INSTALL`           | `ON`    | generate installation rules                                              |
| `SIOXX_BUILD_DOCS`        | `OFF`   | build the documentation                                                  |
| `SIOXX_USE_SYSTEM_BOOST`  | `OFF`   | use an already-installed `Boost` package instead of fetching one         |
| `SIOXX_USE_SYSTEM_JSON`   | `OFF`   | use an already-installed `nlohmann_json` package instead of fetching one |
| `SIOXX_ENABLE_CLANG_TIDY` | `OFF`   | run `clang-tidy` while compiling sioxx targets                           |
| `BUILD_SHARED_LIBS`       | `OFF`   | build sioxx as a shared library instead of a static library              |

**Note:** `SIOXX_BUILD_EXAMPLES`, `SIOXX_BUILD_TESTS` and `SIOXX_INSTALL` default to `OFF` when sioxx is included as a subproject.

## Usage guide

### Parser selection

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

### Threading model

`websocket_transport` runs its own `boost::asio::io_context` on a background
thread per connection. All `on_*` callbacks (`socket->on(...)`, open/close
listeners, ack callbacks) fire on that thread — if you're updating UI state
or anything not thread-safe, hop back to your own thread/queue from inside
the callback.

### Example test server

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

## Documentation

**[Read the sioxx documentation](https://jfayot.github.io/sioxx/)** for an
overview of the library, architecture, usage examples, and the complete C++
API reference.

## Development

### Testing

Unit tests use GoogleTest (fetched automatically via `FetchContent`). Build with `-DSIOXX_BUILD_TESTS=ON` and run via `ctest` or
the test binary directly:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIOXX_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
# or: ./build/tests/sioxx_tests
```

The default unit suite does not open real sockets. Real protocol and transport
behavior is covered by the opt-in end-to-end suite below and can also be
exercised manually with `sioxx_basic_client`.

### End-to-end tests

The opt-in end-to-end suite starts a dedicated Node.js Socket.IO server on an
automatically selected local port and runs GoogleTest assertions against it.
Install the server dependency and run the tests with:

```bash
pnpm --dir tests/e2e/server install --frozen-lockfile
cmake -S . -B build-e2e \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIOXX_BUILD_TESTS=ON \
  -DSIOXX_BUILD_E2E_TESTS=ON
cmake --build build-e2e -j
ctest --test-dir build-e2e -L e2e --output-on-failure
```

The regular unit-test build does not require Node.js.

The E2E suite covers WebSocket and polling connections, automatic polling
fallback, JSON and MessagePack interoperability, acknowledgements, binary
MessagePack payloads, routing across multiple namespaces, and WebSocket
reconnection after an unexpected server shutdown.

### Static analysis

Install `clang-tidy`, then enable it when configuring:

```bash
cmake -S . -B build-tidy -DSIOXX_ENABLE_CLANG_TIDY=ON
cmake --build build-tidy -j
```

The checks are configured in `.clang-tidy`. For now, only the
`portability-*` checks are enabled. Analysis is limited to the sioxx library;
fetched dependencies, tests, and examples are excluded.

### Building the documentation

The API documentation is generated with Doxygen and Sphinx using the Breathe
extension and the PyData Sphinx theme. Graphviz's `dot` executable is required
to render the architecture diagram. Install Doxygen and Graphviz, then install
the Python dependencies:

```bash
# Debian/Ubuntu
sudo apt install doxygen graphviz

# macOS
brew install doxygen graphviz

python -m pip install -r docs/requirements.txt
```

Verify that Graphviz is available with `dot -V` before building the
documentation.

Configure a documentation build and build the `Sphinx` target:

```bash
cmake -S . -B build-docs \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIOXX_BUILD_DOCS=ON \
  -DSIOXX_BUILD_TESTS=OFF \
  -DSIOXX_BUILD_EXAMPLES=OFF
cmake --build build-docs --target Sphinx --parallel
```

Open `build-docs/docs/sphinx/index.html` in a browser to view the
generated site. Building the default target also generates both the Doxygen
and Sphinx documentation when `SIOXX_BUILD_DOCS=ON`. To install the generated
HTML alongside the library, run:

```bash
cmake --install build-docs --prefix /usr/local
```

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for
development setup, testing expectations, and pull request guidelines.

## AI disclosure

This project was designed and developed with assistance from AI tools:

| Tool     | Model          | Environment |
| -------- | -------------- | ----------- |
| Codex    | `gpt-5.6-sol`  | Codex       |
| opencode | `gpt-oss:120b` | Local       |

AI-assisted work remains subject to human review and maintainer decisions.

## License

[MIT](LICENSE)
