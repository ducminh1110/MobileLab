// Tests, Issues and Reports navigators. All three read the same job data (GET /tests, /runs, and lazily
// /tests/:id/results when a job row is expanded or its issues are listed).

import { html } from "../core/util.js";
import { icon } from "../core/icons.js";
import { fmtDay, fmtTime, fmtSeconds, plural } from "../core/util.js";
import { state, prefs, isExpanded, isActive } from "../core/state.js";
import { CASE_STATUS, jobStatus, jobTitle, fmtJobDuration, runName, runJobs, RUN_STATUS, runCountsText, suitesOf, suiteStatus, jobIssues, jobDestinationName } from "../core/models.js";
import { ensureResults, resultsOf } from "../core/sync.js";
import { treeHtml } from "./tree.js";
import { skeleton, emptyState, errorState, statusGlyph } from "./nav-common.js";
import { elapsedSpan } from "./ticker.js";

const DAY = 86_400_000;

function jobMatches(job, q) {
  if (!q) return true;
  const hay = `${job.testTarget} ${jobDestinationName(job)} ${jobStatus(job).label} ${job.id}`.toLowerCase();
  return q.split(/\s+/).every((p) => hay.includes(p));
}

function jobTrailing(job) {
  if (job.status === "running") return elapsedSpan(job.startedAt);
  if (job.status === "queued") return "";
  const text = fmtJobDuration(job);
  return text ? html`<span class="row-time">${text}</span>` : "";
}

const rerunButton = (job) =>
  html`<button type="button" class="hover-btn" data-action="rerun-job" data-id="${job.id}" tabindex="-1" aria-hidden="true" title="Run Again">${icon("play.fill", "ic-10")}</button>`;

function jobRow(nav, job, level, extra = {}) {
  return {
    nav,
    key: `job:${job.id}`,
    level,
    sel: { kind: "job", id: job.id },
    open: { kind: "job", id: job.id },
    ctx: "job",
    icon: statusGlyph(jobStatus(job)),
    label: jobTitle(job),
    trailing: jobTrailing(job),
    title: `${jobTitle(job)}\n${jobStatus(job).label}${job.error ? `\n${job.error}` : ""}`,
    ...extra
  };
}

// ---------------------------------------------------------------- Tests

function resultRows(job, level, q) {
  const rows = [];
  if (isActive(job)) {
    rows.push({ nav: "tests", key: `wait:${job.id}`, level, disabled: true, dim: true, icon: "", label: job.status === "queued" ? "Waiting for a simulator…" : "Results appear when the attempt finishes…" });
    return rows;
  }
  const entry = ensureResults(job.id);
  if (entry.status === "loading" && !entry.data) return [{ nav: "tests", key: `load:${job.id}`, level, disabled: true, dim: true, label: html`<span class="spin spin-sm"></span> Loading results…` }];
  if (entry.status === "error") return [{ nav: "tests", key: `err:${job.id}`, level, disabled: true, label: html`<span class="c-fail">${entry.error}</span>` }];
  const data = entry.data || resultsOf(job.id);
  const cases = data?.cases || [];
  if (!cases.length) {
    const why = job.summary?.buildFailed ? "The build failed before any test ran." : "No test results were recorded.";
    return [{ nav: "tests", key: `none:${job.id}`, level, disabled: true, dim: true, label: why }];
  }
  for (const suite of suitesOf(cases)) {
    const list = q ? suite.cases.filter((c) => `${c.name} ${suite.name}`.toLowerCase().includes(q)) : suite.cases;
    if (!list.length) continue;
    const skey = `suite:${job.id}:${suite.className}`;
    const open = q ? true : isExpanded("tests", skey, suite.failed > 0);
    const status = CASE_STATUS[suiteStatus(suite)];
    rows.push({
      nav: "tests", key: skey, level, expandable: true, expanded: open,
      open: { kind: "job", id: job.id, tab: "tests" },
      icon: statusGlyph(status), label: suite.name,
      trailing: html`<span class="row-time">${suite.failed ? `${suite.failed} failed` : plural(suite.cases.length, "test")}</span>`
    });
    if (!open) continue;
    for (const c of list) {
      rows.push({
        nav: "tests", key: `case:${job.id}:${suite.className}/${c.name}`, level: level + 1,
        open: { kind: "job", id: job.id, tab: "logs", caseName: c.name },
        icon: statusGlyph(CASE_STATUS[c.status]), label: c.name,
        trailing: html`<span class="row-time">${fmtSeconds(c.durationSeconds)}</span>`,
        title: c.message ? `${c.name}\n${c.message}` : c.name
      });
    }
  }
  return rows;
}

