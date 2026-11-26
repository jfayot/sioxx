"use strict";

const path = require("path");
const { fork, spawn } = require("child_process");

const testExecutable = process.argv[2];
if (!testExecutable) {
  console.error("usage: node run-tests.js <gtest-executable> [gtest-arguments...]");
  process.exit(2);
}
const testArguments = process.argv.slice(3);

const servers = [];
let testProcess;
let finishing = false;

function startServer(mode, port = 0) {
  return new Promise((resolve, reject) => {
    const child = fork(path.join(__dirname, "server.js"), {
      stdio: ["ignore", "inherit", "inherit", "ipc"],
      env: {
        ...process.env,
        SIOXX_E2E_SERVER_MODE: mode,
        SIOXX_E2E_PORT: String(port),
      },
    });
    servers.push(child);

    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (process.env.SIOXX_E2E_DEBUG) {
        console.error(
          `[e2e] ${mode} server exited (code=${code}, signal=${signal}, restart=${Boolean(child.expectedRestart)})`,
        );
      }
      if (!finishing && !child.expectedRestart) {
        reject(
          new Error(
            `${mode} server exited early (code=${code}, signal=${signal})`
          )
        );
      }
    });
    child.on("message", (message) => {
      if (!message) return;
      if (message.type === "ready") {
        resolve({
          url: `${mode.startsWith("tls") ? "wss" : "ws"}://127.0.0.1:${message.port}`,
        });
      } else if (message.type === "restart") {
        if (process.env.SIOXX_E2E_DEBUG) {
          console.error(`[e2e] restart requested for ${message.mode}:${message.port}`);
        }
        child.expectedRestart = true;
        child.once("exit", () => {
          startServer(message.mode, message.port).catch((error) => {
            console.error(`failed to restart ${message.mode} server: ${error.message}`);
            finish(1);
          });
        });
      }
    });
  });
}

function startProxy() {
  return new Promise((resolve, reject) => {
    const child = fork(path.join(__dirname, "proxy.js"), {
      stdio: ["ignore", "inherit", "inherit", "ipc"],
    });
    servers.push(child);
    child.once("error", reject);
    child.once("exit", (code, signal) => {
      if (!finishing) {
        reject(
          new Error(
            `proxy exited early (code=${code}, signal=${signal})`,
          ),
        );
      }
    });
    child.on("message", (message) => {
      if (message?.type === "ready") {
        resolve({ url: `http://127.0.0.1:${message.port}` });
      }
    });
  });
}

function stopServers() {
  for (const server of servers) {
    if (server.connected) server.send("shutdown");
    else if (!server.killed) server.kill();
  }
}

function finish(exitCode) {
  if (finishing) return;
  finishing = true;
  stopServers();
  process.exitCode = exitCode;
}

async function run() {
  if (testArguments.includes("--gtest_list_tests")) {
    runGoogleTest(process.env);
    return;
  }

  // CTest runs one case at a time. Start the extra TLS fixtures only for
  // cases that use them, or when GoogleTest may select multiple cases.
  const selectedTest = testArguments
    .find((arg) => arg.startsWith("--gtest_filter="))
    ?.match(/^--gtest_filter=(E2E\.\w+)$/)?.[1];
  const extraTlsMode = {
    "E2E.RejectsTlsCertificateForWrongHostname": "tls-wrong-host",
    "E2E.RejectsTlsCertificateForWrongIpAddress": "tls-wrong-host",
    "E2E.RejectsUntrustedTlsCertificate": "tls-untrusted",
    "E2E.ConnectsWithUntrustedCertificateWhenVerificationIsDisabled": "tls-untrusted",
  }[selectedTest];

  const [
    defaultServer,
    pollingServer,
    msgpackServer,
    customOptionsServer,
    tlsServer,
    wrongHostTlsServer,
    untrustedTlsServer,
    proxyServer,
  ] = await Promise.all([
    startServer("default"),
    startServer("polling-only"),
    startServer("msgpack"),
    startServer("custom-options"),
    startServer("tls"),
    !selectedTest || extraTlsMode === "tls-wrong-host"
      ? startServer("tls-wrong-host")
      : undefined,
    !selectedTest || extraTlsMode === "tls-untrusted"
      ? startServer("tls-untrusted")
      : undefined,
    startProxy(),
  ]);

  runGoogleTest({
    ...process.env,
    SSL_CERT_FILE: path.join(__dirname, "fixtures", "localhost-test-cert.pem"),
    SIOXX_E2E_URL: defaultServer.url,
    SIOXX_E2E_POLLING_ONLY_URL: pollingServer.url,
    SIOXX_E2E_MSGPACK_URL: msgpackServer.url,
    SIOXX_E2E_CUSTOM_OPTIONS_URL: customOptionsServer.url,
    SIOXX_E2E_TLS_URL: tlsServer.url,
    SIOXX_E2E_TLS_HOSTNAME_URL: tlsServer.url.replace("127.0.0.1", "localhost"),
    SIOXX_E2E_TLS_WRONG_HOSTNAME_URL: wrongHostTlsServer?.url.replace(
      "127.0.0.1", "localhost"
    ),
    SIOXX_E2E_TLS_WRONG_IP_URL: wrongHostTlsServer?.url,
    SIOXX_E2E_TLS_UNTRUSTED_URL: untrustedTlsServer?.url,
    SIOXX_E2E_PROXY_URL: proxyServer.url,
  });
}

function runGoogleTest(env) {
  testProcess = spawn(testExecutable, testArguments, {
    stdio: "inherit",
    env,
  });

  testProcess.once("error", (error) => {
    console.error(`failed to start GoogleTest executable: ${error.message}`);
    finish(1);
  });
  testProcess.once("exit", (code, signal) => {
    if (signal) {
      console.error(`GoogleTest terminated by ${signal}`);
      finish(1);
    } else {
      finish(code ?? 1);
    }
  });
}

run().catch((error) => {
  console.error(`failed to run E2E tests: ${error.message}`);
  finish(1);
});

process.on("SIGINT", () => {
  if (testProcess) testProcess.kill("SIGINT");
  finish(130);
});
process.on("SIGTERM", () => {
  if (testProcess) testProcess.kill("SIGTERM");
  finish(143);
});
