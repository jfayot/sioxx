# sioxx basic client

This command-line example demonstrates connection lifecycle listeners,
namespaces, events, acknowledgements, parser selection, custom parsers, and
forced HTTP long-polling. It buffers emissions before connecting and gives the
acknowledgement request a five-second deadline. It runs for 30 seconds before
shutting down.

Build it from the repository root:

```bash
cmake -S . -B build -DSIOXX_BUILD_EXAMPLES=ON
cmake --build build --target sioxx_basic_client --parallel
```

Install and start its matching Socket.IO server in another terminal:

```bash
pnpm --dir examples/basic_client/server install --frozen-lockfile
pnpm --dir examples/basic_client/server start
```

Then run the client:

```bash
./build/sioxx_basic_client ws://localhost:3000
```

The client and server also have matching `msgpack`, `cbor`, and `polling`
modes. See the [server guide](server/README.md) for the complete commands.
