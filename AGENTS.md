# sioxx agent guide

## Project overview

sioxx is a C++17 Socket.IO client built with Boost.Asio, Boost.Beast, and
nlohmann-json.

The main project areas are:

- `include/sioxx/`: public API
- `src/client*` and `src/socket*`: Socket.IO client behavior
- `src/engineio_client*`: Engine.IO framing and heartbeat
- `src/*transport*`: WebSocket and HTTP long-polling transports
- `src/*parser*`: JSON and MessagePack parsers
- `tests/`: isolated GoogleTest unit tests
- `tests/e2e/`: GoogleTest scenarios backed by Node.js Socket.IO servers
- `docs/`: Doxygen and Sphinx documentation sources

## Working rules

- Preserve C++17 compatibility and the supported Linux, macOS, and Windows
  builds.
- Keep public declarations in `include/sioxx/` and implementation details in
  `src/`.
- Follow the style and naming of the surrounding code.
- Format C/C++ and cmake changes with the repository's `.clang-format` and `.cmake-format` configurations
  by running `./scripts/format.sh`, then review the diff for unrelated changes.
- Preserve unrelated user changes and do not modify generated build
  directories or fetched dependencies.
- Add or update regression tests for behavior changes and defect fixes.
- Update examples, README content, and the `[Unreleased]` changelog when
  user-visible behavior or developer workflows change.

## Validation

Build and run unit tests with:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIOXX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Transport, parser interoperability, and other real-protocol changes should
also run the end-to-end suite:

```bash
pnpm --dir tests/e2e/server install --frozen-lockfile
cmake -S . -B build-e2e \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIOXX_BUILD_TESTS=ON \
  -DSIOXX_BUILD_E2E_TESTS=ON
cmake --build build-e2e --parallel
ctest --test-dir build-e2e -L e2e --output-on-failure
```

CTest starts and stops the dedicated Node.js E2E servers automatically. Do not
start them manually unless diagnosing the runner itself.

Run clang-tidy as described in `README.md` when changing C++ sources or
headers.

## Documentation

- Add Doxygen-compatible comments for new or changed public APIs.
- Update the documentation sources whenever APIs, behavior, configuration, or
  examples change.
- Regenerate generated documentation when its source changes and the task
  requires updated generated output. Include any tracked generated artifacts
  that need to stay synchronized; never edit generated output manually.
- Configure and verify the documentation build when documentation or public
  APIs change:

```bash
cmake -S . -B build-docs \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIOXX_BUILD_DOCS=ON \
  -DSIOXX_BUILD_TESTS=OFF \
  -DSIOXX_BUILD_EXAMPLES=OFF
cmake --build build-docs --target Sphinx --parallel
```

The Sphinx build treats warnings as errors and should complete without
warnings.

## General coding guidelines (Karpathy inspired)

These guidelines bias toward caution over speed. Use judgment for trivial
tasks.

### Think before coding

- State assumptions explicitly and ask when uncertain.
- Present reasonable interpretations and tradeoffs instead of choosing
  silently.
- Point out simpler approaches and push back when warranted.
- Stop and explain what is unclear when confusion would affect the result.

### Prefer simplicity

- Write the minimum code needed to satisfy the request.
- Do not add speculative features, flexibility, or configurability.
- Avoid abstractions for single-use code and handling for impossible cases.
- Simplify an implementation when it is substantially larger than necessary.

### Make surgical changes

- Touch only what the request requires; do not improve or refactor adjacent
  code.
- Match the existing style even when another approach is preferable.
- Mention unrelated dead code rather than removing it.
- Remove only imports, variables, functions, or files made obsolete by the
  current change.
- Ensure every changed line traces directly to the request.

### Execute against verifiable goals

- Define concrete success criteria before implementing non-trivial changes.
- Reproduce defects with a regression test before fixing them when practical.
- Verify refactors preserve behavior before and after the change.
- For multi-step tasks, use a brief plan whose steps each include a validation
  check, and continue until the checks pass or a blocker is reported.
