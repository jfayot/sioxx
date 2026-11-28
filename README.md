<h1><img src="docs/_static/sioxx-logo.svg" alt="sioxx logo" height="80" align="center"> sioxx: modern Socket.IO client for C++</h1>

![GitHub Release](https://img.shields.io/github/v/release/jfayot/sioxx)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/jfayot/sioxx/blob/main/LICENSE)
[![Docs](https://img.shields.io/badge/Docs-online-blue.svg)](https://jfayot.github.io/sioxx/)
[![codecov](https://codecov.io/gh/jfayot/sioxx/graph/badge.svg)](https://codecov.io/gh/jfayot/sioxx)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/707d283e21d84d34bfdc1f59725dfa94)](https://app.codacy.com/gh/jfayot/sioxx/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Coverity Scan](https://scan.coverity.com/projects/jfayot-sioxx/badge.svg)](https://scan.coverity.com/projects/jfayot-sioxx)

[![Build](https://github.com/jfayot/sioxx/actions/workflows/build.yml/badge.svg)](https://github.com/jfayot/sioxx/actions/workflows/build.yml)

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
  - [FetchContent](#fetchcontent)
  - [Conan](#conan)
  - [Shared libraries](#shared-libraries)
  - [CMake options](#cmake-options)
- [Usage guide](#usage-guide)
  - [TLS server verification](#tls-server-verification)
  - [Parser selection](#parser-selection)
  - [Event buffers](#event-buffers)
  - [Acknowledgement deadlines](#acknowledgement-deadlines)
  - [Acknowledgement retries](#acknowledgement-retries)
  - [Threading model](#threading-model)
  - [Example test server](#example-test-server)
- [Compatibility](#compatibility)
- [Known limitations](#known-limitations)
- [Documentation](#documentation)
- [Development](#development)
  - [Testing](#testing)
  - [Sanitizers](#sanitizers)
  - [Static analysis](#static-analysis)
  - [Building the documentation](#building-the-documentation)
- [Contributing](#contributing)
- [AI disclosure](#ai-disclosure)
- [License](#license)

## Quick start

**Requires:** CMake ≥ 3.28, a C++17 compiler, Boost ≥ 1.74.0 (asio + beast),
nlohmann-json ≥ 3.8.0 and OpenSSL.

Boost 1.90.0 and nlohmann-json 3.12.0 are fetched automatically with CMake's
`FetchContent` unless you pass `-DSIOXX_USE_SYSTEM_BOOST=ON` and
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
opts.proxy = sioxx::proxy_options{  // optional
    "http://proxy.example.com:8080", "user", "password"};

sioxx::client client(opts);
client.set_open_listener([] { /* Engine.IO connection opened */ });
client.set_close_listener([](const std::string& reason) { /* ... */ });
client.set_reconnect_listener([](unsigned attempt, unsigned delay_ms) {
    /* reconnect scheduled; attempt is zero-based */
});
client.set_reconnecting_listener([] { /* reconnect attempt starting */ });

auto sock = client.socket("/chat");  // any namespace path
sock->on("message", [](const std::string& event, sioxx::message data) {
    // data is an nlohmann::json array of the event's arguments
});
sock->on_any([](const std::string& event, sioxx::message data) {
    // observes every incoming event after its event-specific listener
});

client.connect("wss://chat.example");

if (client.opened()) {
    const std::string session_id = client.get_sessionid();
}

// Events emitted before the namespace connects are buffered and sent in order.
sock->emit("hello", sioxx::json{"world"});
sock->emit("ping_ack", sioxx::json::array({1, 2, 3}), [](sioxx::message reply) {
    // ack callback
});

client.sync_close();
```

Obtain namespace sockets with `client.socket()`. The client constructs the
socket, manages its connection state, and delivers events and acknowledgements
from the server to registered callbacks.

## Installation and configuration

### FetchContent

CMake projects can consume sioxx directly from its tagged Git repository
without installing it first:

```cmake
cmake_minimum_required(VERSION 3.28)
project(my_app LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
  sioxx
  GIT_REPOSITORY https://github.com/jfayot/sioxx.git
  GIT_TAG v0.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sioxx)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE sioxx::sioxx)
```

The release tag keeps builds reproducible. sioxx fetches Boost and
nlohmann-json by default; OpenSSL must be available on the system.

To select other supported dependency versions, declare them before sioxx.
CMake uses the first `FetchContent_Declare()` call for each dependency:

```cmake
include(FetchContent)

FetchContent_Declare(
  Boost
  URL https://archives.boost.org/release/1.74.0/source/boost_1_74_0.tar.bz2
  URL_HASH SHA256=83bfc1507731a0906e387fc28b7ef5417d591429e51e788417fe9ff025e116b1
  SOURCE_SUBDIR __sioxx_no_cmake_subdir
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.8.0
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR __sioxx_no_cmake_subdir
  EXCLUDE_FROM_ALL
)

FetchContent_Declare(
  sioxx
  GIT_REPOSITORY https://github.com/jfayot/sioxx.git
  GIT_TAG v0.4.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sioxx)

target_link_libraries(my_app PRIVATE sioxx::sioxx)
```

The minimum supported versions are Boost 1.74.0 and nlohmann-json 3.8.0.
Pin the selected tag or archive URL, and provide an expected hash when the
upstream project publishes one.

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

Consumers can require `sioxx/0.4.0` and use Conan's `CMakeDeps` and
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
| `SIOXX_BUILD_EXAMPLES`    | `ON`    | build the basic client and, when Qt 5/6 is available, the Qt chat client |
| `SIOXX_BUILD_TESTS`       | `ON`    | build and register the GoogleTest suite in `tests/`                      |
| `SIOXX_INSTALL`           | `ON`    | generate installation rules                                              |
| `SIOXX_BUILD_DOCS`        | `OFF`   | build the documentation                                                  |
| `SIOXX_USE_SYSTEM_BOOST`  | `OFF`   | use an already-installed `Boost` package instead of fetching one         |
| `SIOXX_USE_SYSTEM_JSON`   | `OFF`   | use an already-installed `nlohmann_json` package instead of fetching one |
| `SIOXX_ENABLE_CLANG_TIDY` | `OFF`   | run `clang-tidy` while compiling sioxx targets                           |
| `BUILD_SHARED_LIBS`       | `OFF`   | build sioxx as a shared library instead of a static library              |

**Note:** `SIOXX_BUILD_EXAMPLES`, `SIOXX_BUILD_TESTS` and `SIOXX_INSTALL` default to `OFF` when sioxx is included as a subproject.

## Usage guide

### TLS server verification

With `client_options::verify_tls` enabled by default, secure WebSocket and
HTTPS polling connections require a trusted certificate chain and a certificate
that matches the DNS name or IP address in the connection URL. This also
applies to the Socket.IO server reached through an HTTP proxy tunnel.

sioxx loads CA certificates from OpenSSL's default trust paths. For a private
CA, include it in a trusted CA bundle and point `SSL_CERT_FILE` at that bundle.
Use `verify_tls = false` only for controlled development. It disables both
certificate chain and server identity verification.

### Parser selection

Both parsers implement the same `sioxx::parser_base` interface
(`json_parser` / `msgpack_parser`) and are picked with
`client_options::parser`. This must match whatever parser the server side is
configured with (e.g. Node's `socket.io` default vs.
`socket.io-msgpack-parser`) — sioxx does not negotiate it automatically,
exactly like the JS clients don't either.

Applications can supply their own strategy with `parser_factory`. It takes
precedence over `parser` and is called once during construction. The client
owns the returned parser and reuses it across connections.

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

Both built-in parsers support `nlohmann::json::binary_t` values created with
`sioxx::binary_message()`, including values nested in arrays and objects. The
default JSON parser uses Socket.IO's `BINARY_EVENT`/`BINARY_ACK` placeholder
and attachment frames. `msgpack_parser` uses nlohmann-json's MessagePack
support to carry these values natively in one binary frame.

The default JSON parser limits each incoming binary event or acknowledgement
to 64 attachments and 8 MiB, counting the Socket.IO header and decoded
attachment bytes together. It discards packets that exceed either limit and
resumes at the next text packet.

The built-in JSON and MessagePack decoders accept up to 128 levels of arrays
and objects, counting the outermost container as level one. sioxx discards
Socket.IO packets that exceed the limit and continues decoding subsequent
packets. An over-limit Engine.IO handshake is rejected.

### Event buffers

Each namespace has an in-memory FIFO buffer for events emitted while that
namespace is disconnected. All `emit()` overloads share the buffer;
it flushes when the server acknowledges the namespace CONNECT.

| Lifecycle event | Buffered events and pending acknowledgements |
| --- | --- |
| Before the first connection | Queue events for the namespace CONNECT. |
| Namespace disconnect, including `socket->disconnect()` | Preserve the buffer and ACK callbacks; resume on the next namespace CONNECT. |
| Transport failure or automatic reconnect | Preserve unsent events and ACK callbacks; timed deadlines keep running. |
| Reconnect attempts exhausted | Retain unsent events for a later `client.connect()`; timed events still expire. |
| `client.close()`, `sync_close()`, or client destruction | Discard all buffered events and pending ACK callbacks silently. |
| Socket destruction | Discard its buffer and pending ACK callbacks silently. |

After explicit client shutdown, every `emit()` overload throws
`std::logic_error` until `client.connect()` is called again. This also applies
to retained sockets and sockets obtained after shutdown. Reconnecting starts
with an empty buffer; listeners and namespace authentication are retained.

Packets already handed to a transport, including an emission in progress
when shutdown begins, cannot be recalled. They are only automatically retried
when the retry overload described below is used.
Successful transport handoff does not guarantee server delivery. ACK callbacks
without a deadline can remain pending indefinitely until an ACK or shutdown.
The buffer has no size limit; use timed emissions to expire stale work.
FIFO order applies within each namespace's buffer, with concurrent calls ordered
by acquisition of the socket mutex; no order is promised between namespaces.

```cpp
client.close();                    // Discard work from the previous session.
client.connect("ws://localhost:3000");
sock->emit("save", sioxx::json::array({"fresh draft"}));
```

### Acknowledgement deadlines

Use the timeout overload to bound how long an emission waits for a reply:

```cpp
sock->emit("save", sioxx::json::array({"draft"}), std::chrono::seconds(5),
    [](sioxx::socket::ack_status status, sioxx::message reply) {
        if (status == sioxx::socket::ack_status::timeout) {
            // No acknowledgement arrived before the deadline.
            return;
        }
        // Use reply.
    });
```

The duration must be positive and the callback nonempty. The deadline starts
at `emit()`, including packet preparation, waiting for the socket lock, and
time buffered before connection. Events that expire during preparation or in
the socket buffer are discarded. Namespace/transport disconnects and automatic
reconnection do not reset deadlines. Explicit client shutdown and socket
destruction cancel pending timed acknowledgements and their buffered events
without notification. After
client shutdown, all emissions throw `std::logic_error` until `client.connect()`
is called again. A timeout returns null reply
data and does not prove that the server did not process an already-sent event.
Late acknowledgements are ignored; this overload does not retry. The
three-argument acknowledgement overload has no automatic deadline.

### Acknowledgement retries

Opt in for an individual event by passing the number of additional attempts:

```cpp
sock->emit("save", sioxx::json::array({{{"operation_id", "save-42"}, {"text", "draft"}}}),
    std::chrono::seconds(5), 2,
    [](sioxx::socket::ack_status status, sioxx::message reply) {
        // One callback: acknowledged, or timeout after at most three attempts.
    });
```

Here `2` means at most two retries after the initial attempt. Each attempt gets
five seconds, including preparation, buffering, and disconnected time. Expiry
consumes an attempt even if it was never sent; an offline operation therefore
still exhausts its budget. Zero retries behaves like the timeout-only overload.
There is no extra back-off delay, and existing overloads do not opt in to retries.
Connected retries use a separate send worker so encoding and send contention
do not hold up other acknowledgement deadlines. An encoding error during a
buffered send or background retry leaves that attempt to expire normally.
If encoding fails synchronously while the acknowledgement is pending and
within its deadline, `emit()` throws and cancels that acknowledgement. After
the deadline passes, the normal timeout and retry rules apply.

Every attempt reuses the payload and gets a fresh ACK ID. Replies to older
attempts are ignored. The callback runs once, on success or final timeout with
null reply data. It runs outside socket locks on the ACK delivery thread or the
existing deadline worker, including with a caller-owned `io_context`.
Callbacks must not throw. Shutdown and socket destruction cancel remaining
attempts silently; `sync_close()` drains running retry operations and timed
callbacks. A new `client.connect()` does not revive cancelled retries.

A retry joins the end of the namespace buffer. Other events can pass an event
waiting for its ACK; there is no ordering guarantee between different retry
operations. Retries can repeat an operation the server already processed.
Include a stable application operation ID, as above, and deduplicate it on the
server when effects must only happen once. Transport reconnection remains
separately configured through `client_options`.

### Threading model

WebSocket connections run a dedicated `boost::asio::io_context` thread, while
HTTP polling uses separate poll and write workers. The `on_*` callbacks
(`socket->on(...)`, `socket->on_any(...)`, open/close listeners, ack callbacks)
run on the thread handling the event—normally a library worker, though an
intentional close can notify on the thread calling `close()`. If you're
updating UI state or anything not thread-safe, hop back to your own thread or
queue from inside the callback.

`client.close()` requests shutdown and is safe to call from a library
callback. Queued Engine.IO frames arriving after shutdown are ignored.
`client.sync_close()` waits for library-owned transport,
reconnect, retry, and heartbeat workers and active timed acknowledgement
callbacks. Each concurrent `sync_close()` call waits for that work. Call
`sync_close()` and destroy the client outside library callbacks.

To integrate transport networking with an existing Asio event loop, pass its
`io_context` when constructing the client:

```cpp
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <sioxx/sioxx.hpp>
#include <thread>

boost::asio::io_context io_context;
auto work = boost::asio::make_work_guard(io_context);
sioxx::client client(io_context);
client.connect("wss://example.com");
std::thread io_thread([&] { io_context.run(); });

// From the application thread when ready to shut down.
client.sync_close();
work.reset();
io_thread.join();
```

In this mode, WebSocket and HTTP polling operations and their callbacks run on
the supplied context and are serialized by an internal strand. The application
owns running and stopping the context and keeps it alive longer than the
client. `sync_close()` and client destruction request transport shutdown on
that context. Pending I/O and ordinary transport callbacks can outlive the
call. Keep the context running and keep callback state alive until the
application has drained the client's work. If the context is stopped, restart
it and resume dispatch to complete shutdown. The example uses a dedicated
context and joins its thread after releasing the work guard.

Reconnection delays and Engine.IO heartbeat monitoring use sioxx workers.
Acknowledgement deadlines also use a lazily started worker
per socket, including with a caller-owned context. Timeout callbacks run on
that worker, or on the ACK delivery thread if it observes an expired deadline.
Callbacks run outside socket locks and must not throw. `close()` cancels
pending acknowledgements. `sync_close()` also waits for active timed
acknowledgement callbacks, including callbacks running on the supplied
context. Idle deadline workers may remain allocated until their sockets are
destroyed.

### Example test server

The repository includes a small Socket.IO server in
[`examples/basic_client/server`](examples/basic_client/server) for exercising
`sioxx_basic_client` against a live server. It uses the `/your_namespace`
namespace, logs the example's `hello` and `ping_ack` events, replies to the
acknowledgement, and periodically emits `your_message`.

Start it in a separate terminal:

```bash
cd examples/basic_client/server
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
[`examples/basic_client/cbor_parser.hpp`](examples/basic_client/cbor_parser.hpp). It uses
`nlohmann::json`'s built-in CBOR support. Run the matching bundled Node server
with `pnpm start:cbor`.

The server and client parser modes must match. The test server defaults to
port `3000`; override it with `PORT=3001 pnpm start` if needed. See the
[test-server README](examples/basic_client/server/README.md) for details.

For a GUI integration, the optional [Qt chat example](examples/qt_chat)
demonstrates namespace authentication, reconnects, acknowledgements,
background-thread callback dispatch, transport selection, and MessagePack
binary payloads. It is built with the other examples when Qt 5 or Qt 6 Widgets
is available; otherwise CMake skips it without making Qt a required dependency.

## Compatibility

The v1.0 release requires the end-to-end suite to pass against the
[pinned Socket.IO 4.8.1 server](tests/e2e/server/package.json) over Engine.IO v4
in each [Build matrix job](.github/workflows/build.yml).

| Platform | Architecture | Compiler |
| --- | --- | --- |
| Linux | x64 | GCC and Clang |
| macOS | ARM64 | Apple Clang |
| Windows | x64 and ARM64 | MSVC |

The suite covers WS, WSS, HTTP and HTTPS long-polling, HTTP proxies with
Basic authentication, and TLS server verification. It exercises WebSocket-first
connections with automatic polling fallback and the polling-only mode set by
`client_options::force_http_polling`.

### v1.x source and binary compatibility

Code using the documented public API can be rebuilt against newer v1.x
releases. This covers `client`, `socket`, their options and callbacks, message
types and helpers, and the custom parser extension API (`parser_factory`,
`parser_base`, `frame_writer`, `packet`, and `packet_type`). See the
[API reference](docs/api.rst) and [upgrade guide](docs/migration.rst).

Installed CMake packages use `SameMajorVersion` starting at v1.0.
For example, an installed v1.1 package satisfies
`find_package(sioxx 1.0 CONFIG REQUIRED)`.

Rebuild and relink applications and libraries that use sioxx when upgrading
the shared library. `sioxx::message` exposes `nlohmann::json`, so use the same
JSON version and ABI-affecting settings as the library. Starting at v1.0,
shared libraries use the full release version as `SOVERSION` on platforms
that support versioned shared-library names.

## Known limitations

- WS and WSS connections close if an incoming WebSocket message exceeds
  16 MiB, including when the message arrives in fragments.
- Reconnection uses capped exponential backoff with symmetric jitter. Configure
  it with `reconnect_attempts`, `reconnect_delay`, `reconnect_delay_max`, and
  `reconnect_randomization_factor`.
- HTTP long-polling is used automatically only when the initial WebSocket
  connection fails. It is intentionally not upgraded back to WebSocket, and
  it opens a fresh HTTP connection for each poll/write. Polling response bodies
  are limited to 8 MiB, and packets within each response are delivered in order.
- Proxy connections support HTTP proxies. HTTPS proxy endpoints and proxy
  authentication schemes other than Basic are not currently supported.

See the [resource-limit review](docs/resource_limits.rst) for decoder and HTTP
framing limits and operational boundaries. Receive-size limits do not bound
total process memory, traffic rates, or outgoing queues. Polling requests have
no request deadline; a stalled peer can delay `sync_close()` with library-owned
I/O.

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
cmake --build build-e2e --config Release --parallel
ctest --test-dir build-e2e -C Release -L e2e --no-tests=error --output-on-failure --parallel 1
```

The regular unit-test build does not require Node.js.

The E2E suite covers WebSocket and polling connections, automatic polling
fallback, JSON and MessagePack interoperability, acknowledgements, binary
payloads with both built-in parsers, routing across multiple namespaces, and
reconnection after dropped WebSockets and server restarts. CI combines unit
and E2E coverage in the report published to Codecov. Codecov requires at least
95% overall coverage; the patch coverage check is disabled.

The package compatibility configurations used by CI are captured in
`CMakePresets.json` files at the repository root and under
`tests/integration/`. List the available root workflows with:

```bash
cmake --workflow --list-presets
```

The root presets build and install static or shared sioxx variants, while the
consumer and subproject presets configure, build, and test the corresponding
downstream integration projects.

### Sanitizers

Linux CI runs the unit and E2E suites in separate ThreadSanitizer and
AddressSanitizer/UBSan builds. AddressSanitizer also checks for leaks. Sanitizer
findings fail the job; no suppression lists are applied. Acknowledgement
retries and concurrency, heartbeat shutdown, buffer lifecycle, and callback-triggered
shutdown and reconnect regressions run another 20 times per build to exercise
callback delivery, reconnect cancellation, and shutdown.

To reproduce a Linux build with Clang, install the E2E server dependencies as
above and run:

```bash
cmake -S . -B /tmp/sioxx-sanitizers \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS_DEBUG="-O1 -g" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DSIOXX_BUILD_EXAMPLES=OFF \
  -DSIOXX_BUILD_TESTS=ON \
  -DSIOXX_BUILD_E2E_TESTS=ON
cmake --build /tmp/sioxx-sanitizers --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir /tmp/sioxx-sanitizers --output-on-failure --timeout 60
```

For ThreadSanitizer, use a separate build directory, replace
`-fsanitize=address,undefined` with `-fsanitize=thread`, and run CTest with
`TSAN_OPTIONS=halt_on_error=1`. On Linux with LLVM 18, the CI runner sets
`vm.mmap_rnd_bits=28` to make room for ThreadSanitizer's shadow memory.

### Parser fuzz tests

The Linux ASan/UBSan job runs separate 10-second libFuzzer tests for the
built-in JSON and MessagePack decoders, URL parsing, Engine.IO frame handling,
and HTTP polling payloads. The JSON test feeds sequences of text and binary
frames through one parser and checks that it can still decode a valid event
afterwards. The polling test checks that decoded binary packets can round-trip
through the encoder. CI preserves failing inputs as an artifact.

With Clang and its libFuzzer runtime installed, run the same tests locally.

```bash
cmake -S . -B /tmp/sioxx-fuzz \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DSIOXX_BUILD_TESTS=OFF \
  -DSIOXX_BUILD_EXAMPLES=OFF \
  -DSIOXX_BUILD_FUZZ_TESTS=ON
cmake --build /tmp/sioxx-fuzz --parallel --target \
  sioxx_fuzz_json sioxx_fuzz_msgpack sioxx_fuzz_url sioxx_fuzz_engineio sioxx_fuzz_polling
ctest --test-dir /tmp/sioxx-fuzz -L fuzz --no-tests=error --output-on-failure
```

Each fuzzer reads seeds from `tests/fuzz/corpus` and writes new inputs and
failure artifacts under the build directory. Pass a saved input file directly
to the matching executable to replay it.

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
development setup, testing expectations, and pull request guidelines. Work
toward the stable API is tracked in the [1.0.0 TODO](TODO.md).

## AI disclosure

This project was designed and developed with assistance from AI tools:

| Tool     | Model          | Environment |
| -------- | -------------- | ----------- |
| Codex    | `gpt-5.6-sol`  | Codex       |
| opencode | `gpt-oss:120b` | Local       |

AI-assisted work remains subject to human review and maintainer decisions.

## License

[MIT](LICENSE)
