/** Where output goes. Injected so tests (and embedders) can capture it; `defaultIO()` wires the real process. */
export interface Writer {
  write(text: string): void;
  isTTY?: boolean;
  columns?: number;
  /** The underlying stream, only present on real terminals; spinners need it. */
  raw?: NodeJS.WriteStream;
}

export interface CliIO {
  stdout: Writer;
  stderr: Writer;
  env: NodeJS.ProcessEnv;
  /** Base for relative file paths (`--junit`, `-o`). */
  cwd: string;
  /**
   * Aborted when the user interrupts (Ctrl+C / SIGTERM). `signal.reason` may be the exit code to use
   * (130 for SIGINT, 143 for SIGTERM).
   */
  interrupt?: AbortSignal;
  /** How often a followed job is polled. Live events wake the poll early, so this mostly bounds latency when the socket is down. */
  pollIntervalMs?: number;
}

function streamWriter(stream: NodeJS.WriteStream): Writer {
  return {
    write: (text) => {
      stream.write(text);
    },
    get isTTY() {
      return Boolean(stream.isTTY);
    },
    get columns() {
      return stream.columns;
    },
    raw: stream
  };
}

export function defaultIO(): CliIO {
  return {
    stdout: streamWriter(process.stdout),
    stderr: streamWriter(process.stderr),
    env: process.env,
    cwd: process.cwd()
  };
}

export function resolveIO(partial: Partial<CliIO> = {}): CliIO {
  return { ...defaultIO(), ...partial };
}
