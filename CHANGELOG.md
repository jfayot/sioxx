# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.0.6] - 2026-07-19

### Added

- A new SIOXX_USE_SYSTEM_BOOST to allow boost system dependency

## [0.0.5] - 2026-07-15

### Added

- A public parser factory for supplying application-defined Socket.IO packet
  strategies while retaining the built-in JSON and MessagePack parsers.
- A custom CBOR parser example for the C++ client.
- A matching CBOR option for the bundled Node.js test server, including
  WebSocket and HTTP long-polling scripts.

### Changed

- Custom parser decoding may maintain per-client state.

## [0.0.4] - 2026-07-14

### Added

- Exponential reconnection backoff with a configurable maximum delay and
  symmetric jitter.

### Fixed

- GitHub Actions release workflow execution for version tags.

## [0.0.3] - 2026-07-14

### Added

- Engine.IO HTTP long-polling transport support and WebSocket fallback.

## [0.0.2] - 2026-07-14

### Fixed

- MessagePack dependency/version handling.
- WebSocket write-queue and recursive-mutex deadlocks.
- Windows builds and test configuration in CMake and VS Code.

[Unreleased]: https://github.com/jfayot/sioxx/compare/v0.0.6...HEAD
[0.0.6]: https://github.com/jfayot/sioxx/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/jfayot/sioxx/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/jfayot/sioxx/compare/v0.0.3...v0.0.4
[0.0.3]: https://github.com/jfayot/sioxx/compare/v0.0.2...v0.0.3
[0.0.2]: https://github.com/jfayot/sioxx/releases/tag/v0.0.2
