"use strict";

const http = require("http");
const { Server } = require("socket.io");

const mode = process.env.SIOXX_E2E_SERVER_MODE || "default";
const configuredPort = Number(process.env.SIOXX_E2E_PORT || 0);
const options = {};

if (mode === "polling-only") {
  options.transports = ["polling"];
} else if (mode === "msgpack") {
  options.parser = require("socket.io-msgpack-parser");
} else if (mode === "custom-options") {
  options.path = "/realtime/";
}

const httpServer = http.createServer();
const io = new Server(httpServer, options);

function installCommonHandlers(namespace) {
  namespace.on("connection", (socket) => {
    socket.on("namespace_with_ack", (acknowledgement) => {
      acknowledgement(socket.nsp.name);
    });
    socket.on("request_scoped_event", () => {
      socket.emit("scoped_event", socket.nsp.name);
    });
  });
}

const e2e = io.of("/e2e");
installCommonHandlers(e2e);
installCommonHandlers(io.of("/e2e-a"));
installCommonHandlers(io.of("/e2e-b"));

if (mode === "custom-options") {
  io.of("/private").on("connection", (socket) => {
    socket.on("connection_details_with_ack", (acknowledgement) => {
      acknowledgement({
        auth: socket.handshake.auth,
        query: socket.handshake.query,
        transport: socket.conn.transport.name,
      });
    });
    socket.on("disconnect_namespace", (acknowledgement) => {
      acknowledgement();
      socket.disconnect();
    });
  });
}

e2e.on("connection", (socket) => {
  socket.emit("server_arguments", 1, "two", { three: 3 });

  socket.emit("server_ack_request", 7, "question", (...reply) => {
    socket.emit("server_ack_reply_received", ...reply);
  });

  socket.on("echo_with_ack", (...args) => {
    const acknowledgement = args.pop();
    acknowledgement({ received: args });
  });

  socket.on("transport_with_ack", (acknowledgement) => {
    acknowledgement(socket.conn.transport.name);
  });

  socket.on("drop_transport", (acknowledgement) => {
    acknowledgement("dropping");
    setTimeout(() => {
      if (process.send) {
        process.send(
          {
            type: "restart",
            mode,
            port: httpServer.address().port,
          },
          () => process.exit(0)
        );
      } else {
        process.exit(0);
      }
    }, 10);
  });

  socket.on("msgpack_echo_with_ack", (...args) => {
    const acknowledgement = args.pop();
    acknowledgement(...args);
  });
});

function close() {
  io.close(() => process.exit(0));
}

process.on("SIGTERM", close);
process.on("SIGINT", close);
process.on("message", (message) => {
  if (message === "shutdown") close();
});

httpServer.listen(configuredPort, "127.0.0.1", () => {
  const address = httpServer.address();
  if (process.send) {
    process.send({ type: "ready", mode, port: address.port });
  }
});
