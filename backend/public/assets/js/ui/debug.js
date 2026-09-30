// Debug area (bottom of the editor column): the debug bar, the variables view (test cases grouped by class,
// with P / F / S badges) on the left and the green console on the right. The console shows the live output and
// events of the selected job, or the global activity log when nothing is selected. Windowed like the log editor.

import { html, raw, esc, keyLabel, fmtSeconds, fmtTimeSec, fmtClock } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, setPref, invalidate, onRender, deviceById, jobById, runById, isActive, isExpanded } from "../core/state.js";
import { capsuleStatus, suitesOf, jobStatus, jobTitle, runName, RUN_STATUS, deviceState } from "../core/models.js";
import { resultsOf, ensureResults, loadHistory } from "../core/sync.js";
import { acquireLog, releaseLog } from "../core/logstore.js";
import { cancelJob, rerunJob, retryFailed, screenshotDevice, regions } from "../core/actions.js";
import { action, field } from "./dispatch.js";
import { openMenu } from "./menu.js";
import { treeHtml } from "./tree.js";

const ROW = 17;
const OVERSCAN = 20;

const VAR_MODES = [["auto", "Auto"], ["all", "All"], ["failures", "Failures"], ["running", "Running"]];
const CONSOLE_MODES = [["all", "All Output"], ["errors", "Errors"], ["build", "Build"], ["tests", "Tests"]];

// ---------------------------------------------------------------- variables view

function badge(status) {
  const map = { passed: ["P", "b-pass", "Passed"], failed: ["F", "b-fail", "Failed"], skipped: ["S", "b-skip", "Skipped"], running: ["R", "b-run", "Running"] };
  const [letter, cls, label] = map[status] || ["?", "b-skip", status];
  return html`<span class="vbadge ${cls}" role="img" aria-label="${label}">${letter}</span>`;
}

function varRow(key, level, iconHtml, name, value, extra = {}) {
  return { nav: "vars", key, level, cls: "var", icon: iconHtml, label: html`<span class="var-name">${name}</span>${value ? html` <span class="var-val">${value}</span>` : ""}`, ...extra };
}

