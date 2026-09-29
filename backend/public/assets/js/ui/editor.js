// The editor area: Welcome, Simulator preview, Report (Summary | Tests | Logs), Run report.

import { html, raw, fmtWhen, fmtSeconds, fmtBytes, fmtDuration } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, setPref, select, invalidate, onRender, deviceById, jobById, runById, isActive } from "../core/state.js";
import {
  CASE_STATUS, JOB_STATUS, RUN_STATUS, jobStatus, jobTitle, jobDestinationName, jobRuntimeName, jobDevice, fmtJobDuration, jobDurationMs, runName, runJobs, runCountsText,
  suitesOf, suiteStatus, parseIssueMessage, currentScheme, shortClass
} from "../core/models.js";
import { ensureResults, ensureArtifacts, resultsOf, loadDoctor } from "../core/sync.js";
import { acquireLog, releaseLog } from "../core/logstore.js";
import {
  hooks, runScheme, rerunJob, cancelJob, cancelRun, exportJUnit, downloadArtifact, bootDevice, runOnDevice, screenshotDevice, openJob, retryFailed, copyLog
} from "../core/actions.js";
import { action } from "./dispatch.js";
import { statusGlyph } from "./nav-common.js";
import { elapsedSpan } from "./ticker.js";
import { previewHtml, syncPreview, refreshShot, zoomStep, zoomSet } from "./preview.js";
import { LogView } from "./logview.js";

// ---------------------------------------------------------------- Welcome

function welcomeHtml() {
  const caps = state.caps;
  const demo = (state.health?.mode || caps?.mode) === "demo";
  const version = state.health?.version || caps?.version || "";
  const recent = state.jobs.slice(0, 8);
  const reason = caps?.modeReason === "env" ? "IOSLAB_SIMULATOR_MOCK is set" : caps ? `this host is ${caps.platform}, not macOS` : "";
  return html`
    <div class="ed ed-welcome" data-key="ed-welcome">
      <div class="welcome">
        <div class="wl-main">
          <div class="wl-mark">${icon("mark", "ic-48")}</div>
          <h1>Welcome to MobileLab</h1>
          <p class="wl-version">${version ? `Version ${version}` : "Connecting…"}</p>
          ${demo ? html`<div class="wl-notice" role="note">${icon("info.circle", "ic-16")}<div><strong>Demo mode.</strong> ${reason ? `${reason[0].toUpperCase()}${reason.slice(1)}, so` : "So"} simulators and test results here are simulated: nothing real runs on this host. Run the backend on a Mac with Xcode to drive real simulators.</div></div>` : ""}
          <ul class="wl-actions">
            <li><button type="button" class="wl-row" data-action="run"><span class="wl-ic">${icon("play.fill", "ic-24")}</span><span class="wl-txt"><strong>Run Tests…</strong><span>Run ${currentScheme() ? html`<em>${currentScheme()}</em>` : "a scheme"} on the selected destination</span></span></button></li>
            <li><button type="button" class="wl-row" data-action="new-simulator"><span class="wl-ic">${icon("iphone", "ic-24")}</span><span class="wl-txt"><strong>Create Simulator…</strong><span>Add a simulator from the runtimes this host offers</span></span></button></li>
            <li><button type="button" class="wl-row" data-action="run-diagnostics"><span class="wl-ic">${icon("stethoscope", "ic-24")}</span><span class="wl-txt"><strong>Run Diagnostics…</strong><span>Check this host and get a remedy for anything that fails</span></span></button></li>
          </ul>
        </div>
        <div class="wl-recent">
          <h2>Recent Runs</h2>
          ${!state.loaded.jobs ? html`<div class="skeleton"><i></i><i></i><i></i><i></i></div>` : ""}
          ${state.loaded.jobs && !recent.length ? html`<p class="wl-empty">No runs yet. Press Run to start one.</p>` : ""}
          <ul>
            ${recent.map((job) => html`<li><button type="button" class="wl-run" data-action="open-job" data-id="${job.id}">
              ${statusGlyph(jobStatus(job), "ic-16")}
              <span class="wl-run-txt"><strong>${jobTitle(job)}</strong><span>${job.status === "running" ? html`Running for ${elapsedSpan(job.startedAt)}` : `${jobStatus(job).label} · ${fmtWhen(job.finishedAt || job.updatedAt)}`}</span></span>
            </button></li>`)}
          </ul>
        </div>
      </div>
    </div>`;
}

