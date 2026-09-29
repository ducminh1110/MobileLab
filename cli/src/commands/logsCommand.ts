import { CliContext } from "../context";
import { EventFeed } from "../client/events";
import { isTerminal, JobOutput, TestJob } from "../client/types";
import { BackendUnreachableError, EXIT, UsageError } from "../errors";
import { formatBytes, oneLine, sanitize, shortId } from "../utils/format";
import { logLines, OutputRelay } from "../utils/outputRelay";
import { resolveJobId, withListHint } from "../utils/resolve";
import { Waker } from "../utils/wait";

const JOBS_HINT = 'Run "ioslab test list" to see recent jobs.';
/** How long to let the stream deliver its last lines once the job is done. */
const FINISH_GRACE_MS = 500;
/** How much of the log to fetch when catching up without the stream. */
const CATCH_UP_BYTES = 1024 * 1024;

export interface LogsOptions {
  follow?: boolean;
}

function noteTruncation(ctx: CliContext, output: JobOutput): void {
  if (output.truncated) {
    ctx.out.errLine(ctx.out.dim(`(showing the last ${formatBytes(Buffer.byteLength(output.text))} of ${formatBytes(output.sizeBytes)}; the rest is in the job's log on the backend)`));
  }
}

/** `ioslab logs <job>`: the tail of the job's raw xcodebuild output. With `-f`, keeps going until the job finishes. */
export async function logsCommand(ctx: CliContext, ref: string, options: LogsOptions): Promise<number> {
  const { client, out } = ctx;
  if (options.follow && ctx.config.json) throw new UsageError("--json cannot be combined with --follow.");
  const id = await resolveJobId(client, ref);

  if (!options.follow) {
    let output: JobOutput;
    try {
      output = await client.jobOutput(id);
    } catch (error) {
      throw withListHint(error, JOBS_HINT);
    }
    if (ctx.config.json) {
      out.json(output);
      return EXIT.OK;
    }
    noteTruncation(ctx, output);
    if (output.text === "") {
      out.errLine("This job has no output yet.");
      return EXIT.OK;
    }
    const text = sanitize(output.text.replace(/\r\n/g, "\n"));
    ctx.io.stdout.write(text.endsWith("\n") ? text : `${text}\n`);
    return EXIT.OK;
  }

  return followLogs(ctx, id);
}

async function followLogs(ctx: CliContext, id: string): Promise<number> {
  const { client, out, interrupt } = ctx;
  // Ctrl+C only stops watching; unlike `test run`, the job itself is left alone.
  interrupt.claimed = true;

  let job: TestJob;
  try {
    job = await client.getJob(id);
  } catch (error) {
    throw withListHint(error, JOBS_HINT);
  }
  const alreadyDone = isTerminal(job.status);

  const relay = new OutputRelay((_jobId, line) => out.line(sanitize(line)));
  const waker = new Waker();
  let finishedEvent = false;
  const feed = new EventFeed(client, { jobId: id, output: true }, (event) => {
    if (event.action === "output" && event.source === "xcodebuild") relay.push(id, event.message);
    else if (event.action === "job_finished") {
      finishedEvent = true;
      waker.wake();
    }
  });

  try {
    // Connect first, then read the log so far: nothing written in between can be missed (see OutputRelay).
    const connected = await feed.opened;
    const first = await client.jobOutput(id, connected ? undefined : CATCH_UP_BYTES);
    noteTruncation(ctx, first);
    const lines = logLines(first.text, first.truncated);
    if (connected) relay.prime(id, lines, first.attempt);
    else relay.catchUp(id, lines, first.truncated, first.attempt);

    let outageSince: number | undefined;
    const maxOutageMs = Math.max(ctx.pollIntervalMs * 15, 1000);
    while (!alreadyDone) {
      if (interrupt.aborted) return interrupt.exitCode;
      try {
        job = await client.getJob(id);
        outageSince = undefined;
        if (isTerminal(job.status)) break;
        if (!feed.healthy) {
          const more = await client.jobOutput(id, CATCH_UP_BYTES);
          relay.catchUp(id, logLines(more.text, more.truncated), more.truncated, more.attempt);
        }
      } catch (error) {
        if (!(error instanceof BackendUnreachableError)) throw error;
        outageSince ??= Date.now();
        if (Date.now() - outageSince > maxOutageMs) throw error;
      }
      await waker.wait(ctx.pollIntervalMs, interrupt.signal);
    }

    if (!alreadyDone) {
      if (feed.healthy) {
        const until = Date.now() + FINISH_GRACE_MS;
        while (feed.healthy && !finishedEvent && Date.now() < until) await waker.wait(until - Date.now());
      } else {
        const rest = await client.jobOutput(id, CATCH_UP_BYTES);
        relay.catchUp(id, logLines(rest.text, rest.truncated), rest.truncated, rest.attempt);
      }
    }
  } finally {
    feed.close();
  }

  const detail = job.status !== "completed" && job.error ? `: ${oneLine(job.error)}` : "";
  out.errLine(`Job ${shortId(job.id)} ${job.status === "completed" ? "passed" : job.status}${detail}`);
  return job.status === "completed" ? EXIT.OK : EXIT.FAILED;
}