export function testsView() {
  if (!state.loaded.jobs || !state.loaded.runs) return { html: skeleton(9), count: 0 };
  if (state.errors.jobs && !state.jobs.length) return { html: errorState("test runs", state.errors.jobs), count: 0 };
  if (!state.jobs.length) {
    return { html: emptyState({ iconName: "diamond", title: "No test runs yet", text: "Run a scheme on a simulator and its results show up here.", action: "run", actionLabel: "Run Tests" }), count: 0 };
  }

  const q = state.ui.filter.tests.trim().toLowerCase();
  const failedOnly = prefs.failedOnly.tests;
  const recentOnly = prefs.recentOnly.tests;
  const now = Date.now();
  const runsById = new Map(state.runs.map((r) => [r.id, r]));
  const jobOk = (j) => (!failedOnly || j.status === "failed") && (!recentOnly || now - Date.parse(j.createdAt) < DAY);

  const entries = [];
  const seen = new Set();
  for (const job of state.jobs) {
    if (job.runId && runsById.has(job.runId)) {
      if (!seen.has(job.runId)) { seen.add(job.runId); entries.push({ run: runsById.get(job.runId) }); }
    } else entries.push({ job });
  }

  const rows = [];
  const rootOpen = !!q || isExpanded("tests", "root", true);
  rows.push({ nav: "tests", key: "root", level: 0, expandable: true, expanded: rootOpen, icon: icon("folder.fill", "ic-16"), label: "Runs", cls: "root" });
  let shown = 0;
  if (rootOpen) {
    for (const entry of entries) {
      if (entry.run) {
        const run = entry.run;
        const jobs = runJobs(run).filter((j) => jobOk(j) && (jobMatches(j, q) || `${runName(run)}`.toLowerCase().includes(q)));
        if (!jobs.length || (failedOnly && run.status !== "failed" && !jobs.length)) continue;
        const key = `run:${run.id}`;
        const open = !!q || isExpanded("tests", key, false);
        rows.push({
          nav: "tests", key, level: 1, expandable: true, expanded: open,
          sel: { kind: "run", id: run.id }, open: { kind: "run", id: run.id }, ctx: "run",
          icon: statusGlyph(RUN_STATUS[run.status] || RUN_STATUS.queued), label: runName(run),
          secondary: runCountsText(run), title: `${runName(run)}\n${runCountsText(run)}`
        });
        shown += 1;
        if (!open) continue;
        for (const job of jobs) {
          const jkey = `job:${job.id}`;
          const jopen = !!q || isExpanded("tests", jkey, false);
          rows.push(jobRow("tests", job, 2, { expandable: true, expanded: jopen, hoverAction: rerunButton(job) }));
          if (jopen) rows.push(...resultRows(job, 3, q));
        }
      } else {
        const job = entry.job;
        if (!jobOk(job)) continue;
        const caseMatch = q ? (resultsOf(job.id)?.cases || []).some((c) => `${c.name} ${c.className}`.toLowerCase().includes(q)) : false;
        if (!jobMatches(job, q) && !caseMatch) continue;
        const key = `job:${job.id}`;
        const open = !!q && caseMatch ? true : isExpanded("tests", key, false);
        rows.push(jobRow("tests", job, 1, { expandable: true, expanded: open, hoverAction: rerunButton(job) }));
        shown += 1;
        if (open) rows.push(...resultRows(job, 2, q));
      }
    }
  }
  if ((q || failedOnly || recentOnly) && !shown) {
    return { html: emptyState({ iconName: "magnifyingglass", title: "No matching runs", text: failedOnly ? "No failed runs match." : "Nothing matches the filter." }), count: 0 };
  }
  return { html: treeHtml(rows, { nav: "tests", label: "Tests" }), count: shown };
}

// ---------------------------------------------------------------- Issues

const ISSUE_JOB_LIMIT = 40;