// ---------------------------------------------------------------- Report

const TABS = [["summary", "Summary"], ["tests", "Tests"], ["logs", "Logs"]];

function headline(job) {
  const s = job.summary;
  switch (job.status) {
    case "completed": return { label: "Tests Passed", def: JOB_STATUS.completed };
    case "failed":
      if (s?.buildFailed) return { label: "Build Failed", def: JOB_STATUS.failed };
      if (s && s.failed) return { label: "Tests Failed", def: JOB_STATUS.failed };
      return { label: "Failed", def: JOB_STATUS.failed };
    case "cancelled": return { label: "Cancelled", def: JOB_STATUS.cancelled };
    case "running": return { label: "Running", def: JOB_STATUS.running };
    case "retrying": return { label: "Retrying", def: JOB_STATUS.retrying };
    default: return { label: "Queued", def: JOB_STATUS.queued };
  }
}

function meta(label, value) {
  return value === "" || value == null ? "" : html`<div class="meta-row"><dt>${label}</dt><dd>${value}</dd></div>`;
}

function counters(summary) {
  if (!summary) return "";
  const total = Math.max(1, summary.total);
  const seg = (n) => `${Math.round((n / total) * 1000) / 10}%`;
  return html`<div class="counters">
      <div class="counter"><span class="counter-n c-pass">${summary.passed}</span><span class="counter-l">Passed</span></div>
      <div class="counter"><span class="counter-n ${summary.failed ? "c-fail" : ""}">${summary.failed}</span><span class="counter-l">Failed</span></div>
      <div class="counter"><span class="counter-n ${summary.skipped ? "c-warn" : ""}">${summary.skipped}</span><span class="counter-l">Skipped</span></div>
      <div class="counter"><span class="counter-n">${summary.total}</span><span class="counter-l">Total</span></div>
    </div>
    <div class="segbar" role="img" aria-label="${summary.passed} passed, ${summary.failed} failed, ${summary.skipped} skipped">
      <i class="seg seg-pass" data-v="--w:${seg(summary.passed)}"></i><i class="seg seg-fail" data-v="--w:${seg(summary.failed)}"></i><i class="seg seg-skip" data-v="--w:${seg(summary.skipped)}"></i>
    </div>`;
}

