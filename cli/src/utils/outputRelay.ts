/** Splits the text of GET /tests/:id/output into lines. A truncated tail starts mid-line, so its first line is dropped. */
export function logLines(text: string, truncated: boolean): string[] {
  const lines = text.split("\n").map((line) => line.replace(/\r$/, ""));
  if (lines.length && lines[lines.length - 1] === "") lines.pop();
  return truncated ? lines.slice(1) : lines;
}

/** The server cuts streamed lines to this length; log lines are compared after the same cut. */
const STREAM_LINE_LIMIT = 2000;

/** The backend writes one banner line at the top of each attempt's log; it is not part of the live stream. */
const LOG_HEADER = /^# MobileLab job /;

/**
 * How many leading lines of `streamed` are already the last lines of `log`. Used when the log tail is
 * fetched right after the live stream connected: whatever arrived on the stream before the tail was read
 * is in both, and must be printed once.
 */
export function overlapLength(log: string[], streamed: string[]): number {
  const max = Math.min(log.length, streamed.length);
  for (let n = max; n > 0; n -= 1) {
    let same = true;
    for (let i = 0; i < n; i += 1) {
      if (log[log.length - n + i].slice(0, STREAM_LINE_LIMIT) !== streamed[i]) {
        same = false;
        break;
      }
    }
    if (same) return n;
  }
  return 0;
}

interface JobStream {
  primed: boolean;
  buffer: string[];
  /** Lines of the log body (banner excluded) shown so far. */
  printed: number;
  /** Which attempt's log `printed` counts lines of; a retry starts a new log file. */
  attempt?: number;
  header?: string;
}

/**
 * Merges "the log so far" (a one-off fetch) with "new lines" (a live stream) into one gapless,
 * duplicate-free sequence per job. Stream lines are held back until the log tail has been printed.
 */
export class OutputRelay {
  private readonly jobs = new Map<string, JobStream>();

  constructor(private readonly emit: (jobId: string, line: string) => void) {}

  private state(jobId: string): JobStream {
    let state = this.jobs.get(jobId);
    if (!state) {
      state = { primed: false, buffer: [], printed: 0 };
      this.jobs.set(jobId, state);
    }
    return state;
  }

  private show(jobId: string, state: JobStream, lines: string[]): void {
    for (const line of lines) this.emit(jobId, line);
    state.printed += lines.length;
  }

  private split(state: JobStream, jobId: string, log: string[]): string[] {
    const header = log.length && LOG_HEADER.test(log[0]) ? log[0] : undefined;
    if (header && header !== state.header) {
      state.header = header;
      this.emit(jobId, header);
    }
    return header ? log.slice(1) : log;
  }

  /** A message from the live stream (one output line; the mock backend occasionally sends several at once). */
  push(jobId: string, message: string): void {
    const state = this.state(jobId);
    const lines = message.split("\n");
    if (state.primed) this.show(jobId, state, lines);
    else state.buffer.push(...lines);
  }

  /** The log as it was after the stream connected. Prints it, then the stream lines that are not already in it. */
  prime(jobId: string, log: string[], attempt?: number): void {
    const state = this.state(jobId);
    if (state.primed) return;
    const body = this.split(state, jobId, log);
    this.show(jobId, state, body);
    this.show(jobId, state, state.buffer.slice(overlapLength(body, state.buffer)));
    state.buffer = [];
    state.primed = true;
    state.attempt = attempt;
  }

  /**
   * Catch-up from a log fetch when the stream is not (or no longer) delivering: prints the lines after the
   * ones already shown. A truncated tail cannot be aligned with what was shown, so it is only used when
   * nothing was shown yet.
   */
  catchUp(jobId: string, log: string[], truncated: boolean, attempt?: number): void {
    const state = this.state(jobId);
    if (attempt !== undefined && state.attempt !== undefined && state.attempt !== attempt) state.printed = 0;
    state.attempt = attempt;
    const body = this.split(state, jobId, log);
    const from = truncated ? (state.printed === 0 ? 0 : body.length) : Math.min(state.printed, body.length);
    this.show(jobId, state, body.slice(from));
    state.buffer = [];
    state.primed = true;
  }
}
