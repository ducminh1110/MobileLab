// Selectors: turn raw API data into what the panes show. Pure functions of `state`; no DOM, no fetching.

import { state, prefs, isActive } from "./state.js";
import { fmtWhen, fmtDuration, plural } from "./util.js";

// ---------------------------------------------------------------- names

export function runtimeLabel(identifier) {
  const m = /SimRuntime\.([A-Za-z]+)-([\d-]+)$/.exec(identifier || "");
  return m ? `${m[1]} ${m[2].replace(/-/g, ".")}` : identifier || "Unknown runtime";
}

export function modelLabel(identifier) {
  const tail = String(identifier || "").split(".").pop() || "";
  return tail ? tail.replace(/-/g, " ") : "Any device";
}

export const deviceRuntime = (d) => d.runtimeName || runtimeLabel(d.runtime);
export const deviceModel = (d) => d.modelName || modelLabel(d.modelId);
export const isTablet = (d) => /^ipad/i.test(d.modelName || d.modelId?.split(".").pop() || d.name || "");
export const deviceIcon = (d) => (d.type === "vm" ? "cpu" : isTablet(d) ? "ipad" : "iphone");

const versionOf = (label) => (/(\d+(?:\.\d+)*)/.exec(label)?.[1] || "0").split(".").map(Number);
export function compareVersionsDesc(a, b) {
  const pa = versionOf(a);
  const pb = versionOf(b);
  for (let i = 0; i < Math.max(pa.length, pb.length); i += 1) {
    const diff = (pb[i] || 0) - (pa[i] || 0);
    if (diff) return diff;
  }
  return 0;
}

export const DEVICE_STATE = {
  created: { label: "Created", dot: "idle" },
  booting: { label: "Booting", dot: "warn", pulse: true },
  ready: { label: "Ready", dot: "pass" },
  busy: { label: "Busy running tests", dot: "accent", pulse: true },
  shutting_down: { label: "Shutting down", dot: "warn", pulse: true },
  stopped: { label: "Shut down", dot: "idle" },
  error: { label: "Error", dot: "fail" }
};
export const deviceState = (d) => DEVICE_STATE[d.status] || { label: d.status, dot: "idle" };
export const isBooted = (d) => d.status === "ready" || d.status === "busy";
export const isTransitioning = (d) => d.status === "booting" || d.status === "shutting_down";

/** Devices grouped by runtime, newest runtime first (the "blue folders" of the Devices navigator). */
export function deviceGroups(devices = state.devices) {
  const map = new Map();
  for (const device of devices) {
    if (device.type === "vm") continue;
    const key = device.runtime;
    if (!map.has(key)) map.set(key, { key, name: deviceRuntime(device), devices: [] });
    map.get(key).devices.push(device);
  }
  const groups = [...map.values()];
  for (const g of groups) g.devices.sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
  groups.sort((a, b) => compareVersionsDesc(a.name, b.name));
  return groups;
}

export const vmDevices = () => state.devices.filter((d) => d.type === "vm");

// ---------------------------------------------------------------- jobs

export const JOB_STATUS = {
  queued: { label: "Queued", icon: "diamond", cls: "c-dim" },
  running: { label: "Running", spinner: true, cls: "c-accent" },
  retrying: { label: "Retrying", spinner: true, cls: "c-warn" },
  completed: { label: "Passed", icon: "checkmark.diamond.fill", cls: "c-pass" },
  failed: { label: "Failed", icon: "xmark.diamond.fill", cls: "c-fail" },
  cancelled: { label: "Cancelled", icon: "minus.diamond", cls: "c-dim" }
};
export const jobStatus = (job) => JOB_STATUS[job.status] || { label: job.status, icon: "diamond", cls: "c-dim" };

export const CASE_STATUS = {
  passed: { label: "Passed", icon: "checkmark.diamond.fill", cls: "c-pass" },
  failed: { label: "Failed", icon: "xmark.diamond.fill", cls: "c-fail" },
  skipped: { label: "Skipped", icon: "minus.diamond", cls: "c-warn" }
};