function summaryHtml(job) {
  const head = headline(job);
  const active = isActive(job);
  const entry = active ? null : ensureResults(job.id);
  const results = entry ? entry.data || resultsOf(job.id) : undefined;
  const summary = results?.summary || job.summary;
  const failures = (results?.cases || []).filter((c) => c.status === "failed");
  const arts = ensureArtifacts(job.id);
  const dev = jobDevice(job);
  const runtime = jobRuntimeName(job);
  const run = job.runId ? runById(job.runId) : null;
  const duration = jobDurationMs(job);

  return html`<div class="report-scroll" tabindex="-1">
    <div class="summary">
      <header class="sum-head">
        <div class="sum-glyph">${statusGlyph(head.def, "ic-32")}</div>
        <div class="sum-title">
          <h1>${head.label}</h1>
          <p>${job.testTarget} on ${jobDestinationName(job)}${runtime ? ` (${runtime})` : ""}</p>
        </div>
        <div class="sum-actions">
          <button type="button" class="btn" data-action="job-rerun" data-id="${job.id}" ${active ? raw("disabled") : ""}>${icon("play.fill", "ic-12")}Run Again</button>
          ${active ? html`<button type="button" class="btn btn-danger" data-action="job-cancel" data-id="${job.id}">${icon("stop.fill", "ic-12")}Cancel</button>` : ""}
          <button type="button" class="btn" data-action="job-junit" data-id="${job.id}" ${active ? raw("disabled") : ""}>${icon("square.and.arrow.down", "ic-12")}Export JUnit</button>
        </div>
      </header>

      ${job.status === "queued" ? html`<div class="notice"><span class="spin spin-sm"></span> ${job.waitingReason || "Waiting for a simulator"}</div>` : ""}
      ${job.status === "retrying" ? html`<div class="notice"><span class="spin spin-sm c-warn"></span> Attempt ${job.attempts} failed. Retrying (${job.retries} of ${job.maxRetries} retries used).</div>` : ""}
      ${job.error && job.status !== "completed" ? html`<div class="notice notice-fail" role="alert">${icon("exclamationmark.triangle.fill", "ic-16")}<span>${job.error}</span></div>` : ""}

      <dl class="meta">
        ${meta("Scheme", job.testTarget)}
        ${meta("Destination", html`${jobDestinationName(job)}${runtime ? html` <span class="c-secondary">· ${runtime}</span>` : ""}${dev ? "" : html` <span class="c-dim">(device removed)</span>`}`)}
        ${meta("Started", job.startedAt ? fmtWhen(job.startedAt) : active ? "Not started yet" : "Never started")}
        ${meta("Duration", job.status === "running" ? elapsedSpan(job.startedAt) : duration != null ? fmtDuration(duration) : "")}
        ${meta("Attempts", job.attempts ? `${job.attempts} of ${job.maxRetries + 1}` : `0 of ${job.maxRetries + 1}`)}
        ${meta("Run", run ? html`<button type="button" class="link" data-action="open-run" data-id="${run.id}">${runName(run)}</button>` : "")}
        ${meta("Exit code", job.exitCode != null ? html`<code>${job.exitCode}</code>` : "")}
        ${meta("Project", job.projectPath || job.workspacePath || job.workingDirectory ? html`<code>${job.projectPath || job.workspacePath || job.workingDirectory}</code>` : "")}
        ${meta("Job ID", html`<code>${job.id}</code>`)}
      </dl>

      ${counters(summary)}

      ${entry && entry.status === "loading" && !entry.data ? html`<div class="notice"><span class="spin spin-sm"></span> Loading results…</div>` : ""}
      ${entry && entry.status === "error" ? html`<div class="notice notice-fail" role="alert">${entry.error}</div>` : ""}

      ${summary?.buildFailed ? html`<section class="sec"><h2>Build Errors</h2>
        ${(summary.errors?.length ? summary.errors : [job.error || "The build failed."]).map((e) => html`<div class="fail-card"><code>${e}</code><button type="button" class="link" data-action="open-log-query" data-id="${job.id}" data-query="${parseIssueMessage(e).text}">Show in Log</button></div>`)}</section>` : ""}

      ${failures.length ? html`<section class="sec"><h2>Failures <span class="sec-count">${failures.length}</span></h2>
        ${failures.map((c) => {
          const info = parseIssueMessage(c.message);
          return html`<div class="fail-card">
            <div class="fc-head"><span class="c-fail">${icon("xmark.diamond.fill", "ic-14")}</span><strong>${c.name}</strong><span class="c-secondary">${shortClass(c.className)}</span></div>
            <div class="fc-msg"><code>${info.text}</code>${info.file ? html`<span class="fc-where">${info.file}:${info.line}</span>` : ""}</div>
            <button type="button" class="link" data-action="open-log-case" data-id="${job.id}" data-case="${c.name}">Show in Log</button>
          </div>`;
        })}</section>` : ""}

      <section class="sec"><h2>Artifacts${arts?.items?.length ? html` <span class="sec-count">${arts.items.length}</span>` : ""}</h2>
        ${arts?.status === "loading" && !arts.items.length ? html`<div class="notice"><span class="spin spin-sm"></span> Loading artifacts…</div>` : ""}
        ${arts?.status === "error" ? html`<div class="notice notice-fail" role="alert">${arts.error}</div>` : ""}
        ${arts?.status === "ok" && !arts.items.length ? html`<p class="sec-empty">${active ? "Artifacts appear as attempts finish." : "This job left no artifacts."}</p>` : ""}
        <ul class="artifacts">
          ${(arts?.items || []).map((a, i) => html`<li class="artifact">
            <span class="af-ic">${icon(a.isDirectory ? "folder" : a.type === "screenshot" ? "camera" : "doc.text", "ic-16")}</span>
            <span class="af-name"><strong>${a.name}</strong><span>${a.type}${a.attempt ? ` · attempt ${a.attempt}` : ""} · ${fmtBytes(a.sizeBytes)}</span></span>
            <button type="button" class="btn btn-sm" data-action="artifact" data-id="${job.id}" data-i="${i}">${a.isDirectory ? "Copy Path" : "Download"}</button>
          </li>`)}
        </ul>
      </section>
    </div>
  </div>`;
}

