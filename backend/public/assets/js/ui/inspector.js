// Inspector (right): Attributes, History, Quick Help for whatever the editor shows; for nothing selected,
// the backend itself. Sections have bold 11px headers with a disclosure triangle and 22px two-column rows.

import { html, raw, fmtWhen, fmtBytes, fmtTimeSec, fmtDuration, debounce } from "../core/util.js";
import { icon } from "../core/icons.js";
import { patch } from "../core/morph.js";
import { state, prefs, setPref, savePrefs, invalidate, onRender, deviceById, jobById, runById, isActive } from "../core/state.js";
import { deviceModel, deviceRuntime, deviceState, jobDestinationName, jobRuntimeName, jobStatus, jobTitle, runName, runCountsText, modelLabel, runtimeLabel, capsuleStatus } from "../core/models.js";
import { loadHistory, onEvent } from "../core/sync.js";
import { copy } from "../core/actions.js";
import { action } from "./dispatch.js";
import { deviceDot } from "./nav-common.js";

const TABS = [
  { id: "attributes", label: "Attributes", icon: "doc.text" },
  { id: "history", label: "History", icon: "clock" },
  { id: "help", label: "Quick Help", icon: "questionmark.circle" }
];

const row = (label, value, opts = {}) =>
  value === "" || value == null ? "" : html`<div class="irow${opts.mono ? " mono" : ""}"><span class="il">${label}</span><span class="iv">${value}</span></div>`;

function section(key, title, rows) {
  const content = html`${rows}`.text.trim();
  if (!content) return "";
  const open = !prefs.collapsedSections[key];
  return html`<section class="isec" data-key="sec-${key}">
    <button type="button" class="isec-head" data-action="isec-toggle" data-arg="${key}" aria-expanded="${open ? "true" : "false"}" aria-controls="isec-${key}"><span class="isec-disc">${icon("chevron.right", "ic-10")}</span>${title}</button>
    <div class="isec-wrap" id="isec-${key}" data-open="${open ? "1" : "0"}" ${open ? "" : raw("inert")}><div class="isec-body">${rows}</div></div>
  </section>`;
}

const copyButton = (text, what) => html`<button type="button" class="mini-btn" data-action="copy" data-text="${text}" data-what="${what}" title="Copy" aria-label="Copy ${what}">${icon("doc.on.doc", "ic-12")}</button>`;
const yesNo = (v) => (v ? "Yes" : "No");

// ---------------------------------------------------------------- Attributes

function deviceAttributes(d) {
  const st = deviceState(d);
  return html`
    ${section("d-identity", "Identity and Type", html`
      ${row("Name", d.name)}${row("Runtime", deviceRuntime(d))}${row("Device Type", deviceModel(d))}
      ${row("Kind", d.type === "vm" ? "Experimental VM (simulated)" : "Simulator")}
      ${row("Backend", d.backend)}${row("Auto-created", d.ephemeral ? "Yes, removed when no queued job needs it" : "No")}`)}
    ${section("d-location", "Location", html`
      ${row("Simulator UDID", d.simulatorUdid ? html`<span class="ival-wrap"><code class="ival">${d.simulatorUdid}</code>${copyButton(d.simulatorUdid, "the UDID")}</span>` : "None yet")}
      ${row("Device ID", html`<span class="ival-wrap"><code class="ival">${d.id}</code>${copyButton(d.id, "the device ID")}</span>`)}`)}
    ${section("d-resources", "Resources", html`
      ${row("Cost Units", String(d.type === "vm" ? 4 : 1))}
      ${row("Runs Tests", yesNo(d.canRunTests))}
      ${row("Current Job", d.currentJobId ? html`<button type="button" class="link" data-action="open-job" data-id="${d.currentJobId}">${jobTitle(jobById(d.currentJobId) || { testTarget: "Job", assignedDeviceName: d.currentJobId.slice(0, 8) })}</button>` : "None")}
      ${d.cpu ? row("CPU", `${d.cpu} cores`) : ""}${d.memory ? row("Memory", `${d.memory} GB`) : ""}${d.disk ? row("Disk", `${d.disk} GB`) : ""}`)}
    ${section("d-status", "Status", html`
      ${row("State", html`<span class="state-line">${deviceDot(d)} ${st.label}</span>`)}
      ${d.lastError ? row("Last Error", html`<span class="c-fail">${d.lastError}</span>`) : row("Last Error", "None")}
      ${row("Created", fmtWhen(d.createdAt))}${row("Updated", fmtWhen(d.updatedAt))}`)}`;
}