export function jobDevice(job) {
  return job.assignedDeviceId ? state.devices.find((d) => d.id === job.assignedDeviceId) : undefined;
}

export function jobDestinationName(job) {
  if (job.assignedDeviceName) return job.assignedDeviceName;
  const parts = [];
  if (job.requiredModelId) parts.push(modelLabel(job.requiredModelId));
  if (job.requiredRuntime) parts.push(runtimeLabel(job.requiredRuntime));
  return parts.length ? parts.join(", ") : "Any Simulator";
}

export const jobTitle = (job) => `${job.testTarget} on ${jobDestinationName(job)}`;

export function jobRuntimeName(job) {
  const device = jobDevice(job);
  if (device) return deviceRuntime(device);
  return job.requiredRuntime ? runtimeLabel(job.requiredRuntime) : "";
}

export function jobResultText(job) {
  const s = job.summary;
  if (job.status === "completed") return s ? `${s.passed} of ${s.total} passed` : "Passed";
  if (job.status === "failed") {
    if (s?.buildFailed) return "Build failed";
    if (s && s.failed) return `${s.failed} of ${s.total} failed`;
    return job.error ? "Failed" : "Failed";
  }
  return jobStatus(job).label;
}

export function jobDurationMs(job, now = Date.now()) {
  if (job.durationMs != null) return job.durationMs;
  if (job.startedAt) return (job.finishedAt ? Date.parse(job.finishedAt) : now) - Date.parse(job.startedAt);
  return null;
}

export const jobFinishedAt = (job) => job.finishedAt || job.updatedAt;

export function runName(run) {
  return run.name || `${run.scheme} matrix`;
}

export function runJobs(run) {
  const set = new Set(run.jobIds);
  return state.jobs.filter((j) => set.has(j.id));
}

export const RUN_STATUS = {
  queued: JOB_STATUS.queued,
  running: JOB_STATUS.running,
  passed: JOB_STATUS.completed,
  failed: JOB_STATUS.failed,
  cancelled: JOB_STATUS.cancelled
};

export function runCountsText(run) {
  const c = run.counts || {};
  const parts = [];
  if (c.completed) parts.push(`${c.completed} passed`);
  if (c.failed) parts.push(`${c.failed} failed`);
  if (c.running) parts.push(`${c.running} running`);
  if (c.retrying) parts.push(`${c.retrying} retrying`);
  if (c.queued) parts.push(`${c.queued} queued`);
  if (c.cancelled) parts.push(`${c.cancelled} cancelled`);
  return parts.join(", ") || plural(run.jobIds.length, "job");
}

export function activeJobs() {
  return state.jobs.filter(isActive);
}

// ---------------------------------------------------------------- schemes and destinations

export function schemeNames() {
  const names = [];
  const seen = new Set();
  for (const s of prefs.schemes) if (!seen.has(s.name)) { seen.add(s.name); names.push(s.name); }
  for (const j of state.jobs) if (!seen.has(j.testTarget)) { seen.add(j.testTarget); names.push(j.testTarget); }
  return names;
}

/** Stored scheme settings, or the settings of the newest job that used that name. */
export function schemeConfig(name) {
  const stored = prefs.schemes.find((s) => s.name === name);
  if (stored) return stored;
  const job = state.jobs.find((j) => j.testTarget === name);
  if (!job) return { name };
  return {
    name,
    projectPath: job.projectPath,
    workspacePath: job.workspacePath,
    workingDirectory: job.workingDirectory,
    configuration: job.configuration,
    onlyTesting: job.onlyTesting,
    maxRetries: job.maxRetries,
    autoProvision: job.autoProvision
  };
}

export const currentScheme = () => (prefs.scheme && schemeNames().includes(prefs.scheme) ? prefs.scheme : schemeNames()[0] || "");

/** Test-capable simulators that are up. */
export const bootedSimulators = () => state.devices.filter((d) => d.type === "simulator" && d.canRunTests && isBooted(d));

