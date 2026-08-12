"use strict";

const http = require("http");
const { Server } = require("socket.io");

const useMsgpack = process.argv.includes("--msgpack");
const usePollingOnly = process.argv.includes("--polling");
const port = process.env.PORT || 3000;
const options = {};

if (useMsgpack) {
  options.parser = require("socket.io-msgpack-parser");
}
if (usePollingOnly) {
  options.transports = ["polling"];
}

const httpServer = http.createServer();
const io = new Server(httpServer, options);
const chat = io.of("/chat");

chat.use((socket, next) => {
  const username = socket.handshake.auth?.username;
  if (typeof username !== "string" || username.trim() === "") {
    next(new Error("a display name is required"));
    return;
  }
  socket.data.username = username.trim().slice(0, 40);
  next();
});

chat.on("connection", (socket) => {
  const username = socket.data.username;
  console.log(`[/chat] connected: ${username} (${socket.id})`);
  socket.broadcast.emit("server_notice", `${username} joined the chat`);

  socket.emit(
    "welcome",
    { text: `Welcome, ${username}. This event requests a client acknowledgement.` },
    (...reply) => console.log(`[/chat] welcome acknowledged by ${username}:`, reply),
  );

  socket.on("chat_message", (payload, acknowledge) => {
    const text = typeof payload?.text === "string" ? payload.text.trim() : "";
    if (text === "") {
      if (acknowledge) acknowledge({ status: "rejected" });
      return;
    }

    chat.emit("chat_message", {
      username,
      text: text.slice(0, 1000),
      sentAt: Date.now(),
    });
    if (acknowledge) acknowledge({ status: "delivered" });
  });

  socket.on("attachment", (payload, acknowledge) => {
    const bytes = payload?.bytes;
    if (useMsgpack && typeof payload?.name === "string" && bytes instanceof Uint8Array) {
      const buffer = Buffer.from(bytes);
      chat.emit("attachment", {
        username,
        name: payload.name.slice(0, 200),
        bytes: buffer,
      });
      if (acknowledge) {
        acknowledge({ status: "delivered", bytes: buffer.length });
      }
      return;
    }

    if (acknowledge) {
      acknowledge({
        status: "rejected",
        reason: "use MessagePack for binary data",
      });
    }
  });

  socket.on("disconnect", () => {
    socket.broadcast.emit("server_notice", `${username} left the chat`);
  });
});

httpServer.listen(port, () => {
  const parser = useMsgpack ? "MessagePack" : "JSON";
  const transport = usePollingOnly ? "HTTP polling" : "WebSocket + polling";
  console.log(`[sioxx-qt-chat-server] listening on ws://localhost:${port}/chat`);
  console.log(`[sioxx-qt-chat-server] parser: ${parser}`);
  console.log(`[sioxx-qt-chat-server] transport: ${transport}`);
});
