# Changelog

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/jfayot/sioxx/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/jfayot/sioxx/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/jfayot/sioxx/compare/v0.0.6...v0.1.0
[0.0.6]: https://github.com/jfayot/sioxx/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/jfayot/sioxx/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/jfayot/sioxx/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/jfayot/sioxx/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/jfayot/sioxx/releases/tag/v0.0.2