function testsHtml(job) {
  const active = isActive(job);
  const entry = active ? null : ensureResults(job.id);
  const results = entry ? entry.data || resultsOf(job.id) : undefined;
  if (active) {
    return html`<div class="report-empty">${statusGlyph(jobStatus(job), "ic-32")}<strong>${job.status === "queued" ? "Waiting to start" : "Running"}</strong><p>Test results appear when the attempt finishes. Watch the log in the meantime.</p><button type="button" class="btn" data-action="open-log" data-id="${job.id}">Show Log</button></div>`;
  }
  if (entry.status === "loading" && !entry.data) return html`<div class="report-empty"><span class="spin c-accent"></span><p>Loading results…</p></div>`;
  if (entry.status === "error") return html`<div class="report-empty"><span class="c-fail">${icon("exclamationmark.triangle", "ic-32")}</span><strong>Could not load the results</strong><p class="err-inline" role="alert">${entry.error}</p></div>`;
  const cases = results?.cases || [];
  if (!cases.length) {
    return html`<div class="report-empty">${icon("diamond", "ic-32")}<strong>No test results</strong><p>${job.summary?.buildFailed ? "The build failed before any test ran." : "This attempt did not record any test cases."}</p><button type="button" class="btn" data-action="open-log" data-id="${job.id}">Show Log</button></div>`;
  }
  const rows = [];
  for (const suite of suitesOf(cases)) {
    rows.push(html`<tr class="t-suite" data-key="s:${suite.className}"><td><span class="tt-name">${statusGlyph(CASE_STATUS[suiteStatus(suite)], "ic-14")}<strong>${suite.name}</strong><span class="c-secondary">${suite.className !== suite.name ? suite.className : ""}</span></span></td><td>${suite.failed ? `${suite.failed} failed` : "Passed"}</td><td class="num">${fmtSeconds(suite.cases.reduce((s, c) => s + c.durationSeconds, 0))}</td></tr>`);
    for (const c of suite.cases) {
      rows.push(html`<tr class="t-case${c.status === "failed" ? " is-fail" : ""}" data-key="c:${suite.className}/${c.name}" data-action="open-log-case" data-id="${job.id}" data-case="${c.name}" tabindex="0" data-native-keys>
        <td><span class="tt-name tt-indent">${statusGlyph(CASE_STATUS[c.status], "ic-14")}<span>${c.name}</span></span>${c.message ? html`<div class="tt-msg"><code>${c.message}</code></div>` : ""}</td>
        <td>${CASE_STATUS[c.status].label}</td><td class="num">${fmtSeconds(c.durationSeconds)}</td></tr>`);
    }
  }
  return html`<div class="report-scroll"><table class="tests-table"><thead><tr><th>Name</th><th>Status</th><th class="num">Duration</th></tr></thead><tbody>${rows}</tbody></table></div>`;
}

function reportHtml(job) {
  const tab = prefs.reportTab;
  const head = headline(job);
  let body;
  if (tab === "tests") body = testsHtml(job);
  else if (tab === "logs") body = html`<div class="log-holder" data-key="log-holder"><div class="log-host" id="log-host" data-morph="static"></div></div>`;
  else body = summaryHtml(job);
  return html`<div class="ed ed-report" data-key="ed-report">
    <div class="report-bar">
      <div class="segmented" role="tablist" aria-label="Report sections">
        ${TABS.map(([id, label]) => html`<button type="button" role="tab" id="rt-${id}" aria-selected="${tab === id ? "true" : "false"}" aria-controls="report-body" tabindex="${tab === id ? 0 : -1}" data-action="report-tab" data-arg="${id}">${label}</button>`)}
      </div>
      <span class="report-title">${statusGlyph(head.def, "ic-14")}<span>${jobTitle(job)}</span></span>
      ${tab === "logs" ? html`<button type="button" class="btn btn-sm" data-action="job-copy-log" data-id="${job.id}">${icon("doc.on.doc", "ic-12")}Copy Log</button>` : ""}
    </div>
    <div class="report-body" id="report-body" role="tabpanel" aria-labelledby="rt-${tab}">${body}</div>
  </div>`;
}

// ---------------------------------------------------------------- Run report