function variablesView() {
  const sel = state.sel;
  const mode = prefs.varsMode;
  const q = state.ui.varsFilter.trim().toLowerCase();
  const rows = [];

  if (sel.kind === "job") {
    const job = jobById(sel.id);
    if (!job) return { html: html`<div class="pane-empty">This report is not available.</div>`, count: 0 };
    if (isActive(job)) {
      if (mode === "failures") return { html: html`<div class="pane-empty">No failures yet. The job is ${jobStatus(job).label.toLowerCase()}.</div>`, count: 0 };
      rows.push(varRow("state", 0, badge("running"), job.testTarget, `= ${jobStatus(job).label.toLowerCase()}: ${job.status === "queued" ? job.waitingReason || "waiting for a simulator" : "results appear when the attempt finishes"}`));
      return { html: treeHtml(rows, { nav: "vars", label: "Variables" }), count: 1 };
    }
    const entry = ensureResults(job.id);
    const data = entry.data || resultsOf(job.id);
    if (entry.status === "loading" && !data) return { html: html`<div class="pane-empty"><span class="spin spin-sm"></span> Loading results…</div>`, count: 0 };
    if (entry.status === "error") return { html: html`<div class="pane-empty err-inline" role="alert">${entry.error}</div>`, count: 0 };
    const cases = data?.cases || [];
    if (!cases.length) return { html: html`<div class="pane-empty">${job.summary?.buildFailed ? "The build failed before any test ran." : "No test cases were recorded."}</div>`, count: 0 };
    const onlyFail = mode === "failures";
    for (const suite of suitesOf(cases)) {
      let list = suite.cases;
      if (onlyFail) list = list.filter((c) => c.status === "failed");
      if (mode === "running") list = [];
      if (q) list = list.filter((c) => `${c.name} ${suite.name}`.toLowerCase().includes(q));
      if (!list.length) continue;
      const key = `suite:${suite.className}`;
      const open = q ? true : isExpanded("vars", key, suite.failed > 0 || suitesOf(cases).length <= 3);
      const status = suite.failed ? "failed" : suite.passed ? "passed" : "skipped";
      rows.push(varRow(key, 0, badge(status), suite.name, `= ${suite.cases.length} tests: ${suite.passed} passed${suite.failed ? `, ${suite.failed} failed` : ""}${suite.skipped ? `, ${suite.skipped} skipped` : ""}`, { expandable: true, expanded: open, open: { kind: "job", id: job.id, tab: "tests" } }));
      if (!open) continue;
      for (const c of list) {
        rows.push(varRow(`case:${suite.className}/${c.name}`, 1, badge(c.status), c.name, `= ${c.status}: ${fmtSeconds(c.durationSeconds)}${c.message ? ` — ${c.message.split("\n")[0]}` : ""}`, { open: { kind: "job", id: job.id, tab: "logs", caseName: c.name }, title: c.message || c.name }));
      }
    }
    if (!rows.length) return { html: html`<div class="pane-empty">${mode === "failures" ? "No failed tests." : mode === "running" ? "Nothing is running in this job." : q ? "No matching variables." : "No test cases."}</div>`, count: 0 };
    return { html: treeHtml(rows, { nav: "vars", label: "Variables" }), count: rows.length };
  }

  if (sel.kind === "device") {
    const d = deviceById(sel.id);
    if (!d) return { html: html`<div class="pane-empty">This simulator is not available.</div>`, count: 0 };
    const st = deviceState(d);
    const props = [["name", `"${d.name}": String`], ["status", `${d.status}: DeviceStatus (${st.label})`], ["runtime", `"${d.runtimeName || d.runtime}": String`], ["modelId", d.modelId ? `"${d.modelId}": String` : "nil: String?"], ["backend", `${d.backend}: DeviceBackend`], ["canRunTests", `${d.canRunTests}: Bool`], ["ephemeral", `${d.ephemeral}: Bool`], ["currentJobId", d.currentJobId ? `"${d.currentJobId}": String` : "nil: String?"], ["lastError", d.lastError ? `"${d.lastError}": String` : "nil: String?"]];
    props.forEach(([n, v]) => { if (!q || `${n} ${v}`.toLowerCase().includes(q)) rows.push(varRow(`p:${n}`, 0, "", n, `= ${v}`)); });
    rows.unshift(varRow("self", 0, html`<span class="vbadge b-acc">D</span>`, "self", d.name));
    return { html: treeHtml(rows, { nav: "vars", label: "Variables" }), count: rows.length };
  }

  // nothing selected or a run: the newest jobs as variables
  const scope = sel.kind === "run" ? (runById(sel.id)?.jobIds || []) : null;
  let jobs = scope ? state.jobs.filter((j) => scope.includes(j.id)) : state.jobs.slice(0, 40);
  if (mode === "failures") jobs = jobs.filter((j) => j.status === "failed");
  if (mode === "running") jobs = jobs.filter(isActive);
  if (q) jobs = jobs.filter((j) => jobTitle(j).toLowerCase().includes(q));
  if (!jobs.length) return { html: html`<div class="pane-empty">${!state.loaded.jobs ? "Loading…" : mode === "failures" ? "No failed jobs." : mode === "running" ? "Nothing is running." : "No jobs yet. Press Run to start one."}</div>`, count: 0 };
  for (const j of jobs) {
    const s = j.summary;
    const st = j.status === "completed" ? "passed" : j.status === "failed" ? "failed" : isActive(j) ? "running" : "skipped";
    rows.push(varRow(`job:${j.id}`, 0, badge(st), jobTitle(j), `= ${jobStatus(j).label.toLowerCase()}${s ? ` (${s.passed} of ${s.total} tests passed)` : ""}`, { open: { kind: "job", id: j.id }, ctx: "job" }));
  }
  return { html: treeHtml(rows, { nav: "vars", label: "Variables" }), count: rows.length };
}

// ---------------------------------------------------------------- console model

const cleared = new Map(); // key -> number of hidden model lines / last hidden event id
let held = null; // { jobId, model, unsubscribe }