function jobAttributes(j) {
  const s = j.summary;
  const run = j.runId ? runById(j.runId) : null;
  return html`
    ${section("j-identity", "Identity", html`
      ${row("Job ID", html`<span class="ival-wrap"><code class="ival">${j.id}</code>${copyButton(j.id, "the job ID")}</span>`)}
      ${row("Scheme", j.testTarget)}
      ${row("Status", html`<span class="state-line">${jobStatus(j).label}</span>`)}
      ${row("Run", run ? html`<button type="button" class="link" data-action="open-run" data-id="${run.id}">${runName(run)}</button>` : "")}`)}
    ${section("j-destination", "Destination", html`
      ${row("Device", jobDestinationName(j))}${row("Runtime", jobRuntimeName(j) || (j.requiredRuntime ? runtimeLabel(j.requiredRuntime) : "Any"))}
      ${row("Device Type", j.requiredModelId ? modelLabel(j.requiredModelId) : "Any")}${row("Auto-provision", yesNo(j.autoProvision))}`)}
    ${section("j-execution", "Execution", html`
      ${row("Attempts", `${j.attempts} of ${j.maxRetries + 1}`)}${row("Retries Used", `${j.retries} of ${j.maxRetries}`)}
      ${row("Exit Code", j.exitCode != null ? String(j.exitCode) : "")}
      ${row("Duration", j.durationMs != null ? fmtDuration(j.durationMs) : "")}
      ${row("Started", j.startedAt ? fmtWhen(j.startedAt) : "")}${row("Finished", j.finishedAt ? fmtWhen(j.finishedAt) : "")}
      ${row("Configuration", j.configuration)}${row("Only Testing", j.onlyTesting?.length ? j.onlyTesting.join(", ") : "")}
      ${row("Project", j.projectPath || j.workspacePath || j.workingDirectory || "", { mono: true })}
      ${j.waitingReason && j.status === "queued" ? row("Waiting", j.waitingReason) : ""}`)}
    ${section("j-result", "Result", s ? html`
      ${row("Passed", String(s.passed))}${row("Failed", s.failed ? html`<span class="c-fail">${s.failed}</span>` : "0")}${row("Skipped", String(s.skipped))}${row("Total", String(s.total))}
      ${row("Build Failed", yesNo(s.buildFailed))}${s.errors?.length ? row("Errors", html`<span class="c-fail">${s.errors.join("; ")}</span>`) : ""}` : (j.error ? row("Error", html`<span class="c-fail">${j.error}</span>`) : row("Result", isActive(j) ? "Not finished yet" : "No result recorded")))}`;
}

function runAttributes(r) {
  return html`
    ${section("r-identity", "Identity", html`${row("Run ID", html`<span class="ival-wrap"><code class="ival">${r.id}</code>${copyButton(r.id, "the run ID")}</span>`)}${row("Name", runName(r))}${row("Scheme", r.scheme)}${row("Status", r.status)}`)}
    ${section("r-jobs", "Jobs", html`${row("Jobs", String(r.jobIds.length))}${row("Breakdown", runCountsText(r))}${row("Max Parallel", r.maxParallel ? String(r.maxParallel) : "Unlimited")}${row("Created", fmtWhen(r.createdAt))}${r.finishedAt ? row("Finished", fmtWhen(r.finishedAt)) : ""}`)}`;
}

function backendAttributes() {
  const c = state.caps;
  const h = state.health;
  const cap = c?.capacity || state.capacity;
  if (!c && !h) return html`<div class="insp-empty">${state.conn === "offline" ? "The backend is not reachable." : "Loading…"}</div>`;
  const demo = (h?.mode || c?.mode) === "demo";
  return html`
    ${section("b-backend", "Backend", html`
      ${row("Version", h?.version || c?.version)}
      ${row("Mode", html`${demo ? html`<span class="tag tag-demo">Demo</span> simulated` : "Live"}`)}
      ${demo ? row("Reason", c?.modeReason === "env" ? "IOSLAB_SIMULATOR_MOCK is set" : c?.modeReason === "auto" ? "This host is not macOS" : "") : ""}
      ${row("Connection", state.conn === "online" ? "Connected" : state.conn === "offline" ? "Disconnected" : "Connecting…")}
      ${row("API", html`<code class="ival">${location.origin}</code>`)}
      ${row("Sign-in", c ? (c.auth?.required ? "API token required" : "Open (no token)") : "")}`)}
    ${section("b-host", "Host", c ? html`
      ${row("Platform", `${c.platform} ${c.architecture}`)}${row("Kernel", c.kernel)}${row("Node", c.node)}
      ${row("Targets", c.supportedTargets?.length ? c.supportedTargets.join(", ") : "None (simulators are not available on this host)")}` : "")}
    ${section("b-capacity", "Capacity", cap ? html`
      ${row("In Use", `${cap.load} of ${cap.maxLoad} units`)}${row("CPU Cores", String(cap.cpuCores))}${row("Memory", `${Math.round(cap.memoryGb)} GB`)}
      ${row("VM Backend", c?.vm ? (c.vm.enabled ? "Enabled (simulated)" : "Disabled") : "")}` : "")}
    ${section("b-paths", "Storage", c ? html`${row("Data", c.dataDir, { mono: true })}${row("Workspace", c.workspaceRoot, { mono: true })}${state.metrics ? row("Artifacts", fmtBytes(state.metrics.artifactBytes)) : ""}` : "")}`;
}

