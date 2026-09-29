// Keeps `state` in step with the backend: an initial fetch, then a WebSocket subscription (/ws/events?replay=100)
// whose events trigger small debounced refetches. While the socket is down a supervisor probes /health with
// backoff and polls every 5 s (never faster than 1 s). Lazy per-job data (results, artifacts) is fetched on demand.

import { api, ApiError, wsUrl } from "./api.js";
import { state, invalidate, isTerminal } from "./state.js";
import { notifyError } from "./notify.js";

const EVENT_LIMIT = 1500;

let ws = null;
let wsOpenTimer = 0;
let probeTimer = 0;
let probeDelay = 1000;
let started = false;
let refreshTimer = 0;
let refreshFirst = 0;
const pending = { jobs: new Set(), devices: false, runs: false, all: false };

// ---------------------------------------------------------------- basic loaders

function setError(key, error) {
  state.errors[key] = error ? error.message || String(error) : null;
}

function sortJobs(jobs) {
  return jobs.sort((a, b) => (a.createdAt < b.createdAt ? 1 : a.createdAt > b.createdAt ? -1 : 0));
}

export async function loadHealth() {
  state.health = await api.get("/health", { quiet401: true });
  return state.health;
}

export async function loadCaps() {
  try {
    state.caps = await api.get("/capabilities");
    state.auth = "ok";
    setError("caps", null);
  } catch (error) {
    setError("caps", error);
    throw error;
  }
}

export async function loadDevices() {
  try {
    const res = await api.get("/devices");
    state.devices = res.items || [];
    state.capacity = res.capacity || state.capacity;
    setError("devices", null);
  } catch (error) {
    setError("devices", error);
    throw error;
  } finally {
    state.loaded.devices = true;
    invalidate("nav", "toolbar", "editor", "inspector", "jump", "debug");
  }
}

export async function loadJobs() {
  try {
    const res = await api.get("/tests?limit=500");
    const jobs = sortJobs(res.items || []);
    noteJobChanges(state.jobs, jobs);
    state.jobs = jobs;
    setError("jobs", null);
  } catch (error) {
    setError("jobs", error);
    throw error;
  } finally {
    state.loaded.jobs = true;
    invalidate("nav", "toolbar", "editor", "inspector", "jump", "debug");
  }
}

export async function loadRuns() {
  try {
    const res = await api.get("/runs");
    state.runs = (res.items || []).sort((a, b) => (a.createdAt < b.createdAt ? 1 : -1));
    setError("runs", null);
  } catch (error) {
    setError("runs", error);
    throw error;
  } finally {
    state.loaded.runs = true;
    invalidate("nav", "toolbar", "editor", "inspector", "jump");
  }
}

/** Drops cached per-job data when a job moved on (finished, or started another attempt). */
function noteJobChanges(before, after) {
  const old = new Map(before.map((j) => [j.id, j]));
  for (const job of after) {
    const prev = old.get(job.id);
    if (!prev) continue;
    if (prev.status !== job.status || prev.attempts !== job.attempts || prev.updatedAt !== job.updatedAt) {
      if (prev.status !== job.status || prev.attempts !== job.attempts) {
        state.results.delete(job.id);
        state.artifacts.delete(job.id);
      }
    }
  }
}

async function loadJob(id) {
  try {
    const job = await api.get(`/tests/${encodeURIComponent(id)}`);
    const list = state.jobs.slice();
    const at = list.findIndex((j) => j.id === id);
    const prev = at === -1 ? undefined : list[at];
    if (prev) noteJobChanges([prev], [job]);
    if (at === -1) list.push(job);
    else list[at] = job;
    state.jobs = sortJobs(list);
  } catch (error) {
    if (error instanceof ApiError && error.status === 404) {
      state.jobs = state.jobs.filter((j) => j.id !== id);
    } else if (!(error instanceof ApiError && error.network)) {
      notifyError(error, "Could not refresh a job");
    }
  }
}

export async function loadAll() {
  const results = await Promise.allSettled([loadCaps(), loadDevices(), loadJobs(), loadRuns()]);
  invalidate("all");
  return results;
}

// ---------------------------------------------------------------- debounced refresh driven by events

function scheduleRefresh() {
  const now = Date.now();
  if (!refreshFirst) refreshFirst = now;
  clearTimeout(refreshTimer);
  const run = () => { refreshFirst = 0; refreshTimer = 0; void flushRefresh(); };
  if (now - refreshFirst > 1000) run();
  else refreshTimer = setTimeout(run, 250);
}

