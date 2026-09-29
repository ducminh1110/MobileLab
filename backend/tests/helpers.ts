import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { App, createApp, CreateAppOptions } from "../src/app";
import { AppConfig, loadConfig } from "../src/config/env";

export async function makeApp(
  overrides: Partial<AppConfig> = {},
  options: CreateAppOptions = { ephemeral: true },
  env: NodeJS.ProcessEnv = {}
): Promise<App & { dataDir: string }> {
  const dataDir = overrides.dataDir ?? fs.mkdtempSync(path.join(os.tmpdir(), "mobilelab-test-"));
  const config = loadConfig(
    { NODE_ENV: "test", IOSLAB_SIMULATOR_MOCK: "true", IOSLAB_DATA_DIR: dataDir, IOSLAB_MAX_LOAD: "4", ...env },
    { retryBackoffMs: 1, retentionDays: 0, ...overrides }
  );
  const app = await createApp(config, options);
  return { ...app, dataDir };
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

export function cleanup(dataDir: string): void {
  fs.rmSync(dataDir, { recursive: true, force: true });
}