export function issuesView() {
  if (!state.loaded.jobs) return { html: skeleton(6), count: 0 };
  if (state.errors.jobs && !state.jobs.length) return { html: errorState("issues", state.errors.jobs), count: 0 };
  const q = state.ui.filter.issues.trim().toLowerCase();
  const failed = state.jobs.filter((j) => j.status === "failed");
  const considered = failed.slice(0, ISSUE_JOB_LIMIT);

  const rows = [];
  let total = 0;
  for (const job of considered) {
    const needsResults = !!job.summary && !job.summary.buildFailed && job.summary.failed > 0;
    const entry = needsResults ? ensureResults(job.id) : null;
    const results = entry ? entry.data || resultsOf(job.id) : undefined;
    const issues = jobIssues(job, results).filter((i) => !q || `${i.title} ${i.message} ${i.file} ${jobTitle(job)}`.toLowerCase().includes(q));
    if (!issues.length && !(entry && entry.status === "loading" && !q)) continue;
    const key = `job:${job.id}`;
    const open = !!q || isExpanded("issues", key, true);
    rows.push({
      nav: "issues", key, level: 0, expandable: true, expanded: open,
      sel: { kind: "job", id: job.id }, open: { kind: "job", id: job.id, tab: "logs" }, ctx: "job",
      icon: statusGlyph(jobStatus(job)), label: jobTitle(job),
      trailing: html`<span class="badge">${issues.length || "…"}</span>`
    });
    if (!open) continue;
    if (!issues.length) rows.push({ nav: "issues", key: `load:${job.id}`, level: 1, disabled: true, dim: true, label: html`<span class="spin spin-sm"></span> Loading issues…` });
    issues.forEach((issue, i) => {
      total += 1;
      const where = issue.file ? `${issue.file}:${issue.line}` : "";
      rows.push({
        nav: "issues", key: `issue:${job.id}:${i}`, level: 1,
        open: { kind: "job", id: job.id, tab: "logs", caseName: issue.caseName, query: issue.kind !== "test" ? issue.message : undefined },
        icon: icon(issue.kind === "test" ? "xmark.circle.fill" : "exclamationmark.triangle.fill", `ic-16 ${issue.kind === "test" ? "c-fail" : "c-warn"}`),
        label: html`<span class="issue-title">${issue.title}</span>`,
        secondary: where,
        title: `${issue.title}\n${issue.message}${where ? `\n${where}` : ""}`
      });
    });
  }
  if (!rows.length) {
    if (q) return { html: emptyState({ iconName: "magnifyingglass", title: "No matching issues", text: `Nothing matches “${state.ui.filter.issues.trim()}”.` }), count: 0 };
    return { html: emptyState({ iconName: "checkmark.diamond.fill", title: "No Issues", text: state.jobs.length ? "No failed tests or build errors in your recent runs." : "Run some tests and any failures are listed here.", pass: true }), count: 0 };
  }
  const note = failed.length > ISSUE_JOB_LIMIT ? html`<div class="stale-note">Showing issues of the ${ISSUE_JOB_LIMIT} most recent failed jobs.</div>` : "";
  return { html: html`${treeHtml(rows, { nav: "issues", label: "Issues" })}${note}`, count: total };
}

// ---------------------------------------------------------------- Reports

export function reportsView() {
  if (!state.loaded.jobs) return { html: skeleton(8), count: 0 };
  if (state.errors.jobs && !state.jobs.length) return { html: errorState("reports", state.errors.jobs), count: 0 };
  if (!state.jobs.length) {
    return { html: emptyState({ iconName: "doc.text", title: "No reports yet", text: "Every finished run leaves a report with its summary, tests and log.", action: "run", actionLabel: "Run Tests" }), count: 0 };
  }
  const q = state.ui.filter.reports.trim().toLowerCase();
  const failedOnly = prefs.failedOnly.reports;
  const recentOnly = prefs.recentOnly.reports;
  const now = Date.now();
  const ok = (j) => (!failedOnly || j.status === "failed") && (!recentOnly || now - Date.parse(j.createdAt) < DAY) && jobMatches(j, q);

  const groups = new Map();
  const progress = state.jobs.filter((j) => isActive(j) && ok(j));
  if (progress.length) groups.set("In Progress", progress);
  const finished = state.jobs.filter((j) => !isActive(j) && ok(j));
  finished.sort((a, b) => (a.finishedAt || a.updatedAt) < (b.finishedAt || b.updatedAt) ? 1 : -1);
  for (const job of finished) {
    const label = fmtDay(job.finishedAt || job.updatedAt);
    if (!groups.has(label)) groups.set(label, []);
    groups.get(label).push(job);
  }
  if (!groups.size) return { html: emptyState({ iconName: "magnifyingglass", title: "No matching reports", text: failedOnly ? "No failed reports match." : "Nothing matches the filter." }), count: 0 };

  const rows = [];
  let count = 0;
  for (const [label, jobs] of groups) {
    const key = `day:${label}`;
    const open = !!q || isExpanded("reports", key, true);
    rows.push({ nav: "reports", key, level: 0, expandable: true, expanded: open, label, cls: "group", secondary: plural(jobs.length, "report") });
    if (!open) continue;
    for (const job of jobs) {
      count += 1;
      const when = job.finishedAt || job.updatedAt;
      const dur = fmtJobDuration(job);
      rows.push({
        nav: "reports", key: `job:${job.id}`, level: 1,
        sel: { kind: "job", id: job.id }, open: { kind: "job", id: job.id }, ctx: "job",
        icon: statusGlyph(jobStatus(job)), label: jobTitle(job),
        trailing: isActive(job) ? (job.status === "running" ? elapsedSpan(job.startedAt) : html`<span class="row-time">${jobStatus(job).label}</span>`) : html`<span class="row-time">${fmtTime(when)}${dur ? ` · ${dur}` : ""}</span>`,
        title: `${jobTitle(job)}\n${jobStatus(job).label}${job.error ? `\n${job.error}` : ""}`
      });
    }
  }
  return { html: treeHtml(rows, { nav: "reports", label: "Reports" }), count };
}

