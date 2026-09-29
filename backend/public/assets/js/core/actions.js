// Everything the user can do, as functions: run, stop, boot, delete... Each call reports the server's own message when
// it fails (toast, plus an inline error where there is a place for one). The command registry at the bottom feeds
// the keyboard handler, the app menu, Open Quickly and the shortcuts sheet, so they can never disagree.

import { api, ApiError, download } from "./api.js";
import { notify, notifyError } from "./notify.js";
import { state, prefs, setPref, select, invalidate, jobById, deviceById, isActive, historyGo, canGoBack, canGoForward } from "./state.js";
import { activeJobs, currentScheme, runPlan, schemeConfig } from "./models.js";
import { requestRefresh, loadDevices, loadJobs, loadRuns, ensureCatalog, loadDoctor, resultsOf, ensureResults } from "./sync.js";
import { copyText, plural } from "./util.js";

/** UI modules register how to open sheets / focus regions, so this file stays free of DOM code. */
export const regions = new Map();
export const hooks = {
  sheet: (_name, _props) => {},
  focusRegion: (name) => regions.get(name)?.()
};

function fail(error, prefix) {
  if (error instanceof ApiError && error.status === 401) return; // the auth sheet is already open
  notifyError(error, prefix);
}

export function setRunError(message) {
  state.ui.runError = { message, at: Date.now() };
  invalidate("toolbar");
  setTimeout(() => invalidate("toolbar"), 8200);
}

// ---------------------------------------------------------------- schemes

export function jobFieldsFor(config) {
  const fields = {};
  if (config.projectPath) fields.projectPath = config.projectPath;
  if (config.workspacePath) fields.workspacePath = config.workspacePath;
  if (config.workingDirectory) fields.workingDirectory = config.workingDirectory;
  if (config.configuration) fields.configuration = config.configuration;
  if (config.onlyTesting && config.onlyTesting.length) fields.onlyTesting = config.onlyTesting;
  if (Number.isInteger(config.maxRetries)) fields.maxRetries = config.maxRetries;
  fields.autoProvision = config.autoProvision !== false;
  return fields;
}

export function saveScheme(config, { makeCurrent = true } = {}) {
  const list = prefs.schemes.filter((s) => s.name !== (config.originalName || config.name));
  const { originalName: _o, ...clean } = config;
  list.unshift(clean);
  setPref("schemes", list);
  if (makeCurrent) setPref("scheme", clean.name);
  invalidate("toolbar");
}

export function deleteScheme(name) {
  setPref("schemes", prefs.schemes.filter((s) => s.name !== name));
  if (prefs.scheme === name) setPref("scheme", "");
  invalidate("toolbar");
}

export function chooseScheme(name) {
  setPref("scheme", name);
  invalidate("toolbar");
}

export function setDestinations(keys) {
  setPref("destinations", keys);
  invalidate("toolbar");
}

// ---------------------------------------------------------------- run / stop

export async function runScheme({ scheme, plan, overrides } = {}) {
  const name = scheme || currentScheme();
  if (!name) {
    hooks.sheet("scheme", { runAfterSave: true, isNew: true });
    return null;
  }
  const config = { ...schemeConfig(name), ...(overrides || {}) };
  const target = plan || runPlan();
  const fields = jobFieldsFor(config);
  try {
    if (target.matrix) {
      const body = { scheme: name, runtimes: target.runtimes, models: target.models, ...fields, autoProvision: true };
      if (Number.isInteger(config.maxParallel)) body.maxParallel = config.maxParallel;
      const res = await api.post("/runs", body);
      await Promise.allSettled([loadJobs(), loadRuns()]);
      select("run", res.run.id);
      notify(`Started ${name}: ${plural(res.jobs.length, "job")} in a matrix run.`, { kind: "success" });
      return res;
    }
    const body = { testTarget: name, ...fields };
    if (target.runtimes.length === 1) body.requiredRuntime = target.runtimes[0];
    if (target.models.length === 1) body.requiredModelId = target.models[0];
    if (target.infos.some((i) => i.onDemand)) body.autoProvision = true;
    const res = await api.post("/tests/run", body);
    await Promise.allSettled([loadJobs(), loadDevices()]);
    setPref("reportTab", "logs");
    select("job", res.job.id);
    return res;
  } catch (error) {
    setRunError(error.message);
    fail(error, `Could not run ${name}`);
    return null;
  }
}