function syncModel(jobId) {
  if (held && held.jobId === jobId) return held.model;
  if (held) { held.unsubscribe(); releaseLog(held.jobId); held = null; }
  if (!jobId) return null;
  const model = acquireLog(jobId);
  held = { jobId, model, unsubscribe: model.subscribe((kind) => consoleView?.onModel(kind)) };
  return model;
}

const ERR = /error:|\bfailed\b|\bFAILED\b|\bfatal\b|\*\* (?:TEST|BUILD) FAILED/;
const OK = /\bpassed\b|\bSUCCEEDED\b/;

function lineClass(text) {
  if (ERR.test(text)) return "c-err";
  if (OK.test(text)) return "c-ok";
  return "";
}

function eventLine(e) {
  const cls = e.type === "error" ? "c-err" : e.type === "finished" ? "c-ok" : "c-dim";
  return { text: `${fmtTimeSec(e.timestamp)}  ${e.source.padEnd(12)} ${e.message}`, cls };
}

function consoleKey() {
  const sel = state.sel;
  return sel.kind === "job" ? `job:${sel.id}` : sel.kind === "run" ? `run:${sel.id}` : sel.kind === "device" ? `device:${sel.id}` : "all";
}

/** The provider is { count, get(i) } over "activity events, then output lines", filtered by mode and text. */
function makeProvider() {
  const sel = state.sel;
  const mode = prefs.consoleMode;
  const q = state.ui.consoleFilter.trim().toLowerCase();
  const key = consoleKey();
  const hide = cleared.get(key) || 0;
  const match = (text) => !q || text.toLowerCase().includes(q);

  let events = [];
  let model = null;
  if (sel.kind === "job") {
    const h = state.eventHistory;
    if (!h || h.key !== `job:${sel.id}`) void loadHistory("job", sel.id);
    events = (h && h.key === `job:${sel.id}` ? h.items.slice().reverse() : []);
    model = syncModel(sel.id);
  } else {
    syncModel(null);
    events = state.events.slice();
    if (sel.kind === "run") events = events.filter((e) => e.runId === sel.id);
    if (sel.kind === "device") events = events.filter((e) => e.deviceId === sel.id);
  }
  const lastId = sel.kind === "job" ? 0 : hide;
  if (lastId) events = events.filter((e) => e.id > lastId);

  const eventOk = (e) => {
    if (mode === "errors") return e.type === "error";
    if (mode === "build") return e.source === "xcodebuild" || /build/i.test(e.action);
    if (mode === "tests") return !!(e.jobId || e.runId);
    return true;
  };
  const head = events.filter((e) => eventOk(e)).map(eventLine).filter((l) => match(l.text));

  let idx = null;
  let lines = [];
  if (model) {
    lines = model.lines;
    const firstSuite = mode === "build" || mode === "tests" ? lines.findIndex((l) => /^Test Suite |^Test Case /.test(l)) : -1;
    const from = Math.min(hide, lines.length);
    if (mode === "all" && !q) idx = null;
    else {
      idx = [];
      for (let i = from; i < lines.length; i += 1) {
        const l = lines[i];
        if (mode === "errors" && !ERR.test(l)) continue;
        if (mode === "build" && firstSuite !== -1 && i >= firstSuite) continue;
        if (mode === "tests" && firstSuite !== -1 && i < firstSuite) continue;
        if (q && !l.toLowerCase().includes(q)) continue;
        idx.push(i);
      }
    }
    if (mode === "all" && !q) idx = from ? Array.from({ length: lines.length - from }, (_, k) => from + k) : null;
  }
  const outCount = model ? (idx ? idx.length : lines.length) : 0;
  return {
    key: `${key}|${mode}|${q}|${hide}|${model ? model.version : 0}|${head.length}|${state.events.length}`,
    count: head.length + outCount,
    headCount: head.length,
    model,
    get(i) {
      if (i < head.length) return head[i];
      const at = i - head.length;
      const text = model.lines[idx ? idx[at] : at];
      return { text, cls: lineClass(text) };
    }
  };
}

// ---------------------------------------------------------------- console view (windowed)

