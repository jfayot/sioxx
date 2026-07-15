# AGENTS.md

**Build**

- Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (add any of the options below).
- Options:
  - `-DSIOXX_BUILD_EXAMPLES=ON|OFF` – build the `examples/basic_client.cpp` executable (`sioxx_basic_client`).
  - `-DSIOXX_BUILD_TESTS=ON|OFF` – include the GoogleTest suite in `tests/`.
  - `-DSIOXX_USE_SYSTEM_JSON=ON|OFF` – use an already‑installed `nlohmann_json` instead of the vendored one.
- Build: `cmake --build build -j` (or add `--config Release` on Windows).
- Install (optional): `cmake --install build --prefix /usr/local` – installs `libsioxx.a`, headers (`include/sioxx` and, if vendored, `nlohmann`), and `sioxxConfig.cmake` for downstream `find_package(sioxx)`.

**Test**

- Enable tests with `-DSIOXX_BUILD_TESTS=ON`.
- Run via CTest: `ctest --test-dir build --output-on-failure` (or execute the binary directly: `./build/tests/sioxx_tests`).

**CI / Multi‑platform workflow**

- GitHub Actions matrix builds on Ubuntu, Windows, macOS with GCC, Clang, and MSVC. The workflow sets `build-output-dir=${{ github.workspace }}/build`.
- Steps: checkout → configure (`cmake -B ${{...}} -DCMAKE_CXX_COMPILER=${{ matrix.cpp_compiler }} -DCMAKE_C_COMPILER=${{ matrix.c_compiler }} -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} -S ${{ github.workspace }}`) → build → `ctest`.

**Dependencies**

- Boost 1.90 (asio & beast), OpenSSL, Threads, and nlohmann json (vendored unless `SIOXX_USE_SYSTEM_JSON=ON`). CPM.cmake automatically fetches these.

**Runtime notes**

- `client_options.parser` must match the server’s parser (`json` (default) or `msgpack`). The JSON parser does **not** support binary attachment reconstruction; use the MessagePack parser for binary payloads.
- Reconnection is a simple fixed‑delay loop (`reconnect_attempts` / `reconnect_delay`); no exponential back‑off.
- No HTTP long‑polling fallback – only WebSocket (`ws://`/`wss://`).

**Typical workflow**

1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSIOXX_BUILD_EXAMPLES=ON -DSIOXX_BUILD_TESTS=ON`
2. `cmake --build build -j`
3. `ctest --test-dir build --output-on-failure` (optional)
4. Run example: `./build/sioxx_basic_client` (after successful build).
