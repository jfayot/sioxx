# sioxx 1.0.0 TODO

This checklist tracks work needed to define and ship sioxx 1.0.0. The broader
roadmap in the documentation remains a list of possible future extensions;
items listed here should either be completed or explicitly resolved before the
1.0.0 release.

## Release criteria

- [ ] Define and document the supported Socket.IO and Engine.IO compatibility
      matrix for 1.0.0.
- [ ] Freeze the public API after completing the API review below.
- [ ] Confirm supported compiler, Boost, OpenSSL, and nlohmann-json versions.
- [ ] Pass the full Linux, macOS, and Windows CI matrices with unit, E2E,
      integration, packaging, static-analysis, and documentation checks.
- [ ] Resolve every open issue and pull request assigned to the 1.0.0
      milestone.

## Protocol and transports

- [ ] Upgrade established HTTP long-polling connections to WebSocket when the
      server offers that upgrade.
- [ ] Add explicit transport selection and ordering rather than only
      WebSocket-first fallback and polling-only modes.
- [ ] Verify WebSocket, secure WebSocket, HTTP polling, and HTTPS polling
      interoperability against every supported server version.
- [ ] Verify authenticated HTTP proxy behavior on every supported platform.

## Reliability

- [ ] Add configurable acknowledgement timeouts.
- [ ] Add bounded retries for events that are not acknowledged.
- [ ] Define event-buffer behavior across namespace disconnects, transport
      failures, reconnection exhaustion, and explicit client shutdown.
- [ ] Review shutdown, reconnection, heartbeat, and callback dispatch for race
      conditions under ThreadSanitizer or an equivalent concurrency check.

## Public API stabilization

- [ ] Audit public naming, ownership, error reporting, and callback lifetime
      guarantees.
- [ ] Decide whether to group the published reconnection fields under a
      `reconnection_options` type and document any migration.
- [ ] Decide whether 1.0.0 requires an application-provided
      `boost::asio::io_context`.
- [ ] Decide whether coroutine-friendly APIs are part of 1.0.0 or a later
      release.
- [ ] Decide whether outgoing catch-all listeners and richer connection and
      transport diagnostics are required for 1.0.0.
- [ ] Document API and ABI compatibility expectations for releases after
      1.0.0.

## Security and limits

- [ ] Document trust boundaries for TLS verification, origin headers,
      namespace authentication, and proxy credentials.
- [ ] Review protocol parsers and transport response limits for memory and CPU
      exhaustion cases.
- [ ] Run sanitizers and fuzz the URL, Engine.IO, JSON, MessagePack, and polling
      payload parsers.
- [ ] Establish a vulnerability-reporting and supported-security-release
      policy.

## Documentation and examples

- [ ] Ensure every public type and function has complete Doxygen documentation.
- [ ] Review the quick start, examples, migration guide, and generated API
      documentation against the final 1.0.0 API.
- [ ] Add upgrade notes covering every breaking change made after 0.2.0.
- [ ] Document production deployment guidance for TLS, proxies, reconnects,
      timeouts, threading, and orderly shutdown.

## Packaging and release engineering

- [ ] Change the CMake package compatibility policy from `SameMinorVersion` to
      `SameMajorVersion`.
- [ ] Change the library `SOVERSION` to the project major version and verify
      shared-library naming on all platforms.
- [ ] Validate installed-package, FetchContent, subproject, Conan, and shared
      library consumers against the release candidate.
- [ ] Confirm exported CMake targets do not leak private dependencies or build
      paths.
- [ ] Prepare the 1.0.0 changelog, migration notes, release candidate, signed
      tag, and GitHub release.

## Explicitly post-1.0

- WebTransport support, pending demand and suitable C++ library support.
- A native C++ Socket.IO server and its rooms, adapters, middleware, and
  distributed deployment features.
