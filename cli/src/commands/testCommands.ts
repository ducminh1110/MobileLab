import fs from "node:fs";
import path from "node:path";
import { CliContext } from "../context";
import { ApiError, BackendUnreachableError, CliError, EXIT, UsageError, errorText } from "../errors";
import { isTerminal, JobResults, JobStatus, TestJob, TestRunView } from "../client/types";
import { formatDuration, formatSeconds, oneLine, plural, sanitize, shortId } from "../utils/format";
import { resolveJobId, withListHint } from "../utils/resolve";
import { renderTable } from "../utils/table";
import { demoBanner } from "./statusCommand";
import { followJobs, FollowResult } from "./follow";
import { failureLines, jobListColumns, resultColumns } from "./jobFormat";

const JOBS_HINT = 'Run "ioslab test list" to see recent jobs.';

// ------------------------------------------------------------------ submitting

export interface RunOptions {
  project?: string;
  workspace?: string;
  dir?: string;
  configuration?: string;
  onlyTesting?: string[];
  runtime?: string[];
  model?: string[];
  parallel?: number;
  retries?: number;
  /** `--no-provision` sets this to false. */
  provision?: boolean;
  /** `--no-wait` sets this to false. */
  wait?: boolean;
  timeout?: number;
  junit?: string;
  verbose?: boolean;
}

interface Submitted {
  jobs: TestJob[];
  run?: TestRunView;
}

function unique(values: string[] | undefined): string[] {
  return [...new Set((values ?? []).map((v) => v.trim()).filter(Boolean))];
}

/** Warns (on stderr) when the backend only simulates; a green CI job against a demo backend proves nothing. */
async function noteDemoMode(ctx: CliContext): Promise<void> {
  try {
    const health = await ctx.client.health();
    if (health.mode === "demo") ctx.out.errLine(demoBanner(ctx));
  } catch (error) {
    // No backend at all is the same answer the real request would get, just sooner.
    if (error instanceof BackendUnreachableError) throw error;
  }
}

async function submit(ctx: CliContext, scheme: string, options: RunOptions): Promise<Submitted> {
  const shared = {
    testTarget: scheme,
    projectPath: options.project,
    workspacePath: options.workspace,
    workingDirectory: options.dir,
    configuration: options.configuration,
    onlyTesting: options.onlyTesting?.length ? options.onlyTesting : undefined,
    maxRetries: options.retries,
    autoProvision: options.provision === false ? false : undefined
  };
  const runtimes = unique(options.runtime);
  const models = unique(options.model);

  if (runtimes.length > 1 || models.length > 1) {
    const { run, jobs } = await ctx.client.createRun({
      ...shared,
      runtimes: runtimes.length ? runtimes : undefined,
      models: models.length ? models : undefined,
      maxParallel: options.parallel
    });
    return { run, jobs };
  }
  const { job } = await ctx.client.runTest({ ...shared, requiredRuntime: runtimes[0], requiredModelId: models[0] });
  return { jobs: [job] };
}

function requirementText(job: TestJob): string {
  return [job.requiredRuntime, job.requiredModelId].filter((v): v is string => !!v).map((id) => oneLine(id.split(".").pop() ?? id)).join(" · ");
}

function printQueued(ctx: CliContext, submitted: Submitted): void {
  const { out } = ctx;
  if (ctx.config.json) {
    out.json({ run: submitted.run, jobs: submitted.jobs });
    return;
  }
  const scheme = sanitize(submitted.jobs[0]?.testTarget ?? "");
  if (submitted.run) {
    out.line(`Queued run ${submitted.run.id} (${scheme}, ${plural(submitted.jobs.length, "job")})`);
    for (const job of submitted.jobs) out.line(`  job ${job.id}  ${out.dim(requirementText(job) || "default runtime and device")}`);
  } else {
    out.line(`Queued job ${submitted.jobs[0].id} (${scheme})`);
  }
}

// ------------------------------------------------------------------ waiting, reporting

function suffixed(file: string, suffix: string): string {
  const parsed = path.parse(file);
  return path.join(parsed.dir, `${parsed.name}-${suffix}${parsed.ext}`);
}

/**
 * Fetches each job's JUnit XML and writes it. One job: exactly `file`. Several: `file` with the job's
 * short id added before the extension. Returns the paths written.
 */
