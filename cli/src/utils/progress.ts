import { CliContext } from "../context";
import { createStyle } from "./style";

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

const FRAMES = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];

/** A one-line spinner on a terminal stream. Only ever created when the stream is interactive. */
class Spinner {
  text: string;
  private frame = 0;
  private timer?: NodeJS.Timeout;

  constructor(
    private readonly stream: { write(chunk: string): unknown },
    text: string,
    private readonly paint: (text: string) => string
  ) {
    this.text = text;
  }

  start(): this {
    this.render();
    this.timer = setInterval(() => {
      this.frame = (this.frame + 1) % FRAMES.length;
      this.render();
    }, 80);
    this.timer.unref();
    return this;
  }

  render(): void {
    this.stream.write(`\r\u001b[2K${this.paint(FRAMES[this.frame])} ${this.text}`);
  }

  clear(): void {
    this.stream.write("\r\u001b[2K");
  }

  stop(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = undefined;
    this.clear();
  }
}

class SpinnerProgress implements Progress {
  constructor(
    private readonly spinner: Spinner,
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

function startSpinner(ctx: CliContext, text: string): Spinner | undefined {
  const stream = ctx.io.stderr.raw;
  if (!ctx.config.interactive || !stream) return undefined;
  return new Spinner(stream, text, createStyle(ctx.config.color).cyan).start();
}

/** Progress for a multi-step wait. With `quiet` (`--json`) it prints nothing at all. */
export async function createProgress(ctx: CliContext, options: { quiet?: boolean; text?: string } = {}): Promise<Progress> {
  const write = (line: string) => ctx.out.line(line);
  if (options.quiet) return new PlainProgress(undefined);
  const spinner = startSpinner(ctx, options.text ?? "");
  return spinner ? new SpinnerProgress(spinner, write) : new PlainProgress(write);
}

/** Runs one slow operation behind a spinner (terminal only). Plain output stays silent until the caller prints the result. */
export async function withSpinner<T>(ctx: CliContext, text: string, task: () => Promise<T>): Promise<T> {
  const spinner = startSpinner(ctx, text);
  try {
    return await task();
  } finally {
    spinner?.stop();
  }
}
