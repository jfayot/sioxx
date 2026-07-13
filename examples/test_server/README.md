# sioxx-test-server

A minimal Node.js socket.io server for exercising the `sioxx` example client
(`examples/basic_client.cpp` in the [sioxx repo](https://github.com/jfayot/sioxx)).

It mirrors exactly what that example does:

- namespace `/your_namespace`
- listens for `hello` (logs whatever the client sends)
- listens for `ping_ack` and replies via the ack callback with
  `{ reply: "pong", received: [...] }`
- emits `your_message` once shortly after a client connects, and again
  every 10s as a heartbeat broadcast, so `sock->on("your_message", ...)`
  has something to print even if you don't emit anything yourself

## Setup

```bash
pnpm install
```

## Run

```bash
pnpm start              # default JSON parser (matches sioxx::parser_kind::json)
pnpm start:msgpack      # socket.io-msgpack-parser (matches sioxx::parser_kind::msgpack)
```

By default it listens on port `3000` (override with `PORT=xxxx pnpm start`).

## Testing against the sioxx example client

```bash
# from the sioxx build directory
./sioxx_basic_client ws://localhost:3000          # JSON parser
./sioxx_basic_client ws://localhost:3000 msgpack   # MessagePack parser
```

**Important:** the client and server parsers must match — connecting a
`parser_kind::json` client to `pnpm start:msgpack` (or vice versa)
will fail the handshake, exactly like the JS clients.

You should see connect/disconnect, `hello`, and `ping_ack` logged
server-side, and `your_message` printed on the client.