class ConsoleView {
  constructor(host) {
    this.host = host;
    this.follow = true;
    this.raf = 0;
    this.lastKey = "";
    this.provider = { count: 0, get: () => ({ text: "", cls: "" }), key: "" };
    host.innerHTML = `<div class="cv-scroll" tabindex="0" role="log" aria-label="Console" aria-live="off"><div class="cv-sizer"><div class="cv-rows"></div></div></div><div class="cv-empty" hidden></div>`;
    this.scroll = host.querySelector(".cv-scroll");
    this.sizer = host.querySelector(".cv-sizer");
    this.rows = host.querySelector(".cv-rows");
    this.empty = host.querySelector(".cv-empty");
    this.scroll.addEventListener("scroll", () => {
      if (!this.programmatic) this.follow = this.scroll.scrollHeight - this.scroll.scrollTop - this.scroll.clientHeight < ROW * 1.5;
      this.schedule();
    }, { passive: true });
    this.ro = typeof ResizeObserver === "function" ? new ResizeObserver(() => this.schedule(true)) : null;
    this.ro?.observe(this.scroll);
  }

  onModel() { this.refresh(); }

  refresh() {
    const p = makeProvider();
    const changed = p.key !== this.provider.key;
    this.provider = p;
    if (!changed) return;
    this.sizer.style.height = `${p.count * ROW + 8}px`;
    let max = 0;
    if (p.model) max = Math.min(600, p.model.maxLen);
    this.sizer.style.width = `${Math.max(200, Math.ceil(max * 7.2) + 40)}px`;
    this.emptyState(p);
    if (this.follow) this.toEnd();
    this.schedule(true);
  }

  emptyState(p) {
    let text = "";
    const sel = state.sel;
    if (!p.count) {
      if (sel.kind === "job" && p.model && p.model.loading) text = "Loading output…";
      else if (sel.kind === "job") text = prefs.consoleMode !== "all" || state.ui.consoleFilter ? "Nothing matches the filter." : "No output yet.";
      else text = state.ui.consoleFilter ? "Nothing matches the filter." : "No activity yet. Events appear here as MobileLab works.";
    }
    this.empty.textContent = text;
    this.empty.hidden = !text;
  }

  toEnd() {
    this.programmatic = true;
    this.scroll.scrollTop = this.scroll.scrollHeight;
    requestAnimationFrame(() => { this.programmatic = false; });
  }

  schedule(force = false) {
    if (force) this.lastKey = "";
    if (this.raf) return;
    this.raf = requestAnimationFrame(() => { this.raf = 0; this.paint(); });
  }

  paint() {
    const p = this.provider;
    const top = this.scroll.scrollTop;
    const height = this.scroll.clientHeight || 200;
    const first = Math.max(0, Math.floor(top / ROW) - OVERSCAN);
    const last = Math.min(p.count, Math.ceil((top + height) / ROW) + OVERSCAN);
    const key = `${first}:${last}:${p.key}`;
    if (key === this.lastKey) return;
    this.lastKey = key;
    let out = "";
    for (let i = first; i < last; i += 1) {
      const { text, cls } = p.get(i);
      out += `<div class="cl${cls ? " " + cls : ""}">${esc(text.length > 3000 ? text.slice(0, 3000) + "…" : text) || "&nbsp;"}</div>`;
    }
    this.rows.style.transform = `translateY(${first * ROW}px)`;
    this.rows.innerHTML = out;
  }

  clear() {
    const key = consoleKey();
    if (state.sel.kind === "job") cleared.set(key, this.provider.model ? this.provider.model.lines.length : 0);
    else cleared.set(key, state.events.length ? state.events[state.events.length - 1].id : 0);
    this.follow = true;
    this.refresh();
    invalidate("debug");
  }
}

let consoleView = null;

// ---------------------------------------------------------------- bar and footers

function runningTest(model) {
  if (!model) return "";
  for (let i = model.lines.length - 1; i >= Math.max(0, model.lines.length - 200); i -= 1) {
    const m = /^Test [Cc]ase '(.+)' started/.exec(model.lines[i]);
    if (m) return m[1].replace(/^-\[\S+\.(\S+) (\S+)\]$/, "$1.$2");
    if (/^Test [Cc]ase '.+' (passed|failed)/.test(model.lines[i])) break;
  }
  return "";
}

