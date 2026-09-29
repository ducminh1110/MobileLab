// Log models for jobs. One model per open job (shared by the Logs editor and the debug-area console, reference
// counted): it loads the tail of /tests/:id/output, subscribes to /ws/events?jobId=..&output=1 for live lines
// (buffering them until the snapshot has arrived and merging by suffix/prefix overlap), and reloads when the job
// starts another attempt or finishes so the text always equals what is on disk.

import { api, wsUrl } from "./api.js";
import { jobById, isActive } from "./state.js";

const TAIL_BYTES = 8 * 1024 * 1024;
const MAX_LINES = 400_000;

function splitLines(text) {
  if (!text) return [];
  const lines = text.split("\n");
  if (lines[lines.length - 1] === "") lines.pop();
  return lines.map((l) => (l.endsWith("\r") ? l.slice(0, -1) : l));
}

export class LogModel {
  constructor(jobId) {
    this.jobId = jobId;
    this.lines = [];
    this.maxLen = 0;
    this.sizeBytes = 0;
    this.truncated = false;
    this.loading = true;
    this.loaded = false;
    this.error = "";
    this.attempt = 0;
    this.version = 0;
    this.listeners = new Set();
    this.refs = 0;
    this.socket = null;
    this.buffer = [];
    this.reconnect = 0;
    this.reconnectTimer = 0;
    this.stopped = false;
    this.loadToken = 0;
  }

  subscribe(fn) { this.listeners.add(fn); return () => this.listeners.delete(fn); }

  /** Called with the current job record; reloads the text when the job moved on (finished or started another attempt). */
  observe(job) {
    if (!job) return;
    const active = isActive(job);
    if (this.seen && this.loaded && ((this.seen.active && !active) || this.seen.attempts !== job.attempts)) void this.load();
    this.seen = { active, attempts: job.attempts };
  }
  notify(kind, from = 0) { this.version += 1; for (const fn of this.listeners) fn(kind, from); }

  setLines(lines) {
    this.lines = lines.length > MAX_LINES ? lines.slice(lines.length - MAX_LINES) : lines;
    let max = 0;
    for (const l of this.lines) if (l.length > max) max = l.length;
    this.maxLen = max;
  }

  push(newLines) {
    if (!newLines.length) return;
    const from = this.lines.length;
    for (const l of newLines) {
      this.lines.push(l);
      if (l.length > this.maxLen) this.maxLen = l.length;
    }
    if (this.lines.length > MAX_LINES) this.lines.splice(0, this.lines.length - MAX_LINES);
    this.notify("append", from);
  }

  start() {
    this.stopped = false;
    this.connect();
    void this.load();
  }

  stop() {
    this.stopped = true;
    clearTimeout(this.reconnectTimer);
    if (this.socket) {
      this.socket.onopen = this.socket.onclose = this.socket.onmessage = this.socket.onerror = null;
      try { this.socket.close(); } catch { /* closed */ }
      this.socket = null;
    }
  }

  connect() {
    if (this.stopped) return;
    let socket;
    try {
      socket = new WebSocket(wsUrl("/ws/events", { jobId: this.jobId, output: 1 }));
    } catch { return; }
    this.socket = socket;
    socket.onopen = () => {
      if (this.reconnect > 0) void this.load();
      this.reconnect = 0;
    };
    socket.onmessage = (message) => {
      let event;
      try { event = JSON.parse(message.data); } catch { return; }
      if (event.action !== "output" || event.source !== "xcodebuild") return;
      const lines = splitLines(String(event.message ?? "") + "\n");
      if (this.loading) this.buffer.push(...lines);
      else this.push(lines);
    };
    socket.onclose = () => {
      if (this.stopped || this.socket !== socket) return;
      this.socket = null;
      const job = jobById(this.jobId);
      // A finished job has nothing more to stream; a live one reconnects with backoff.
      if (job && !isActive(job)) return;
      this.reconnect += 1;
      this.reconnectTimer = setTimeout(() => this.connect(), Math.min(10_000, 800 * 2 ** Math.min(this.reconnect, 4)));
    };
    socket.onerror = () => {};
  }

  async load() {
    const token = ++this.loadToken;
    this.loading = !this.loaded;
    try {
      const res = await api.get(`/tests/${encodeURIComponent(this.jobId)}/output?tail=${TAIL_BYTES}`);
      if (token !== this.loadToken || this.stopped) return;
      let lines = splitLines(res.text);
      if (res.truncated && lines.length) lines = lines.slice(1);
      // Merge live lines that arrived while the snapshot was in flight (they may already be in it).
      const buffered = this.buffer.splice(0);
      let overlap = 0;
      const limit = Math.min(buffered.length, lines.length);
      for (let k = limit; k > 0; k -= 1) {
        let same = true;
        for (let i = 0; i < k; i += 1) {
          if (lines[lines.length - k + i].slice(0, 2000) !== buffered[i].slice(0, 2000)) { same = false; break; }
        }
        if (same) { overlap = k; break; }
      }
      const attemptChanged = this.loaded && res.attempt !== this.attempt;
      this.setLines(lines.concat(buffered.slice(overlap)));
      this.sizeBytes = res.sizeBytes;
      this.truncated = !!res.truncated;
      this.attempt = res.attempt;
      this.error = "";
      this.loading = false;
      this.loaded = true;
      this.notify(attemptChanged ? "reset" : "reload");
    } catch (error) {
      if (token !== this.loadToken || this.stopped) return;
      this.error = error.message;
      this.loading = false;
      this.notify("error");
    }
  }
}

const models = new Map();

export function acquireLog(jobId) {
  let model = models.get(jobId);
  if (!model) {
    model = new LogModel(jobId);
    models.set(jobId, model);
    model.start();
  }
  model.refs += 1;
  return model;
}

export function releaseLog(jobId) {
  const model = models.get(jobId);
  if (!model) return;
  model.refs -= 1;
  if (model.refs <= 0) {
    // keep finished logs around briefly so switching back and forth is instant
    model.refs = 0;
    setTimeout(() => {
      if (model.refs === 0 && models.get(jobId) === model) { model.stop(); models.delete(jobId); }
    }, 20_000);
  }
}

/** Reload a model when its job changed state (called from the render layer when it notices). */
export function refreshLog(jobId) {
  const model = models.get(jobId);
  if (model && model.loaded) void model.load();
}

export function peekLog(jobId) { return models.get(jobId); }

export function dropLogs() {
  for (const model of models.values()) model.stop();
  models.clear();
}