export function effectiveDestinations() {
  const devices = new Map(state.devices.map((d) => [d.id, d]));
  const keys = prefs.destinations.filter((k) => k === "auto" || k.startsWith("new:") || (k.startsWith("dev:") && devices.has(k.slice(4))));
  if (keys.length) return keys;
  const first = bootedSimulators()[0];
  return first ? [`dev:${first.id}`] : ["auto"];
}

export function destinationInfo(key) {
  if (key === "auto") return { key, label: "Any Available Simulator", icon: "iphone", runtime: undefined, model: undefined };
  if (key.startsWith("dev:")) {
    const d = state.devices.find((x) => x.id === key.slice(4));
    return d ? { key, label: d.name, icon: deviceIcon(d), runtime: d.runtime, model: d.modelId, device: d } : null;
  }
  const [runtime, model] = key.slice(4).split("|");
  const rt = state.catalog?.runtimes?.find((r) => r.identifier === runtime);
  const ty = state.catalog?.deviceTypes?.find((t) => t.identifier === model);
  const type = ty?.name || modelLabel(model);
  const runtimeName = rt?.name || runtimeLabel(runtime);
  return { key, label: `${type} (${runtimeName})`, icon: /^ipad/i.test(type) ? "ipad" : "iphone", runtime, model, onDemand: true };
}

/** How the current destination selection maps onto the API: one job, or a matrix run. */
export function runPlan() {
  const infos = effectiveDestinations().map(destinationInfo).filter(Boolean);
  const auto = infos.some((i) => i.key === "auto");
  const runtimes = new Set();
  const models = new Set();
  if (!auto) for (const i of infos) { if (i.runtime) runtimes.add(i.runtime); if (i.model) models.add(i.model); }
  const matrix = runtimes.size > 1 || models.size > 1;
  return {
    infos,
    auto,
    runtimes: [...runtimes],
    models: [...models],
    matrix,
    jobs: matrix ? Math.max(1, runtimes.size) * Math.max(1, models.size) : 1
  };
}

export function destinationLabel() {
  const infos = effectiveDestinations().map(destinationInfo).filter(Boolean);
  if (!infos.length) return "No Destination";
  if (infos.length === 1) return infos[0].label;
  return `${infos.length} Destinations`;
}

export function destinationIcon() {
  const infos = effectiveDestinations().map(destinationInfo).filter(Boolean);
  return infos.length === 1 ? infos[0].icon : "iphone";
}

// ---------------------------------------------------------------- toolbar status (spec section 5)

/** The job the capsule describes: the running one, else the newest of the selected scheme, else the newest overall. */
export function capsuleJob() {
  const scheme = currentScheme();
  const active = state.jobs.filter(isActive);
  const rank = { running: 0, retrying: 1, queued: 2 };
  if (active.length) {
    const sorted = [...active].sort((a, b) => (rank[a.status] - rank[b.status]) || ((a.testTarget === scheme ? 0 : 1) - (b.testTarget === scheme ? 0 : 1)));
    return { job: sorted[0], more: active.length - 1 };
  }
  const own = scheme ? state.jobs.find((j) => j.testTarget === scheme) : undefined;
  return { job: own || (scheme ? undefined : state.jobs[0]), more: 0 };
}

/**
 * kind drives the glyph and colour; `elapsedFrom` makes the detail tick in place.
 * Returns { kind, label, detail, elapsedFrom, jobId }.
 */
