import { ResolvedConfig } from "../config";
import { CliIO } from "../io";
import { DeviceStatus, JobStatus } from "../client/types";
import { sanitize } from "./format";
import { createStyle, Style } from "./style";

/** Everything a command prints goes through here, so it can be captured, colored or silenced in one place. */
export class Output {
  /** Colours for this run: plain text when colours are off. */
  readonly c: Style;

  constructor(
    readonly io: CliIO,
    readonly config: ResolvedConfig
  ) {
    this.c = createStyle(config.color);
  }

  /** A line on stdout. Server-provided text must be `sanitize`d by the caller (tables and helpers here do it). */
  line(text = ""): void {
    this.io.stdout.write(`${text}\n`);
  }

  lines(items: string[]): void {
    for (const item of items) this.line(item);
  }

  /** A line on stderr. */
  errLine(text = ""): void {
    this.io.stderr.write(`${text}\n`);
  }

  warn(text: string): void {
    this.errLine(this.c.yellow(`Warning: ${text}`));
  }

  json(value: unknown): void {
    this.line(JSON.stringify(value, null, 2));
  }

  /** Width tables should fit in: the terminal's, or unlimited when piping. */
  get tableWidth(): number | undefined {
    return this.io.stdout.isTTY ? this.io.stdout.columns || 100 : undefined;
  }

  // Arrow properties so they can be passed around as callbacks (`headerStyle: out.dim`).
  readonly heading = (text: string): string => this.c.bold(text);
  readonly dim = (text: string): string => this.c.dim(text);

  deviceStatus(status: DeviceStatus | string): string {
    const text = sanitize(status);
    switch (status) {
      case "ready":
        return this.c.green(text);
      case "busy":
        return this.c.cyan(text);
      case "booting":
      case "shutting_down":
      case "created":
        return this.c.yellow(text);
      case "error":
        return this.c.red(text);
      default:
        return this.c.dim(text);
    }
  }

  jobStatus(status: JobStatus | string): string {
    const text = sanitize(status);
    switch (status) {
      case "completed":
      case "passed":
        return this.c.green(text);
      case "failed":
        return this.c.red(text);
      case "cancelled":
        return this.c.yellow(text);
      case "running":
        return this.c.cyan(text);
      default:
        return this.c.yellow(text);
    }
  }
}