export async function stopAll() {
  const jobs = activeJobs();
  if (!jobs.length) return;
  const runs = new Set(jobs.filter((j) => j.runId).map((j) => j.runId));
  const standalone = jobs.filter((j) => !j.runId);
  const results = await Promise.allSettled([
    ...[...runs].map((id) => api.post(`/runs/${encodeURIComponent(id)}/cancel`)),
    ...standalone.map((j) => api.post(`/tests/${encodeURIComponent(j.id)}/cancel`))
  ]);
  const failed = results.filter((r) => r.status === "rejected");
  await Promise.allSettled([loadJobs(), loadRuns()]);
  if (failed.length) fail(failed[0].reason, "Could not stop");
  else notify(`Stopped ${plural(jobs.length, "job")}.`);
}

export async function cancelJob(id) {
  try {
    await api.post(`/tests/${encodeURIComponent(id)}/cancel`);
    requestRefresh({ jobs: [id], runs: true });
  } catch (error) { fail(error, "Could not cancel"); }
}

export async function cancelRun(id) {
  try {
    await api.post(`/runs/${encodeURIComponent(id)}/cancel`);
    requestRefresh({ all: true });
  } catch (error) { fail(error, "Could not cancel the run"); }
}

export async function rerunJob(id) {
  try {
    const res = await api.post(`/tests/${encodeURIComponent(id)}/rerun`);
    await Promise.allSettled([loadJobs(), loadRuns()]);
    setPref("reportTab", "logs");
    select("job", res.job.id);
    return res;
  } catch (error) {
    fail(error, "Could not run again");
    return null;
  }
}

/** Reruns the failed tests of a job only, when their identifiers can be reconstructed; otherwise the whole job. */
export async function retryFailed(id) {
  const job = jobById(id);
  if (!job) return;
  let results = resultsOf(id);
  if (!results) {
    try { results = await api.get(`/tests/${encodeURIComponent(id)}/results`); } catch (error) { return fail(error, "Could not read the results"); }
  }
  const failed = (results.cases || []).filter((c) => c.status === "failed");
  const only = failed
    .map((c) => {
      const cls = String(c.className || "");
      const at = cls.indexOf(".");
      return at > 0 ? `${cls.slice(0, at)}/${cls.slice(at + 1)}/${c.name}` : "";
    })
    .filter(Boolean);
  if (!failed.length || only.length !== failed.length) return rerunJob(id);
  try {
    const body = {
      testTarget: job.testTarget,
      onlyTesting: only,
      requiredRuntime: job.requiredRuntime,
      requiredModelId: job.requiredModelId,
      autoProvision: job.autoProvision,
      maxRetries: job.maxRetries
    };
    for (const key of ["projectPath", "workspacePath", "workingDirectory", "configuration"]) if (job[key]) body[key] = job[key];
    const res = await api.post("/tests/run", body);
    await loadJobs();
    select("job", res.job.id);
    notify(`Retrying ${plural(failed.length, "failed test")}.`);
  } catch (error) { fail(error, "Could not retry the failed tests"); }
}

export async function exportJUnit(id) {
  try {
    await download(`/tests/${encodeURIComponent(id)}/junit`, `mobilelab-${id.slice(0, 8)}-junit.xml`);
  } catch (error) { fail(error, "Could not export JUnit"); }
}

export async function copyLog(id) {
  try {
    const res = await api.get(`/tests/${encodeURIComponent(id)}/output?tail=${8 * 1024 * 1024}`);
    const ok = await copyText(res.text || "");
    notify(ok ? `Copied the log (${(res.text || "").split("\n").length} lines${res.truncated ? ", tail only" : ""}).` : "Could not copy the log.", { kind: ok ? "success" : "error" });
  } catch (error) { fail(error, "Could not copy the log"); }
}

export async function downloadArtifact(artifact) {
  if (artifact.isDirectory || !artifact.downloadUrl) {
    const ok = await copyText(artifact.path);
    notify(ok ? `Copied the path of ${artifact.name}. Open it on the host running the backend.` : artifact.path);
    return;
  }
  try { await download(artifact.downloadUrl, artifact.name); } catch (error) { fail(error, `Could not download ${artifact.name}`); }
}

// ---------------------------------------------------------------- devices

async function deviceCall(id, verb, label) {
  try {
    await api.post(`/devices/${encodeURIComponent(id)}/${verb}`);
    requestRefresh({ devices: true });
  } catch (error) {
    fail(error, label);
    requestRefresh({ devices: true });
  }
}