function attributesHtml() {
  const sel = state.sel;
  if (sel.kind === "device") { const d = deviceById(sel.id); if (d) return deviceAttributes(d); }
  if (sel.kind === "job") { const j = jobById(sel.id); if (j) return jobAttributes(j); }
  if (sel.kind === "run") { const r = runById(sel.id); if (r) return runAttributes(r); }
  return backendAttributes();
}

// ---------------------------------------------------------------- History

function historyKey() {
  const sel = state.sel;
  return sel.kind === "device" || sel.kind === "job" || sel.kind === "run" ? `${sel.kind}:${sel.id}` : "all";
}

function eventRow(e) {
  const err = e.type === "error";
  return html`<li class="ev${err ? " ev-err" : ""}" data-key="ev-${e.id}">
    <span class="ev-ic ${err ? "c-fail" : e.type === "finished" ? "c-pass" : "c-secondary"}">${icon(err ? "exclamationmark.triangle.fill" : e.type === "finished" ? "checkmark.circle.fill" : e.type === "started" ? "play" : "circle.fill", "ic-12")}</span>
    <span class="ev-txt"><span class="ev-msg">${e.message}</span><span class="ev-meta">${fmtTimeSec(e.timestamp)} · ${e.source} · ${e.action}</span></span>
  </li>`;
}

function historyHtml() {
  const key = historyKey();
  let items;
  let loading = false;
  let error = "";
  if (key === "all") items = state.events.slice(-100).reverse();
  else {
    const h = state.eventHistory;
    if (!h || h.key !== key) {
      const [kind, id] = key.split(":");
      void loadHistory(kind, id);
      loading = true;
      items = [];
    } else { items = h.items; loading = h.loading && !items.length; error = h.error; }
  }
  if (error) return html`<div class="insp-empty err-inline" role="alert">${error}</div>`;
  if (loading) return html`<div class="insp-empty"><span class="spin spin-sm"></span> Loading history…</div>`;
  if (!items.length) return html`<div class="insp-empty">No events recorded yet${key === "all" ? " since this page connected" : " for this item"}.</div>`;
  return html`<ul class="events" aria-label="Events, newest first">${items.map(eventRow)}</ul>`;
}

// ---------------------------------------------------------------- Quick Help

const JOB_HELP = {
  queued: ["Queued", "The job is waiting for a simulator that matches its runtime and device type. If Auto-provision is on, MobileLab creates one when capacity allows.", "Watch the waiting reason in the toolbar, free up capacity by shutting down a simulator, or cancel the job."],
  running: ["Running", "xcodebuild is executing the tests on the assigned simulator. The log streams live.", "Open the Logs tab to follow along, or press Stop in the toolbar to cancel."],
  retrying: ["Retrying", "The attempt failed and the job is waiting out a short backoff before it tries again.", "Nothing to do. If it keeps failing, check the log of the last attempt."],
  completed: ["Tests Passed", "Every test of the last attempt passed.", "Run again, export JUnit for your CI, or download the artifacts from the Summary tab."],
  failed: ["Failed", "The last attempt did not finish cleanly: tests failed, the build failed, or the run was interrupted.", "Open the Issues navigator for the assertion messages, or the Logs tab for the full output. Retry Failed Tests reruns only what failed."],
  cancelled: ["Cancelled", "The job was stopped before it finished.", "Run it again from the Summary tab or the toolbar."]
};

const DEVICE_HELP = {
  ready: ["Ready", "The simulator is booted and idle.", "Run tests on it, take a screenshot, or shut it down to free capacity."],
  busy: ["Busy", "A job is running on this simulator.", "It cannot be shut down or deleted until the job finishes."],
  booting: ["Booting", "The simulator is starting.", "This normally takes a few seconds. Actions unlock when it is ready."],
  shutting_down: ["Shutting down", "The simulator is stopping.", "Wait a moment, then boot it again or delete it."],
  stopped: ["Shut down", "The simulator exists but is not running.", "Boot it to see its screen and to run tests on it."],
  created: ["Created", "The simulator record exists but was not started yet.", "Boot it from the context menu or the preview."],
  error: ["Error", "MobileLab could not bring the simulator to the state you asked for.", "Read the last error in Attributes. Delete the simulator and create a new one if it persists."]
};