export function capsuleStatus() {
  if (state.conn === "offline") return { kind: "offline", label: "Disconnected", detail: "Retrying…" };
  if (state.auth === "required") return { kind: "warn", label: "Sign In Required", detail: "API token needed" };
  if (state.conn === "connecting" && !state.loaded.jobs) return { kind: "idle", label: "Connecting", detail: "" };
  const { job, more } = capsuleJob();
  if (!job) {
    const booted = bootedSimulators().length;
    return { kind: "idle", label: "Ready", detail: booted ? `${plural(booted, "simulator")} booted` : "No simulators" };
  }
  const extra = more ? ` (+${more} more)` : "";
  switch (job.status) {
    case "queued":
      return { kind: "queued", label: "Queued", detail: job.waitingReason || "Waiting for a simulator", jobId: job.id };
    case "running":
      return { kind: "running", label: "Running", detail: `${jobTitle(job)}`, elapsedFrom: job.startedAt, extra, jobId: job.id };
    case "retrying":
      return { kind: "retrying", label: "Retrying", detail: `attempt ${Math.min(job.attempts + 1, job.maxRetries + 1)} of ${job.maxRetries + 1}${extra}`, jobId: job.id };
    case "completed":
      return { kind: "pass", label: "Tests Passed", detail: fmtWhen(jobFinishedAt(job)), jobId: job.id };
    case "failed": {
      const s = job.summary;
      if (s?.buildFailed) return { kind: "fail", label: "Build Failed", detail: s.errors?.[0] || job.error || "See the build log", jobId: job.id };
      if (s && s.failed) return { kind: "fail", label: "Tests Failed", detail: `${s.failed} of ${s.total} tests failed`, jobId: job.id };
      return { kind: "fail", label: "Failed", detail: job.error || "See the log", jobId: job.id };
    }
    case "cancelled":
      return { kind: "cancelled", label: "Cancelled", detail: fmtWhen(jobFinishedAt(job)), jobId: job.id };
    default:
      return { kind: "idle", label: job.status, detail: "" };
  }
}

// ---------------------------------------------------------------- test results

export function shortClass(className) {
  const parts = String(className || "").split(".");
  return parts[parts.length - 1] || className || "Tests";
}

/** Groups result cases into suites, keeping xcodebuild's order. */
export function suitesOf(cases) {
  const map = new Map();
  for (const c of cases || []) {
    const key = c.className || "Tests";
    if (!map.has(key)) map.set(key, { className: key, name: shortClass(key), cases: [] });
    map.get(key).cases.push(c);
  }
  return [...map.values()].map((s) => ({
    ...s,
    passed: s.cases.filter((c) => c.status === "passed").length,
    failed: s.cases.filter((c) => c.status === "failed").length,
    skipped: s.cases.filter((c) => c.status === "skipped").length
  }));
}

export function suiteStatus(suite) {
  return suite.failed ? "failed" : suite.passed || suite.skipped ? (suite.passed ? "passed" : "skipped") : "skipped";
}

/** "LoginTests.swift:41 XCTAssertEqual failed..." -> { file, line, text } */
export function parseIssueMessage(message) {
  const first = String(message || "").split("\n")[0];
  const m = /^(\S+?\.\w+):(\d+)(?::\d+)?:?\s+(.*)$/.exec(first);
  if (m) return { file: m[1], line: Number(m[2]), text: m[3], full: message };
  return { file: "", line: 0, text: first, full: message };
}

export function jobIssues(job, results) {
  const issues = [];
  const s = job.summary;
  const cases = results?.cases || [];
  for (const c of cases) {
    if (c.status !== "failed") continue;
    const info = parseIssueMessage(c.message || "Test failed");
    issues.push({ kind: "test", severity: "fail", title: c.name, className: c.className, message: info.text, file: info.file, line: info.line, caseName: c.name });
  }
  if (s?.errors?.length) {
    for (const e of s.errors) {
      const info = parseIssueMessage(e);
      issues.push({ kind: "build", severity: "fail", title: info.text, message: info.text, file: info.file, line: info.line, raw: e });
    }
  } else if (job.status === "failed" && job.error && !cases.some((c) => c.status === "failed")) {
    issues.push({ kind: "job", severity: "fail", title: job.error, message: job.error, file: "", line: 0 });
  }
  return issues;
}

export function fmtJobDuration(job) {
  const ms = jobDurationMs(job);
  return ms == null ? "" : fmtDuration(ms);
}