export const bootDevice = (id) => deviceCall(id, "boot", `Could not boot ${deviceById(id)?.name ?? "the simulator"}`);
export const shutdownDevice = (id) => deviceCall(id, "shutdown", `Could not shut down ${deviceById(id)?.name ?? "the simulator"}`);

export async function deleteDevice(id) {
  const device = deviceById(id);
  try {
    await api.del(`/devices/${encodeURIComponent(id)}`);
    if (state.sel.kind === "device" && state.sel.id === id) select("welcome");
    requestRefresh({ devices: true });
    notify(`Deleted ${device?.name ?? "the simulator"}.`);
  } catch (error) { fail(error, `Could not delete ${device?.name ?? "the simulator"}`); }
}

export function confirmDeleteDevice(id) {
  const device = deviceById(id);
  if (!device) return;
  hooks.sheet("confirm", {
    title: `Delete “${device.name}”?`,
    message: "The simulator and its data are removed from the host. This cannot be undone.",
    confirm: "Delete",
    danger: true,
    onConfirm: () => deleteDevice(id)
  });
}

export async function spawnSimulator({ runtime, modelId, name }) {
  const body = { wait: false };
  if (runtime) body.runtime = runtime;
  if (modelId) body.modelId = modelId;
  if (name) body.name = name;
  const device = await api.post("/devices/spawn", body); // errors are shown inline by the sheet
  await loadDevices();
  select("device", device.id);
  return device;
}

export async function syncDevices() {
  try {
    await api.post("/devices/sync");
    await loadDevices();
    notify("Simulators reconciled with the host.", { kind: "success" });
  } catch (error) { fail(error, "Could not sync simulators"); }
}

export async function runOnDevice(id) {
  const device = deviceById(id);
  if (!device) return;
  const name = currentScheme();
  if (!name) {
    setDestinations([`dev:${id}`]);
    hooks.sheet("scheme", { runAfterSave: true, isNew: true });
    return;
  }
  setDestinations([`dev:${id}`]);
  return runScheme({ scheme: name });
}