function helpHtml() {
  const sel = state.sel;
  let entry;
  if (sel.kind === "job") { const j = jobById(sel.id); entry = j && JOB_HELP[j.status]; }
  else if (sel.kind === "device") { const d = deviceById(sel.id); entry = d && (d.type === "vm" ? ["Simulated VM", "Experimental and simulated: no VM is started and no test can run on it.", "Use Simulators for real work. This entry exists to exercise the VM API."] : DEVICE_HELP[d.status]); }
  else if (sel.kind === "run") entry = ["Matrix run", "A run creates one job for every runtime and device-type combination you ticked in the destination popup.", "Open a device row to see its report. Cancel Run stops the jobs that have not finished."];
  else {
    const st = capsuleStatus();
    entry = ["MobileLab", "MobileLab runs your test schemes on iOS Simulators and keeps the results. Pick a scheme and destination in the toolbar, then press Run.", `Right now: ${st.label}${st.detail ? `, ${st.detail}` : ""}. Press ? for keyboard shortcuts.`];
  }
  if (!entry) return html`<div class="insp-empty">No help for this item.</div>`;
  return html`<div class="help"><h3>${entry[0]}</h3><p>${entry[1]}</p><h4>What you can do</h4><p>${entry[2]}</p></div>`;
}

// ---------------------------------------------------------------- render

function inspectorHtml() {
  const tab = prefs.inspTab;
  const body = tab === "history" ? historyHtml() : tab === "help" ? helpHtml() : attributesHtml();
  return html`
    <div class="insp-tabs-row"><div class="insp-tabs glass glass-capsule glass-tabs" data-glass-shape="capsule" role="tablist" aria-label="Inspector">
      ${TABS.map((t) => html`<button type="button" class="it${tab === t.id ? " selected" : ""}" role="tab" id="itab-${t.id}" data-action="insp-tab" data-arg="${t.id}" aria-selected="${tab === t.id ? "true" : "false"}" aria-controls="insp-body" tabindex="${tab === t.id ? 0 : -1}" title="${t.label}" aria-label="${t.label}">${icon(t.icon, "ic-16")}</button>`)}
      <button type="button" class="it it-close" data-action="close-sheet" title="Close" aria-label="Close inspector">${icon("xmark", "ic-14")}</button>
    </div></div>
    <div class="insp-body" id="insp-body" role="tabpanel" aria-labelledby="itab-${tab}" data-key="insp-body-${tab}">${body}</div>`;
}

onRender("inspector", () => patch(document.getElementById("inspector"), inspectorHtml()));

export function initInspector() {
  action("insp-tab", ({ el }) => { setPref("inspTab", el.dataset.arg); invalidate("inspector"); });
  action("isec-toggle", ({ el }) => {
    const key = el.dataset.arg;
    prefs.collapsedSections[key] = !prefs.collapsedSections[key];
    savePrefs();
    invalidate("inspector");
  });
  action("copy", ({ el }) => copy(el.dataset.text, `Copied ${el.dataset.what}`));

  const refresh = debounce(() => {
    const key = historyKey();
    if (prefs.inspTab === "history" && key !== "all") { const [kind, id] = key.split(":"); void loadHistory(kind, id); }
  }, 400);
  onEvent((event) => {
    if (prefs.inspTab !== "history") return;
    const sel = state.sel;
    if ((sel.kind === "job" && event.jobId === sel.id) || (sel.kind === "device" && event.deviceId === sel.id) || (sel.kind === "run" && event.runId === sel.id)) refresh();
    else if (sel.kind === "welcome") invalidate("inspector");
  });

  document.addEventListener("keydown", (event) => {
    const tab = event.target.closest?.('.insp-tabs [role="tab"]');
    if (!tab || event.ctrlKey || event.metaKey || event.altKey) return;
    const list = Array.from(tab.parentElement.querySelectorAll('[role="tab"]:not(.it-close)'));
    const at = list.indexOf(tab);
    let next = -1;
    if (event.key === "ArrowRight") next = (at + 1) % list.length;
    else if (event.key === "ArrowLeft") next = (at - 1 + list.length) % list.length;
    if (next < 0) return;
    event.preventDefault();
    setPref("inspTab", list[next].dataset.arg);
    invalidate("inspector");
    requestAnimationFrame(() => document.getElementById(`itab-${list[next].dataset.arg}`)?.focus());
  });
}