async function writeJunitReports(ctx: CliContext, jobs: TestJob[], file: string): Promise<string[]> {
  const written: string[] = [];
  for (const job of jobs) {
    if (!isTerminal(job.status)) continue;
    const target = path.resolve(ctx.io.cwd, jobs.length > 1 ? suffixed(file, shortId(job.id)) : file);
    let xml: string;
    try {
      xml = await ctx.client.jobJunit(job.id);
    } catch (error) {
      // A job that never got as far as running has no results to report; that is already a failed job.
      if (error instanceof ApiError && error.status === 404) {
        ctx.out.warn(`No JUnit report for job ${shortId(job.id)}: ${error.message}`);
        continue;
      }
      throw error;
    }
    try {
      fs.mkdirSync(path.dirname(target), { recursive: true });
      fs.writeFileSync(target, xml.endsWith("\n") ? xml : `${xml}\n`);
    } catch (error) {
      throw new CliError(`Could not write the JUnit report to ${target}: ${errorText(error)}`);
    }
    written.push(target);
  }
  return written;
}

async function cancelAfterInterrupt(ctx: CliContext, submitted: Submitted): Promise<void> {
  const { out, client } = ctx;
  const open = submitted.jobs.length;
  out.errLine(`Interrupted. Asking the backend to cancel ${open === 1 ? "the job" : `the ${open} jobs`}...`);
  try {
    if (submitted.run) {
      const { run } = await client.cancelRun(submitted.run.id);
      out.errLine(`Cancelled run ${shortId(run.id)} (${plural(run.counts.cancelled, "job")} cancelled, ${run.counts.completed + run.counts.failed} had already finished).`);
    } else {
      const job = submitted.jobs[0];
      const after = await client.cancelJob(job.id);
      out.errLine(`Cancelled job ${shortId(after.id)} (${sanitize(after.testTarget)}).`);
    }
  } catch (error) {
    // Already finished on its own is fine; anything else the user should know about.
    if (error instanceof ApiError && error.status === 409) out.errLine(`Nothing to cancel: ${error.message}`);
    else out.errLine(`Could not cancel: ${errorText(error)}`);
  }
}

async function fetchResults(ctx: CliContext, jobs: TestJob[], all: boolean): Promise<Map<string, JobResults>> {
  const wanted = jobs.filter((job) => isTerminal(job.status) && job.attempts > 0 && (all || job.summary?.failed || job.summary?.buildFailed));
  const fetched = await Promise.all(wanted.map(async (job) => [job.id, await ctx.client.jobResults(job.id)] as const));
  return new Map(fetched);
}

function summaryLine(ctx: CliContext, jobs: TestJob[]): string {
  const { c } = ctx.out;
  const count = (status: JobStatus) => jobs.filter((j) => j.status === status).length;
  const passed = count("completed");
  if (passed === jobs.length) return `${c.green("✔")} ${jobs.length === 1 ? "The job passed." : `All ${jobs.length} jobs passed.`}`;
  const notDone = jobs.filter((j) => !isTerminal(j.status)).length;
  const parts = [
    count("failed") ? `${count("failed")} failed` : "",
    count("cancelled") ? `${count("cancelled")} cancelled` : "",
    notDone ? `${notDone} unfinished` : ""
  ].filter(Boolean);
  const text = jobs.length === 1 ? `The job did not pass (${parts.join(", ")}).` : `${jobs.length - passed} of ${jobs.length} jobs did not pass (${parts.join(", ")}).`;
  return `${c.red("✖")} ${text}`;
}

interface WaitOptions {
  verbose?: boolean;
  timeout?: number;
  junit?: string;
}

