import { CliContext } from "../context";
import { EventFeed } from "../client/events";
import { EngineEvent, isTerminal, JobStatus, TestJob, TestRunView } from "../client/types";
import { BackendUnreachableError, errorText } from "../errors";
import { OutputRelay, logLines } from "../utils/outputRelay";
import { createProgress } from "../utils/progress";
import { formatDuration, sanitize, shortId } from "../utils/format";
import { Output } from "../utils/output";
import { Waker } from "../utils/wait";

/** How long the first poll waits to learn whether the live event stream works (so lines are not reported twice). */
const FEED_GRACE_MS = 2000;
/** Once every job is done, how long to let the stream deliver its last events before results are printed. */
const FINISH_GRACE_MS = 500;
/** How much of a job's log to fetch when the stream cannot be trusted. */
const LOG_FETCH_BYTES = 1024 * 1024;

export interface FollowInput {
  jobs: TestJob[];
  /** Set for a matrix run: polls the run once instead of every job, and streams by run id. */
  runId?: string;
  /** Show raw xcodebuild output as it happens. */
  verbose: boolean;
  timeoutMs?: number;
}

export interface FollowResult {
  status: "done" | "timeout" | "interrupted";
  jobs: TestJob[];
  run?: TestRunView;
}

/** The line the backend itself would announce for a job that just finished; used when the stream did not deliver it. */
function finishLine(job: TestJob): string {
  const word = job.status === "completed" ? "passed" : job.status;
  return `${job.testTarget} ${word}${job.error && job.status !== "completed" ? `: ${job.error}` : ""}`;
}

/**
 * Progress lines derived from two consecutive polls. Only used to fill in for a stream that is not
 * connected. A poll can only see where a job is now, so anything that happened in between (a job that
 * ran and finished within one poll interval) is reconstructed from its fields.
 */
function pollLines(prev: TestJob | undefined, job: TestJob, streamIsLive: boolean): string[] {
  const lines: string[] = [];
  if (streamIsLive) return lines;
  const label = job.testTarget;
  if (!prev) lines.push(`Queued ${label}`);
  if (job.attempts > (prev?.attempts ?? 0)) {
    lines.push(`Running ${label} on ${job.assignedDeviceName ?? "a device"}${job.attempts > 1 ? ` (attempt ${job.attempts})` : ""}`);
  }
  if (job.status === "retrying" && prev?.status !== "retrying") {
    lines.push(`${label} failed (${job.error ?? "unknown error"}); retry ${job.retries}/${job.maxRetries}`);
  }
  if (isTerminal(job.status) && prev?.status !== job.status) lines.push(finishLine(job));
  return lines;
}

function eventLine(out: Output, event: EngineEvent): string {
  const text = sanitize(event.message);
  if (event.type === "error") return out.c.red(text);
  if (event.action === "job_finished" && text.endsWith(" passed")) return out.c.green(text);
  return text;
}

function spinnerText(jobs: TestJob[], startedAt: number): string {
  const order: JobStatus[] = ["queued", "running", "retrying", "completed", "failed", "cancelled"];
  const parts = order.map((status) => [status, jobs.filter((j) => j.status === status).length] as const).filter(([, n]) => n > 0).map(([status, n]) => `${n} ${status}`);
  const waiting = jobs.length === 1 && jobs[0].status === "queued" && jobs[0].waitingReason ? ` – ${sanitize(jobs[0].waitingReason)}` : "";
  return `${sanitize(jobs[0]?.testTarget ?? "")}: ${parts.join(", ")}${waiting} (${formatDuration(Date.now() - startedAt)})`;
}

/**
 * Follows jobs until they all finish, the deadline passes, or the user interrupts.
 *
 * Polling is the source of truth: the loop ends when the backend says every job is terminal, whatever
 * the WebSocket did. The live event stream only makes progress appear sooner (and wakes the poll the
 * moment a job finishes); if it never connects, or dies, plain polling carries on and prints the
 * status changes itself.
 */
