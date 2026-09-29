import { ApiClient } from "./client/apiClient";
import { GlobalOptions, ResolvedConfig, resolveConfig } from "./config";
import { EXIT } from "./errors";
import { CliIO } from "./io";
import { Output } from "./utils/output";

/**
 * Shared between `main()` and the command that is running, so a long-running command can take over
 * Ctrl+C handling (cancel its jobs) instead of the default "just stop".
 */
export class InterruptState {
  /** Set by a command that handles the interrupt itself. */
  claimed = false;

  constructor(readonly signal: AbortSignal) {}

  get aborted(): boolean {
    return this.signal.aborted;
  }

  /** 130 for SIGINT, 143 for SIGTERM (whatever `index.ts` passed as the abort reason). */
  get exitCode(): number {
    const reason: unknown = this.signal.reason;
    return typeof reason === "number" ? reason : EXIT.INTERRUPTED;
  }
}

export interface CliContext {
  io: CliIO;
  config: ResolvedConfig;
  out: Output;
  client: ApiClient;
  interrupt: InterruptState;
  pollIntervalMs: number;
}

export const DEFAULT_POLL_INTERVAL_MS = 1000;

export function createContext(options: GlobalOptions, io: CliIO, interrupt: InterruptState): CliContext {
  const config = resolveConfig(options, io);
  return {
    io,
    config,
    out: new Output(io, config),
    client: new ApiClient({ baseUrl: config.apiUrl, token: config.token }),
    interrupt,
    pollIntervalMs: io.pollIntervalMs ?? DEFAULT_POLL_INTERVAL_MS
  };
}
