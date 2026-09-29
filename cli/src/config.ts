import { CliIO } from "./io";
import { UsageError } from "./errors";

export const DEFAULT_API_URL = "http://127.0.0.1:4000";

/** The global flags, as commander parses them (`--no-color` arrives as `color: false`). */
export interface GlobalOptions {
  api?: string;
  token?: string;
  json?: boolean;
  color?: boolean;
}

export interface ResolvedConfig {
  apiUrl: string;
  token?: string;
  /** Machine-readable output: JSON on stdout, nothing decorative. */
  json: boolean;
  /** ANSI colors are allowed. */
  color: boolean;
  /** Spinners and in-place updates are allowed (both stdout and stderr are terminals). */
  interactive: boolean;
}

/** Accepts `host:port` as well as full URLs, drops trailing slashes, keeps a path prefix (reverse proxies). */
export function normalizeApiUrl(raw: string): string {
  const trimmed = raw.trim();
  const withScheme = /^[a-z][a-z0-9+.-]*:\/\//i.test(trimmed) ? trimmed : `http://${trimmed}`;
  let url: URL;
  try {
    url = new URL(withScheme);
  } catch {
    throw new UsageError(`Invalid API URL "${raw}". Expected something like ${DEFAULT_API_URL}.`);
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") {
    throw new UsageError(`Invalid API URL "${raw}": only http:// and https:// are supported.`);
  }
  return `${url.origin}${url.pathname.replace(/\/+$/, "")}`;
}

export function resolveConfig(options: GlobalOptions, io: CliIO): ResolvedConfig {
  const env = io.env;
  const api = options.api ?? (env.IOSLAB_API_URL || DEFAULT_API_URL);
  const token = options.token || env.IOSLAB_API_TOKEN || undefined;

  const dumbTerminal = env.TERM === "dumb";
  const stdoutIsTty = Boolean(io.stdout.isTTY);
  const noColor = options.color === false || (env.NO_COLOR !== undefined && env.NO_COLOR !== "");
  const color = stdoutIsTty && !noColor && !dumbTerminal;
  const json = Boolean(options.json);
  const interactive = stdoutIsTty && Boolean(io.stderr.isTTY) && !dumbTerminal && !json;

  return { apiUrl: normalizeApiUrl(api), token, json, color, interactive };
}
