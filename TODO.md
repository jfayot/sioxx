# sioxx 1.0.0 TODO

This checklist tracks work needed to define and ship sioxx 1.0.0. The broader
roadmap in the documentation remains a list of possible future extensions;
items listed here should either be completed or explicitly resolved before the
1.0.0 release.

## Release criteria

- [x] Define and document the [v1.0 compatibility matrix](README.md#compatibility)
      for Socket.IO 4.8.1 and Engine.IO v4.
- [x] Freeze the public API documented in the [API reference](docs/api.rst).
- [x] Document the minimum Boost and nlohmann-json versions in the
      [README](README.md#quick-start).
- [ ] Confirm and document supported compiler and OpenSSL versions.
- [ ] Pass the full Linux, macOS, and Windows CI matrices on the `1.0.0`
      release commit, including unit, E2E, integration, packaging,
      static-analysis, and documentation checks.
- [ ] At release time, confirm that the 1.0.0 milestone has no open issues or
      pull requests.

## Protocol and transports

- [x] Verify WebSocket-first fallback and polling-only modes, including WS,
      WSS, HTTP polling, and HTTPS polling, in every Build matrix job.
- [x] Verify TLS server identity checks and authenticated HTTP proxy behavior in
      every Build matrix job.

## Reliability

- [x] Add configurable acknowledgement timeouts.
- [x] Add bounded retries for events that are not acknowledged.
- [x] Define event-buffer behavior across namespace disconnects, transport
      failures, reconnection exhaustion, and explicit client shutdown.
- [x] Run unit and E2E tests under ASan, UBSan, and TSan in CI, with repeated
      concurrency regressions for shutdown, reconnection, heartbeat, and
      callback dispatch.
- [x] Complete the race-condition review for shutdown, reconnection, heartbeat,
      and callback dispatch using the sanitizer coverage above.

## Public API stabilization

- [x] Make `socket` construction and packet dispatch private.
- [x] Document and test `sync_close()` guarantees with a caller-owned
      `boost::asio::io_context`.
- [x] Review public naming, ownership, error reporting, and callback lifetime
      guarantees and document the upgrade from 0.x.
- [x] Keep the published reconnection fields on `client_options`.
- [x] Support library-owned I/O and a caller-owned `boost::asio::io_context`.
- [x] Include the custom parser extension API in the v1.x source compatibility
      promise.
- [x] Exercise both I/O modes, listener registration, timed acknowledgements,
      and a custom parser in the installed-package consumer.
- [x] Document the [v1.x source compatibility and rebuild policy](README.md#v1x-source-and-binary-compatibility).

## Security and limits

- [x] Bound incoming JSON binary packet assembly by attachment count and
      combined packet size.
- [ ] Document trust boundaries for TLS verification, origin headers,
      namespace authentication, and proxy credentials.
- [x] Review protocol parsers and transport response limits for memory and CPU
      exhaustion cases; record [limits, regression coverage, and operational
      boundaries](docs/resource_limits.rst).
- [x] Fuzz the JSON and MessagePack decoders in ASan/UBSan CI.
- [x] Fuzz the URL, Engine.IO, and polling payload parsers.
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

- [x] Use `SameMinorVersion` for 0.x and `SameMajorVersion` from 1.0 onward.
      Keep the installed consumer's request at the first minor of its stable
      major version.
- [x] Use the full release version as `SOVERSION` from 1.0 onward.
- [ ] Verify shared-library naming and version metadata on the release platforms.
- [ ] Validate installed-package, FetchContent, subproject, Conan, and shared
      library consumers against the exact `1.0.0` release commit before
      pushing the release tag.
- [ ] Confirm exported CMake targets do not leak private dependencies or build
      paths.
- [ ] Prepare the `1.0.0` version bump, changelog, and migration notes.
- [ ] After validating the release commit, sign and push `v1.0.0` and verify
      the GitHub release.

## Explicitly post-1.0

- Coroutine-friendly APIs.
- Outgoing catch-all listeners and richer connection and transport diagnostics.
- Upgrade established HTTP long-polling connections to WebSocket.
- Explicit transport selection and ordering.
- WebTransport support, pending demand and suitable C++ library support.
- A native C++ Socket.IO server and its rooms, adapters, middleware, and
  distributed deployment features.