export async function screenshotDevice(id) {
  const device = deviceById(id);
  try {
    const blob = await api.blob(`/devices/${encodeURIComponent(id)}/screenshot`);
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${(device?.name || "simulator").replace(/[^\w.-]+/g, "-")}-${Date.now()}.png`;
    a.className = "sr-only";
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
  } catch (error) { fail(error, `Could not take a screenshot of ${device?.name ?? "the simulator"}`); }
}

// ---------------------------------------------------------------- maintenance

export async function runCleanup(days) {
  const res = await api.post("/maintenance/cleanup", { days });
  await Promise.allSettled([loadJobs(), loadRuns(), loadDevices()]);
  return res;
}

export async function copy(text, what = "Copied") {
  const ok = await copyText(text);
  notify(ok ? `${what}.` : `Could not copy: ${text}`, { kind: ok ? "success" : "error" });
}

// ---------------------------------------------------------------- panels

export function togglePanel(name, open) {
  if (state.ui.narrow && name !== "navigator") {
    state.ui.narrowSheet = open === false || state.ui.narrowSheet === name ? null : name;
    invalidate("shell", "toolbar", "debug", "inspector");
    return;
  }
  const key = { navigator: "navOpen", inspector: "inspOpen", debug: "debugOpen" }[name];
  const next = open === undefined ? !prefs[key] : open;
  setPref(key, next);
  invalidate("shell", "nav", "editor", "inspector", "debug", "toolbar", "jump");
}

export function showNavigator(tab) {
  setPref("navTab", tab);
  if (!prefs.navOpen) setPref("navOpen", true);
  if (state.ui.narrow) state.ui.narrowScreen = "nav";
  if (tab === "debug" || tab === "devices") void ensureCatalog();
  invalidate("shell", "nav", "toolbar");
}

export function openJob(id, { tab, line, caseName, query } = {}) {
  if (tab) setPref("reportTab", tab);
  select("job", id, { reveal: line || caseName || query ? { jobId: id, line, caseName, query } : null });
  const job = jobById(id);
  if (job && !isActive(job)) ensureResults(id);
}

// ---------------------------------------------------------------- command registry

export const NAV_TABS = [
  { id: "devices", label: "Devices", icon: "iphone", key: "1" },
  { id: "tests", label: "Tests", icon: "diamond", key: "2" },
  { id: "issues", label: "Issues", icon: "exclamationmark.triangle", key: "3" },
  { id: "find", label: "Find", icon: "magnifyingglass", key: "4" },
  { id: "debug", label: "Debug", icon: "cpu", key: "5" },
  { id: "reports", label: "Reports", icon: "doc.text", key: "6" }
];

export const commands = [
  { id: "run", title: "Run", group: "Product", keys: "mod+enter", run: () => runScheme(), enabled: () => state.conn === "online" },
  { id: "stop", title: "Stop", group: "Product", keys: "mod+.", run: () => stopAll(), enabled: () => activeJobs().length > 0 },
  { id: "new-simulator", title: "New Simulator…", group: "File", keys: "mod+alt+n", run: () => hooks.sheet("simulator") },
  { id: "new-scheme", title: "New Scheme…", group: "Product", run: () => hooks.sheet("scheme", { isNew: true }) },
  { id: "edit-scheme", title: "Edit Scheme…", group: "Product", run: () => hooks.sheet("scheme", {}), enabled: () => !!currentScheme() },
  { id: "toggle-navigator", title: "Show / Hide Navigator", group: "View", keys: "mod+shift+0", run: () => togglePanel("navigator") },
  { id: "toggle-inspector", title: "Show / Hide Inspector", group: "View", keys: "mod+alt+0", run: () => togglePanel("inspector") },
  { id: "toggle-debug", title: "Show / Hide Debug Area", group: "View", keys: "mod+shift+y", run: () => togglePanel("debug") },
  ...NAV_TABS.map((t) => ({
    id: `nav-${t.id}`, title: `Show ${t.label} Navigator`, group: "Navigate", keys: `mod+shift+${t.key}`, run: () => showNavigator(t.id)
  })),
  { id: "open-quickly", title: "Open Quickly…", group: "Navigate", keys: "mod+shift+o", run: () => hooks.sheet("quick") },
  { id: "find-navigator", title: "Find in Navigator", group: "Navigate", keys: "mod+shift+f", run: () => { showNavigator("find"); hooks.focusRegion("filter"); } },
  { id: "back", title: "Go Back", group: "Navigate", run: () => historyGo(-1), enabled: () => canGoBack() },
  { id: "forward", title: "Go Forward", group: "Navigate", run: () => historyGo(1), enabled: () => canGoForward() },
  { id: "clear-console", title: "Clear Console", group: "Debug", keys: "mod+k", run: () => hooks.focusRegion("clear-console") },
  { id: "settings", title: "Settings…", group: "MobileLab", keys: "mod+,", run: () => hooks.sheet("settings", {}) },
  { id: "diagnostics", title: "Run Diagnostics…", group: "MobileLab", run: () => { hooks.sheet("settings", { pane: "environment" }); void loadDoctor(); } },
  { id: "sync-devices", title: "Sync Simulators with Host", group: "File", run: () => syncDevices(), enabled: () => state.conn === "online" },
  { id: "shortcuts", title: "Keyboard Shortcuts", group: "Help", keys: "?", run: () => hooks.sheet("shortcuts") }
];

export const commandById = (id) => commands.find((c) => c.id === id);
export const isEnabled = (cmd) => (cmd.enabled ? !!cmd.enabled() : true);

/** Which command a keyboard event triggers, by the Xcode-on-the-web rules of spec section 11. */
export function commandForEvent(event, typing) {
  const mod = /Mac|iPhone|iPad/.test(navigator.platform || "") ? event.metaKey : event.ctrlKey;
  const code = event.code || "";
  const key = event.key || "";
  for (const cmd of commands) {
    if (!cmd.keys) continue;
    const parts = cmd.keys.split("+");
    const want = { mod: parts.includes("mod"), alt: parts.includes("alt"), shift: parts.includes("shift") };
    const k = parts[parts.length - 1];
    if (k === "?") {
      if (typing || mod || event.altKey) continue;
      if (key === "?") return cmd;
      continue;
    }
    if (want.mod !== mod || want.alt !== event.altKey || want.shift !== event.shiftKey) continue;
    if (!mod && !want.alt) continue;
    const match =
      k === "enter" ? key === "Enter"
      : k === "." ? code === "Period" || key === "."
      : k === "," ? code === "Comma" || key === ","
      : /^\d$/.test(k) ? code === `Digit${k}` || code === `Numpad${k}`
      : /^[a-z]$/.test(k) ? code === `Key${k.toUpperCase()}` || key.toLowerCase() === k
      : false;
    if (match) return cmd;
  }
  return null;
}