/** Follows a submission to the end and reports. Returns the process exit code. */
async function waitAndReport(ctx: CliContext, submitted: Submitted, options: WaitOptions): Promise<number> {
  const { out } = ctx;
  const result: FollowResult = await followJobs(ctx, {
    jobs: submitted.jobs,
    runId: submitted.run?.id,
    verbose: Boolean(options.verbose),
    timeoutMs: options.timeout ? options.timeout * 1000 : undefined
  });

  if (result.status === "interrupted") {
    await cancelAfterInterrupt(ctx, submitted);
    return ctx.interrupt.exitCode;
  }

  const { jobs } = result;
  const results = await fetchResults(ctx, jobs, ctx.config.json);
  const written = options.junit ? await writeJunitReports(ctx, jobs, options.junit) : [];
  const allPassed = result.status === "done" && jobs.every((job) => job.status === "completed");

  if (ctx.config.json) {
    out.json({
      ok: allPassed,
      timedOut: result.status === "timeout",
      run: result.run ?? submitted.run,
      jobs,
      results: Object.fromEntries(results),
      junit: written
    });
  } else {
    out.line();
    out.line(out.heading(`Results: ${sanitize(jobs[0]?.testTarget ?? "")}`));
    out.lines(renderTable(resultColumns(out), jobs, { maxWidth: out.tableWidth, headerStyle: out.dim }));

    for (const job of jobs) {
      const lines = failureLines(out, job, results.get(job.id));
      if (lines.length === 0) continue;
      out.line();
      out.line(out.heading(jobs.length > 1 ? `Failures on ${oneLine(job.assignedDeviceName ?? "no device")} (job ${shortId(job.id)})` : "Failures"));
      out.lines(lines);
    }

    out.line();
    out.line(summaryLine(ctx, jobs));
    for (const file of written) out.line(`Wrote JUnit report: ${file}`);
  }

  if (result.status === "timeout") {
    const pending = jobs.filter((j) => !isTerminal(j.status));
    const example = shortId(pending[0]?.id ?? "");
    throw new CliError(
      `Timed out after ${options.timeout}s; ${plural(pending.length, "job")} ${pending.length === 1 ? "is" : "are"} still unfinished on the backend. ` +
        `Check with "ioslab test show ${example}" or stop with "ioslab test cancel ${example}".`,
      EXIT.FAILED
    );
  }
  return allPassed ? EXIT.OK : EXIT.FAILED;
}

// ------------------------------------------------------------------ commands

export async function runCommand(ctx: CliContext, scheme: string, options: RunOptions): Promise<number> {
  if (options.project && options.workspace) throw new UsageError("Give either --project or --workspace, not both.");
  if (options.wait === false && options.junit) throw new UsageError("--junit needs the results, so it cannot be combined with --no-wait.");
  const matrix = unique(options.runtime).length > 1 || unique(options.model).length > 1;
  if (options.parallel !== undefined && !matrix) ctx.out.warn("--parallel only applies when more than one --runtime or --model is given; ignoring it.");

  // From here on Ctrl+C means "cancel what I submitted", including if it arrives while submitting.
  ctx.interrupt.claimed = true;
  await noteDemoMode(ctx);
  const submitted = await submit(ctx, scheme, options);

  if (options.wait === false) {
    printQueued(ctx, submitted);
    return EXIT.OK;
  }
  return waitAndReport(ctx, submitted, options);
}

export interface ListOptions {
  status?: JobStatus;
  limit?: number;
  run?: string;
}

export async function listCommand(ctx: CliContext, options: ListOptions): Promise<number> {
  const { out } = ctx;
  const { items } = await ctx.client.listJobs({ status: options.status, limit: options.limit ?? 20, runId: options.run });
  if (ctx.config.json) {
    out.json({ items });
    return 0;
  }
  if (items.length === 0) {
    out.line(options.status ? `No ${options.status} jobs.` : 'No jobs yet. Run some with "ioslab test run <scheme>".');
    return 0;
  }
  out.lines(renderTable(jobListColumns(out), items, { maxWidth: out.tableWidth, headerStyle: out.dim }));
  return 0;
}

function field(label: string, value: string | undefined): string | undefined {
  return value === undefined || value === "" ? undefined : `${label.padEnd(11)}${value}`;
}