function barCrumbs() {
  const sel = state.sel;
  const mark = icon("mark", "ic-14");
  if (sel.kind === "job") {
    const job = jobById(sel.id);
    if (!job) return [{ icon: mark, label: "MobileLab" }];
    const def = jobStatus(job);
    const model = held?.jobId === job.id ? held.model : null;
    const running = job.status === "running" ? runningTest(model) : "";
    const last = running || (job.status === "running" ? "Running" : { completed: "Tests Passed", failed: job.summary?.buildFailed ? "Build Failed" : "Tests Failed", cancelled: "Cancelled", queued: "Queued", retrying: "Retrying" }[job.status]);
    return [
      { icon: mark, label: "MobileLab" },
      { icon: icon("iphone", "ic-14"), label: job.testTarget },
      { icon: def.spinner ? html`<span class="spin spin-sm ${def.cls}"></span>` : icon(def.icon, `ic-14 ${def.cls}`), label: last || def.label }
    ];
  }
  if (sel.kind === "device") {
    const d = deviceById(sel.id);
    if (!d) return [{ icon: mark, label: "MobileLab" }];
    return [{ icon: mark, label: "MobileLab" }, { icon: icon("iphone", "ic-14"), label: d.name }, { icon: "", label: deviceState(d).label }];
  }
  if (sel.kind === "run") {
    const r = runById(sel.id);
    if (!r) return [{ icon: mark, label: "MobileLab" }];
    const def = RUN_STATUS[r.status] || RUN_STATUS.queued;
    return [{ icon: mark, label: "MobileLab" }, { icon: icon("diamond", "ic-14"), label: runName(r) }, { icon: def.spinner ? html`<span class="spin spin-sm ${def.cls}"></span>` : icon(def.icon, `ic-14 ${def.cls}`), label: def.label }];
  }
  const online = state.conn === "online";
  return [{ icon: mark, label: "MobileLab" }, { icon: icon("bolt.horizontal", "ic-14"), label: "Backend" }, { icon: "", label: online ? "Connected" : state.conn === "offline" ? "Disconnected" : "Connecting…" }];
}

function collapsedBar() {
  const st = capsuleStatus();
  const label = "Show debug area";
  const keys = keyLabel("mod+shift+y");
  return html`<div class="debug-bar debug-bar-collapsed" role="toolbar" aria-label="Debug bar">
    <button type="button" class="db-btn db-hide" data-action="toggle-debug" aria-expanded="false" aria-controls="debug-panes" title="Show Debug Area (${keys})" aria-label="${label}">${icon("breakpoint.fill", "ic-18")}</button>
    <span class="db-status"><strong>${st.label}</strong>${st.detail ? html` <span class="db-status-detail">| ${st.detail}</span>` : ""}</span>
    <span class="db-right">
      <button type="button" class="db-btn db-panel" data-action="toggle-debug" aria-expanded="false" aria-controls="debug-panes" title="Show Debug Area (${keys})" aria-label="${label}">${icon("sidebar.bottom", "ic-16")}</button>
    </span>
  </div>`;
}

