# sioxx Qt chat

This small Qt Widgets client demonstrates the parts of sioxx most desktop
applications need:

- connection, namespace, error, disconnect, and automatic-reconnect handling;
- namespace authentication and typed JSON events;
- outgoing and incoming Socket.IO acknowledgements;
- the catch-all event listener;
- WebSocket or forced HTTP long-polling;
- JSON or MessagePack parsing, including small binary attachments; and
- safe delivery of sioxx background-thread callbacks to Qt's GUI thread.

Qt is optional. When example builds are enabled, CMake adds this target if Qt 6
or Qt 5 Widgets is available and otherwise skips it. From the repository root:

```bash
cmake -S . -B build-qt \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIOXX_BUILD_TESTS=OFF \
  -DSIOXX_BUILD_EXAMPLES=ON
cmake --build build-qt --config Release --target sioxx_qt_chat --parallel
```

Start the matching Socket.IO server in another terminal:

```bash
pnpm --dir examples/qt_chat install --frozen-lockfile
pnpm --dir examples/qt_chat start
```

Launch the client from the repository root:

| Platform | Command |
| --- | --- |
| Linux with a single-configuration generator | `./build-qt/examples/qt_chat/sioxx_qt_chat` |
| macOS with a single-configuration generator | `open build-qt/examples/qt_chat/sioxx_qt_chat.app` |
| Windows with Visual Studio | `.\build-qt\examples\qt_chat\Release\sioxx_qt_chat.exe` |

Keep the default JSON parser, choose a display name, and connect. Open a
second client to chat between them.

Binary file sharing works with either parser. To use MessagePack, start the
server with `pnpm --dir examples/qt_chat start:msgpack` and select MessagePack
in every client before connecting. The JSON and MessagePack parsers cannot be
mixed on one server.
