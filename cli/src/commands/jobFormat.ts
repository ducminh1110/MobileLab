import { JobResults, TestJob } from "../client/types";
import { Column } from "../utils/table";
import { formatDuration, formatSeconds, oneLine, sanitize, shortId, timeAgo } from "../utils/format";
import { Output } from "../utils/output";

/** `passed/total`, or an en dash when the job has no test summary (never ran, or the build failed). */
export function testsCell(job: TestJob): string {
  const s = job.summary;
  if (!s || s.buildFailed) return "–";
  return `${s.passed}/${s.total}${s.skipped ? ` (${s.skipped} skipped)` : ""}`;
}

export function durationCell(job: TestJob): string {
  if (job.durationMs !== undefined) return formatDuration(job.durationMs);
  return job.summary ? formatSeconds(job.summary.durationSeconds) : "–";
}

export function deviceCell(job: TestJob): string {
  return job.assignedDeviceName ?? "–";
}

/** Columns for a list of jobs (`test list`, `status`). */
export function jobListColumns(out: Output, now: number = Date.now()): Array<Column<TestJob>> {
  return [
    { header: "ID", value: (j) => shortId(j.id) },
    { header: "SCHEME", value: (j) => j.testTarget, flex: true },
    { header: "STATUS", value: (j) => j.status, style: (t) => out.jobStatus(t) },
    { header: "DEVICE", value: deviceCell, flex: true },
    { header: "TESTS", value: testsCell, align: "right" },
    { header: "DURATION", value: durationCell, align: "right" },
    { header: "CREATED", value: (j) => timeAgo(j.createdAt, now) }
  ];
}

/** Columns for the final results of a run: one row per job. */
export function resultColumns(out: Output): Array<Column<TestJob>> {
  return [
    { header: "JOB", value: (j) => shortId(j.id) },
    { header: "DEVICE", value: deviceCell, flex: true },
    { header: "STATUS", value: (j) => j.status, style: (t) => out.jobStatus(t) },
    { header: "PASSED", value: testsCell, align: "right" },
    { header: "DURATION", value: durationCell, align: "right" },
    { header: "ERROR", value: (j) => (j.status === "completed" ? "" : (j.error ?? "")), flex: true, minWidth: 20, style: (t) => out.c.red(t) }
  ];
}

const MAX_MESSAGE_LINES = 6;

/** The failing test cases of one job, each with its assertion message. */
export function failureLines(out: Output, job: TestJob, results: JobResults | undefined, indent = "  "): string[] {
  const lines: string[] = [];
  const failed = (results?.cases ?? []).filter((c) => c.status === "failed");
  for (const test of failed) {
    const name = test.className ? `${test.className}.${test.name}` : test.name;
    lines.push(`${indent}${out.c.red("✖")} ${sanitize(name)}`);
    const message = sanitize(test.message ?? "").split("\n").map((l) => l.trimEnd()).filter(Boolean);
    for (const line of message.slice(0, MAX_MESSAGE_LINES)) lines.push(`${indent}    ${out.dim(line)}`);
    if (message.length > MAX_MESSAGE_LINES) lines.push(`${indent}    ${out.dim(`… ${message.length - MAX_MESSAGE_LINES} more lines`)}`);
  }
  const summary = job.summary ?? results?.summary ?? undefined;
  if (summary?.buildFailed) {
    lines.push(`${indent}${out.c.red("✖")} The build failed before any test ran:`);
    for (const error of summary.errors.slice(0, MAX_MESSAGE_LINES)) lines.push(`${indent}    ${out.dim(oneLine(error))}`);
  } else if (failed.length === 0 && job.status === "failed" && job.error) {
    lines.push(`${indent}${out.c.red("✖")} ${oneLine(job.error)}`);
  }
  return lines;
}