async function flushRefresh() {
  const jobs = [...pending.jobs];
  const wants = { devices: pending.devices || pending.all, runs: pending.runs || pending.all, all: pending.all };
  pending.jobs.clear();
  pending.devices = pending.runs = pending.all = false;
  const tasks = [];
  if (wants.all || jobs.length > 6) tasks.push(loadJobs());
  else for (const id of jobs) tasks.push(loadJob(id).then(() => invalidate("nav", "toolbar", "editor", "inspector", "jump", "debug")));
  if (wants.devices) tasks.push(loadDevices());
  if (wants.runs || jobs.length) tasks.push(loadRuns());
  await Promise.allSettled(tasks);
  invalidate("nav", "toolbar", "editor", "inspector", "jump", "debug");
}

export function requestRefresh({ jobs = [], devices = false, runs = false, all = false } = {}) {
  for (const id of jobs) pending.jobs.add(id);
  pending.devices ||= devices;
  pending.runs ||= runs;
  pending.all ||= all;
  scheduleRefresh();
}

function handleEvent(event) {
  if (event.action === "output") return;
  const events = state.events;
  if (events.length && events[events.length - 1].id >= event.id && events.some((e) => e.id === event.id)) return;
  events.push(event);
  if (events.length > EVENT_LIMIT) events.splice(0, events.length - EVENT_LIMIT);

  if (event.jobId) requestRefresh({ jobs: [event.jobId] });
  if (event.runId) requestRefresh({ runs: true });
  if (event.deviceId) requestRefresh({ devices: true });
  if (!event.jobId && !event.deviceId && !event.runId) requestRefresh({ all: true });
  invalidate("debug");
  for (const fn of eventListeners) fn(event);
}

const eventListeners = new Set();
export function onEvent(fn) { eventListeners.add(fn); return () => eventListeners.delete(fn); }

// ---------------------------------------------------------------- connection supervisor

function setConn(next, error = "") {
  if (state.conn === next && state.connError === error) return;
  state.conn = next;
  state.connError = error;
  document.getElementById("app")?.setAttribute("data-conn", next);
  invalidate("toolbar", "nav", "debug", "editor", "inspector");
}

function closeSocket() {
  clearTimeout(wsOpenTimer);
  if (ws) {
    ws.onopen = ws.onclose = ws.onmessage = ws.onerror = null;
    try { ws.close(); } catch { /* already closed */ }
    ws = null;
  }
}

function scheduleProbe(delay) {
  clearTimeout(probeTimer);
  probeTimer = setTimeout(probe, delay);
}

async function probe() {
  try {
    await loadHealth();
  } catch (error) {
    if (error instanceof ApiError && error.status && error.status !== 401) {
      setConn("offline", error.message);
    } else {
      setConn("offline", "The backend is not reachable.");
    }
    probeDelay = Math.min(15_000, probeDelay * 2);
    scheduleProbe(probeDelay);
    return;
  }
  // Reachable over REST. If a token is needed and missing, say so instead of pretending to be online.
  try {
    await loadCaps();
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) {
      closeSocket();
      setConn("online");
      return;
    }
  }
  probeDelay = 1000;
  setConn("online");
  void loadAll();
  connectEvents();
}

function connectEvents() {
  closeSocket();
  if (state.auth === "required") return;
  let socket;
  try {
    socket = new WebSocket(wsUrl("/ws/events", { replay: 100 }));
  } catch {
    scheduleProbe(5000);
    return;
  }
  ws = socket;
  wsOpenTimer = setTimeout(() => {
    if (ws === socket && socket.readyState !== WebSocket.OPEN) {
      closeSocket();
      scheduleProbe(5000);
    }
  }, 5000);
  socket.onopen = () => {
    clearTimeout(wsOpenTimer);
    state.events = [];
    state.results.clear();
    state.artifacts.clear();
    setConn("online");
  };
  socket.onmessage = (message) => {
    try { handleEvent(JSON.parse(message.data)); } catch { /* ignore a malformed frame */ }
  };
  socket.onclose = () => {
    if (ws !== socket) return;
    ws = null;
    clearTimeout(wsOpenTimer);
    // REST may still work (proxy without WebSocket support): probe decides between Disconnected and 5 s polling.
    scheduleProbe(state.conn === "online" ? 1000 : probeDelay);
    if (state.conn === "online") pollWhileDown();
  };
  socket.onerror = () => { /* close follows */ };
}

let pollTimer = 0;
function pollWhileDown() {
  clearInterval(pollTimer);
  pollTimer = setInterval(() => {
    if (ws && ws.readyState === WebSocket.OPEN) { clearInterval(pollTimer); return; }
    if (state.conn === "online" && state.auth !== "required") void loadAll();
  }, 5000);
}

