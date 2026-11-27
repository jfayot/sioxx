# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changes :construction:

- Added bounded fuzzing for JSON, MessagePack, URLs, Engine.IO frames, and
  polling payloads to Linux ASan/UBSan CI.
- Reduce memory use when delivering HTTP polling batches by processing one
  packet at a time.
- Set an explicit 16 MiB receive limit for WebSocket and secure WebSocket
  messages.

### Fixes :wrench:

- Ignore queued Engine.IO frames after explicit shutdown, and leave active
  heartbeat callbacks for `sync_close()` to drain. Synchronize HTTP polling
  writer shutdown with the writer's condition-variable wait.
- Make overlapping `sync_close()` calls wait for the same shutdown work,
  including an active reconnect callback.
- Limit incoming JSON and MessagePack to 128 levels of arrays and objects,
  counting the outermost container as level one. Discard over-limit Socket.IO
  packets and reject over-limit Engine.IO handshakes.
- Reject malformed Engine.IO open handshakes through the connection error path
  on WebSocket and HTTP polling.
- Bound incoming JSON binary events and acknowledgements to 64 attachments
  and 8 MiB for the Socket.IO header and attachments combined. The parser
  discards packets that exceed either limit and resumes at the next text
  packet.

## [0.4.0] - 2026-11-27

### Breaking changes :mega:

- Made the `socket` constructor and the `dispatch_event()`, `dispatch_ack()`,
  and `mark_connected()` methods private. Obtain namespace sockets through
  `client.socket()`.

### Additions :tada:

- Added opt-in per-emission acknowledgement retries with per-attempt timeouts,
  fresh ACK IDs, one final outcome callback, and silent shutdown cancellation.
  Retry budgets continue to expire while disconnected; retries append to the
  namespace buffer and may duplicate effects already processed by the server.
- Added per-emission acknowledgement deadlines with success/timeout outcomes,
  expiry of buffered events, and suppression of late acknowledgements.
  Explicit shutdown cancels pending deadlines, and `sync_close()` drains
  running timed acknowledgement callbacks.

### Changes :construction:

- Froze the v1.0 public API, including both I/O ownership modes, the existing
  reconnect settings, and custom parser support. Clarified the lifecycle
  listener and callback lifetime contracts and expanded installed-package API
  coverage.
- Defined the v1.x source compatibility and shared-library rebuild policy.
  From v1.0, installed CMake packages match within the same major version and
  shared-library `SOVERSION` uses the full release version.
- Documented and tested the `sync_close()` shutdown guarantees for clients
  using a caller-owned Asio context.
- Made the Socket.IO 4.8.1 end-to-end suite part of the release build on Linux,
  macOS, and Windows and documented the v1.0 compatibility matrix.
- Defined per-namespace FIFO buffer behavior across disconnects, transport
  failures, and reconnect exhaustion. Explicit client shutdown now discards
  every buffered event and pending ACK callback, and all `emit()` overloads
  reject calls until `client.connect()` is called again.
- Added Linux ThreadSanitizer and AddressSanitizer/UBSan CI jobs covering unit
  and E2E tests, with repeated synchronized acknowledgement concurrency tests.
- Set Codecov's overall coverage target to 95% and disable the patch coverage
  check.

### Fixes :wrench:

- Stabilized reconnect E2E tests across platforms by explicitly rejecting
  handshakes and preventing server fixture restarts after test shutdown.
- Made the WebSocket reconnect test independent of server restart timing.
- Secure WebSocket and HTTPS polling now reject certificates that do not match
  the destination DNS name or IP address when TLS verification is enabled,
  including for connections through an HTTP proxy.
- Released unopened WebSocket transports when reconnect is cancelled before
  their I/O worker starts.
- Marked all namespaces disconnected before invoking their callbacks outside
  the client socket registry lock. Callbacks can buffer work on other
  namespaces or request shutdown after a transport failure.
- Isolated retry sends from deadline processing and bound attempts to their
  connection. Expired or cancelled attempts are discarded before handoff.
- Cancelled timed emissions when synchronous encoding fails. Failed buffered
  and background sends expire normally within the configured retry budget.