function debugBar() {
  if (!prefs.debugOpen && !state.ui.narrow) return collapsedBar();
  const sel = state.sel;
  const job = sel.kind === "job" ? jobById(sel.id) : null;
  const dev = sel.kind === "device" ? deviceById(sel.id) : job?.assignedDeviceId ? deviceById(job.assignedDeviceId) : null;
  const crumbs = barCrumbs();
  const failed = !!job && job.status === "failed" && !!job.summary && job.summary.failed > 0;
  const up = !!dev && (dev.status === "ready" || dev.status === "busy") && dev.type === "simulator";
  return html`<div class="debug-bar" role="toolbar" aria-label="Debug bar">
    <button type="button" class="db-btn db-hide" data-action="toggle-debug" aria-expanded="true" aria-controls="debug-panes" title="Hide Debug Area (${keyLabel("mod+shift+y")})" aria-label="Hide debug area">${icon("breakpoint.fill", "ic-18")}</button>
    <span class="db-sep"></span>
    <button type="button" class="db-btn" data-action="db-rerun" ${job && !isActive(job) ? "" : raw("disabled")} title="Run Again" aria-label="Run again">${icon("play", "ic-16")}</button>
    <button type="button" class="db-btn" data-action="db-stop" ${job && isActive(job) ? "" : raw("disabled")} title="Cancel this job" aria-label="Cancel job">${icon("stop.fill", "ic-16")}</button>
    <button type="button" class="db-btn" data-action="db-retry" ${failed ? "" : raw("disabled")} title="Retry Failed Tests" aria-label="Retry failed tests">${icon("arrow.clockwise", "ic-16")}</button>
    <button type="button" class="db-btn" data-action="db-shot" ${up ? "" : raw("disabled")} title="Save a Screenshot" aria-label="Save screenshot">${icon("camera", "ic-16")}</button>
    <span class="db-sep"></span>
    <nav class="db-crumbs" aria-label="Debug path">${crumbs.map((c, i) => html`${i ? html`<span class="crumb-sep">${icon("chevron.right", "ic-10")}</span>` : ""}<span class="db-crumb">${c.icon}<span>${c.label}</span></span>`)}</nav>
    <span class="db-right">
      ${job ? html`<span class="db-attempt">Attempt ${Math.max(job.attempts, 1)} of ${job.maxRetries + 1}</span>` : ""}
      <button type="button" class="db-btn db-panel" data-action="toggle-debug" aria-expanded="true" aria-controls="debug-panes" title="Hide Debug Area (${keyLabel("mod+shift+y")})" aria-label="Hide debug area">${icon("sidebar.bottom", "ic-16")}</button>
      <button type="button" class="db-btn db-close" data-action="close-sheet" title="Close" aria-label="Close debug area">${icon("xmark", "ic-14")}</button>
    </span>
  </div>`;
}

function popupLabel(list, value) { return (list.find(([id]) => id === value) || list[0])[1]; }

function varsFoot() {
  return html`<div class="pane-foot vars-foot">
    <button type="button" class="popup popup-flat" data-action="vars-mode" aria-haspopup="menu"><span>${popupLabel(VAR_MODES, prefs.varsMode)}</span>${icon("chevron.up.chevron.down", "ic-10")}</button>
    <span class="foot-grow"></span>
    <label class="foot-field"><span class="nf-icon">${icon("line.3.horizontal.decrease.circle", "ic-12")}</span><input type="text" class="foot-input" data-key="vars-filter" data-scope="varsfilter" data-field="q" value="${state.ui.varsFilter}" placeholder="Filter" aria-label="Filter variables" autocomplete="off" spellcheck="false"></label>
  </div>`;
}

function consoleStatusLine() {
  const sel = state.sel;
  if (sel.kind === "job") {
    const job = jobById(sel.id);
    const model = held?.model;
    if (!job) return "";
    const lines = model ? model.lines.length : 0;
    if (job.status === "running") return html`<span class="prompt">(mobilelab)</span> running for <span class="tick" data-elapsed="${job.startedAt || ""}">${job.startedAt ? fmtClock(Date.now() - Date.parse(job.startedAt)) : "00:00"}</span> · ${lines} lines`;
    return html`<span class="prompt">(mobilelab)</span> ${jobStatus(job).label.toLowerCase()} · ${lines} lines${model?.sizeBytes ? ` · ${(model.sizeBytes / 1024).toFixed(1)} KB` : ""}`;
  }
  return html`<span class="prompt">(mobilelab)</span> ${state.conn === "online" ? "listening for events" : "disconnected"} · ${state.events.length} events`;
}