export async function showCommand(ctx: CliContext, ref: string): Promise<number> {
  const { out, client } = ctx;
  const id = await resolveJobId(client, ref);
  let job: TestJob;
  let results: JobResults | undefined;
  try {
    job = await client.getJob(id);
    if (job.attempts > 0) results = await client.jobResults(id);
  } catch (error) {
    throw withListHint(error, JOBS_HINT);
  }

  if (ctx.config.json) {
    out.json({ job, results });
    return 0;
  }

  const summary = job.summary;
  const requested = requirementText(job);
  const lines = [
    field("Job", job.id),
    field("Scheme", sanitize(job.testTarget)),
    field("Status", `${out.jobStatus(job.status)}${job.status === "queued" && job.waitingReason ? out.dim(` (${sanitize(job.waitingReason)})`) : ""}`),
    field("Run", job.runId),
    field("Device", job.assignedDeviceName ? oneLine(job.assignedDeviceName) : undefined),
    field("Requested", requested || undefined),
    field("Project", job.projectPath ? oneLine(job.projectPath) : undefined),
    field("Workspace", job.workspacePath ? oneLine(job.workspacePath) : undefined),
    field("Directory", job.workingDirectory ? oneLine(job.workingDirectory) : undefined),
    field("Config", job.configuration ? oneLine(job.configuration) : undefined),
    field("Only", job.onlyTesting?.length ? job.onlyTesting.map(oneLine).join(", ") : undefined),
    field("Attempts", job.attempts ? `${job.attempts} (${plural(job.retries, "retry", "retries")} used of ${job.maxRetries} allowed)` : undefined),
    field("Created", job.createdAt),
    field("Started", job.startedAt),
    field("Finished", job.finishedAt),
    field("Duration", job.durationMs !== undefined ? formatDuration(job.durationMs) : undefined),
    field("Exit code", job.exitCode !== undefined ? String(job.exitCode) : undefined),
    field("Error", job.error && job.status !== "completed" ? out.c.red(oneLine(job.error)) : undefined),
    field(
      "Tests",
      summary && !summary.buildFailed
        ? `${summary.total} total, ${out.c.green(`${summary.passed} passed`)}, ${summary.failed ? out.c.red(`${summary.failed} failed`) : "0 failed"}, ${summary.skipped} skipped (${formatSeconds(summary.durationSeconds)})`
        : undefined
    )
  ].filter((line): line is string => line !== undefined);
  out.lines(lines);

  const failures = failureLines(out, job, results);
  if (failures.length) {
    out.line();
    out.line(out.heading("Failures"));
    out.lines(failures);
  }
  return 0;
}

export async function cancelCommand(ctx: CliContext, ref: string): Promise<number> {
  const id = await resolveJobId(ctx.client, ref);
  let job: TestJob;
  try {
    job = await ctx.client.cancelJob(id);
  } catch (error) {
    throw withListHint(error, JOBS_HINT);
  }
  if (ctx.config.json) ctx.out.json(job);
  else ctx.out.line(`${ctx.out.c.yellow("Cancelled")} job ${shortId(job.id)} (${sanitize(job.testTarget)}); it is now ${ctx.out.jobStatus(job.status)}.`);
  return 0;
}

export interface RerunOptions {
  wait?: boolean;
  timeout?: number;
  junit?: string;
  verbose?: boolean;
}

export async function rerunCommand(ctx: CliContext, ref: string, options: RerunOptions): Promise<number> {
  if (options.wait === false && options.junit) throw new UsageError("--junit needs the results, so it cannot be combined with --no-wait.");
  const id = await resolveJobId(ctx.client, ref);
  ctx.interrupt.claimed = true;
  await noteDemoMode(ctx);
  let job: TestJob;
  try {
    job = (await ctx.client.rerunJob(id)).job;
  } catch (error) {
    throw withListHint(error, JOBS_HINT);
  }
  const submitted: Submitted = { jobs: [job] };
  if (options.wait === false) {
    printQueued(ctx, submitted);
    return EXIT.OK;
  }
  return waitAndReport(ctx, submitted, options);
}

export async function junitCommand(ctx: CliContext, ref: string, options: { output?: string }): Promise<number> {
  const id = await resolveJobId(ctx.client, ref);
  let xml: string;
  try {
    xml = await ctx.client.jobJunit(id);
  } catch (error) {
    throw withListHint(error, JOBS_HINT);
  }
  const text = xml.endsWith("\n") ? xml : `${xml}\n`;
  if (!options.output) {
    ctx.io.stdout.write(text);
    return 0;
  }
  const target = path.resolve(ctx.io.cwd, options.output);
  try {
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, text);
  } catch (error) {
    throw new CliError(`Could not write ${target}: ${errorText(error)}`);
  }
  ctx.out.line(`Wrote JUnit report: ${target}`);
  return 0;
}