export async function followJobs(ctx: CliContext, input: FollowInput): Promise<FollowResult> {
  const { client, out, interrupt } = ctx;
  const quiet = ctx.config.json;
  const ids = input.jobs.map((job) => job.id);
  const multi = ids.length > 1;
  const latest = new Map<string, TestJob>();
  let run: TestRunView | undefined;
  const finishedEvents = new Set<string>();
  const waker = new Waker();
  const startedAt = Date.now();
  const deadline = input.timeoutMs ? startedAt + input.timeoutMs : Number.POSITIVE_INFINITY;
  const pollMs = ctx.pollIntervalMs;
  const maxOutageMs = Math.max(pollMs * 15, 1000);

  const progress = await createProgress(ctx, { quiet, text: `${sanitize(input.jobs[0]?.testTarget ?? "")}: waiting…` });
  const tag = (jobId?: string) => (multi && jobId ? out.dim(`[${shortId(jobId)}] `) : "");

  const relay = input.verbose
    ? new OutputRelay((jobId, line) => {
        const text = `${tag(jobId)}${out.dim(sanitize(line))}`;
        // Under --json stdout carries the JSON document only; raw output goes to stderr.
        if (quiet) out.errLine(text);
        else progress.log(text);
      })
    : undefined;

  let lastEventId = 0;
  const onEvent = (event: EngineEvent) => {
    if (event.id <= lastEventId) return;
    lastEventId = event.id;
    if (event.jobId && !ids.includes(event.jobId)) return;
    if (event.action === "output" && event.source === "xcodebuild") {
      if (event.jobId) relay?.push(event.jobId, event.message);
      return;
    }
    progress.log(`${tag(event.jobId)}${eventLine(out, event)}`);
    if (event.action === "job_finished" && event.jobId) {
      finishedEvents.add(event.jobId);
      waker.wake();
    }
  };

  const feed = new EventFeed(client, { jobId: input.runId ? undefined : ids[0], runId: input.runId, output: input.verbose, replay: 200 }, onEvent);
  if (input.verbose) {
    void feed.opened.then((connected) => {
      if (!connected) out.errLine(out.dim(`(live event stream unavailable: ${feed.failure ?? "unknown reason"}; polling instead)`));
    });
  }

  // With -v, print each job's log so far as soon as the stream is up, so the start of the output is not lost.
  const primed: Promise<void> = input.verbose
    ? feed.opened.then(async (connected) => {
        if (!connected) return;
        await Promise.all(
          ids.map(async (id) => {
            try {
              const output = await client.jobOutput(id);
              relay!.prime(id, logLines(output.text, output.truncated), output.attempt);
            } catch {
              relay!.prime(id, []);
            }
          })
        );
      })
    : Promise.resolve();

  const fetchState = async (): Promise<TestJob[]> => {
    if (input.runId) {
      const state = await client.getRun(input.runId);
      run = state.run;
      return state.jobs;
    }
    return Promise.all(ids.map((id) => client.getJob(id)));
  };

  let status: FollowResult["status"] = "done";
  try {
    const connecting = new Waker();
    void feed.opened.then(() => connecting.wake());
    await connecting.wait(FEED_GRACE_MS, interrupt.signal);

    let outageSince: number | undefined;
    for (;;) {
      if (interrupt.aborted) {
        status = "interrupted";
        break;
      }

      let jobs: TestJob[] | undefined;
      try {
        jobs = await fetchState();
        outageSince = undefined;
      } catch (error) {
        // A hiccup mid-run is not the end of the run; a backend that stays gone for a while is.
        if (!(error instanceof BackendUnreachableError)) throw error;
        outageSince ??= Date.now();
        if (Date.now() - outageSince > maxOutageMs) throw error;
      }

      if (jobs) {
        const live = feed.healthy;
        for (const job of jobs) {
          const before = latest.get(job.id);
          for (const line of pollLines(before, job, live)) progress.log(`${tag(job.id)}${line}`);
          if (job.status === "queued" && job.waitingReason && job.waitingReason !== before?.waitingReason) {
            progress.log(`${tag(job.id)}${out.dim(`Waiting: ${sanitize(job.waitingReason)}`)}`);
          }
          latest.set(job.id, job);
        }
        progress.update(spinnerText(jobs, startedAt));
        if (jobs.length > 0 && jobs.every((job) => isTerminal(job.status))) break;
      }

      if (Date.now() >= deadline) {
        status = "timeout";
        break;
      }
      await waker.wait(Math.min(pollMs, deadline - Date.now()), interrupt.signal);
    }

    if (status === "done") {
      // Let the stream deliver its last lines (the final output, "passed"/"failed") before the results follow.
      await primed;
      const until = Date.now() + FINISH_GRACE_MS;
      while (feed.healthy && !ids.every((id) => finishedEvents.has(id)) && Date.now() < until) {
        await waker.wait(until - Date.now());
      }
      if (relay && !feed.healthy) {
        for (const job of latest.values()) {
          if (job.attempts === 0) continue;
          try {
            const output = await client.jobOutput(job.id, LOG_FETCH_BYTES);
            relay.catchUp(job.id, logLines(output.text, output.truncated), output.truncated, output.attempt);
          } catch (error) {
            out.warn(`Could not fetch the output of job ${shortId(job.id)}: ${errorText(error)}`);
          }
        }
      }
    }
  } finally {
    feed.close();
    progress.stop();
  }

  return { status, jobs: input.jobs.map((job) => latest.get(job.id) ?? job), run };
}
