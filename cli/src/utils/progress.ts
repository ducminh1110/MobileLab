import type { Ora } from "ora";
import { CliContext } from "../context";

/**
 * Live feedback while something takes a while. On a terminal: a spinner (on stderr) with lines printed
 * above it. Anywhere else (pipes, CI logs, `--json`): no spinner and no ANSI, just one plain line per event.
 */
export interface Progress {
  /** Replaces the spinner text. Ignored when there is no spinner. */
  update(text: string): void;
  /** A finished line of progress. */
  log(line: string): void;
  stop(): void;
}

class PlainProgress implements Progress {
  constructor(private readonly write: ((line: string) => void) | undefined) {}
  update(): void {}
  log(line: string): void {
    this.write?.(line);
  }
  stop(): void {}
}

class SpinnerProgress implements Progress {
  constructor(
    private readonly spinner: Ora,
    private readonly write: (line: string) => void
  ) {}

  update(text: string): void {
    this.spinner.text = text;
  }

  log(line: string): void {
    this.spinner.clear();
    this.write(line);
    this.spinner.render();
  }

  stop(): void {
    this.spinner.stop();
  }
}

async function startSpinner(ctx: CliContext, text: string): Promise<Ora | undefined> {
  const stream = ctx.io.stderr.raw;
  if (!ctx.config.interactive || !stream) return undefined;
  try {
    // ora is ESM-only; load it only when a spinner will actually be shown.
    const { default: ora } = await import("ora");
    return ora({ text, stream, discardStdin: false }).start();
  } catch {
    return undefined;
  }
}

/** Progress for a multi-step wait. With `quiet` (`--json`) it prints nothing at all. */
export async function createProgress(ctx: CliContext, options: { quiet?: boolean; text?: string } = {}): Promise<Progress> {
  const write = (line: string) => ctx.out.line(line);
  if (options.quiet) return new PlainProgress(undefined);
  const spinner = await startSpinner(ctx, options.text ?? "");
  return spinner ? new SpinnerProgress(spinner, write) : new PlainProgress(write);
}

/** Runs one slow operation behind a spinner (terminal only). Plain output stays silent until the caller prints the result. */
export async function withSpinner<T>(ctx: CliContext, text: string, task: () => Promise<T>): Promise<T> {
  const spinner = await startSpinner(ctx, text);
  try {
    return await task();
  } finally {
    spinner?.stop();
  }
}
