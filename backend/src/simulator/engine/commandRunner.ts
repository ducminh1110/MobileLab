import { spawn } from "node:child_process";
import fs from "node:fs";
import { StringDecoder } from "node:string_decoder";

export interface CommandResult {
  stdout: string;
  stderr: string;
  /** Process exit code, or -1 when it was killed (timeout / abort). */
  code: number;
  signal?: NodeJS.Signals;
  durationMs: number;
  timedOut: boolean;
  aborted: boolean;
  /** True when the captured output was cut to `maxCaptureBytes` (the log file, if any, is complete). */
  truncated: boolean;
}

export interface RunOptions {
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  timeoutMs?: number;
  signal?: AbortSignal;
  /** Called for every complete line of output, as it arrives. */
  onLine?: (line: string, stream: "stdout" | "stderr") => void;
  /** Every byte of output is appended here, so nothing is lost when capture is truncated. */
  logFile?: string;
  /** Per-stream in-memory capture limit. Default 2 MiB (the tail is kept). */
  maxCaptureBytes?: number;
}

export interface CommandRunner {
  /**
   * Runs a command without a shell. Resolves with the result for any exit code (a failing build is
   * data, not an exception); rejects only when the process cannot be started at all.
   */
  run(command: string, args: string[], options?: RunOptions): Promise<CommandResult>;
}

export class CommandError extends Error {
  constructor(
    message: string,
    public readonly command: string,
    public readonly result?: CommandResult
  ) {
    super(message);
    this.name = "CommandError";
  }
}

/** Throws a CommandError (carrying tool output) unless the command exited 0. */
export function ensureSuccess(command: string, result: CommandResult): CommandResult {
  if (result.code === 0 && !result.timedOut && !result.aborted) return result;
  const reason = result.timedOut ? "timed out" : result.aborted ? "was cancelled" : `exited with code ${result.code}`;
  const detail = (result.stderr.trim() || result.stdout.trim()).split("\n").slice(-6).join("\n");
  throw new CommandError(`${command} ${reason}${detail ? `: ${detail}` : ""}`, command, result);
}

class TailBuffer {
  private chunks: string[] = [];
  private size = 0;
  truncated = false;

  constructor(private readonly limit: number) {}

  push(text: string): void {
    this.chunks.push(text);
    this.size += text.length;
    while (this.size > this.limit && this.chunks.length > 1) {
      this.size -= this.chunks.shift()!.length;
      this.truncated = true;
    }
  }

  toString(): string {
    return this.chunks.join("");
  }
}

function killTree(pid: number | undefined, signal: NodeJS.Signals): void {
  if (!pid) return;
  try {
    // The child is a process-group leader (detached), so a negative pid reaches xcodebuild's
    // helper processes as well; otherwise cancelling a test run would leave xctest running.
    process.kill(process.platform === "win32" ? pid : -pid, signal);
  } catch {
    /* already gone */
  }
}

export class RealCommandRunner implements CommandRunner {
  run(command: string, args: string[], options: RunOptions = {}): Promise<CommandResult> {
    return new Promise((resolve, reject) => {
      const started = Date.now();
      const limit = options.maxCaptureBytes ?? 2 * 1024 * 1024;
      const stdout = new TailBuffer(limit);
      const stderr = new TailBuffer(limit);
      const logStream = options.logFile ? fs.createWriteStream(options.logFile, { flags: "a" }) : undefined;

      let timedOut = false;
      let aborted = false;
      let settled = false;
      let timer: NodeJS.Timeout | undefined;
      let killTimer: NodeJS.Timeout | undefined;

      const child = spawn(command, args, {
        cwd: options.cwd,
        env: options.env ?? process.env,
        stdio: ["ignore", "pipe", "pipe"],
        detached: process.platform !== "win32"
      });

      const stop = () => {
        killTree(child.pid, "SIGTERM");
        killTimer = setTimeout(() => killTree(child.pid, "SIGKILL"), 5_000);
        killTimer.unref();
      };

      const onAbort = () => {
        aborted = true;
        stop();
      };

      if (options.signal) {
        if (options.signal.aborted) onAbort();
        else options.signal.addEventListener("abort", onAbort, { once: true });
      }

      if (options.timeoutMs && options.timeoutMs > 0) {
        timer = setTimeout(() => {
          timedOut = true;
          stop();
        }, options.timeoutMs);
        timer.unref();
      }

      const attach = (source: NodeJS.ReadableStream, sink: TailBuffer, name: "stdout" | "stderr") => {
        const decoder = new StringDecoder("utf8");
        let partial = "";
        const flushLines = (text: string) => {
          partial += text;
          let index = partial.indexOf("\n");
          while (index !== -1) {
            const line = partial.slice(0, index).replace(/\r$/, "");
            partial = partial.slice(index + 1);
            options.onLine?.(line, name);
            index = partial.indexOf("\n");
          }
        };
        source.on("data", (chunk: Buffer) => {
          const text = decoder.write(chunk);
          sink.push(text);
          logStream?.write(text);
          if (options.onLine) flushLines(text);
        });
        source.on("end", () => {
          const rest = decoder.end();
          if (rest) {
            sink.push(rest);
            logStream?.write(rest);
            if (options.onLine) flushLines(rest);
          }
          if (options.onLine && partial) options.onLine(partial, name);
        });
      };

      attach(child.stdout, stdout, "stdout");
      attach(child.stderr, stderr, "stderr");

      const finish = (finalize: () => void) => {
        if (settled) return;
        settled = true;
        if (timer) clearTimeout(timer);
        if (killTimer) clearTimeout(killTimer);
        options.signal?.removeEventListener("abort", onAbort);
        if (logStream) logStream.end(finalize);
        else finalize();
      };

      child.on("error", (error: NodeJS.ErrnoException) => {
        finish(() => {
          const message =
            error.code === "ENOENT"
              ? `Command not found: ${command}${command === "xcrun" || command === "xcodebuild" ? " (is Xcode installed and selected with xcode-select?)" : ""}`
              : `Failed to start ${command}: ${error.message}`;
          reject(new CommandError(message, command));
        });
      });

      child.on("close", (code, signal) => {
        finish(() =>
          resolve({
            stdout: stdout.toString(),
            stderr: stderr.toString(),
            code: code ?? -1,
            signal: signal ?? undefined,
            durationMs: Date.now() - started,
            timedOut,
            aborted,
            truncated: stdout.truncated || stderr.truncated
          })
        );
      });
    });
  }
}