- Synchronized reconnect cancellation with the worker's wait mutex to prevent
  missed wake-ups and shutdown delays of a full reconnect interval.
- Synchronized heartbeat stop notification with the worker's wait mutex to
  prevent missed wake-ups and shutdown delays of a full heartbeat interval.
- Discard timed emissions whose deadline elapsed while preparing the packet
  or waiting for the socket lock, before they reach the transport.
- Synchronized listener replacement with reconnect callback invocation and
  prevented overflow when calculating reconnect delays for invalid attempts.
- Synchronized HTTP polling worker ownership during shutdown and transport
  listener replacement during asynchronous callback delivery.

## [0.3.0] - 2026-08-22

### Additions :tada:

- Added reconnect scheduling/attempt callbacks and thread-safe
  `client::opened()` and `client::get_sessionid()` accessors for closer
  `socket.io-client-cpp` API compatibility.
- Added client constructors accepting a caller-owned Boost.Asio `io_context`
  for WebSocket and HTTP long-polling networking.
- Added complete binary attachment support to the default JSON parser,
  including nested and multiple `BINARY_EVENT` and `BINARY_ACK` attachments
  over WebSocket and HTTP long-polling.
- Added HTTP proxy support with optional Basic authentication for WebSocket,
  secure WebSocket, HTTP polling, and HTTPS polling connections.
- Added a dedicated checklist for planning and validating the 1.0.0 release.
- Added tagged CMake `FetchContent` examples for consuming sioxx directly and
  selecting parent-provided Boost and nlohmann-json versions.
- Added an optional Qt Widgets chat example demonstrating safe GUI-thread
  dispatch, namespace authentication, events, acknowledgements, reconnection,
  HTTP polling, MessagePack, and binary payloads.

### Changes :construction:

- Made `client` explicitly non-copyable so one façade has unambiguous
  ownership of connection shutdown.
- Lowered the supported dependency minimums to Boost 1.74.0 and nlohmann-json
  3.8.0 while retaining the newer default fetched versions.
- Grouped the basic client, custom CBOR parser, and matching Socket.IO server
  under `examples/basic_client`.

### Fixes :wrench:

- Kept FetchContent examples and the installed-package integration consumer
  synchronized when preparing a new release.
- Added explicit two-phase client shutdown: `close()` requests shutdown
  without waiting, while `sync_close()` cancels pending reconnect delays and
  waits for transport and heartbeat workers. Client destruction uses the
  synchronous form.
- Invoked the client open listener when Engine.IO opens, even when the client
  only requests non-root Socket.IO namespaces.
- Serialized HTTP long-polling writes so multi-frame binary packets and
  back-to-back events preserve their wire order.
- Avoided passing the comparatively large `client_options` structure by value
  when constructing a client while retaining efficient temporary handling.

## [0.2.0] - 2026-08-12

### Additions :tada:

- Added catch-all `socket::on_any()` listeners for observing every incoming
  namespace event, including acknowledgement-aware listeners.
- Added per-namespace buffering for outgoing events emitted before the
  namespace connects, including events that request acknowledgements.
- Added a practical, standalone migration guide from `socket.io-client-cpp` to
  `sioxx` to the generated HTML documentation.
- Added acknowledgement-aware event listeners for replying to server events
  that request an acknowledgement.
- Added per-namespace authentication payloads, configurable Engine.IO endpoint
  paths, and URL-encoded handshake query parameters.
- Added an opt-in GoogleTest end-to-end suite backed by dedicated Node.js
  Socket.IO servers, covering events, acknowledgements, HTTP polling,
  automatic fallback, MessagePack with binary data, and multiple namespaces.
- Added consistent sioxx logo branding to the README and generated Sphinx
  documentation.
- Added a contributing guide and GitHub issue and pull request templates.
- Added a `format.sh` script, VS Code formatter defaults, and contributor
  documentation for formatting C/C++ code and cmake files.
- Added matrix strategy to build-windows workflow to test on Windows ARM64
  architecture

### Changes :construction:

- Reported each end-to-end GoogleTest case separately through CTest instead of
  presenting the suite as a single test result.
- Isolated the Ninja-based CMake integration FetchContent cache from workflows
  using other generators.
