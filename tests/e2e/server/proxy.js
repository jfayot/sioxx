"use strict";

const http = require("http");
const net = require("net");

const expectedAuthorization =
  "Basic " + Buffer.from("sioxx:proxy-password").toString("base64");

function authorized(request) {
  return request.headers["proxy-authorization"] === expectedAuthorization;
}

const server = http.createServer((request, response) => {
  if (!authorized(request)) {
    response.writeHead(407, { "Proxy-Authenticate": 'Basic realm="sioxx"' });
    response.end();
    return;
  }

  let target;
  try {
    target = new URL(request.url);
  } catch (error) {
    response.writeHead(400);
    response.end(error.message);
    return;
  }

  const headers = { ...request.headers };
  delete headers["proxy-authorization"];
  delete headers["proxy-connection"];
  const upstream = http.request(
    {
      hostname: target.hostname,
      port: target.port || 80,
      method: request.method,
      path: `${target.pathname}${target.search}`,
      headers,
    },
    (upstreamResponse) => {
      response.writeHead(upstreamResponse.statusCode, upstreamResponse.headers);
      upstreamResponse.pipe(response);
    },
  );
  upstream.on("error", (error) => {
    if (!response.headersSent) response.writeHead(502);
    response.end(error.message);
  });
  request.pipe(upstream);
});

server.on("connect", (request, clientSocket, head) => {
  if (!authorized(request)) {
    clientSocket.end(
      'HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm="sioxx"\r\nContent-Length: 0\r\n\r\n',
    );
    return;
  }

  const separator = request.url.lastIndexOf(":");
  const host = request.url.slice(0, separator).replace(/^\[|\]$/g, "");
  const port = Number(request.url.slice(separator + 1));
  const upstreamSocket = net.connect(port, host, () => {
    clientSocket.write("HTTP/1.1 200 Connection Established\r\n\r\n");
    if (head.length) upstreamSocket.write(head);
    upstreamSocket.pipe(clientSocket);
    clientSocket.pipe(upstreamSocket);
  });
  upstreamSocket.on("error", () => clientSocket.destroy());
});

server.listen(0, "127.0.0.1", () => {
  if (process.send) {
    process.send({ type: "ready", port: server.address().port });
  }
});

process.on("message", (message) => {
  if (message === "shutdown") server.close(() => process.exit(0));
});
