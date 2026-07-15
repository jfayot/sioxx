'use strict';
// Minimal socket.io server for exercising sioxx's example client
// (examples/basic_client.cpp). Matches its namespace and events:
//
//   sock->on("your_message", ...)
//   sock->emit("hello", sioxx::json{"world"})
//   sock->emit("ping_ack", sioxx::json::array({1,2,3}), ackCallback)
//
// Usage:
//   npm install
//   node server.js              # default JSON parser
//   node server.js --msgpack    # socket.io-msgpack-parser, to match
//                                # sioxx::parser_kind::msgpack on the client
//   node server.js --cbor       # custom CBOR parser example
//   node server.js --polling    # disable WebSocket; exercise HTTP polling

const http = require('http');
const { Server } = require('socket.io');

const useMsgpack = process.argv.includes('--msgpack');
const useCbor = process.argv.includes('--cbor');
const usePollingOnly = process.argv.includes('--polling');
const port = process.env.PORT || 3000;
const namespacePath = '/your_namespace';

if (useMsgpack && useCbor) {
  throw new Error('--msgpack and --cbor are mutually exclusive');
}

let ioOptions = {};
if (useMsgpack) {
  ioOptions.parser = require('socket.io-msgpack-parser');
} else if (useCbor) {
  ioOptions.parser = require('./cbor-parser');
}
if (usePollingOnly) {
  ioOptions.transports = ['polling'];
}

const httpServer = http.createServer();
const io = new Server(httpServer, ioOptions);
const missionEvents = io.of(namespacePath);

missionEvents.on('connection', (socket) => {
  console.log(`[${namespacePath}] connected: ${socket.id} (${socket.conn.transport.name})`);

  socket.on('hello', (arg) => {
    console.log(`[${namespacePath}] hello from ${socket.id} ->`, arg);
  });

  socket.on('ping_ack', (...args) => {
    // socket.io appends the ack callback as the last argument when the
    // client emitted with one.
    const ack = typeof args[args.length - 1] === 'function' ? args.pop() : null;
    console.log(`[${namespacePath}] ping_ack from ${socket.id} ->`, args);
    if (ack) ack({ reply: 'pong', received: args });
  });

  socket.on('disconnect', (reason) => {
    console.log(`[${namespacePath}] disconnected: ${socket.id} (${reason})`);
  });

  // Give the client something to receive shortly after it connects.
  setTimeout(() => {
    socket.emit('your_message', { id: 1, status: 'active', note: 'hello from server' });
  }, 1000);
});

// Periodic broadcast so your_message keeps flowing even with an idle client.
setInterval(() => {
  missionEvents.emit('your_message', { id: 0, status: 'heartbeat', ts: Date.now() });
}, 10000);

httpServer.listen(port, () => {
  console.log(`[sioxx-test-server] listening on ws://localhost:${port}`);
  console.log(`[sioxx-test-server] namespace: ${namespacePath}`);
  const parserName = useMsgpack ? 'msgpack' : useCbor ? 'cbor' : 'json (default)';
  console.log(`[sioxx-test-server] parser: ${parserName}`);
  console.log(`[sioxx-test-server] transport: ${usePollingOnly ? 'polling only' : 'websocket + polling'}`);
});