function runHtml(run) {
  const jobs = runJobs(run);
  const def = RUN_STATUS[run.status] || RUN_STATUS.queued;
  const active = run.status === "queued" || run.status === "running";
  const label = { passed: "Run Passed", failed: "Run Failed", cancelled: "Run Cancelled", running: "Run In Progress", queued: "Run Queued" }[run.status] || "Run";
  const totals = jobs.reduce((t, j) => ({ passed: t.passed + (j.summary?.passed || 0), total: t.total + (j.summary?.total || 0), failed: t.failed + (j.summary?.failed || 0) }), { passed: 0, total: 0, failed: 0 });
  return html`<div class="ed ed-run" data-key="ed-run"><div class="report-scroll" tabindex="-1"><div class="summary">
    <header class="sum-head">
      <div class="sum-glyph">${statusGlyph(def, "ic-32")}</div>
      <div class="sum-title"><h1>${label}</h1><p>${runName(run)} · ${run.scheme}</p></div>
      <div class="sum-actions">
        <button type="button" class="btn" data-action="run-again" data-arg="${run.scheme}" ${active ? raw("disabled") : ""}>${icon("play.fill", "ic-12")}Run Again</button>
        ${active ? html`<button type="button" class="btn btn-danger" data-action="run-cancel" data-id="${run.id}">${icon("stop.fill", "ic-12")}Cancel Run</button>` : ""}
      </div>
    </header>
    <dl class="meta">
      ${meta("Scheme", run.scheme)}
      ${meta("Jobs", `${jobs.length} (${runCountsText(run)})`)}
      ${meta("Created", fmtWhen(run.createdAt))}
      ${meta("Max parallel", run.maxParallel ? String(run.maxParallel) : "")}
      ${meta("Tests", totals.total ? `${totals.passed} of ${totals.total} passed${totals.failed ? `, ${totals.failed} failed` : ""}` : "")}
      ${meta("Run ID", html`<code>${run.id}</code>`)}
    </dl>
    <section class="sec"><h2>Devices <span class="sec-count">${jobs.length}</span></h2>
      <table class="tests-table run-table"><thead><tr><th>Device</th><th>Status</th><th>Tests</th><th class="num">Duration</th></tr></thead><tbody>
        ${jobs.map((j) => html`<tr class="t-case" data-key="j:${j.id}" data-action="open-job" data-id="${j.id}" tabindex="0" data-native-keys>
          <td><span class="tt-name">${statusGlyph(jobStatus(j), "ic-14")}<span>${jobDestinationName(j)}</span><span class="c-secondary">${jobRuntimeName(j)}</span></span></td>
          <td>${jobStatus(j).label}</td>
          <td>${j.summary ? `${j.summary.passed} of ${j.summary.total}` : ""}${j.summary?.failed ? html` <span class="c-fail">· ${j.summary.failed} failed</span>` : ""}</td>
          <td class="num">${j.status === "running" ? elapsedSpan(j.startedAt) : fmtJobDuration(j)}</td></tr>`)}
      </tbody></table>
      ${!jobs.length ? html`<p class="sec-empty">The jobs of this run are no longer in the history.</p>` : ""}
    </section>
  </div></div></div>`;
}

// ---------------------------------------------------------------- Device

function deviceHtml(device) {
  return html`<div class="ed ed-device" data-key="ed-device">${previewHtml(device)}</div>`;
}

function missingHtml(what) {
  return html`<div class="ed ed-missing" data-key="ed-missing"><div class="report-empty">${icon("questionmark.circle", "ic-32")}<strong>${what} is not available</strong><p>It may have been deleted or cleaned up.</p><button type="button" class="btn" data-action="open-welcome">Back to Welcome</button></div></div>`;
}

function loadingHtml() {
  return html`<div class="ed ed-loading" data-key="ed-loading"><div class="report-empty"><span class="spin c-accent"></span><p>Loading…</p></div></div>`;
}

// ---------------------------------------------------------------- log view lifecycle

let logView = null;

function syncLogView() {
  const host = document.getElementById("log-host");
  const want = host && state.sel.kind === "job" && prefs.reportTab === "logs" ? state.sel.id : null;
  if (logView && (!host || logView.host !== host || logView.jobId !== want)) {
    logView.view.destroy();
    releaseLog(logView.jobId);
    logView = null;
  }
  if (!logView && host && want) {
    const model = acquireLog(want);
    logView = { view: new LogView(host, model, { jobId: want }), jobId: want, host, model };
  }
  if (logView) {
    const job = jobById(logView.jobId);
    logView.model.observe(job);
    logView.view.setJob(job);
    logView.view.setReveal(state.reveal);
  }
}

