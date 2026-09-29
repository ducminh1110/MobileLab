import fs from "node:fs";
import http from "node:http";
import net, { AddressInfo } from "node:net";
import os from "node:os";
import path from "node:path";
import { AppServices, createApp } from "../../backend/src/app";
import { AppConfig, loadConfig } from "../../backend/src/config/env";
import { main } from "../src/cli";

export interface TestBackend {
  url: string;
  port: number;
  services: AppServices;
  dataDir: string;
  close(): Promise<void>;
}

/** The real backend, in this process, in demo mode, on an ephemeral port with a throwaway data directory. */
export async function startBackend(overrides: Partial<AppConfig> = {}, env: NodeJS.ProcessEnv = {}): Promise<TestBackend> {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), "ioslab-cli-test-"));
  const config = loadConfig(
    { NODE_ENV: "test", IOSLAB_SIMULATOR_MOCK: "true", IOSLAB_DATA_DIR: dataDir, IOSLAB_MAX_LOAD: "4", ...env },
    { retryBackoffMs: 1, retentionDays: 0, ...overrides }
  );
  const { app, services, close } = await createApp(config, { ephemeral: true });
  await app.listen({ port: 0, host: "127.0.0.1" });
  const port = (app.server.address() as AddressInfo).port;
  return {
    url: `http://127.0.0.1:${port}`,
    port,
    services,
    dataDir,
    async close() {
      await close();
      fs.rmSync(dataDir, { recursive: true, force: true });
    }
  };
}

export async function withBackend<T>(fn: (backend: TestBackend) => Promise<T>, overrides: Partial<AppConfig> = {}, env: NodeJS.ProcessEnv = {}): Promise<T> {
  const backend = await startBackend(overrides, env);
  try {
    return await fn(backend);
  } finally {
    await backend.close();
  }
}

/** A URL nothing is listening on: a port that was just free. */
export async function closedPortUrl(): Promise<string> {
  const server = net.createServer();
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address() as AddressInfo;
  await new Promise<void>((resolve) => server.close(() => resolve()));
  return `http://127.0.0.1:${port}`;
}

export interface CliResult {
  code: number;
  stdout: string;
  stderr: string;
}

export interface CliOptions {
  /** Sets IOSLAB_API_URL. Omit to test the flag or the default. */
  url?: string;
  env?: NodeJS.ProcessEnv;
  /** Pretend stdout is a terminal (colors on; no spinner because stderr is not). */
  tty?: boolean;
  interrupt?: AbortSignal;
  cwd?: string;
  pollIntervalMs?: number;
}

/** Runs the CLI in-process and captures what it prints. The environment is exactly `env`, never the real one. */
export async function runCli(args: string[], options: CliOptions = {}): Promise<CliResult> {
  let stdout = "";
  let stderr = "";
  const env: NodeJS.ProcessEnv = { ...(options.url ? { IOSLAB_API_URL: options.url } : {}), ...options.env };
  const code = await main(args, {
    stdout: { write: (text) => void (stdout += text), isTTY: options.tty ?? false, columns: 120 },
    stderr: { write: (text) => void (stderr += text), isTTY: false },
    env,
    cwd: options.cwd ?? process.cwd(),
    interrupt: options.interrupt,
    pollIntervalMs: options.pollIntervalMs ?? 20
  });
  return { code, stdout, stderr };
}

export async function waitFor<T>(probe: () => T | undefined | false | Promise<T | undefined | false>, timeoutMs = 5000, label = "condition"): Promise<T> {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const value = await probe();
    if (value) return value;
    if (Date.now() > deadline) throw new Error(`Timed out waiting for ${label}`);
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
}

export interface Proxy {
  url: string;
  /** Stops answering: closes the listener and every open connection. */
  stop(): Promise<void>;
}

/**
 * An HTTP-only front for the backend. WebSocket upgrades are refused (`upgrade: "refuse"`) or dropped
 * (`"drop"`), which is how a corporate proxy or a flaky network breaks the live stream while plain
 * requests keep working.
 */
export async function startProxy(targetPort: number, upgrade: "refuse" | "drop"): Promise<Proxy> {
  const server = http.createServer((req, res) => {
    const upstream = http.request({ host: "127.0.0.1", port: targetPort, path: req.url, method: req.method, headers: req.headers }, (up) => {
      res.writeHead(up.statusCode ?? 502, up.headers);
      up.pipe(res);
    });
    upstream.on("error", () => {
      res.writeHead(502).end();
    });
    req.pipe(upstream);
  });
  server.on("upgrade", (_req, socket) => {
    if (upgrade === "drop") socket.destroy();
    else socket.end("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
  });
  await new Promise<void>((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address() as AddressInfo;
  return {
    url: `http://127.0.0.1:${port}`,
    stop: () =>
      new Promise<void>((resolve) => {
        server.close(() => resolve());
        server.closeAllConnections();
      })
  };
}

export const ANSI = /\u001b\[/;