/** Initial load. Safe to call again after a token was entered. */
export async function start() {
  if (started) {
    clearTimeout(probeTimer);
    probeDelay = 1000;
    await probe();
    return;
  }
  started = true;
  setConn("connecting");
  await probe();
  setInterval(() => {
    if (state.conn === "online" && state.auth !== "required" && document.visibilityState === "visible") void loadJobs().catch(() => undefined);
  }, 30_000);
}

// ---------------------------------------------------------------- lazy loaders

export function ensureResults(jobId, { force = false } = {}) {
  const cached = state.results.get(jobId);
  if (cached && !force && (cached.status === "loading" || cached.status === "ok")) return cached;
  const entry = { status: "loading", data: cached?.data, error: "" };
  state.results.set(jobId, entry);
  api.get(`/tests/${encodeURIComponent(jobId)}/results`).then(
    (data) => { state.results.set(jobId, { status: "ok", data, error: "" }); invalidate("nav", "editor", "inspector", "debug"); },
    (error) => { state.results.set(jobId, { status: "error", data: undefined, error: error.message }); invalidate("nav", "editor", "debug"); }
  );
  return entry;
}

export function resultsOf(jobId) {
  const entry = state.results.get(jobId);
  return entry && entry.status === "ok" ? entry.data : undefined;
}

export function ensureArtifacts(jobId, { force = false } = {}) {
  const cached = state.artifacts.get(jobId);
  if (cached && !force && (cached.status === "loading" || cached.status === "ok")) return cached;
  state.artifacts.set(jobId, { status: "loading", items: cached?.items || [], error: "" });
  api.get(`/tests/${encodeURIComponent(jobId)}/artifacts`).then(
    (res) => { state.artifacts.set(jobId, { status: "ok", items: res.items || [], error: "" }); invalidate("editor", "inspector"); },
    (error) => { state.artifacts.set(jobId, { status: "error", items: [], error: error.message }); invalidate("editor"); }
  );
  return state.artifacts.get(jobId);
}

export async function ensureCatalog({ force = false } = {}) {
  if (state.catalog && !force && Date.now() - (state.catalogAt || 0) < 30_000) return state.catalog;
  try {
    state.catalog = await api.get("/catalog");
    state.catalogAt = Date.now();
    state.catalogError = "";
  } catch (error) {
    state.catalogError = error.message;
  }
  invalidate("toolbar");
  return state.catalog;
}

export async function loadDoctor() {
  state.doctorLoading = true;
  state.doctorError = "";
  invalidate("overlay");
  try {
    state.doctor = await api.get("/doctor");
  } catch (error) {
    state.doctorError = error.message;
  } finally {
    state.doctorLoading = false;
    invalidate("overlay");
  }
}

let metricsTimer = 0;
async function sampleMetrics() {
  try {
    const summary = await api.get("/metrics/summary");
    state.metrics = summary;
    state.metricsHistory.push({
      t: Date.now(),
      cpu: summary.process?.cpuPercent ?? 0,
      mem: summary.process?.rssBytes ?? 0,
      disk: summary.artifactBytes ?? 0,
      load: summary.capacity?.load ?? 0
    });
    if (state.metricsHistory.length > 40) state.metricsHistory.shift();
    invalidate("nav");
  } catch { /* the connection state already explains it */ }
}

/** Polls /metrics/summary every 2 s while the Debug navigator is visible. */
export function setMetricsPolling(on) {
  if (on && !metricsTimer) {
    void sampleMetrics();
    metricsTimer = setInterval(() => {
      if (state.conn === "online" && state.auth !== "required" && document.visibilityState === "visible") void sampleMetrics();
    }, 2000);
  } else if (!on && metricsTimer) {
    clearInterval(metricsTimer);
    metricsTimer = 0;
  }
}

/** Lifecycle events for a job or device (Inspector > History). */
export async function loadHistory(kind, id) {
  const key = `${kind}:${id}`;
  const param = kind === "job" ? "jobId" : kind === "device" ? "deviceId" : "runId";
  const entry = state.eventHistory && state.eventHistory.key === key ? state.eventHistory : null;
  state.eventHistory = { key, items: entry?.items || [], loading: true, error: "" };
  invalidate("inspector", "debug");
  try {
    const res = await api.get(`/events?${param}=${encodeURIComponent(id)}&limit=200`);
    if (state.eventHistory && state.eventHistory.key === key) state.eventHistory = { key, items: (res.items || []).slice().reverse(), loading: false, error: "" };
  } catch (error) {
    if (state.eventHistory && state.eventHistory.key === key) state.eventHistory = { key, items: [], loading: false, error: error.message };
  }
  invalidate("inspector", "debug");
}

export const isRunning = (job) => !isTerminal(job);