function consoleFoot() {
  return html`<div class="pane-foot console-foot">
    <button type="button" class="popup popup-flat" data-action="console-mode" aria-haspopup="menu"><span>${popupLabel(CONSOLE_MODES, prefs.consoleMode)}</span>${icon("chevron.up.chevron.down", "ic-10")}</button>
    <span class="foot-grow"></span>
    <label class="foot-field"><span class="nf-icon">${icon("line.3.horizontal.decrease.circle", "ic-12")}</span><input type="text" class="foot-input" data-key="console-filter" data-scope="consolefilter" data-field="q" value="${state.ui.consoleFilter}" placeholder="Filter" aria-label="Filter console" autocomplete="off" spellcheck="false"></label>
    <button type="button" class="db-btn" data-action="console-clear" title="Clear Console (Ctrl+K)" aria-label="Clear console">${icon("trash", "ic-14")}</button>
    <span class="db-sep"></span>
    <button type="button" class="db-btn" data-action="debug-layout" data-arg="vars" aria-pressed="${prefs.debugLayout === "vars" ? "true" : "false"}" title="Variables Only" aria-label="Show variables only">${icon("sidebar.left", "ic-14")}</button>
    <button type="button" class="db-btn" data-action="debug-layout" data-arg="console" aria-pressed="${prefs.debugLayout === "console" ? "true" : "false"}" title="Console Only" aria-label="Show console only">${icon("sidebar.right", "ic-14")}</button>
  </div>`;
}

// ---------------------------------------------------------------- render

let built = false;

function build(root) {
  root.innerHTML = `
    <div id="debug-bar"></div>
    <div class="debug-panes" id="debug-panes">
      <section class="vars" aria-label="Variables"><div class="vars-body" id="vars-body"></div><div id="vars-foot"></div></section>
      <section class="console" aria-label="Console"><div class="console-body" id="console-body"></div><div class="console-status" id="console-status"></div><div id="console-foot"></div></section>
    </div>`;
  consoleView = new ConsoleView(root.querySelector("#console-body"));
  built = true;
}

function renderDebug() {
  const root = document.getElementById("debug");
  if (!built) build(root);
  const panes = document.getElementById("debug-panes");
  panes.dataset.layout = prefs.debugLayout;
  panes.toggleAttribute("inert", !prefs.debugOpen && !state.ui.narrow);
  patch(document.getElementById("debug-bar"), debugBar());
  const vars = variablesView();
  patch(document.getElementById("vars-body"), vars.html);
  patch(document.getElementById("vars-foot"), varsFoot());
  patch(document.getElementById("console-foot"), consoleFoot());
  patch(document.getElementById("console-status"), consoleStatusLine());
  consoleView.refresh();
  if (state.sel.kind !== "job" && held) { held.unsubscribe(); releaseLog(held.jobId); held = null; }
}

onRender("debug", renderDebug);

export function initDebug() {
  action("db-rerun", () => { if (state.sel.kind === "job") void rerunJob(state.sel.id); });
  action("db-stop", () => { if (state.sel.kind === "job") void cancelJob(state.sel.id); });
  action("db-retry", () => { if (state.sel.kind === "job") void retryFailed(state.sel.id); });
  action("db-shot", () => {
    const job = state.sel.kind === "job" ? jobById(state.sel.id) : null;
    const id = state.sel.kind === "device" ? state.sel.id : job?.assignedDeviceId;
    if (id) void screenshotDevice(id);
  });
  action("vars-mode", ({ el }) => openMenu({
    anchor: el, label: "Variables",
    items: VAR_MODES.map(([id, label]) => ({ label, checked: prefs.varsMode === id, run: () => { setPref("varsMode", id); invalidate("debug"); } }))
  }));
  action("console-mode", ({ el }) => openMenu({
    anchor: el, label: "Console",
    items: CONSOLE_MODES.map(([id, label]) => ({ label, checked: prefs.consoleMode === id, run: () => { setPref("consoleMode", id); invalidate("debug"); } }))
  }));
  action("console-clear", () => consoleView?.clear());
  action("debug-layout", ({ el }) => {
    const target = el.dataset.arg;
    setPref("debugLayout", prefs.debugLayout === target ? "both" : target);
    invalidate("debug");
  });
  field("varsfilter", ({ value }) => { state.ui.varsFilter = value; invalidate("debug"); });
  field("consolefilter", ({ value }) => { state.ui.consoleFilter = value; invalidate("debug"); });
  regions.set("clear-console", () => consoleView?.clear());
}