- Consolidated Linux, macOS, and Windows builds into one matrix workflow,
  cancelled superseded pull request runs, and removed the duplicate standalone
  end-to-end build while retaining end-to-end coverage testing.
- Moved package compatibility configurations into reusable CMake workflow
  presets, limited compatibility and Conan jobs to relevant changes, and
  standardized action permissions, versions, and FetchContent cache keys.
- Added scheduled Coverity Scan analysis and its README status badge.
- Added combined unit and end-to-end test coverage reporting through Codecov.
- Added a repository agent guide covering project structure, validation, and
  generated documentation maintenance.
- Expanded the agent guide with Karpathy-inspired principles for simple,
  focused, and verifiable code changes.
- Documented the use of Codex and opencode, including the AI models used to
  assist with project design and development.
- Reorganized the README around quick-start, usage, configuration, and
  development workflows, with a linked table of contents.

### Fixes :wrench:

- Broke internal callback ownership cycles and joined polling write workers on
  close so TLS requests finish before OpenSSL process cleanup.
- Made the 8 MiB HTTP polling response-body limit and 64 KiB network read
  buffer explicit to prevent excessive memory consumption from oversized
  server responses.
- Fixed heartbeat reconfiguration races, polling close-thread lifetime, and
  ignored transport shutdown errors reported by Coverity Scan.
- Fixed the CMake clang-tidy integration passing literal quotes in the
  configuration file path.
- Split Engine.IO polling payloads containing multiple record-separated
  packets, preventing intermittent namespace connection timeouts.
- Fixed unexpected WebSocket closure handling so stopping the Engine.IO
  heartbeat cannot delay close notification and reconnection.
- Defined the minimum Windows target version when compiling sioxx.

## [0.1.1] - 2026-07-29

### Changes :construction:

- Replaced `CPM.cmake` by pure `FetchContent`.
- Improved CMake dependency management with header-only Boost fetching and
  support for system Boost and nlohmann-json packages.
- Improved installation and package consumption for static and shared
  libraries across Linux, macOS, and Windows.
- Added Conan 2 packaging with static and shared consumer tests.
- Added versioned API documentation generated with Doxygen and Sphinx.
- Improved CI coverage with dedicated CMake integration and packaging
  workflows and dependency caching.
- Added optional `clang-tidy` integration.

## [0.1.0] - 2026-07-22

### Breaking changes :mega:

- Hid implementation details from the public includes
- Renamed client and related classes for consistency

### Additions :tada:

- Split gh workflow into build per platform and dedicated release workflow
- Introduced package-lock.cmake for centralized dependency management

## [0.0.6] - 2026-07-19

### Additions :tada:

- Added new SIOXX_USE_SYSTEM_BOOST to allow boost system dependency

## [0.0.5] - 2026-07-15

### Additions :tada:

- Added public parser factory for supplying application-defined Socket.IO packet
  strategies while retaining the built-in JSON and MessagePack parsers.
- Added custom CBOR parser example for the C++ client.
- Added matching CBOR option for the bundled Node.js test server, including
  WebSocket and HTTP long-polling scripts.
- Custom parser decoding may maintain per-client state.

## [0.0.4] - 2026-07-14

### Additions :tada:

- Added exponential reconnection backoff with a configurable maximum delay and
  symmetric jitter.

### Fixes :wrench:

- Fixed gh action release workflow execution for version tags.

## [0.0.3] - 2026-07-14

### Additions :tada:

- Added Engine.IO HTTP long-polling transport support and WebSocket fallback.

## [0.0.2] - 2026-07-14

### Fixes :wrench:

- Fixed MessagePack dependency/version handling.
- Fixed WebSocket write-queue and recursive-mutex deadlocks.
- Fixed windows builds and test configuration in CMake and VS Code.

[Unreleased]: https://github.com/jfayot/sioxx/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/jfayot/sioxx/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/jfayot/sioxx/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/jfayot/sioxx/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/jfayot/sioxx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/jfayot/sioxx/compare/v0.0.6...v0.1.0
[0.0.6]: https://github.com/jfayot/sioxx/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/jfayot/sioxx/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/jfayot/sioxx/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/jfayot/sioxx/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/jfayot/sioxx/releases/tag/v0.0.2
