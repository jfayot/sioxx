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
          url: `ws://127.0.0.1:${message.port}`,
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

  const [defaultServer, pollingServer, msgpackServer, customOptionsServer] =
    await Promise.all([
      startServer("default"),
      startServer("polling-only"),
      startServer("msgpack"),
      startServer("custom-options"),
    ]);

  runGoogleTest({
    ...process.env,
    SIOXX_E2E_URL: defaultServer.url,
    SIOXX_E2E_POLLING_ONLY_URL: pollingServer.url,
    SIOXX_E2E_MSGPACK_URL: msgpackServer.url,
    SIOXX_E2E_CUSTOM_OPTIONS_URL: customOptionsServer.url,
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