// ---------------------------------------------------------------- render

function renderEditor() {
  const host = document.getElementById("editor");
  const sel = state.sel;
  let markup;
  if (sel.kind === "device") {
    const device = deviceById(sel.id);
    markup = device ? deviceHtml(device) : state.loaded.devices ? missingHtml("This simulator") : loadingHtml();
  } else if (sel.kind === "job") {
    const job = jobById(sel.id);
    markup = job ? reportHtml(job) : state.loaded.jobs ? missingHtml("This report") : loadingHtml();
  } else if (sel.kind === "run") {
    const run = runById(sel.id);
    markup = run ? runHtml(run) : state.loaded.runs ? missingHtml("This run") : loadingHtml();
  } else markup = welcomeHtml();
  patch(host, markup);
  syncPreview();
  syncLogView();
}

onRender("editor", renderEditor);

export function initEditor() {
  action("open-welcome", () => select("welcome"));
  action("run-diagnostics", () => { hooks.sheet("settings", { pane: "environment" }); void loadDoctor(); });
  action("open-job", ({ el }) => openJob(el.dataset.id));
  action("open-run", ({ el }) => select("run", el.dataset.id));
  action("open-log", ({ el }) => openJob(el.dataset.id, { tab: "logs" }));
  action("open-log-case", ({ el }) => openJob(el.dataset.id, { tab: "logs", caseName: el.dataset.case }));
  action("open-log-query", ({ el }) => openJob(el.dataset.id, { tab: "logs", query: el.dataset.query }));
  action("report-tab", ({ el }) => {
    setPref("reportTab", el.dataset.arg);
    invalidate("editor", "jump");
  });
  action("job-rerun", ({ el }) => rerunJob(el.dataset.id));
  action("job-cancel", ({ el }) => cancelJob(el.dataset.id));
  action("job-junit", ({ el }) => exportJUnit(el.dataset.id));
  action("job-copy-log", ({ el }) => copyLog(el.dataset.id));
  action("job-retry-failed", ({ el }) => retryFailed(el.dataset.id));
  action("run-again", ({ el }) => runScheme({ scheme: el.dataset.arg }));
  action("run-cancel", ({ el }) => cancelRun(el.dataset.id));
  action("artifact", ({ el }) => {
    const items = state.artifacts.get(el.dataset.id)?.items || [];
    const a = items[Number(el.dataset.i)];
    if (a) void downloadArtifact(a);
  });
  action("device-boot", ({ el }) => bootDevice(el.dataset.id));
  action("device-run", ({ el }) => runOnDevice(el.dataset.id));
  action("device-shot", ({ el }) => screenshotDevice(el.dataset.id));
  action("shot-refresh", ({ el }) => refreshShot(el.dataset.id));
  action("zoom-in", () => zoomStep(1));
  action("zoom-out", () => zoomStep(-1));
  action("zoom-actual", () => zoomSet(1));
  action("zoom-fit", () => zoomSet("fit"));

  // report tab keyboard
  document.addEventListener("keydown", (event) => {
    const tab = event.target.closest?.('.report-bar [role="tab"]');
    if (!tab || event.ctrlKey || event.metaKey || event.altKey) return;
    const list = Array.from(tab.parentElement.querySelectorAll('[role="tab"]'));
    const at = list.indexOf(tab);
    let next = -1;
    if (event.key === "ArrowRight") next = (at + 1) % list.length;
    else if (event.key === "ArrowLeft") next = (at - 1 + list.length) % list.length;
    if (next < 0) return;
    event.preventDefault();
    setPref("reportTab", list[next].dataset.arg);
    invalidate("editor", "jump");
    requestAnimationFrame(() => document.getElementById(`rt-${list[next].dataset.arg}`)?.focus());
  });

  // rows that act like buttons (tests table)
  document.addEventListener("keydown", (event) => {
    if ((event.key !== "Enter" && event.key !== " ") || event.target.tagName !== "TR") return;
    event.preventDefault();
    event.target.click();
  });
}

