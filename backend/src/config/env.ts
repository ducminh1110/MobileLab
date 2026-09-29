import os from "node:os";
import path from "node:path";
import { z } from "zod";

const flag = z
  .string()
  .optional()
  .transform((value) => {
    if (value === undefined || value === "") return undefined;
    const normalized = value.trim().toLowerCase();
    if (["1", "true", "yes", "on"].includes(normalized)) return true;
    if (["0", "false", "no", "off"].includes(normalized)) return false;
    throw new Error(`expected a boolean (true/false), got "${value}"`);
  });

const positiveInt = z
  .string()
  .optional()
  .transform((value, ctx) => {
    if (value === undefined || value === "") return undefined;
    const parsed = Number(value);
    if (!Number.isInteger(parsed) || parsed <= 0) {
      ctx.addIssue({ code: "custom", message: `expected a positive integer, got "${value}"` });
      return z.NEVER;
    }
    return parsed;
  });

const envSchema = z.object({
  NODE_ENV: z.string().optional(),
  HOST: z.string().optional(),
  PORT: positiveInt,
  LOG_LEVEL: z.string().optional(),
  IOSLAB_API_TOKEN: z.string().optional(),
  IOSLAB_DATA_DIR: z.string().optional(),
  IOSLAB_SIMULATOR_MOCK: flag,
  IOSLAB_EXPERIMENTAL_VM: flag,
  IOSLAB_MAX_LOAD: positiveInt,
  IOSLAB_WEBHOOK_URL: z.string().optional(),
  IOSLAB_RETENTION_DAYS: positiveInt,
  IOSLAB_TEST_TIMEOUT_MS: positiveInt,
  IOSLAB_WORKSPACE_ROOT: z.string().optional(),
  IOSLAB_MOCK_LATENCY_MS: z.string().optional()
});

export type ModeReason = "env" | "auto" | "default";

export interface AppConfig {
  nodeEnv: string;
  host: string;
  port: number;
  logLevel: string;
  /** When set, every API call (except /health and the dashboard shell) must present it. */
  apiToken?: string;
  dataDir: string;
  /** True when simulator commands are simulated instead of executed. */
  mock: boolean;
  mockReason: ModeReason;
  mockLatencyMs: number;
  /** Simulated VM backend. It never runs tests; it exists to exercise the VM lifecycle API. */
  experimentalVm: boolean;
  /** Overrides the CPU/memory derived capacity. */
  maxLoad?: number;
  webhookUrl?: string;
  retentionDays: number;
  testTimeoutMs: number;
  /** Base delay before a failed job is retried (doubles each retry). */
  retryBackoffMs: number;
  /** Directory xcodebuild runs in when a job does not name a project. */
  workspaceRoot: string;
}

/**
 * Reads configuration from the environment (or a supplied map). Invalid values fail fast with a
 * message that names the variable instead of silently falling back to a default.
 */
export function loadConfig(source: NodeJS.ProcessEnv = process.env, overrides: Partial<AppConfig> = {}): AppConfig {
  const parsed = envSchema.safeParse(source);
  if (!parsed.success) {
    const details = parsed.error.issues.map((issue) => `${issue.path.join(".")}: ${issue.message}`).join("; ");
    throw new Error(`Invalid configuration: ${details}`);
  }
  const raw = parsed.data;

  // Simulators only exist on macOS. Anywhere else we fall back to a clearly-labelled demo mode so
  // the API and dashboard can still be explored, instead of failing every command.
  let mock: boolean;
  let mockReason: ModeReason;
  if (raw.IOSLAB_SIMULATOR_MOCK !== undefined) {
    mock = raw.IOSLAB_SIMULATOR_MOCK;
    mockReason = "env";
  } else if (os.platform() !== "darwin") {
    mock = true;
    mockReason = "auto";
  } else {
    mock = false;
    mockReason = "default";
  }

  const latency = raw.IOSLAB_MOCK_LATENCY_MS === undefined ? undefined : Number(raw.IOSLAB_MOCK_LATENCY_MS);
  if (latency !== undefined && (!Number.isFinite(latency) || latency < 0)) {
    throw new Error(`Invalid configuration: IOSLAB_MOCK_LATENCY_MS: expected a non-negative number, got "${raw.IOSLAB_MOCK_LATENCY_MS}"`);
  }

  const config: AppConfig = {
    nodeEnv: raw.NODE_ENV ?? "development",
    host: raw.HOST ?? "127.0.0.1",
    port: raw.PORT ?? 4000,
    logLevel: raw.LOG_LEVEL ?? (raw.NODE_ENV === "test" ? "silent" : "info"),
    apiToken: raw.IOSLAB_API_TOKEN || undefined,
    dataDir: path.resolve(raw.IOSLAB_DATA_DIR ?? path.join(os.homedir(), ".mobilelab")),
    mock,
    mockReason,
    mockLatencyMs: latency ?? (raw.NODE_ENV === "test" ? 0 : 350),
    experimentalVm: raw.IOSLAB_EXPERIMENTAL_VM ?? mock,
    maxLoad: raw.IOSLAB_MAX_LOAD,
    webhookUrl: raw.IOSLAB_WEBHOOK_URL || undefined,
    retentionDays: raw.IOSLAB_RETENTION_DAYS ?? 14,
    testTimeoutMs: raw.IOSLAB_TEST_TIMEOUT_MS ?? 30 * 60_000,
    retryBackoffMs: 500,
    workspaceRoot: path.resolve(raw.IOSLAB_WORKSPACE_ROOT ?? process.cwd())
  };

  return { ...config, ...overrides };
}
